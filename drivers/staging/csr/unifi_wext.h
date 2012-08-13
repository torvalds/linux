/*
 *****************************************************************************
 *
 * FILE : unifi_wext.h
 *
 * PURPOSE : Private header file for unifi driver support to wireless extensions.
 *
 * Copyright (C) 2005-2008 by Cambridge Silicon Radio Ltd.
 *
 * Refer to LICENSE.txt included with this source code for details on
 * the license terms.
 *
*****************************************************************************
 */
#ifndef __LINUX_UNIFI_WEXT_H__
#define __LINUX_UNIFI_WEXT_H__ 1

#include <linux/kernel.h>
#include <net/iw_handler.h>
#include "csr_wifi_sme_prim.h"

/*
 *      wext.c
 */
/* A few details needed for WEP (Wireless Equivalent Privacy) */
#define UNIFI_MAX_KEY_SIZE      16
#define NUM_WEPKEYS              4
#define SMALL_KEY_SIZE           5
#define LARGE_KEY_SIZE          13
typedef struct wep_key_t {
    int len;
    unsigned char key[UNIFI_MAX_KEY_SIZE];  /* 40-bit and 104-bit keys */
} wep_key_t;

#define UNIFI_SCAN_ACTIVE       0
#define UNIFI_SCAN_PASSIVE      1
#define UNIFI_MAX_SSID_LEN      32

#define MAX_WPA_IE_LEN 64
#define MAX_RSN_IE_LEN 255

/*
 * Function to register in the netdev to report wireless stats.
 */
struct iw_statistics *unifi_get_wireless_stats(struct net_device *dev);

void uf_sme_wext_set_defaults(unifi_priv_t *priv);


/*
 *      wext_events.c
 */
/* Functions to generate Wireless Extension events */
void wext_send_scan_results_event(unifi_priv_t *priv);
void wext_send_assoc_event(unifi_priv_t *priv, unsigned char *bssid,
                           unsigned char *req_ie, int req_ie_len,
                           unsigned char *resp_ie, int resp_ie_len,
                           unsigned char *scan_ie, unsigned int scan_ie_len);
void wext_send_disassoc_event(unifi_priv_t *priv);
void wext_send_michaelmicfailure_event(unifi_priv_t *priv,
                                       u16 count, CsrWifiMacAddress address,
                                       CsrWifiSmeKeyType keyType, u16 interfaceTag);
void wext_send_pmkid_candidate_event(unifi_priv_t *priv, CsrWifiMacAddress bssid, u8 preauth_allowed, u16 interfaceTag);
void wext_send_started_event(unifi_priv_t *priv);


static inline int
uf_iwe_stream_add_point(struct iw_request_info *info, char *start, char *stop,
                        struct iw_event *piwe, char *extra)
{
    char *new_start;

    new_start = iwe_stream_add_point(
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 27) || defined (IW_REQUEST_FLAG_COMPAT)
                                     info,
#endif
                                     start, stop, piwe, extra);
    if (unlikely(new_start == start))
    {
        return -E2BIG;
    }

    return (new_start - start);
}


static inline int
uf_iwe_stream_add_event(struct iw_request_info *info, char *start, char *stop,
                        struct iw_event *piwe, int len)
{
    char *new_start;

    new_start = iwe_stream_add_event(
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 27) || defined(IW_REQUEST_FLAG_COMPAT)
                                     info,
#endif
                                     start, stop, piwe, len);
    if (unlikely(new_start == start)) {
        return -E2BIG;
    }

    return (new_start - start);
}

static inline int
uf_iwe_stream_add_value(struct iw_request_info *info, char *stream, char *start,
                        char *stop, struct iw_event *piwe, int len)
{
    char *new_start;

    new_start = iwe_stream_add_value(
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 27) || defined(IW_REQUEST_FLAG_COMPAT)
                                     info,
#endif
                                     stream, start, stop, piwe, len);
    if (unlikely(new_start == start)) {
        return -E2BIG;
    }

    return (new_start - start);
}


#endif /* __LINUX_UNIFI_WEXT_H__ */
