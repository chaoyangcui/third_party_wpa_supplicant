/*
* Copyright (c) Hisilicon Technologies Co., Ltd. 2019-2019. All rights reserved.
* Description: driver_hisi
* Author: hisilicon
* Create: 2019-03-04
*/

#include "driver_hisi.h"
#include "includes.h"
#include "common.h"
#include "driver.h"
#include "ap/wpa_auth.h"
#include "l2_packet/l2_packet.h"
#include "ap/ap_config.h"
#include "ap/hostapd.h"
#include "driver_hisi_ioctl.h"
#include "../wpa_supplicant/wpa_supplicant_i.h"
#include "securec.h"
#include "wpa_msg_service.h"

#ifdef __cplusplus
#if __cplusplus
extern "C" {
#endif
#endif

typedef struct _hisi_cmd_stru {
    int32 cmd;
    const struct wpabuf *src;
} hisi_cmd_stu;

uint8 g_ssid_prefix_flag = WPA_FLAG_OFF;

#define SCAN_TIME_OUT 5

hisi_driver_data_stru *g_driverData = NULL;
hi_wifi_iftype g_driverType = HI_WIFI_IFTYPE_UNSPECIFIED;

uint32 hisi_alg_to_cipher_suite(enum wpa_alg alg, size_t key_len);
int32 hisi_is_ap_interface(hisi_iftype_enum_uint8 nlmode);
static void hisi_key_ext_free(hisi_key_ext_stru *key_ext);
static void hisi_ap_settings_free(hisi_ap_settings_stru  *apsettings);
int32 hisi_set_key(const char *ifname, void *priv, enum wpa_alg alg, const uint8 *addr, int32 key_idx,
                   int32 set_tx, const uint8 *seq, size_t seq_len, const uint8 *key, size_t key_len);
int32 hisi_set_ap(void *priv, struct wpa_driver_ap_params *params);
int32 hisi_send_mlme(void *priv, const uint8 *data, size_t data_len, int32 noack, unsigned int freq,
                     const u16 *csa_offs, size_t csa_offs_len);
int32 hisi_send_eapol(void *priv, const uint8 *addr, const uint8 *data, size_t data_len, int32 encrypt,
                      const uint8 *own_addr, uint32 flags);
int32 hisi_driver_send_event(const char *ifname, int32 cmd, const uint8 *buf, uint32 length);
void hisi_driver_event_process(void *eloop_data, void *user_ctx);
void * hisi_hapd_init(struct hostapd_data *hapd, struct wpa_init_params *params);
static void hisi_hw_feature_data_free(struct hostapd_hw_modes *modes, uint16 modes_num);
void hisi_rx_mgmt_process(void *ctx, int8 *data_ptr, union wpa_event_data *event);
void hisi_tx_status_process(void *ctx, int8 *data_ptr, union wpa_event_data *event);
void hisi_drv_deinit(void *priv);
void hisi_hapd_deinit(void *priv);
void hisi_driver_scan_timeout(void *eloop_ctx, void *timeout_ctx);
hisi_driver_data_stru * hisi_drv_init(void *ctx, const struct wpa_init_params *params);
struct hostapd_hw_modes * hisi_get_hw_feature_data(void *priv, uint16 *num_modes, uint16 *flags);
int32 hisi_connect(hisi_driver_data_stru *drv, struct wpa_driver_associate_params *params);
int32 hisi_try_connect(hisi_driver_data_stru *drv, struct wpa_driver_associate_params *params);
void hisi_set_conn_keys(const struct wpa_driver_associate_params *wpa_params, hisi_associate_params_stru *params);
uint32 hisi_cipher_to_cipher_suite(uint32 cipher);
int32 hisi_disconnect(hisi_driver_data_stru *drv, int32 reason_code);
void hisi_excep_recv(void *priv, const int8 *data);
int32 hisi_sta_remove(void *priv, const uint8 *addr);


void deinit_wifi_service()
{
    if (g_driverType == HI_WIFI_IFTYPE_STATION) {
        hisi_wpa_deinit(g_driverData);
    } else if (g_driverType == HI_WIFI_IFTYPE_AP) {
        hisi_hapd_deinit(g_driverData);
    } else {
        printf("no need to cleanup \n");
    }
}

int32 hisi_is_ap_interface(hisi_iftype_enum_uint8 nlmode)
{
    return ((nlmode == HI_WIFI_IFTYPE_AP) || (nlmode == HI_WIFI_IFTYPE_P2P_GO));
}

static void hisi_key_ext_free(hisi_key_ext_stru *key_ext)
{
    if (key_ext == NULL)
        return;

    if (key_ext->addr != NULL) {
        os_free(key_ext->addr);
        key_ext->addr = NULL;
    }

    if (key_ext->seq != NULL) {
        os_free(key_ext->seq);
        key_ext->seq = NULL;
    }

    if (key_ext->key != NULL) {
        os_free(key_ext->key);
        key_ext->key = NULL;
    }

    os_free(key_ext);
}

static void hisi_ap_settings_free(hisi_ap_settings_stru *apsettings)
{
    if (apsettings == NULL)
        return;

    if (apsettings->mesh_ssid != NULL) {
        os_free(apsettings->mesh_ssid);
        apsettings->mesh_ssid = NULL;
    }

    if (apsettings->ssid != NULL) {
        os_free(apsettings->ssid);
        apsettings->ssid = NULL;
    }

    if (apsettings->beacon_data.head != NULL) {
        os_free(apsettings->beacon_data.head);
        apsettings->beacon_data.head = NULL;
    }

    if (apsettings->beacon_data.tail != NULL) {
        os_free(apsettings->beacon_data.tail);
        apsettings->beacon_data.tail = NULL;
    }

    os_free(apsettings);
}

uint32 hisi_alg_to_cipher_suite(enum wpa_alg alg, size_t key_len)
{
    switch (alg) {
        case WPA_ALG_WEP:
            /* key_len = 5 : WEP40, 13 : WEP104 */
            if (key_len == WPA_WEP40_KEY_LEN)
                return RSN_CIPHER_SUITE_WEP40;
            return RSN_CIPHER_SUITE_WEP104;
        case WPA_ALG_TKIP:
            return RSN_CIPHER_SUITE_TKIP;
        case WPA_ALG_CCMP:
            return RSN_CIPHER_SUITE_CCMP;
        case WPA_ALG_GCMP:
            return RSN_CIPHER_SUITE_GCMP;
        case WPA_ALG_CCMP_256:
            return RSN_CIPHER_SUITE_CCMP_256;
        case WPA_ALG_GCMP_256:
            return RSN_CIPHER_SUITE_GCMP_256;
        case WPA_ALG_IGTK:
            return RSN_CIPHER_SUITE_AES_128_CMAC;
        case WPA_ALG_BIP_GMAC_128:
            return RSN_CIPHER_SUITE_BIP_GMAC_128;
        case WPA_ALG_BIP_GMAC_256:
            return RSN_CIPHER_SUITE_BIP_GMAC_256;
        case WPA_ALG_BIP_CMAC_256:
            return RSN_CIPHER_SUITE_BIP_CMAC_256;
        case WPA_ALG_SMS4:
            return RSN_CIPHER_SUITE_SMS4;
        case WPA_ALG_KRK:
            return RSN_CIPHER_SUITE_KRK;
        case WPA_ALG_NONE:
        case WPA_ALG_PMK:
            return 0;
        default:
            return 0;
    }
}

int hisi_init_key(hisi_key_ext_stru *key_ext, enum wpa_alg alg, const uint8 *addr, int32 key_idx, int32 set_tx,
                  const uint8 *seq, size_t seq_len, const uint8 *key, size_t key_len)
{
    key_ext->default_types = HISI_KEY_DEFAULT_TYPE_INVALID;
    key_ext->seq_len = seq_len;
    key_ext->key_len = key_len;
    key_ext->key_idx = key_idx;
    key_ext->type = HISI_KEYTYPE_DEFAULT_INVALID;
    key_ext->cipher = hisi_alg_to_cipher_suite(alg, key_len);
    if ((alg != WPA_ALG_NONE) && (key != NULL) && (key_len != 0)) {
        key_ext->key = (uint8 *)os_zalloc(key_len); /* freed by hisi_key_ext_free */
        if ((key_ext->key == NULL) || (memcpy_s(key_ext->key, key_len, key, key_len) != EOK))
            return -HISI_EFAIL;
    }
    if ((seq != NULL) && (seq_len != 0)) {
        key_ext->seq = (uint8 *)os_zalloc(seq_len); /* freed by hisi_key_ext_free */
        if ((key_ext->seq == NULL) || (memcpy_s(key_ext->seq, seq_len, seq, seq_len) != EOK))
            return -HISI_EFAIL;
    }
    if ((addr != NULL) && (!is_broadcast_ether_addr(addr))) {
        key_ext->addr = (uint8 *)os_zalloc(ETH_ADDR_LEN); /* freed by hisi_key_ext_free */
        if ((key_ext->addr == NULL) || (memcpy_s(key_ext->addr, ETH_ADDR_LEN, addr, ETH_ADDR_LEN) != EOK))
            return -HISI_EFAIL;
        if ((alg != WPA_ALG_WEP) && (key_idx != 0) && (set_tx == 0))
            key_ext->type = HISI_KEYTYPE_GROUP;
    } else if ((addr != NULL) && (is_broadcast_ether_addr(addr))) {
        key_ext->addr = NULL;
    }
    if (key_ext->type == HISI_KEYTYPE_DEFAULT_INVALID)
        key_ext->type = (key_ext->addr != NULL) ? HISI_KEYTYPE_PAIRWISE : HISI_KEYTYPE_GROUP;
    if ((alg == WPA_ALG_IGTK) || (alg == WPA_ALG_BIP_GMAC_128) ||
        (alg == WPA_ALG_BIP_GMAC_256) || (alg == WPA_ALG_BIP_CMAC_256))
        key_ext->defmgmt = HISI_TRUE;
    else
        key_ext->def = HISI_TRUE;
    if ((addr != NULL) && (is_broadcast_ether_addr(addr)))
        key_ext->default_types = HISI_KEY_DEFAULT_TYPE_MULTICAST;
    else if (addr != NULL)
        key_ext->default_types = HISI_KEY_DEFAULT_TYPE_UNICAST;
    return HISI_SUCC;
}

int32 hisi_set_key(const  char *ifname, void *priv, enum wpa_alg alg, const uint8 *addr, int32 key_idx,
                   int32 set_tx, const uint8 *seq, size_t seq_len, const uint8 *key, size_t key_len)
{
    int32 ret = HISI_SUCC;
    hisi_key_ext_stru *key_ext = NULL;
    hisi_driver_data_stru *drv = priv;

    /* addr, seq, key will be checked below */
    if ((ifname == NULL) || (priv == NULL))
        return -HISI_EFAIL;

    /* Ignore for P2P Device */
    if (drv->nlmode == HI_WIFI_IFTYPE_P2P_DEVICE)
        return HISI_SUCC;

    key_ext = os_zalloc(sizeof(hisi_key_ext_stru));
    if (key_ext == NULL)
        return -HISI_EFAIL;

    if (hisi_init_key(key_ext, alg, addr, key_idx, set_tx, seq, seq_len, key, key_len) != HISI_SUCC) {
        hisi_key_ext_free(key_ext);
        return -HISI_EFAIL;
    }

    if (alg == WPA_ALG_NONE) {
        ret = hisi_ioctl_del_key((int8 *)ifname, key_ext);
    } else {
        ret = hisi_ioctl_new_key((int8 *)ifname, key_ext);
        /* if set new key fail, just return without setting default key */
        if ((ret != HISI_SUCC) || (set_tx == 0) || (alg == WPA_ALG_NONE)) {
            hisi_key_ext_free(key_ext);
            return ret;
        }

        if ((hisi_is_ap_interface(drv->nlmode)) && (key_ext->addr != NULL) &&
            (!is_broadcast_ether_addr(key_ext->addr))) {
            hisi_key_ext_free(key_ext);
            return ret;
        }
        ret = hisi_ioctl_set_key((int8 *)ifname, key_ext);
    }

    hisi_key_ext_free(key_ext);
    return ret;
}

static void hisi_set_ap_freq(hisi_ap_settings_stru *apsettings, const struct wpa_driver_ap_params *params)
{
    if (params->freq != NULL) {
        apsettings->freq_params.mode               = params->freq->mode;
        apsettings->freq_params.freq               = params->freq->freq;
        apsettings->freq_params.channel            = params->freq->channel;
        apsettings->freq_params.ht_enabled         = params->freq->ht_enabled;
        apsettings->freq_params.sec_channel_offset = params->freq->sec_channel_offset;
        apsettings->freq_params.center_freq1       = params->freq->center_freq1;
        apsettings->freq_params.bandwidth          = params->freq->bandwidth;
        if (params->freq->bandwidth == 20) /* 20: BW20 */
            apsettings->freq_params.bandwidth = HISI_CHAN_WIDTH_20;
        else
            apsettings->freq_params.bandwidth = HISI_CHAN_WIDTH_40;
    }
}

static int hisi_set_ap_beacon_data(hisi_ap_settings_stru *apsettings, const struct wpa_driver_ap_params *params)
{
    if ((params->head != NULL) && (params->head_len != 0)) {
        apsettings->beacon_data.head_len = params->head_len;
        /* beacon_data.head freed by hisi_ap_settings_free */
        apsettings->beacon_data.head = (uint8 *)os_zalloc(apsettings->beacon_data.head_len);
        if (apsettings->beacon_data.head == NULL)
            return -HISI_EFAIL;
        if (memcpy_s(apsettings->beacon_data.head, apsettings->beacon_data.head_len,
            params->head, params->head_len) != EOK)
            return -HISI_EFAIL;
    }

    if ((params->tail != NULL) && (params->tail_len != 0)) {
        apsettings->beacon_data.tail_len = params->tail_len;
        /* beacon_data.tail freed by hisi_ap_settings_free */
        apsettings->beacon_data.tail = (uint8 *)os_zalloc(apsettings->beacon_data.tail_len);
        if (apsettings->beacon_data.tail == NULL)
            return -HISI_EFAIL;

        if (memcpy_s(apsettings->beacon_data.tail, apsettings->beacon_data.tail_len,
            params->tail, params->tail_len) != EOK)
            return -HISI_EFAIL;
    }
    return HISI_SUCC;
}

int32 hisi_set_ap(void *priv, struct wpa_driver_ap_params *params)
{
    int32 ret;
    hisi_ap_settings_stru *apsettings = NULL;
    hisi_driver_data_stru *drv = (hisi_driver_data_stru *)priv;
    if ((priv == NULL) || (params == NULL) || (params->freq == NULL))
        return -HISI_EFAIL;
    if ((params->freq->bandwidth != 20) && (params->freq->bandwidth != 40)) /* 20: BW20, 40: BW40 */
        return -HISI_EFAIL;

    apsettings = os_zalloc(sizeof(hisi_ap_settings_stru));
    if (apsettings == NULL)
        return -HISI_EFAIL;
    apsettings->beacon_interval = params->beacon_int;
    apsettings->dtim_period     = params->dtim_period;
    apsettings->hidden_ssid     = params->hide_ssid;
    if ((params->auth_algs & (WPA_AUTH_ALG_OPEN | WPA_AUTH_ALG_SHARED)) == (WPA_AUTH_ALG_OPEN | WPA_AUTH_ALG_SHARED))
        apsettings->auth_type = HISI_AUTHTYPE_AUTOMATIC;
    else if ((params->auth_algs & WPA_AUTH_ALG_SHARED) == WPA_AUTH_ALG_SHARED)
        apsettings->auth_type = HISI_AUTHTYPE_SHARED_KEY;
    else
        apsettings->auth_type = HISI_AUTHTYPE_OPEN_SYSTEM;

    /* wifi driver will copy mesh_ssid by itself. */
    if ((params->ssid != NULL) && (params->ssid_len != 0)) {
        apsettings->ssid_len = params->ssid_len;
        apsettings->ssid = (uint8 *)os_zalloc(apsettings->ssid_len);
        if ((apsettings->ssid == NULL) || (memcpy_s(apsettings->ssid, apsettings->ssid_len,
            params->ssid, params->ssid_len) != EOK))
            goto FAILED;
    }
    hisi_set_ap_freq(apsettings, params);
    if (hisi_set_ap_beacon_data(apsettings, params) != HISI_SUCC)
        goto FAILED;
    if (drv->beacon_set == HISI_TRUE)
        ret = hisi_ioctl_change_beacon(drv->iface, apsettings);
    else
        ret = hisi_ioctl_set_ap(drv->iface, apsettings);
    if (ret == HISI_SUCC)
        drv->beacon_set = HISI_TRUE;
    hisi_ap_settings_free(apsettings);
    return ret;
FAILED:
    hisi_ap_settings_free(apsettings);
    return -HISI_EFAIL;
}

int32 hisi_send_mlme(void *priv, const uint8 *data, size_t data_len,
                     int32 noack, unsigned int freq, const u16 *csa_offs, size_t csa_offs_len)
{
    int32 ret;
    hisi_driver_data_stru *drv = priv;
    hisi_mlme_data_stru *mlme_data = NULL;
    errno_t rc;
    (void)freq;
    (void)csa_offs;
    (void)csa_offs_len;
    (void)noack;
    if ((priv == NULL) || (data == NULL))
        return -HISI_EFAIL;
    mlme_data = os_zalloc(sizeof(hisi_mlme_data_stru));
    if (mlme_data == NULL)
        return -HISI_EFAIL;
    mlme_data->data = NULL;
    mlme_data->data_len = data_len;
    mlme_data->send_action_cookie = &(drv->send_action_cookie);
    if ((data != NULL) && (data_len != 0)) {
        mlme_data->data = (uint8 *)os_zalloc(data_len);
        if (mlme_data->data == NULL) {
            os_free(mlme_data);
            mlme_data = NULL;
            return -HISI_EFAIL;
        }
        rc = memcpy_s(mlme_data->data, data_len, data, data_len);
        if (rc != EOK) {
            os_free(mlme_data->data);
            mlme_data->data = NULL;
            os_free(mlme_data);
            return -HISI_EFAIL;
        }
    }
    ret = hisi_ioctl_send_mlme(drv->iface, mlme_data);
    os_free(mlme_data->data);
    mlme_data->data = NULL;
    os_free(mlme_data);
    if (ret != HISI_SUCC)
        ret = -HISI_EFAIL;
    return ret;
}

void hisi_receive_eapol(void *ctx, const uint8 *src_addr, const uint8 *buf, uint32 len)
{
    wpa_printf(MSG_ERROR, "hisi_receive_eapol enter.");
    hisi_driver_data_stru *drv = ctx;
    if ((ctx == NULL) || (src_addr == NULL) || (buf == NULL) || (len < sizeof(struct l2_ethhdr))) {
        wpa_printf(MSG_ERROR, "hisi_receive_eapol invalid input.");
        return;
    }
    wpa_printf(MSG_ERROR, "hisi_receive_eapol drv->ctx addr=%p.", drv->ctx);
    drv_event_eapol_rx(drv->ctx, src_addr, buf + sizeof(struct l2_ethhdr), len - sizeof(struct l2_ethhdr));
    wpa_printf(MSG_ERROR, "hisi_receive_eapol leave.");
}

int32 hisi_send_eapol(void *priv, const uint8 *addr, const uint8 *data, size_t data_len,
                      int32 encrypt, const uint8 *own_addr, uint32 flags)
{
    hisi_driver_data_stru  *drv = priv;
    int32 ret;
    uint32 frame_len;
    uint8 *frame_buf = NULL;
    uint8 *payload = NULL;
    struct l2_ethhdr *l2_ethhdr = NULL;
    errno_t rc;
    (void)encrypt;
    (void)flags;

    if ((priv == NULL) || (addr == NULL) || (data == NULL) || (own_addr == NULL))
        return -HISI_EFAIL;

    frame_len = data_len + sizeof(struct l2_ethhdr);
    frame_buf = os_zalloc(frame_len);
    if (frame_buf == NULL)
        return -HISI_EFAIL;

    l2_ethhdr = (struct l2_ethhdr *)frame_buf;
    rc = memcpy_s(l2_ethhdr->h_dest, ETH_ADDR_LEN, addr, ETH_ADDR_LEN);
    rc |= memcpy_s(l2_ethhdr->h_source, ETH_ADDR_LEN, own_addr, ETH_ADDR_LEN);
    if (rc != EOK) {
        os_free(frame_buf);
        return -HISI_EFAIL;
    }
    l2_ethhdr->h_proto = host_to_be16(ETH_P_PAE);
    payload = (uint8 *)(l2_ethhdr + 1);
    rc = memcpy_s(payload, data_len, data, data_len);
    if (rc != EOK) {
        os_free(frame_buf);
        return -HISI_EFAIL;
    }
    ret = l2_packet_send(drv->eapol_sock, addr, ETH_P_EAPOL, frame_buf, frame_len);
    os_free(frame_buf);
    return ret;
}

static inline void hisi_driver_event_process_internal(hisi_driver_data_stru *drv, int8 *data_ptr, int32 cmd);
int32 hisi_driver_send_event(const char *ifname, int32 cmd, const uint8 *buf, uint32 length)
{
    int8 *data_ptr = NULL;
    int8 *packet = NULL;
    hisi_driver_data_stru *drv  = NULL;
    int32 ret = HISI_OK;
    errno_t rc;

    if (ifname == NULL)
        return -HISI_EFAIL;

    drv = g_driverData;
    if (drv == NULL)
        return -HISI_EFAIL;

    hisi_driver_event_process_internal(drv, buf, cmd);
    return ret;
}

static inline void hisi_driver_event_new_sta_process(const hisi_driver_data_stru *drv, int8 *data_ptr)
{
    hisi_new_sta_info_stru new_sta_info;
    uint8 *ptr = NULL;
    union wpa_event_data event;
    int32 rc;

    wpa_printf(MSG_ERROR, "hisi_driver_event_new_sta_process enter");
    (void)memset_s(&event, sizeof(union wpa_event_data), 0, sizeof(union wpa_event_data));
    ptr = (uint8 *)data_ptr;
    new_sta_info.reassoc = *((int32 *)ptr);
    ptr += sizeof(int32);
    new_sta_info.ielen = *((uint32 *)ptr);
    ptr += sizeof(uint32);
    new_sta_info.ie = ptr;
    ptr += new_sta_info.ielen;
    rc = memcpy_s(new_sta_info.macaddr, ETH_ADDR_LEN, ptr, ETH_ADDR_LEN);
    if (rc != EOK) {
        return;
    }

    if (is_zero_ether_addr((const uint8 *)new_sta_info.macaddr)) {
        wpa_supplicant_event(drv->ctx, EVENT_DISASSOC, NULL);
    } else {
        event.assoc_info.reassoc     = new_sta_info.reassoc;
        event.assoc_info.req_ies     = new_sta_info.ie;
        event.assoc_info.req_ies_len = new_sta_info.ielen;
        event.assoc_info.addr        = new_sta_info.macaddr;
        wpa_supplicant_event(drv->ctx, EVENT_ASSOC, &event);
    }
    wpa_printf(MSG_ERROR, "hisi_driver_event_new_sta_process leave");
}
static inline void hisi_driver_event_scan_result_process(hisi_driver_data_stru *drv, int8 *data_ptr)
{
    hisi_scan_result_stru scan_result;
    uint8 *ptr = NULL;
    uint8 *ie = NULL;
    uint8 *beacon_ie = NULL;
    struct wpa_scan_res *res = NULL;
    errno_t rc;

    ptr = (uint8 *)data_ptr;
    scan_result.beacon_int = *((int16 *)ptr);
    ptr += sizeof(int16);
    scan_result.caps = *((int16 *)ptr);
    ptr += sizeof(int16);
    scan_result.level = *((int32 *)ptr);
    ptr += sizeof(int32);
    scan_result.freq = *((int32 *)ptr);
    ptr += sizeof(int32);
    scan_result.flags = *((int32 *)ptr);
    ptr += sizeof(int32);
    rc = memcpy_s(scan_result.bssid, ETH_ADDR_LEN, ptr, ETH_ADDR_LEN);
    if (rc != EOK)
        goto FAILED;
    ptr += ETH_ADDR_LEN;
    scan_result.ie_len = *((uint32 *)ptr);
    ptr += sizeof(uint32);
    ie = ptr;
    ptr += scan_result.ie_len;
    scan_result.beacon_ie_len = *((uint32 *)ptr);
    ptr += sizeof(uint32);
    beacon_ie = ptr;


    wpa_printf(MSG_ERROR, "hisi_driver_event_scan_result_process: ie_len=%d, beacon_ie_len=%d", 
        scan_result.ie_len,scan_result.beacon_ie_len);

    res = (struct wpa_scan_res *)os_zalloc(sizeof(struct wpa_scan_res) + scan_result.ie_len + scan_result.beacon_ie_len);
    if (res == NULL)
        goto FAILED;
    res->flags      = scan_result.flags;
    res->freq       = scan_result.freq;
    res->caps       = scan_result.caps;
    res->beacon_int = scan_result.beacon_int;
    res->qual       = 0;
    res->level      = scan_result.level;
    res->age        = 0;
    res->ie_len     = scan_result.ie_len;
    res->beacon_ie_len = scan_result.beacon_ie_len;
    rc = memcpy_s(res->bssid, ETH_ADDR_LEN, scan_result.bssid, ETH_ADDR_LEN);
    if (rc != EOK)
        goto FAILED;
    rc = memcpy_s(&res[1], scan_result.ie_len, ie, scan_result.ie_len);
    rc |= memcpy_s((uint8 *)(&res[1]) + scan_result.ie_len, scan_result.beacon_ie_len, beacon_ie, scan_result.beacon_ie_len);
    if (rc != EOK)
        goto FAILED;
    if (drv->scan_ap_num >= SCAN_AP_LIMIT) {
        wpa_printf(MSG_ERROR, "hisi_driver_event_process: drv->scan_ap_num >= SCAN_AP_LIMIT");
        goto FAILED;
    }
    drv->res[drv->scan_ap_num++] = res;
    return;

FAILED:
    if (res != NULL)
        os_free(res);
}

static inline void hisi_driver_event_connect_result_process(hisi_driver_data_stru *drv, int8 *data_ptr)
{
    hisi_connect_result_stru accoc_info;
    uint8 *ptr = NULL;
    union wpa_event_data event;
    errno_t rc;

    (void)memset_s(&event, sizeof(union wpa_event_data), 0, sizeof(union wpa_event_data));
    ptr = (uint8 *)data_ptr;
    accoc_info.status = *((uint16 *)ptr);
    ptr += sizeof(uint16);
    accoc_info.freq = *((uint16 *)ptr);
    ptr += sizeof(uint16);
    if (memcpy_s(accoc_info.bssid, ETH_ADDR_LEN, ptr, ETH_ALEN) != EOK) {
        return;
    }
    ptr += ETH_ADDR_LEN;
    accoc_info.req_ie_len = *((uint32 *)ptr);
    ptr += sizeof(uint32);
    accoc_info.req_ie = ptr;
    ptr += accoc_info.req_ie_len;
    accoc_info.resp_ie_len = *((uint32 *)ptr);
    ptr += sizeof(uint32);
    accoc_info.resp_ie = ptr;

    if (accoc_info.status != 0) {
        drv->associated = HISI_DISCONNECT;
        wpa_supplicant_event(drv->ctx, EVENT_DISASSOC, NULL);
    } else {
        drv->associated = HISI_CONNECT;
        rc = memcpy_s(drv->bssid, ETH_ALEN, accoc_info.bssid, ETH_ALEN);
        if (rc != EOK) {
            return;
        }
        event.assoc_info.req_ies      = accoc_info.req_ie;
        event.assoc_info.req_ies_len  = accoc_info.req_ie_len;
        event.assoc_info.resp_ies     = accoc_info.resp_ie;
        event.assoc_info.resp_ies_len = accoc_info.resp_ie_len;
        event.assoc_info.addr         = accoc_info.bssid;
        event.assoc_info.freq         = accoc_info.freq;
        wpa_supplicant_event(drv->ctx, EVENT_ASSOC, &event);
    }
}

static inline void hisi_driver_event_disconnect_process(hisi_driver_data_stru *drv, int8 *data_ptr)
{
    hisi_disconnect_stru discon_info;
    uint8 *ptr = NULL;
    union wpa_event_data event;

    (void)memset_s(&event, sizeof(union wpa_event_data), 0, sizeof(union wpa_event_data));
    ptr = (uint8 *)data_ptr;
    discon_info.reason = *((uint16 *)ptr);
    ptr += sizeof(uint16);
    discon_info.ie_len = *((uint32 *)ptr);
    ptr += sizeof(uint32);
    discon_info.ie = ptr;

    drv->associated = HISI_DISCONNECT;
    event.disassoc_info.reason_code = discon_info.reason;
    event.disassoc_info.ie          = discon_info.ie;
    event.disassoc_info.ie_len      = discon_info.ie_len;
    wpa_supplicant_event(drv->ctx, EVENT_DISASSOC, &event);
}

static inline void hisi_driver_event_eapol_recv_process(hisi_driver_data_stru *drv, int8 *data_ptr)
{
    wpa_printf(MSG_ERROR, "hisi_driver_event_eapol_recv_process call");
    l2_packet_receive(drv->eapol_sock, NULL);
}

static inline void hisi_driver_event_scan_done_process(hisi_driver_data_stru *drv, int8 *data_ptr)
{
    hisi_driver_scan_status_stru *status = (hisi_driver_scan_status_stru *)data_ptr;
    eloop_cancel_timeout(hisi_driver_scan_timeout, drv, drv->ctx);
    if (status->scan_status != HISI_SCAN_SUCCESS) {
        wpa_printf(MSG_ERROR, "hisi_driver_event_process: wifi driver scan not success(%d)", status->scan_status);
        return;
    }
    wpa_supplicant_event(drv->ctx, EVENT_SCAN_RESULTS, NULL);
}

static inline void hisi_driver_event_process_internal(hisi_driver_data_stru *drv, int8 *data_ptr, int32 cmd)
{
    union wpa_event_data event;
    wpa_printf(MSG_INFO, "hisi_driver_event_process_internal event=%d", cmd);
    (void)memset_s(&event, sizeof(union wpa_event_data), 0, sizeof(union wpa_event_data));
    switch (cmd) {
        case HISI_ELOOP_EVENT_NEW_STA:
            hisi_driver_event_new_sta_process(drv, data_ptr);
            break;
        case HISI_ELOOP_EVENT_DEL_STA:
            event.disassoc_info.addr = (uint8 *)(data_ptr + sizeof(uint8));
            if (drv->ctx != NULL)
                wpa_supplicant_event(drv->ctx, EVENT_DISASSOC, &event);
            break;
        case HISI_ELOOP_EVENT_RX_MGMT:
            hisi_rx_mgmt_process(drv->ctx, data_ptr, &event);
            break;
        case HISI_ELOOP_EVENT_TX_STATUS:
            hisi_tx_status_process(drv->ctx, data_ptr, &event);
            break;
        case HISI_ELOOP_EVENT_SCAN_DONE:
            hisi_driver_event_scan_done_process(drv, data_ptr);
            break;
        case HISI_ELOOP_EVENT_SCAN_RESULT:
            hisi_driver_event_scan_result_process(drv, data_ptr);
            break;
        case HISI_ELOOP_EVENT_CONNECT_RESULT:
            hisi_driver_event_connect_result_process(drv, data_ptr);
            break;
        case HISI_ELOOP_EVENT_DISCONNECT:
            hisi_driver_event_disconnect_process(drv, data_ptr);
            break;
        case HISI_ELOOP_EVENT_CHANNEL_SWITCH:
            event.ch_switch.freq = (int)(((hisi_ch_switch_stru *)data_ptr)->freq);
            if (drv->ctx != NULL)
                wpa_supplicant_event(drv->ctx, EVENT_CH_SWITCH, &event);
            break;
        case HISI_ELOOP_EVENT_EAPOL_RECV:
            hisi_driver_event_eapol_recv_process(drv, data_ptr);
            break;
        default:
            break;
    }
}
void hisi_rx_mgmt_process(void *ctx, int8 *data_ptr, union wpa_event_data *event)
{
    hisi_rx_mgmt_stru rx_mgmt;
    uint8 *ptr = NULL;

    if ((ctx == NULL) || (data_ptr == NULL) || (event == NULL))
        return;

    ptr = (uint8 *)data_ptr;
    rx_mgmt.freq = *((int32 *)ptr);
    ptr += sizeof(int32);
    rx_mgmt.sig_mbm = *((int32 *)ptr);
    ptr += sizeof(int32);
    rx_mgmt.len = *((uint32 *)ptr);
    ptr += sizeof(uint32);
    rx_mgmt.buf = ptr;

    event->rx_mgmt.frame = rx_mgmt.buf;
    event->rx_mgmt.frame_len = rx_mgmt.len;
    event->rx_mgmt.ssi_signal = rx_mgmt.sig_mbm;
    event->rx_mgmt.freq = rx_mgmt.freq;

    wpa_supplicant_event(ctx, EVENT_RX_MGMT, event);

    return;
}

void hisi_tx_status_process(void *ctx, int8 *data_ptr, union wpa_event_data *event)
{
    uint16 fc;
    struct ieee80211_hdr *hdr = NULL;
    hisi_tx_status_stru tx_status;
    uint8 *ptr = NULL;

    if ((ctx == NULL) || (data_ptr == NULL) || (event == NULL))
        return;

    ptr = (uint8 *)data_ptr;
    tx_status.ack = *((uint8 *)ptr);
    ptr += sizeof(uint8);
    tx_status.len = *((uint32 *)ptr);
    ptr += sizeof(uint32);
    tx_status.buf = ptr;

    hdr = (struct ieee80211_hdr *)tx_status.buf;
    fc = le_to_host16(hdr->frame_control);

    event->tx_status.type = WLAN_FC_GET_TYPE(fc);
    event->tx_status.stype = WLAN_FC_GET_STYPE(fc);
    event->tx_status.dst = hdr->addr1;
    event->tx_status.data = tx_status.buf;
    event->tx_status.data_len = tx_status.len;
    event->tx_status.ack = (tx_status.ack != HISI_FALSE);

    wpa_supplicant_event(ctx, EVENT_TX_STATUS, event);

    return;
}

hisi_driver_data_stru * hisi_drv_init(void *ctx, const struct wpa_init_params *params)
{
    uint8 addr_tmp[ETH_ALEN] = { 0 };
    hisi_driver_data_stru *drv = NULL;
    errno_t rc;

    if ((ctx == NULL) || (params == NULL))
        return NULL;
    drv = os_zalloc(sizeof(hisi_driver_data_stru));
    if (drv == NULL)
        goto INIT_FAILED;

    drv->ctx = ctx;
    rc = memcpy_s((int8 *)drv->iface, sizeof(drv->iface), params->ifname, sizeof(drv->iface));
    if (rc != EOK) {
        os_free(drv);
        drv = NULL;
        goto INIT_FAILED;
    }
    drv->eapol_sock = l2_packet_init((char *)drv->iface, NULL, ETH_P_EAPOL, hisi_receive_eapol, drv, 1);
    if (drv->eapol_sock == NULL)
        goto INIT_FAILED;

    if (l2_packet_get_own_addr(drv->eapol_sock, addr_tmp))
        goto INIT_FAILED;

    /* The mac address is passed to the hostapd data structure for hostapd startup */
    rc = memcpy_s(params->own_addr, ETH_ALEN, addr_tmp, ETH_ALEN);
    rc |= memcpy_s(drv->own_addr, ETH_ALEN, addr_tmp, ETH_ALEN);
    if (rc != EOK)
        goto INIT_FAILED;

    g_driverData = drv;
    return drv;
INIT_FAILED:
    if (drv != NULL) {
        if (drv->eapol_sock != NULL)
            l2_packet_deinit(drv->eapol_sock);
        os_free(drv);
    }
    return NULL;
}


void * hisi_hapd_init(struct hostapd_data *hapd, struct wpa_init_params *params)
{
    g_driverType = HI_WIFI_IFTYPE_AP;
    hisi_driver_data_stru *drv = NULL;
    hisi_set_netdev_stru set_netdev;
    int32 ret;
    int32 send_event_cb_reg_flag;
    hisi_set_mode_stru       st_set_mode;

    if ((hapd == NULL) || (params == NULL) || (hapd->conf == NULL))
        return NULL;

    if (WpaMsgServiceInit() != HISI_SUCC)
        goto INIT_FAILED;

    drv = hisi_drv_init(hapd, params);
    if (drv == NULL)
        return NULL;
    drv->hapd = hapd;
    ret = memcpy_s(drv->hapd->own_addr, ETH_ALEN, drv->own_addr, ETH_ALEN);
    if (ret != EOK){
        return -HISI_EFAIL;
    }


    set_netdev.status = HISI_TRUE;
    set_netdev.iftype = HI_WIFI_IFTYPE_AP;
    set_netdev.phy_mode = WAL_PHY_MODE_11N;
    /* set netdev open or stop */
    ret = hisi_ioctl_set_netdev(drv->iface, &set_netdev);
    if (ret != HISI_SUCC)
        goto INIT_FAILED;

    // APģʽ
    st_set_mode.iftype = HI_WIFI_IFTYPE_AP;
    os_memcpy(st_set_mode.bssid, drv->own_addr, ETH_ADDR_LEN);
    ret = hisi_ioctl_set_mode(drv->iface, &st_set_mode);
    if (ret != HISI_SUCC) {
        wpa_printf(MSG_ERROR, "%s: hisi_ioctl_hapd_init failed!", __FUNCTION__);
        goto INIT_FAILED;
    }

    g_driverData = drv;
    return (void *)drv;
INIT_FAILED:
    hisi_drv_deinit(drv);
    return NULL;
}

void hisi_drv_deinit(void *priv)
{
    hisi_driver_data_stru   *drv = NULL;
    if (priv == NULL)
        return;

    (void)ShutdownMessageRouter();

    drv = (hisi_driver_data_stru *)priv;
    if (drv->eapol_sock != NULL)
        l2_packet_deinit(drv->eapol_sock);
    drv->event_queue = NULL;
    g_driverData = NULL;
    os_free(drv);
}

void hisi_hapd_deinit(void *priv)
{
    int32 ret;
    errno_t rc;
    hisi_driver_data_stru *drv = NULL;
    hisi_set_mode_stru set_mode;
    hisi_set_netdev_stru set_netdev;

    if (priv == NULL)
        return;

    rc = memset_s(&set_mode, sizeof(hisi_set_mode_stru), 0, sizeof(hisi_set_mode_stru));
    if (rc != EOK)
        return;
    drv = (hisi_driver_data_stru *)priv;
    set_mode.iftype = HI_WIFI_IFTYPE_STATION;

    set_netdev.status = HISI_FALSE;
    set_netdev.iftype = HI_WIFI_IFTYPE_AP;
    set_netdev.phy_mode = WAL_PHY_MODE_11N;

    hisi_ioctl_set_netdev(drv->iface, &set_netdev);
    ret = hisi_ioctl_set_mode(drv->iface, &set_mode);
    if (ret != HISI_SUCC) {
        wpa_printf(MSG_ERROR, "hisi_hapd_deinit , hisi_ioctl_set_mode fail.");
        return;
    }
    hisi_drv_deinit(priv);
}

static void hisi_hw_feature_data_free(struct hostapd_hw_modes *modes, uint16 modes_num)
{
    uint16 loop;

    if (modes == NULL)
        return;
    for (loop = 0; loop < modes_num; ++loop) {
        if (modes[loop].channels != NULL) {
            os_free(modes[loop].channels);
            modes[loop].channels = NULL;
        }
        if (modes[loop].rates != NULL) {
            os_free(modes[loop].rates);
            modes[loop].rates = NULL;
        }
    }
    os_free(modes);
}

struct hostapd_hw_modes * hisi_get_hw_feature_data(void *priv, uint16 *num_modes, uint16 *flags)
{
    struct modes modes_data[] = { { 12, HOSTAPD_MODE_IEEE80211G }, { 4, HOSTAPD_MODE_IEEE80211B } };
    size_t loop;
    uint32 index;
    hisi_hw_feature_data_stru hw_feature_data;

    if ((priv == NULL) || (num_modes == NULL) || (flags == NULL))
        return NULL;

    (void)memset_s(&hw_feature_data, sizeof(hisi_hw_feature_data_stru), 0, sizeof(hisi_hw_feature_data_stru));
    hisi_driver_data_stru *drv = (hisi_driver_data_stru *)priv;
    *num_modes = 2; /* 2: mode only for 11b + 11g */
    *flags     = 0;

    if (hisi_ioctl_get_hw_feature(drv->iface, &hw_feature_data) != HISI_SUCC) {
        return NULL;
    }

    struct hostapd_hw_modes *modes = os_calloc(*num_modes, sizeof(struct hostapd_hw_modes));
    if (modes == NULL)
        return NULL;

    for (loop = 0; loop < *num_modes; ++loop) {
        modes[loop].channels = NULL;
        modes[loop].rates    = NULL;
    }

    modes[0].ht_capab = hw_feature_data.ht_capab;
    for (index = 0; index < sizeof(modes_data) / sizeof(struct modes); index++) {
        modes[index].mode         = modes_data[index].mode;
        modes[index].num_channels = hw_feature_data.channel_num;
        modes[index].num_rates    = modes_data[index].modes_num_rates;
        modes[index].channels     = os_calloc(hw_feature_data.channel_num, sizeof(struct hostapd_channel_data));
        modes[index].rates        = os_calloc(modes[index].num_rates, sizeof(int));
        if ((modes[index].channels == NULL) || (modes[index].rates == NULL)) {
            hisi_hw_feature_data_free(modes, *num_modes);
            return NULL;
        }

        for (loop = 0; loop < (size_t)hw_feature_data.channel_num; loop++) {
            modes[index].channels[loop].chan = hw_feature_data.iee80211_channel[loop].channel;
            modes[index].channels[loop].freq = hw_feature_data.iee80211_channel[loop].freq;
            modes[index].channels[loop].flag = hw_feature_data.iee80211_channel[loop].flags;
        }
        for (loop = 0; loop < (size_t)modes[index].num_rates; loop++)
            modes[index].rates[loop] = hw_feature_data.bitrate[loop];
    }
    wpa_printf(MSG_ERROR, "hisi_get_hw_feature_data hw_feature_data.channel_num %d", hw_feature_data.channel_num);
    wpa_printf(MSG_ERROR, "hisi_get_hw_feature_data ok");
    return modes;
}

void * hisi_wpa_init(void *ctx, const char *ifname, void *global_priv)
{
    g_driverType = HI_WIFI_IFTYPE_STATION;
    int32 ret;
    int32 send_event_cb_reg_flag = HISI_FAIL;
    hisi_set_mode_stru set_mode;
    hisi_set_netdev_stru set_netdev;

    (void)global_priv;
    if ((ctx == NULL) || (ifname == NULL))
        return NULL;

    (void)memset_s(&set_mode, sizeof(hisi_set_mode_stru), 0, sizeof(hisi_set_mode_stru));
    hisi_driver_data_stru *drv = os_zalloc(sizeof(hisi_driver_data_stru));
    if (drv == NULL)
        goto INIT_FAILED;
    drv->ctx = ctx;
    if (memcpy_s((int8 *)drv->iface, sizeof(drv->iface), ifname, sizeof(drv->iface)) != EOK)
        goto INIT_FAILED;
    if (WpaMsgServiceInit() != HISI_SUCC)
        goto INIT_FAILED;

    drv->eapol_sock = l2_packet_init((char *)drv->iface, NULL, ETH_P_EAPOL, hisi_receive_eapol, drv, 1);
    if (drv->eapol_sock == NULL) {
        wpa_printf(MSG_ERROR, "drv->eapol_sock == NULL");
        goto INIT_FAILED;
    }

    if (l2_packet_get_own_addr(drv->eapol_sock, drv->own_addr)) {
        wpa_printf(MSG_ERROR, "l2_packet_get_own_addr return");
        goto INIT_FAILED;
    }

    set_netdev.status = HISI_TRUE;
    set_netdev.iftype = HI_WIFI_IFTYPE_STATION;
    set_netdev.phy_mode = WAL_PHY_MODE_11N;
    /* set netdev open or stop */
    wpa_printf(MSG_ERROR, "hisi_ioctl_set_netdev call start");
    ret = hisi_ioctl_set_netdev(drv->iface, &set_netdev);
    wpa_printf(MSG_ERROR, "hisi_ioctl_set_netdev call end");
    if (ret != HISI_SUCC)
        goto INIT_FAILED;

    g_driverData = drv;
    return drv;

INIT_FAILED:
    hisi_drv_deinit(drv);
    return NULL;
}

void hisi_wpa_deinit(void *priv)
{
    hisi_set_netdev_stru set_netdev;
    int32 ret;
    hisi_driver_data_stru *drv = NULL;
    hisi_set_mode_stru set_mode;
    errno_t rc;

    if ((priv == NULL))
        return;

    rc = memset_s(&set_mode, sizeof(hisi_set_mode_stru), 0, sizeof(hisi_set_mode_stru));
    if (rc != EOK)
        return;

    drv = (hisi_driver_data_stru *)priv;
    eloop_cancel_timeout(hisi_driver_scan_timeout, drv, drv->ctx);

    set_netdev.status = HISI_FALSE;
    set_netdev.iftype = HI_WIFI_IFTYPE_STATION;
    set_netdev.phy_mode = WAL_PHY_MODE_11N;
    ret = hisi_ioctl_set_netdev(drv->iface, &set_netdev);
    if (ret != HISI_SUCC)
        wpa_printf(MSG_DEBUG, "hisi_wpa_deinit, close netdev fail");
    hisi_drv_deinit(priv);
}

void hisi_driver_scan_timeout(void *eloop_ctx, void *timeout_ctx)
{
    (void)eloop_ctx;
    if (timeout_ctx == NULL)
        return;
    wpa_supplicant_event(timeout_ctx, EVENT_SCAN_RESULTS, NULL);
}

static void hisi_scan_free(hisi_scan_stru *scan_params)
{
    if (scan_params == NULL)
        return;

    if (scan_params->ssids != NULL) {
        os_free(scan_params->ssids);
        scan_params->ssids = NULL;
    }
    if (scan_params->bssid != NULL) {
        os_free(scan_params->bssid);
        scan_params->bssid = NULL;
    }

    if (scan_params->extra_ies != NULL) {
        os_free(scan_params->extra_ies);
        scan_params->extra_ies = NULL;
    }

    if (scan_params->freqs != NULL) {
        os_free(scan_params->freqs);
        scan_params->freqs = NULL;
    }

    os_free(scan_params);
}

int hisi_scan_process_ssid(struct wpa_driver_scan_params *params, hisi_scan_stru *scan_params)
{
    errno_t rc;
    size_t loop;
    if (params->num_ssids == 0)
        return HISI_SUCC;

    scan_params->num_ssids = params->num_ssids;
    /* scan_params->ssids freed by hisi_scan_free */
    scan_params->ssids = (hisi_driver_scan_ssid_stru *)os_zalloc(sizeof(hisi_driver_scan_ssid_stru) * params->num_ssids);
    if (scan_params->ssids == NULL)
        return -HISI_EFAIL;

    for (loop = 0; (loop < params->num_ssids) && (loop < WPAS_MAX_SCAN_SSIDS); loop++) {
        wpa_hexdump_ascii(MSG_MSGDUMP, "hisi: Scan SSID", params->ssids[loop].ssid, params->ssids[loop].ssid_len);

        if (params->ssids[loop].ssid_len > MAX_SSID_LEN) {
            wpa_printf(MSG_ERROR, "params->ssids[%d].ssid_len is %d, num %d:",
                           loop, params->ssids[loop].ssid_len, params->num_ssids);
            params->ssids[loop].ssid_len = MAX_SSID_LEN;
        }
        if (params->ssids[loop].ssid_len) {
            rc = memcpy_s(scan_params->ssids[loop].ssid, MAX_SSID_LEN,
                          params->ssids[loop].ssid, params->ssids[loop].ssid_len);
            if (rc != EOK)
                return -HISI_EFAIL;
        }
        scan_params->ssids[loop].ssid_len = params->ssids[loop].ssid_len;
    }

    return HISI_SUCC;
}

int hisi_scan_process_bssid(const struct wpa_driver_scan_params *params, hisi_scan_stru *scan_params)
{
    errno_t rc;
    if (params->bssid != NULL) {
        /* scan_params->bssid freed by hisi_scan_free */
        scan_params->bssid = (uint8 *)os_zalloc(ETH_ALEN);
        if (scan_params->bssid == NULL)
            return -HISI_EFAIL;
        rc = memcpy_s(scan_params->bssid, ETH_ALEN, params->bssid, ETH_ALEN);
        if (rc != EOK) {
            return -HISI_EFAIL;
        }
    }
    return HISI_SUCC;
}

int hisi_scan_process_extra_ies(const struct wpa_driver_scan_params *params, hisi_scan_stru *scan_params)
{
    errno_t rc;
    if ((params->extra_ies != NULL) && (params->extra_ies_len != 0)) {
        wpa_hexdump(MSG_MSGDUMP, "hisi: Scan extra IEs", params->extra_ies, params->extra_ies_len);
        /* scan_params->extra_ies freed by hisi_scan_free */
        scan_params->extra_ies = (uint8 *)os_zalloc(params->extra_ies_len);
        if (scan_params->extra_ies == NULL)
            return -HISI_EFAIL;

        rc = memcpy_s(scan_params->extra_ies, params->extra_ies_len, params->extra_ies, params->extra_ies_len);
        if (rc != EOK) {
            return -HISI_EFAIL;
        }
        scan_params->extra_ies_len = params->extra_ies_len;
    }
    return HISI_SUCC;
}

int hisi_scan_process_freq(const struct wpa_driver_scan_params *params, hisi_scan_stru *scan_params)
{
    uint32 num_freqs;
    int32 *freqs = NULL;
    errno_t rc;

    if (params->freqs != NULL) {
        num_freqs = 0;
        freqs = params->freqs;

        /* Calculate the number of channels, non-zero is a valid value */
        for (; *freqs != 0; freqs++)
            num_freqs++;

        scan_params->num_freqs = num_freqs;
        /* scan_params->freqs freed by hisi_scan_free */
        scan_params->freqs = (int32 *)os_zalloc(num_freqs * (sizeof(int32)));
        if (scan_params->freqs == NULL)
            return -HISI_EFAIL;
        rc = memcpy_s(scan_params->freqs, num_freqs * (sizeof(int32)), params->freqs, num_freqs * (sizeof(int32)));
        if (rc != EOK) {
            return -HISI_EFAIL;
        }
    }
    return HISI_SUCC;
}

int32 hisi_scan(void *priv, struct wpa_driver_scan_params *params)
{
    hisi_scan_stru *scan_params = NULL;
    hisi_driver_data_stru *drv = NULL;
    int32 timeout;
    int32 ret;

    if ((priv == NULL) || (params == NULL) || (params->num_ssids > WPAS_MAX_SCAN_SSIDS))
        return -HISI_EFAIL;
    drv = (hisi_driver_data_stru *)priv;
    scan_params = (hisi_scan_stru *)os_zalloc(sizeof(hisi_scan_stru));
    if (scan_params == NULL)
        return -HISI_EFAIL;

    if ((hisi_scan_process_ssid(params, scan_params) != HISI_SUCC) ||
        (hisi_scan_process_bssid(params, scan_params) != HISI_SUCC) ||
        (hisi_scan_process_extra_ies(params, scan_params) != HISI_SUCC) ||
        (hisi_scan_process_freq(params, scan_params) != HISI_SUCC)) {
        hisi_scan_free(scan_params);
        return -HISI_EFAIL;
    }
    scan_params->fast_connect_flag = WPA_FLAG_OFF;
    scan_params->prefix_ssid_scan_flag = g_ssid_prefix_flag;
    wpa_printf(MSG_ERROR, "prefix_ssid_scan_flag = %d", scan_params->prefix_ssid_scan_flag);
    ret = hisi_ioctl_scan(drv->iface, scan_params);
    hisi_scan_free(scan_params);

    timeout = SCAN_TIME_OUT;
    eloop_cancel_timeout(hisi_driver_scan_timeout, drv, drv->ctx);
    eloop_register_timeout(timeout, 0, hisi_driver_scan_timeout, drv, drv->ctx);

    return ret;
}

/*****************************************************************************
* Name         : hisi_get_scan_results
* Description  : get scan results
* Input param  : void *priv
* Return value : struct wpa_scan_results *
*****************************************************************************/
struct wpa_scan_results * hisi_get_scan_results(void *priv)
{
    struct wpa_scan_results *results = NULL;
    hisi_driver_data_stru *drv = priv;
    uint32 loop;
    errno_t rc;

    if ((priv == NULL))
        return NULL;

    results = (struct wpa_scan_results *)os_zalloc(sizeof(struct wpa_scan_results));
    if (results == NULL)
        return NULL;

    results->num = drv->scan_ap_num;
    if (results->num == 0) {
        os_free(results);
        return NULL;
    }
    results->res = (struct wpa_scan_res **)os_zalloc(results->num * sizeof(struct wpa_scan_res *));
    if (results->res == NULL) {
        os_free(results);
        return NULL;
    }
    rc = memcpy_s(results->res, results->num * sizeof(struct wpa_scan_res *),
                  drv->res, results->num * sizeof(struct wpa_scan_res *));
    if (rc != EOK) {
        os_free(results->res);
        os_free(results);
        return NULL;
    }
    drv->scan_ap_num = 0;
    for (loop = 0; loop < SCAN_AP_LIMIT; loop++)
        drv->res[loop] = NULL;
    wpa_printf(MSG_DEBUG, "Received scan results (%u BSSes)", (uint32) results->num);
    return results;
}

/*****************************************************************************
* Name         : hisi_cipher_to_cipher_suite
* Description  : get cipher suite from cipher
* Input param  : uint32 cipher
* Return value : uint32
*****************************************************************************/
uint32 hisi_cipher_to_cipher_suite(uint32 cipher)
{
    switch (cipher) {
        case WPA_CIPHER_CCMP_256:
            return RSN_CIPHER_SUITE_CCMP_256;
        case WPA_CIPHER_GCMP_256:
            return RSN_CIPHER_SUITE_GCMP_256;
        case WPA_CIPHER_CCMP:
            return RSN_CIPHER_SUITE_CCMP;
        case WPA_CIPHER_GCMP:
            return RSN_CIPHER_SUITE_GCMP;
        case WPA_CIPHER_TKIP:
            return RSN_CIPHER_SUITE_TKIP;
        case WPA_CIPHER_WEP104:
            return RSN_CIPHER_SUITE_WEP104;
        case WPA_CIPHER_WEP40:
            return RSN_CIPHER_SUITE_WEP40;
        case WPA_CIPHER_GTK_NOT_USED:
            return RSN_CIPHER_SUITE_NO_GROUP_ADDRESSED;
        default:
            return 0;
    }
}

void hisi_set_conn_keys(const struct wpa_driver_associate_params *wpa_params, hisi_associate_params_stru *params)
{
    int32 loop;
    uint8 privacy = 0; /* The initial value means unencrypted */
    errno_t rc;

    if ((wpa_params == NULL) || (params == NULL))
        return;

    for (loop = 0; loop < 4; loop++) {
        if (wpa_params->wep_key[loop] == NULL)
            continue;
        privacy = 1;
        break;
    }

    if ((wpa_params->wps == WPS_MODE_PRIVACY) ||
        ((wpa_params->pairwise_suite != 0) && (wpa_params->pairwise_suite != WPA_CIPHER_NONE)))
        privacy = 1;
    if (privacy == 0)
        return;
    params->privacy = privacy;
    for (loop = 0; loop < 4; loop++) {
        if (wpa_params->wep_key[loop] == NULL)
            continue;

        params->key_len = wpa_params->wep_key_len[loop];
        params->key = (uint8 *)os_zalloc(params->key_len);
        if (params->key == NULL)
            return;

        rc = memcpy_s(params->key, params->key_len, wpa_params->wep_key[loop], params->key_len);
        if (rc != EOK) {
            os_free(params->key);
            params->key = NULL;
            return;
        }
        params->key_idx = wpa_params->wep_tx_keyidx;
        break;
    }

    return;
}

static void hisi_connect_free(hisi_associate_params_stru *assoc_params)
{
    if (assoc_params == NULL)
        return;

    if (assoc_params->ie != NULL) {
        os_free(assoc_params->ie);
        assoc_params->ie = NULL;
    }
    if (assoc_params->crypto != NULL) {
        os_free(assoc_params->crypto);
        assoc_params->crypto = NULL;
    }
    if (assoc_params->ssid != NULL) {
        os_free(assoc_params->ssid);
        assoc_params->ssid = NULL;
    }
    if (assoc_params->bssid != NULL) {
        os_free(assoc_params->bssid);
        assoc_params->bssid = NULL;
    }
    if (assoc_params->key != NULL) {
        os_free(assoc_params->key);
        assoc_params->key = NULL;
    }

    os_free(assoc_params);
}

static int hisi_assoc_params_set(hisi_driver_data_stru *drv,
                                 struct wpa_driver_associate_params *params,
                                 hisi_associate_params_stru *assoc_params)
{
    if (params->bssid != NULL) {
        assoc_params->bssid = (uint8 *)os_zalloc(ETH_ALEN); /* freed by hisi_connect_free */
        if (assoc_params->bssid == NULL)
            return -HISI_EFAIL;

        if (memcpy_s(assoc_params->bssid, ETH_ALEN, params->bssid, ETH_ALEN) != EOK)
            return -HISI_EFAIL;
    }

    if (params->freq.freq != 0)
        assoc_params->freq = params->freq.freq;

    if (params->ssid_len > MAX_SSID_LEN)
        params->ssid_len = MAX_SSID_LEN;

    if ((params->ssid != NULL) && (params->ssid_len != 0)) {
        assoc_params->ssid = (uint8 *)os_zalloc(params->ssid_len); /* freed by hisi_connect_free */
        if (assoc_params->ssid == NULL)
            return -HISI_EFAIL;
        assoc_params->ssid_len = params->ssid_len;
        if (memcpy_s(assoc_params->ssid, assoc_params->ssid_len, params->ssid, params->ssid_len) != EOK)
            return -HISI_EFAIL;
        if (memset_s(drv->ssid, MAX_SSID_LEN, 0, MAX_SSID_LEN) != EOK)
            return -HISI_EFAIL;
        if (memcpy_s(drv->ssid, MAX_SSID_LEN, params->ssid, params->ssid_len) != EOK)
            return -HISI_EFAIL;
        drv->ssid_len = params->ssid_len;
    }

    if ((params->wpa_ie != NULL) && (params->wpa_ie_len != 0)) {
        assoc_params->ie = (uint8 *)os_zalloc(params->wpa_ie_len); /* freed by hisi_connect_free */
        if (assoc_params->ie == NULL)
            return -HISI_EFAIL;
        assoc_params->ie_len = params->wpa_ie_len;
        if (memcpy_s(assoc_params->ie, assoc_params->ie_len, params->wpa_ie, params->wpa_ie_len) != EOK)
            return -HISI_EFAIL;
    }

    return HISI_SUCC;
}

static int hisi_assoc_param_crypto_set(const struct wpa_driver_associate_params *params,
                                       hisi_associate_params_stru *assoc_params)
{
    hisi_wpa_versions_enum wpa_ver = 0;
    uint32 akm_suites_num = 0;
    uint32 ciphers_pairwise_num = 0;
    int32 mgmt = RSN_AUTH_KEY_MGMT_PSK_OVER_802_1X;
    /* assoc_params->crypto freed by hisi_connect_free */
    assoc_params->crypto = (hisi_crypto_settings_stru *)os_zalloc(sizeof(hisi_crypto_settings_stru));
    if (assoc_params->crypto == NULL)
        return -HISI_EFAIL;

    if (params->wpa_proto != 0) {
        if (params->wpa_proto & WPA_PROTO_WPA)
            wpa_ver |= HISI_WPA_VERSION_1;
        if (params->wpa_proto & WPA_PROTO_RSN)
            wpa_ver |= HISI_WPA_VERSION_2;
        assoc_params->crypto->wpa_versions = wpa_ver;
    }

    if (params->pairwise_suite != WPA_CIPHER_NONE) {
        assoc_params->crypto->ciphers_pairwise[ciphers_pairwise_num++]
            = hisi_cipher_to_cipher_suite(params->pairwise_suite);
        assoc_params->crypto->n_ciphers_pairwise = ciphers_pairwise_num;
    }

    if (params->group_suite != WPA_CIPHER_NONE)
        assoc_params->crypto->cipher_group = hisi_cipher_to_cipher_suite(params->group_suite);

    if (params->key_mgmt_suite == WPA_KEY_MGMT_PSK ||
        params->key_mgmt_suite == WPA_KEY_MGMT_SAE ||
        params->key_mgmt_suite == WPA_KEY_MGMT_PSK_SHA256) {
        switch (params->key_mgmt_suite) {
            case WPA_KEY_MGMT_PSK_SHA256:
                mgmt = RSN_AUTH_KEY_MGMT_PSK_SHA256;
                break;
            case WPA_KEY_MGMT_SAE:
                mgmt = RSN_AUTH_KEY_MGMT_SAE;
                break;
            case WPA_KEY_MGMT_PSK: /* fall through */
            default:
                mgmt = RSN_AUTH_KEY_MGMT_PSK_OVER_802_1X;
                break;
        }
        assoc_params->crypto->akm_suites[akm_suites_num++] = mgmt;
        assoc_params->crypto->n_akm_suites = akm_suites_num;
    }

    return HISI_SUCC;
}

int32 hisi_try_connect(hisi_driver_data_stru *drv, struct wpa_driver_associate_params *params)
{
    hisi_associate_params_stru *assoc_params = NULL;
    hisi_auth_type_enum type = HISI_AUTHTYPE_BUTT;
    int32 algs = 0;

    if ((drv == NULL) || (params == NULL))
        return -HISI_EFAIL;

    assoc_params = (hisi_associate_params_stru *)os_zalloc(sizeof(hisi_associate_params_stru));
    if (assoc_params == NULL)
        goto failure;

    if (hisi_assoc_params_set(drv, params, assoc_params) != HISI_SUCC)
        goto failure;

    if (hisi_assoc_param_crypto_set(params, assoc_params) != HISI_SUCC)
        goto failure;

    assoc_params->mfp = params->mgmt_frame_protection;
    wpa_printf(MSG_ERROR, "hisi try connect: pmf = %d", assoc_params->mfp);

    if ((unsigned int)(params->auth_alg) & WPA_AUTH_ALG_OPEN)
        algs++;
    if ((unsigned int)(params->auth_alg) & WPA_AUTH_ALG_SHARED)
        algs++;
    if ((unsigned int)(params->auth_alg) & WPA_AUTH_ALG_LEAP)
        algs++;
    if (algs > 1) {
        assoc_params->auth_type = HISI_AUTHTYPE_AUTOMATIC;
        goto skip_auth_type;
    }

    if (params->auth_alg & WPA_AUTH_ALG_OPEN)
        type = HISI_AUTHTYPE_OPEN_SYSTEM;
    else if (params->auth_alg & WPA_AUTH_ALG_SHARED)
        type = HISI_AUTHTYPE_SHARED_KEY;
    else if (params->auth_alg & WPA_AUTH_ALG_LEAP)
        type = HISI_AUTHTYPE_NETWORK_EAP;
    else if (params->auth_alg & WPA_AUTH_ALG_FT)
        type = HISI_AUTHTYPE_FT;
    else
        goto failure;

    assoc_params->auth_type = type;

skip_auth_type:
    hisi_set_conn_keys(params, assoc_params);
    if (hisi_ioctl_assoc(drv->iface, assoc_params) != HISI_SUCC) {
        goto failure;
    }

    wpa_printf(MSG_DEBUG, "Connect request send successfully");

    hisi_connect_free(assoc_params);
    return HISI_SUCC;

failure:
    hisi_connect_free(assoc_params);
    return -HISI_EFAIL;
}

int32 hisi_connect(hisi_driver_data_stru *drv, struct wpa_driver_associate_params *params)
{
    int32 ret;
    if ((drv == NULL) || (params == NULL))
        return -HISI_EFAIL;

    ret = hisi_try_connect(drv, params);
    if (ret != HISI_SUCC) {
        if (hisi_disconnect(drv, WLAN_REASON_PREV_AUTH_NOT_VALID))
            return -HISI_EFAIL;
        ret = hisi_try_connect(drv, params);
    }
    return ret;
}


int32 hisi_associate(void *priv, struct wpa_driver_associate_params *params)
{
    hisi_driver_data_stru *drv = priv;
    if ((drv == NULL) || (params == NULL))
        return -HISI_EFAIL;
    return hisi_connect(drv, params);
}

int32 hisi_disconnect(hisi_driver_data_stru *drv, int32 reason_code)
{
    int32 ret;
    uint16 new_reason_code;
    if (drv == NULL)
        return -HISI_EFAIL;
    new_reason_code = (uint16)reason_code;
    ret = hisi_ioctl_disconnet(drv->iface, &new_reason_code);
    if (ret == HISI_SUCC)
        drv->associated = HISI_DISCONNECT;
    return ret;
}

int32 hisi_deauthenticate(void *priv, const uint8 *addr, int32 reason_code)
{
    hisi_driver_data_stru *drv = priv;
    (void)addr;
    if (priv == NULL)
        return -HISI_EFAIL;
    return hisi_disconnect(drv, reason_code);
}

int32 hisi_get_bssid(void *priv, uint8 *bssid)
{
    hisi_driver_data_stru *drv = priv;
    errno_t rc;
    if ((priv == NULL) || (bssid == NULL))
        return -HISI_EFAIL;
    if (drv->associated == HISI_DISCONNECT)
        return -HISI_EFAIL;

    rc = memcpy_s(bssid, ETH_ALEN, drv->bssid, ETH_ALEN);
    if (rc != EOK)
        return -HISI_EFAIL;
    return HISI_SUCC;
}

int32 hisi_get_ssid(void *priv, uint8 *ssid)
{
    hisi_driver_data_stru *drv = priv;
    errno_t rc;
    if ((priv == NULL) || (ssid == NULL))
        return -HISI_EFAIL;
    if (drv->associated == HISI_DISCONNECT)
        return -HISI_EFAIL;
    rc = memcpy_s(ssid, MAX_SSID_LEN, drv->ssid, drv->ssid_len);
    if (rc != EOK)
        return -HISI_EFAIL;
    return (int32)drv->ssid_len;
}

const uint8 * hisi_get_mac_addr(void *priv)
{
    hisi_driver_data_stru *drv = priv;
    if (priv == NULL)
        return NULL;
    return drv->own_addr;
}

int32 hisi_get_drv_flags(void *priv, uint64 *drv_flags)
{
    hisi_driver_data_stru *drv = priv;
    hisi_get_drv_flags_stru *params = NULL;
    int32 ret;

    if ((priv == NULL) || (drv_flags == NULL))
        return -HISI_EFAIL;

    /* get drv_flags from the driver layer */
    params = (hisi_get_drv_flags_stru *)os_zalloc(sizeof(hisi_get_drv_flags_stru));
    if (params == NULL)
        return -HISI_EFAIL;
    params->drv_flags = 0;

    ret = hisi_ioctl_get_drv_flags(drv->iface, params);
    if (ret != HISI_SUCC) {
        wpa_printf(MSG_ERROR, "hisi_get_drv_flags: hisi_ioctl_get_drv_flags failed.");
        os_free(params);
        return -HISI_EFAIL;
    }
    *drv_flags = params->drv_flags;
    os_free(params);
    return ret;
}

int32 hisi_wpa_send_eapol(void *priv, const uint8 *dest, uint16 proto, const uint8 *data, uint32 data_len)
{
    hisi_driver_data_stru *drv = priv;
    int32 ret;
    uint32 frame_len;
    uint8 *frame_buf = NULL;
    uint8 *payload = NULL;
    struct l2_ethhdr *l2_ethhdr = NULL;
    errno_t rc;

    if ((priv == NULL) || (data == NULL) || (dest == NULL)) {
        return -HISI_EFAIL;
    }

    frame_len = data_len + sizeof(struct l2_ethhdr);
    frame_buf = os_zalloc(frame_len);
    if (frame_buf == NULL)
        return -HISI_EFAIL;

    l2_ethhdr = (struct l2_ethhdr *)frame_buf;
    rc = memcpy_s(l2_ethhdr->h_dest, ETH_ADDR_LEN, dest, ETH_ADDR_LEN);
    rc |= memcpy_s(l2_ethhdr->h_source, ETH_ADDR_LEN, drv->own_addr, ETH_ADDR_LEN);
    if (rc != EOK) {
        os_free(frame_buf);
        return -HISI_EFAIL;
    }
    l2_ethhdr->h_proto = host_to_be16(proto);

    payload = (uint8 *)(l2_ethhdr + 1);
    rc = memcpy_s(payload, data_len, data, data_len);
    if (rc != EOK) {
        os_free(frame_buf);
        return -HISI_EFAIL;
    }
    ret = l2_packet_send(drv->eapol_sock, dest, host_to_be16(proto), frame_buf, frame_len);
    os_free(frame_buf);
    return ret;
}

/* hisi_dup_macaddr: malloc mac addr buffer, should be used with os_free()  */
uint8* hisi_dup_macaddr(const uint8 *addr, size_t len)
{
    uint8 *res = NULL;
    errno_t rc;
    if (addr == NULL)
        return NULL;
    res = (uint8 *)os_zalloc(ETH_ADDR_LEN);
    if (res == NULL)
        return NULL;
    rc = memcpy_s(res, ETH_ADDR_LEN, addr, len);
    if (rc) {
        os_free(res);
        return NULL;
    }
    return res;
}

static void hisi_action_data_buf_free(hisi_action_data_stru *hisi_action_data)
{
    if (hisi_action_data == NULL)
        return;

    if (hisi_action_data->dst != NULL) {
        os_free(hisi_action_data->dst);
        hisi_action_data->dst = NULL;
    }
    if (hisi_action_data->src != NULL) {
        os_free(hisi_action_data->src);
        hisi_action_data->src = NULL;
    }
    if (hisi_action_data->bssid != NULL) {
        os_free(hisi_action_data->bssid);
        hisi_action_data->bssid = NULL;
    }
    if (hisi_action_data->data != NULL) {
        os_free(hisi_action_data->data);
        hisi_action_data->data = NULL;
    }
}

int32 hisi_send_action(void *priv, unsigned int freq, unsigned int wait, const u8 *dst, const u8 *src,
                       const u8 *bssid, const u8 *data, size_t data_len, int no_cck)
{
    hisi_action_data_stru hisi_action_data = {0};
    hisi_driver_data_stru *drv = NULL;
    int32 ret;
    (void)freq;
    (void)wait;
    (void)no_cck;
    ret = (priv == NULL) || (data == NULL) || (dst == NULL) || (src == NULL);
    if (ret)
        return -HISI_EFAIL;
    drv = (hisi_driver_data_stru *)priv;
    hisi_action_data.data_len = data_len;
    hisi_action_data.dst = hisi_dup_macaddr(dst, ETH_ADDR_LEN);
    if (hisi_action_data.dst == NULL)
        return -HISI_EFAIL;
    hisi_action_data.src = hisi_dup_macaddr(src, ETH_ADDR_LEN);
    if (hisi_action_data.src == NULL) {
        hisi_action_data_buf_free(&hisi_action_data);
        return -HISI_EFAIL;
    }
    hisi_action_data.bssid = hisi_dup_macaddr(bssid, ETH_ADDR_LEN);
    if (hisi_action_data.bssid == NULL) {
        hisi_action_data_buf_free(&hisi_action_data);
        return -HISI_EFAIL;
    }
    hisi_action_data.data = (uint8 *)dup_binstr(data, data_len);
    if (hisi_action_data.data == NULL) {
        hisi_action_data_buf_free(&hisi_action_data);
        return -HISI_EFAIL;
    }
    ret = hisi_ioctl_send_action(drv->iface, &hisi_action_data);
    hisi_action_data_buf_free(&hisi_action_data);
    return ret;
}

int32 hisi_sta_remove(void *priv, const uint8 *addr)
{
    hisi_driver_data_stru *drv = NULL;
    int32 ret;

    if ((priv == NULL) || (addr == NULL))
        return -HISI_EFAIL;
    drv = (hisi_driver_data_stru *)priv;
    ret = hisi_ioctl_sta_remove(drv->iface, addr);
    if (ret != HISI_SUCC)
        return -HISI_EFAIL;
    return HISI_SUCC;
}

/* wpa_supplicant driver ops */
const struct wpa_driver_ops wpa_driver_hisi_ops =
{
    .name                     = "hisi",
    .desc                     = "hisi liteos driver",
    .get_bssid                = hisi_get_bssid,
    .get_ssid                 = hisi_get_ssid,
    .set_key                  = hisi_set_key,
    .scan2                    = hisi_scan,
    .get_scan_results2        = hisi_get_scan_results,
    .deauthenticate           = hisi_deauthenticate,
    .associate                = hisi_associate,
    .send_eapol               = hisi_wpa_send_eapol,
    .init2                    = hisi_wpa_init,
    .deinit                   = hisi_wpa_deinit,
    .set_ap                   = hisi_set_ap,
    .send_mlme                = hisi_send_mlme,
    .get_hw_feature_data      = hisi_get_hw_feature_data,
    .sta_remove               = hisi_sta_remove,
    .hapd_send_eapol          = hisi_send_eapol,
    .hapd_init                = hisi_hapd_init,
    .hapd_deinit              = hisi_hapd_deinit,
    .send_action              = hisi_send_action,
    .get_mac_addr             = hisi_get_mac_addr,
};

#ifdef __cplusplus
#if __cplusplus
    }
#endif
#endif