//------------------------------------------------------------------------------
// Copyright (c) 2004-2010 Atheros Communications Inc.
// All rights reserved.
//
// 
//
// Permission to use, copy, modify, and/or distribute this software for any
// purpose with or without fee is hereby granted, provided that the above
// copyright notice and this permission notice appear in all copies.
//
// THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
// WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
// MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
// ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
// WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
// ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
// OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
//
//
//
// Author(s): ="Atheros"
//------------------------------------------------------------------------------

#include "ar6000_drv.h"

#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,27)
#define IWE_STREAM_ADD_EVENT(p1, p2, p3, p4, p5) \
    iwe_stream_add_event((p1), (p2), (p3), (p4), (p5))
#else
#define IWE_STREAM_ADD_EVENT(p1, p2, p3, p4, p5) \
    iwe_stream_add_event((p2), (p3), (p4), (p5))
#endif

#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,27)
#define IWE_STREAM_ADD_POINT(p1, p2, p3, p4, p5) \
    iwe_stream_add_point((p1), (p2), (p3), (p4), (p5))
#else
#define IWE_STREAM_ADD_POINT(p1, p2, p3, p4, p5) \
    iwe_stream_add_point((p2), (p3), (p4), (p5))
#endif

#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,27)
#define IWE_STREAM_ADD_VALUE(p1, p2, p3, p4, p5, p6) \
    iwe_stream_add_value((p1), (p2), (p3), (p4), (p5), (p6))
#else
#define IWE_STREAM_ADD_VALUE(p1, p2, p3, p4, p5, p6) \
    iwe_stream_add_value((p2), (p3), (p4), (p5), (p6))
#endif

static void ar6000_set_quality(struct iw_quality *iq, A_INT8 rssi);
extern unsigned int wmitimeout;

#if WIRELESS_EXT > 14
/*
 * Encode a WPA or RSN information element as a custom
 * element using the hostap format.
 */
static u_int
encode_ie(void *buf, size_t bufsize,
    const u_int8_t *ie, size_t ielen,
    const char *leader, size_t leader_len)
{
    u_int8_t *p;
    int i;

    if (bufsize < leader_len)
        return 0;
    p = buf;
    memcpy(p, leader, leader_len);
    bufsize -= leader_len;
    p += leader_len;
    for (i = 0; i < ielen && bufsize > 2; i++)
    {
        p += sprintf((char*)p, "%02x", ie[i]);
        bufsize -= 2;
    }
    return (i == ielen ? p - (u_int8_t *)buf : 0);
}
#endif /* WIRELESS_EXT > 14 */

static A_UINT8
get_bss_phy_capability(bss_t *bss)
{
    A_UINT8 capability = 0;
    struct ieee80211_common_ie *cie = &bss->ni_cie;
#define CHAN_IS_11A(x)              (!((x >= 2412) && (x <= 2484)))
    if (CHAN_IS_11A(cie->ie_chan)) {
        if (cie->ie_htcap) {
            capability = WMI_11NA_CAPABILITY;
        } else {
            capability = WMI_11A_CAPABILITY;
        }
    } else if ((cie->ie_erp) || (cie->ie_xrates)) {
        if (cie->ie_htcap) {
            capability = WMI_11NG_CAPABILITY;
        } else {
            capability = WMI_11G_CAPABILITY;
        }
    }
    return capability;
}

void
ar6000_scan_node(void *arg, bss_t *ni)
{
    struct iw_event iwe;
#if WIRELESS_EXT > 14
    char buf[256];
#endif
    struct ar_giwscan_param *param;
    A_CHAR *current_ev;
    A_CHAR *end_buf;
    struct ieee80211_common_ie  *cie;
    A_CHAR *current_val;
    A_INT32 j;
    A_UINT32 rate_len, data_len = 0;

    /* Node table now contains entries from P2P Action frames and Probe Request also. Return
     * if the frame type is an action frame/ Probe request.
     */
    if ((ni->ni_frametype == ACTION_MGMT_FTYPE) ||  (ni->ni_frametype == PROBEREQ_FTYPE) || (ni->ni_buf == NULL)) {
        return;
    }

    param = (struct ar_giwscan_param *)arg;

    current_ev = param->current_ev;
    end_buf = param->end_buf;

    cie = &ni->ni_cie;

    if ((end_buf - current_ev) > IW_EV_ADDR_LEN)
    {
        A_MEMZERO(&iwe, sizeof(iwe));
        iwe.cmd = SIOCGIWAP;
        iwe.u.ap_addr.sa_family = ARPHRD_ETHER;
        A_MEMCPY(iwe.u.ap_addr.sa_data, ni->ni_macaddr, 6);
        current_ev = IWE_STREAM_ADD_EVENT(param->info, current_ev, end_buf,
                                          &iwe, IW_EV_ADDR_LEN);
    }
    param->bytes_needed += IW_EV_ADDR_LEN;

    data_len = cie->ie_ssid[1] + IW_EV_POINT_LEN;
    if ((end_buf - current_ev) > data_len)
    {
        A_MEMZERO(&iwe, sizeof(iwe));
        iwe.cmd = SIOCGIWESSID;
        iwe.u.data.flags = 1;
        iwe.u.data.length = cie->ie_ssid[1];
        current_ev = IWE_STREAM_ADD_POINT(param->info, current_ev, end_buf,
                                          &iwe, (char*)&cie->ie_ssid[2]);
    }
    param->bytes_needed += data_len;

    if (cie->ie_capInfo & (IEEE80211_CAPINFO_ESS|IEEE80211_CAPINFO_IBSS)) {
        if ((end_buf - current_ev) > IW_EV_UINT_LEN)
        {
            A_MEMZERO(&iwe, sizeof(iwe));
            iwe.cmd = SIOCGIWMODE;
            iwe.u.mode = cie->ie_capInfo & IEEE80211_CAPINFO_ESS ?
                         IW_MODE_MASTER : IW_MODE_ADHOC;
            current_ev = IWE_STREAM_ADD_EVENT(param->info, current_ev, end_buf,
                                              &iwe, IW_EV_UINT_LEN);
        }
        param->bytes_needed += IW_EV_UINT_LEN;
    }

    if ((end_buf - current_ev) > IW_EV_FREQ_LEN)
    {
        A_MEMZERO(&iwe, sizeof(iwe));
        iwe.cmd = SIOCGIWFREQ;
        iwe.u.freq.m = cie->ie_chan * 100000;
        iwe.u.freq.e = 1;
        current_ev = IWE_STREAM_ADD_EVENT(param->info, current_ev, end_buf,
                                          &iwe, IW_EV_FREQ_LEN);
    }
    param->bytes_needed += IW_EV_FREQ_LEN;

    if ((end_buf - current_ev) > IW_EV_QUAL_LEN)
    {
        A_MEMZERO(&iwe, sizeof(iwe));
        iwe.cmd = IWEVQUAL;
        ar6000_set_quality(&iwe.u.qual, ni->ni_snr);
        current_ev = IWE_STREAM_ADD_EVENT(param->info, current_ev, end_buf,
                                          &iwe, IW_EV_QUAL_LEN);
    }
    param->bytes_needed += IW_EV_QUAL_LEN;

    if ((end_buf - current_ev) > IW_EV_POINT_LEN)
    {
        A_MEMZERO(&iwe, sizeof(iwe));
        iwe.cmd = SIOCGIWENCODE;
        if (cie->ie_capInfo & IEEE80211_CAPINFO_PRIVACY) {
            iwe.u.data.flags = IW_ENCODE_ENABLED | IW_ENCODE_NOKEY;
        } else {
            iwe.u.data.flags = IW_ENCODE_DISABLED;
        }
        iwe.u.data.length = 0;
        current_ev = IWE_STREAM_ADD_POINT(param->info, current_ev, end_buf,
                                          &iwe, "");
    }
    param->bytes_needed += IW_EV_POINT_LEN;

    /* supported bit rate */
    A_MEMZERO(&iwe, sizeof(iwe));
    iwe.cmd = SIOCGIWRATE;
    iwe.u.bitrate.fixed = 0;
    iwe.u.bitrate.disabled = 0;
    iwe.u.bitrate.value = 0;
    current_val = current_ev + IW_EV_LCP_LEN;
    param->bytes_needed += IW_EV_LCP_LEN;

    if (cie->ie_rates != NULL) {
        rate_len = cie->ie_rates[1];
        data_len = (rate_len * (IW_EV_PARAM_LEN - IW_EV_LCP_LEN));
        if ((end_buf - current_ev) > data_len)
        {
            for (j = 0; j < rate_len; j++) {
                    unsigned char val;
                    val = cie->ie_rates[2 + j];
                    iwe.u.bitrate.value =
                        (val >= 0x80)? ((val - 0x80) * 500000): (val * 500000);
                    current_val = IWE_STREAM_ADD_VALUE(param->info, current_ev,
                                                       current_val, end_buf,
                                                       &iwe, IW_EV_PARAM_LEN);
            }
        }
        param->bytes_needed += data_len;
    }

    if (cie->ie_xrates != NULL) {
        rate_len = cie->ie_xrates[1];
        data_len = (rate_len * (IW_EV_PARAM_LEN - IW_EV_LCP_LEN));
        if ((end_buf - current_ev) > data_len)
        {
            for (j = 0; j < rate_len; j++) {
                    unsigned char val;
                    val = cie->ie_xrates[2 + j];
                    iwe.u.bitrate.value =
                        (val >= 0x80)? ((val - 0x80) * 500000): (val * 500000);
                    current_val = IWE_STREAM_ADD_VALUE(param->info, current_ev,
                                                       current_val, end_buf,
                                                       &iwe, IW_EV_PARAM_LEN);
            }
        }
        param->bytes_needed += data_len;
    }
    /* remove fixed header if no rates were added */
    if ((current_val - current_ev) > IW_EV_LCP_LEN)
        current_ev = current_val;

#if WIRELESS_EXT >= 18
    /* IE */
    if (cie->ie_wpa != NULL) {
        data_len = cie->ie_wpa[1] + 2 + IW_EV_POINT_LEN;
        if ((end_buf - current_ev) > data_len)
        {
            A_MEMZERO(&iwe, sizeof(iwe));
            iwe.cmd = IWEVGENIE;
            iwe.u.data.length = cie->ie_wpa[1] + 2;
            current_ev = IWE_STREAM_ADD_POINT(param->info, current_ev, end_buf,
                                              &iwe, (char*)cie->ie_wpa);
        }
        param->bytes_needed += data_len;
    }

    if (cie->ie_rsn != NULL && cie->ie_rsn[0] == IEEE80211_ELEMID_RSN) {
        data_len = cie->ie_rsn[1] + 2 + IW_EV_POINT_LEN;
        if ((end_buf - current_ev) > data_len)
        {
            A_MEMZERO(&iwe, sizeof(iwe));
            iwe.cmd = IWEVGENIE;
            iwe.u.data.length = cie->ie_rsn[1] + 2;
            current_ev = IWE_STREAM_ADD_POINT(param->info, current_ev, end_buf,
                                              &iwe, (char*)cie->ie_rsn);
        }
        param->bytes_needed += data_len;
    }

#endif /* WIRELESS_EXT >= 18 */

    if ((end_buf - current_ev) > IW_EV_CHAR_LEN)
    {
        /* protocol */
        A_MEMZERO(&iwe, sizeof(iwe));
        iwe.cmd = SIOCGIWNAME;
        switch (get_bss_phy_capability(ni)) {
        case WMI_11A_CAPABILITY:
            snprintf(iwe.u.name, IFNAMSIZ, "IEEE 802.11a");
            break;
        case WMI_11G_CAPABILITY:
            snprintf(iwe.u.name, IFNAMSIZ, "IEEE 802.11g");
            break;
        case WMI_11NA_CAPABILITY:
            snprintf(iwe.u.name, IFNAMSIZ, "IEEE 802.11na");
            break;
        case WMI_11NG_CAPABILITY:
            snprintf(iwe.u.name, IFNAMSIZ, "IEEE 802.11ng");
            break;
        default:
            snprintf(iwe.u.name, IFNAMSIZ, "IEEE 802.11b");
            break;
        }
        current_ev = IWE_STREAM_ADD_EVENT(param->info, current_ev, end_buf,
                                          &iwe, IW_EV_CHAR_LEN);
    }
    param->bytes_needed += IW_EV_CHAR_LEN;

#if WIRELESS_EXT > 14
    A_MEMZERO(&iwe, sizeof(iwe));
    iwe.cmd = IWEVCUSTOM;
    iwe.u.data.length = snprintf(buf, sizeof(buf), "bcn_int=%d", cie->ie_beaconInt);
    data_len = iwe.u.data.length + IW_EV_POINT_LEN;
    if ((end_buf - current_ev) > data_len)
    {
        current_ev = IWE_STREAM_ADD_POINT(param->info, current_ev, end_buf,
                                          &iwe, buf);
    }
    param->bytes_needed += data_len;

#if WIRELESS_EXT < 18
    if (cie->ie_wpa != NULL) {
        static const char wpa_leader[] = "wpa_ie=";
        data_len = (sizeof(wpa_leader) - 1) + ((cie->ie_wpa[1]+2) * 2) + IW_EV_POINT_LEN;
        if ((end_buf - current_ev) > data_len)
        {
            A_MEMZERO(&iwe, sizeof(iwe));
            iwe.cmd = IWEVCUSTOM;
            iwe.u.data.length = encode_ie(buf, sizeof(buf), cie->ie_wpa,
                                          cie->ie_wpa[1]+2,
                                          wpa_leader, sizeof(wpa_leader)-1);

            if (iwe.u.data.length != 0) {
                current_ev = IWE_STREAM_ADD_POINT(param->info, current_ev, 
                                                  end_buf, &iwe, buf);
            }
        }
        param->bytes_needed += data_len;
    }

    if (cie->ie_rsn != NULL && cie->ie_rsn[0] == IEEE80211_ELEMID_RSN) {
        static const char rsn_leader[] = "rsn_ie=";
        data_len = (sizeof(rsn_leader) - 1) + ((cie->ie_rsn[1]+2) * 2) + IW_EV_POINT_LEN;
        if ((end_buf - current_ev) > data_len)
        {
            A_MEMZERO(&iwe, sizeof(iwe));
            iwe.cmd = IWEVCUSTOM;
            iwe.u.data.length = encode_ie(buf, sizeof(buf), cie->ie_rsn,
                                          cie->ie_rsn[1]+2,
                                          rsn_leader, sizeof(rsn_leader)-1);

            if (iwe.u.data.length != 0) {
                current_ev = IWE_STREAM_ADD_POINT(param->info, current_ev, 
                                                  end_buf, &iwe, buf);
            }
        }
        param->bytes_needed += data_len;
    }
#endif /* WIRELESS_EXT < 18 */

    if (cie->ie_wmm != NULL) {
        static const char wmm_leader[] = "wmm_ie=";
        data_len = (sizeof(wmm_leader) - 1) + ((cie->ie_wmm[1]+2) * 2) + IW_EV_POINT_LEN;
        if ((end_buf - current_ev) > data_len)
        {
            A_MEMZERO(&iwe, sizeof(iwe));
            iwe.cmd = IWEVCUSTOM;
            iwe.u.data.length = encode_ie(buf, sizeof(buf), cie->ie_wmm,
                                          cie->ie_wmm[1]+2,
                                          wmm_leader, sizeof(wmm_leader)-1);
            if (iwe.u.data.length != 0) {
                current_ev = IWE_STREAM_ADD_POINT(param->info, current_ev,
                                                  end_buf, &iwe, buf);
            }
        }
        param->bytes_needed += data_len;
    }

    if (cie->ie_ath != NULL) {
        static const char ath_leader[] = "ath_ie=";
        data_len = (sizeof(ath_leader) - 1) + ((cie->ie_ath[1]+2) * 2) + IW_EV_POINT_LEN;
        if ((end_buf - current_ev) > data_len)
        {
            A_MEMZERO(&iwe, sizeof(iwe));
            iwe.cmd = IWEVCUSTOM;
            iwe.u.data.length = encode_ie(buf, sizeof(buf), cie->ie_ath,
                                          cie->ie_ath[1]+2,
                                          ath_leader, sizeof(ath_leader)-1);
            if (iwe.u.data.length != 0) {
                current_ev = IWE_STREAM_ADD_POINT(param->info, current_ev,
                                                  end_buf, &iwe, buf);
            }
        }
        param->bytes_needed += data_len;
    }

#ifdef WAPI_ENABLE
    if (cie->ie_wapi != NULL) {
        static const char wapi_leader[] = "wapi_ie=";
        data_len = (sizeof(wapi_leader) - 1) + ((cie->ie_wapi[1] + 2) * 2) + IW_EV_POINT_LEN;
        if ((end_buf - current_ev) > data_len) {
            A_MEMZERO(&iwe, sizeof(iwe));
            iwe.cmd = IWEVCUSTOM;
            iwe.u.data.length = encode_ie(buf, sizeof(buf), cie->ie_wapi,
                                      cie->ie_wapi[1] + 2,
                                      wapi_leader, sizeof(wapi_leader) - 1);
            if (iwe.u.data.length != 0) {
                current_ev = IWE_STREAM_ADD_POINT(param->info, current_ev,
                                                  end_buf, &iwe, buf);
            }
        }
        param->bytes_needed += data_len;
    }
#endif /* WAPI_ENABLE */

#endif /* WIRELESS_EXT > 14 */

#if WIRELESS_EXT >= 18
    if (cie->ie_wsc != NULL) {
        data_len = (cie->ie_wsc[1] + 2) + IW_EV_POINT_LEN;
        if ((end_buf - current_ev) > data_len)
        {
            A_MEMZERO(&iwe, sizeof(iwe));
            iwe.cmd = IWEVGENIE;
            iwe.u.data.length = cie->ie_wsc[1] + 2;
            current_ev = IWE_STREAM_ADD_POINT(param->info, current_ev, end_buf,
                                              &iwe, (char*)cie->ie_wsc);
        }
        param->bytes_needed += data_len;
    }
#endif /* WIRELESS_EXT >= 18 */

    param->current_ev = current_ev;
}

int
ar6000_ioctl_giwscan(struct net_device *dev,
            struct iw_request_info *info,
            struct iw_point *data, char *extra)
{
    AR_SOFTC_DEV_T *arPriv = (AR_SOFTC_DEV_T *)ar6k_priv(dev);
    AR_SOFTC_T     *ar     = arPriv->arSoftc;
    struct ar_giwscan_param param;

    if (is_iwioctl_allowed(arPriv->arNextMode, info->cmd) != A_OK) {
        AR_DEBUG_PRINTF(ATH_DEBUG_ERR,("wext_ioctl: cmd=0x%x not allowed in this mode\n", info->cmd));
        return -EOPNOTSUPP;
    }

    if (ar->arWlanState == WLAN_DISABLED) {
        return -EIO;
    }

    if (ar->arWmiReady == FALSE) {
        return -EIO;
    }

    param.current_ev = extra;
    param.end_buf = extra + data->length;
    param.bytes_needed = 0;
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,27)
    param.info = info;
#endif

    /* Translate data to WE format */
    wmi_iterate_nodes(arPriv->arWmi, ar6000_scan_node, &param);

    /* check if bytes needed is greater than bytes consumed */
    if (param.bytes_needed > (param.current_ev - extra))
    {
        /* Request one byte more than needed, because when "data->length" equals bytes_needed,
        it is not possible to add the last event data as all iwe_stream_add_xxxxx() functions
        checks whether (cur_ptr + ev_len) < end_ptr, due to this one more retry would happen*/
        data->length = param.bytes_needed + 1;

        return -E2BIG;
    }

    return 0;
}

extern int reconnect_flag;
/* SIOCSIWESSID */
static int
ar6000_ioctl_siwessid(struct net_device *dev,
                     struct iw_request_info *info,
                     struct iw_point *data, char *ssid)
{
    AR_SOFTC_DEV_T *arPriv = (AR_SOFTC_DEV_T *)ar6k_priv(dev);
    AR_SOFTC_DEV_T *arTempPriv = NULL;
    AR_SOFTC_T     *ar     = arPriv->arSoftc;
    AR_SOFTC_STA_T *arSta  = &arPriv->arSta;
    A_STATUS status;
    A_UINT8     i=0;

    if (is_iwioctl_allowed(arPriv->arNextMode, info->cmd) != A_OK) {
        AR_DEBUG_PRINTF(ATH_DEBUG_ERR,("wext_ioctl: cmd=0x%x not allowed in this mode\n", info->cmd));
        return -EOPNOTSUPP;
    }

    if (ar->bIsDestroyProgress) {
        return -EBUSY;
    }

    if (ar->arWlanState == WLAN_DISABLED) {
        return -EIO;
    }

    if (ar->arWmiReady == FALSE) {
        return -EIO;
    }

#if defined(WIRELESS_EXT)
    if (WIRELESS_EXT > 20) {
        data->length += 1;
    }
#endif
    /* Handling AP-STA Concurrency */ 
    if((ar->arConfNumDev > 1)) {
        if ((data->flags) && 
            (arPriv->arNetworkType == INFRA_NETWORK) && 
            ((arPriv->arSsidLen != (data->length - 1)) ||
                 (A_MEMCMP(arPriv->arSsid, ssid, arPriv->arSsidLen) != 0))){
            for(i=0;i<ar->arConfNumDev;i++) {
                arTempPriv = ar->arDev[i];
                if((AP_NETWORK == arTempPriv->arNetworkType) &&
                        (arTempPriv->arConnected)) { 
                    wmi_disconnect_cmd(arTempPriv->arWmi);
                    /*Restart AP only in non P2P mode*/
                    if((arTempPriv->arNetworkSubType != SUBTYPE_P2PGO) && 
                        (arTempPriv->arNetworkSubType !=SUBTYPE_P2PDEV)){
                        arTempPriv->arHoldConnection = TRUE;
                        ar->arHoldConnection = TRUE;
                        arTempPriv->ap_profile_flag = TRUE;
                    }
                }
            }
        }
    }
    /*
     * iwconfig passes a null terminated string with length including this
     * so we need to account for this
     */
    if (data->flags && (!data->length || (data->length == 1) ||
        ((data->length - 1) > sizeof(arPriv->arSsid))))
    {
        /*
         * ssid is invalid
         */
        return -EINVAL;
    }

    if (arPriv->arNetworkType == AP_NETWORK) {
        if(!data->flags) {
            /* stop AP */
            wmi_disconnect_cmd(arPriv->arWmi);
            if(arPriv->arNetworkSubType == SUBTYPE_P2PGO) {
                wait_event_interruptible_timeout(arPriv->arEvent, arPriv->arConnected == FALSE, wmitimeout * HZ);
                if (signal_pending(current)) {
                    return -EINTR;
                }
            }
            
        } else if(A_MEMCMP(arPriv->arSsid,ssid,32) != 0) {
            /* SSID change for AP network - Will take effect on commit */
            arPriv->arSsidLen = data->length - 1;
            A_MEMZERO(arPriv->arSsid, WMI_MAX_SSID_LEN);
            A_MEMCPY(arPriv->arSsid, ssid, arPriv->arSsidLen);
        }
        arPriv->ap_profile_flag = 1; /* There is a change in profile */
        arPriv->arConnected = FALSE;
        return 0;
    }
   
    /* Added for bug 25178, return an IOCTL error instead of target returning
       Illegal parameter error when either the BSSID or channel is missing
       and we cannot scan during connect.
     */
    if (data->flags) {
        if (arSta->arSkipScan == TRUE &&
            (arPriv->arChannelHint == 0 ||
             (!arSta->arReqBssid[0] && !arSta->arReqBssid[1] && !arSta->arReqBssid[2] &&
              !arSta->arReqBssid[3] && !arSta->arReqBssid[4] && !arSta->arReqBssid[5])))
        {
            return -EINVAL;
        }
    }

    if (down_interruptible(&ar->arSem)) {
        return -ERESTARTSYS;
    }

    if (ar->bIsDestroyProgress || ar->arWlanState == WLAN_DISABLED) {
        up(&ar->arSem);
        return -EBUSY;
    }

    if (ar->arTxPending[wmi_get_control_ep(arPriv->arWmi)]) {
        /*
         * sleep until the command queue drains
         */
        wait_event_interruptible_timeout(arPriv->arEvent,
            ar->arTxPending[wmi_get_control_ep(arPriv->arWmi)] == 0, wmitimeout * HZ);
        if (signal_pending(current)) {
            up(&ar->arSem);
            return -EINTR;
        }
    }

  
    if (!data->flags) {
#ifdef ATH6K_CONFIG_CFG80211
        if (arPriv->arConnected) {
#endif /* ATH6K_CONFIG_CFG80211 */
            ar6000_init_mode_info(arPriv);
#ifdef ATH6K_CONFIG_CFG80211
        }
#endif /* ATH6K_CONFIG_CFG80211 */
    }

    if (((arPriv->arSsidLen) || 
        ((arPriv->arSsidLen == 0) && (arPriv->arConnected || arSta->arConnectPending)) || 
        (!data->flags)))
    {
        if ((!data->flags) ||
            (A_MEMCMP(arPriv->arSsid, ssid, arPriv->arSsidLen) != 0) ||
            (arPriv->arSsidLen != (data->length - 1)))
        {
            /*
             * SSID set previously or essid off has been issued.
             *
             * Disconnect Command is issued in two cases after wmi is ready
             * (1) ssid is different from the previous setting
             * (2) essid off has been issued
             *
             */
            if (ar->arWmiReady == TRUE) {
                reconnect_flag = 0;
                status = wmi_setPmkid_cmd(arPriv->arWmi, arPriv->arBssid, NULL, 0);
                ar6000_disconnect(arPriv);
                A_MEMZERO(arPriv->arSsid, sizeof(arPriv->arSsid));
                arPriv->arSsidLen = 0;
                if (arSta->arSkipScan == FALSE) {
                    A_MEMZERO(arSta->arReqBssid, sizeof(arSta->arReqBssid));
                }
                if (!data->flags) {
                    up(&ar->arSem);
                    return 0;
                }
            } else {
                 up(&ar->arSem);
            }
        }
        else
        {
            /*
             * SSID is same, so we assume profile hasn't changed.
             * If the interface is up and wmi is ready, we issue
             * a reconnect cmd. Issue a reconnect only we are already
             * connected.
             */
            if((arPriv->arConnected == TRUE) && (ar->arWmiReady == TRUE))
            {
                reconnect_flag = TRUE;
                status = wmi_reconnect_cmd(arPriv->arWmi,arSta->arReqBssid,
                                           arPriv->arChannelHint);
                up(&ar->arSem);
                if (status != A_OK) {
                    return -EIO;
                }
                return 0;
            }
            else{
                /*
                 * Dont return if connect is pending.
                 */
                if(!(arSta->arConnectPending)) {
                    up(&ar->arSem);
                    return 0;
                }
            }
        }
    }

    arPriv->arSsidLen = data->length - 1;
    A_MEMCPY(arPriv->arSsid, ssid, arPriv->arSsidLen);

    if (ar6000_connect_to_ap(arPriv)!= A_OK) {
        up(&ar->arSem);
        return -EIO;
    }else{
      up(&ar->arSem);
    }
    return 0;
}

/* SIOCGIWESSID */
static int
ar6000_ioctl_giwessid(struct net_device *dev,
                     struct iw_request_info *info,
                     struct iw_point *data, char *essid)
{
    AR_SOFTC_DEV_T *arPriv = (AR_SOFTC_DEV_T *)ar6k_priv(dev);
    AR_SOFTC_T     *ar     = arPriv->arSoftc;

    if (is_iwioctl_allowed(arPriv->arNextMode, info->cmd) != A_OK) {
        AR_DEBUG_PRINTF(ATH_DEBUG_ERR,("wext_ioctl: cmd=0x%x not allowed in this mode\n", info->cmd));
        return -EOPNOTSUPP;
    }

    if (ar->arWlanState == WLAN_DISABLED) {
        return -EIO;
    }

    if (!arPriv->arSsidLen) {
        return -EINVAL;
    }

    data->flags = 1;
    data->length = arPriv->arSsidLen;
    A_MEMCPY(essid, arPriv->arSsid, arPriv->arSsidLen);

    return 0;
}


void ar6000_install_static_wep_keys(AR_SOFTC_DEV_T *arPriv)
{
    A_UINT8 index;
    A_UINT8 keyUsage;
    AR_SOFTC_AP_T  *ap = &arPriv->arAp;

    for (index = WMI_MIN_KEY_INDEX; index <= WMI_MAX_KEY_INDEX; index++) {
        if (arPriv->arWepKeyList[index].arKeyLen) {
            keyUsage = GROUP_USAGE;
            if (index == arPriv->arDefTxKeyIndex) {
                keyUsage |= TX_USAGE;
            }
            wmi_addKey_cmd(arPriv->arWmi,
                           index,
                           WEP_CRYPT,
                           keyUsage,
                           arPriv->arWepKeyList[index].arKeyLen,
                           NULL,
                           arPriv->arWepKeyList[index].arKey, KEY_OP_INIT_VAL, NULL,
                           NO_SYNC_WMIFLAG);
        }
    }
    ap->deKeySet = FALSE;
}

/*
 * SIOCSIWRATE
 */
int
ar6000_ioctl_siwrate(struct net_device *dev,
            struct iw_request_info *info,
            struct iw_param *rrq, char *extra)
{
    AR_SOFTC_DEV_T *arPriv = (AR_SOFTC_DEV_T *)ar6k_priv(dev);
    AR_SOFTC_T     *ar     = arPriv->arSoftc;

    A_UINT32  kbps;
    A_INT8  rate_idx;

    if (is_iwioctl_allowed(arPriv->arNextMode, info->cmd) != A_OK) {
        AR_DEBUG_PRINTF(ATH_DEBUG_ERR,("wext_ioctl: cmd=0x%x not allowed in this mode\n", info->cmd));
        return -EOPNOTSUPP;
    }

    if (rrq->fixed) {
        kbps = rrq->value / 1000;           /* rrq->value is in bps */
    } else {
        kbps = -1;                          /* -1 indicates auto rate */
    }
    if(kbps != -1 && wmi_validate_bitrate(arPriv->arWmi, kbps, &rate_idx) != A_OK)
    {
        AR_DEBUG_PRINTF(ATH_DEBUG_ERR,("BitRate is not Valid %d\n", kbps));
        return -EINVAL;
    }
    arPriv->arBitRate = kbps;
    if(ar->arWmiReady == TRUE)
    {
        if (wmi_set_bitrate_cmd(arPriv->arWmi, kbps, -1, -1) != A_OK) {
            return -EINVAL;
        }
    }
    return 0;
}

/*
 * SIOCGIWRATE
 */
int
ar6000_ioctl_giwrate(struct net_device *dev,
            struct iw_request_info *info,
            struct iw_param *rrq, char *extra)
{
    AR_SOFTC_DEV_T *arPriv = (AR_SOFTC_DEV_T *)ar6k_priv(dev);
    AR_SOFTC_T     *ar     = arPriv->arSoftc;
    
    int ret = 0;

    if (is_iwioctl_allowed(arPriv->arNextMode, info->cmd) != A_OK) {
        AR_DEBUG_PRINTF(ATH_DEBUG_ERR,("wext_ioctl: cmd=0x%x not allowed in this mode\n", info->cmd));
        return -EOPNOTSUPP;
    }

    if (ar->bIsDestroyProgress) {
        return -EBUSY;
    }

    if (ar->arWlanState == WLAN_DISABLED) {
        return -EIO;
    }

    if ((arPriv->arNextMode != AP_NETWORK && !arPriv->arConnected) || ar->arWmiReady == FALSE) {
        rrq->value = 1000 * 1000;       
        return 0;
    }

    if (arPriv->arBitRate!=-1 && arPriv->arBitRate!=0xFFFF) {
        rrq->fixed = TRUE;
        rrq->value = arPriv->arBitRate * 1000;
        return 0;
    }

    if (down_interruptible(&ar->arSem)) {
        return -ERESTARTSYS;
    }

    if (ar->bIsDestroyProgress || ar->arWlanState == WLAN_DISABLED) {
        up(&ar->arSem);
        return -EBUSY;
    }

    arPriv->arBitRate = 0xFFFF;
    if (wmi_get_bitrate_cmd(arPriv->arWmi) != A_OK) {
        up(&ar->arSem);
        return -EIO;
    }
    wait_event_interruptible_timeout(arPriv->arEvent, arPriv->arBitRate != 0xFFFF, wmitimeout * HZ);
    if (signal_pending(current)) {
        ret = -EINTR;
    }
    /* If the interface is down or wmi is not ready or the target is not
       connected - return the value stored in the device structure */
    if (!ret) {
        if (arPriv->arBitRate == -1) {
            rrq->fixed = TRUE;
            rrq->value = 0;
        } else {
            rrq->fixed = FALSE;
            rrq->value = arPriv->arBitRate * 1000;
        }
        arPriv->arBitRate = -1; /* clean it up for next query */
    }

    up(&ar->arSem);

    return ret;
}

/*
 * SIOCSIWTXPOW
 */
static int
ar6000_ioctl_siwtxpow(struct net_device *dev,
             struct iw_request_info *info,
             struct iw_param *rrq, char *extra)
{
    AR_SOFTC_DEV_T *arPriv = (AR_SOFTC_DEV_T *)ar6k_priv(dev);
    AR_SOFTC_T     *ar     = arPriv->arSoftc;

    A_UINT8 dbM;

    if (is_iwioctl_allowed(arPriv->arNextMode, info->cmd) != A_OK) {
        AR_DEBUG_PRINTF(ATH_DEBUG_ERR,("wext_ioctl: cmd=0x%x not allowed in this mode\n", info->cmd));
        return -EOPNOTSUPP;
    }

    if (ar->arWlanState == WLAN_DISABLED) {
        return -EIO;
    }

    if (rrq->disabled) {
        return -EOPNOTSUPP;
    }

    if (rrq->fixed) {
        if (rrq->flags != IW_TXPOW_DBM) {
            return -EOPNOTSUPP;
        }
        arPriv->arTxPwr= dbM = rrq->value;
        arPriv->arTxPwrSet = TRUE;
    } else {
        arPriv->arTxPwr = dbM = 0;
        arPriv->arTxPwrSet = FALSE;
    }
    if(ar->arWmiReady == TRUE)
    {
        AR_DEBUG_PRINTF(ATH_DEBUG_WLAN_TX,("Set tx pwr cmd %d dbM\n", dbM));
        wmi_set_txPwr_cmd(arPriv->arWmi, dbM);
    }
    return 0;
}

/*
 * SIOCGIWTXPOW
 */
int
ar6000_ioctl_giwtxpow(struct net_device *dev,
            struct iw_request_info *info,
            struct iw_param *rrq, char *extra)
{
    AR_SOFTC_DEV_T *arPriv = (AR_SOFTC_DEV_T *)ar6k_priv(dev);
    AR_SOFTC_T     *ar     = arPriv->arSoftc;

    int ret = 0;

    if (is_iwioctl_allowed(arPriv->arNextMode, info->cmd) != A_OK) {
        AR_DEBUG_PRINTF(ATH_DEBUG_ERR,("wext_ioctl: cmd=0x%x not allowed in this mode\n", info->cmd));
        return -EOPNOTSUPP;
    }

    if (ar->bIsDestroyProgress) {
        return -EBUSY;
    }

    if (ar->arWlanState == WLAN_DISABLED) {
        return -EIO;
    }

    if (down_interruptible(&ar->arSem)) {
        return -ERESTARTSYS;
    }

    if (ar->bIsDestroyProgress) {
        up(&ar->arSem);
        return -EBUSY;
    }

    if((ar->arWmiReady == TRUE) && (arPriv->arConnected == TRUE))
    {
        arPriv->arTxPwr = 0;

        if (wmi_get_txPwr_cmd(arPriv->arWmi) != A_OK) {
            up(&ar->arSem);
            return -EIO;
        }

        wait_event_interruptible_timeout(arPriv->arEvent, arPriv->arTxPwr != 0, wmitimeout * HZ);

        if (signal_pending(current)) {
            ret = -EINTR;
         }
    }
   /* If the interace is down or wmi is not ready or target is not connected
      then return value stored in the device structure */

    if (!ret) {
         if (arPriv->arTxPwrSet == TRUE) {
            rrq->fixed = TRUE;
        }
        rrq->value = arPriv->arTxPwr;
        rrq->flags = IW_TXPOW_DBM;
        //
        // IWLIST need this flag to get TxPower
        //
        rrq->disabled = 0;
    }

    up(&ar->arSem);

    return ret;
}

/*
 * SIOCSIWRETRY
 * since iwconfig only provides us with one max retry value, we use it
 * to apply to data frames of the BE traffic class.
 */
static int
ar6000_ioctl_siwretry(struct net_device *dev,
             struct iw_request_info *info,
             struct iw_param *rrq, char *extra)
{
    AR_SOFTC_DEV_T *arPriv = (AR_SOFTC_DEV_T *)ar6k_priv(dev);
    AR_SOFTC_T     *ar     = arPriv->arSoftc;

    if (is_iwioctl_allowed(arPriv->arNextMode, info->cmd) != A_OK) {
        AR_DEBUG_PRINTF(ATH_DEBUG_ERR,("wext_ioctl: cmd=0x%x not allowed in this mode\n", info->cmd));
        return -EOPNOTSUPP;
    }

    if (ar->arWlanState == WLAN_DISABLED) {
        return -EIO;
    }

    if (rrq->disabled) {
        return -EOPNOTSUPP;
    }

    if ((rrq->flags & IW_RETRY_TYPE) != IW_RETRY_LIMIT) {
        return -EOPNOTSUPP;
    }

    if ( !(rrq->value >= WMI_MIN_RETRIES) || !(rrq->value <= WMI_MAX_RETRIES)) {
            return - EINVAL;
    }
    if(ar->arWmiReady == TRUE)
    {
        if (wmi_set_retry_limits_cmd(arPriv->arWmi, DATA_FRAMETYPE, WMM_AC_BE,
                                     rrq->value, 0) != A_OK){
            return -EINVAL;
        }
    }
    arPriv->arMaxRetries = rrq->value;
    return 0;
}

/*
 * SIOCGIWRETRY
 */
static int
ar6000_ioctl_giwretry(struct net_device *dev,
             struct iw_request_info *info,
             struct iw_param *rrq, char *extra)
{
    AR_SOFTC_DEV_T *arPriv = (AR_SOFTC_DEV_T *)ar6k_priv(dev);
    AR_SOFTC_T     *ar     = arPriv->arSoftc;


    if (is_iwioctl_allowed(arPriv->arNextMode, info->cmd) != A_OK) {
        AR_DEBUG_PRINTF(ATH_DEBUG_ERR,("wext_ioctl: cmd=0x%x not allowed in this mode\n", info->cmd));
        return -EOPNOTSUPP;
    }

    if (ar->arWlanState == WLAN_DISABLED) {
        return -EIO;
    }

    rrq->disabled = 0;
    switch (rrq->flags & IW_RETRY_TYPE) {
    case IW_RETRY_LIFETIME:
        return -EOPNOTSUPP;
        break;
    case IW_RETRY_LIMIT:
        rrq->flags = IW_RETRY_LIMIT;
        switch (rrq->flags & IW_RETRY_MODIFIER) {
        case IW_RETRY_MIN:
            rrq->flags |= IW_RETRY_MIN;
            rrq->value = WMI_MIN_RETRIES;
            break;
        case IW_RETRY_MAX:
            rrq->flags |= IW_RETRY_MAX;
            rrq->value = arPriv->arMaxRetries;
            break;
        }
        break;
    }
    return 0;
}

/*
 * SIOCSIWENCODE
 */
static int
ar6000_ioctl_siwencode(struct net_device *dev,
              struct iw_request_info *info,
              struct iw_point *erq, char *keybuf)
{
    AR_SOFTC_DEV_T *arPriv = (AR_SOFTC_DEV_T *)ar6k_priv(dev);
    AR_SOFTC_T     *ar     = arPriv->arSoftc;
    AR_SOFTC_AP_T  *ap     = &arPriv->arAp;

    int index;
    A_INT32 auth = 0;

    if (is_iwioctl_allowed(arPriv->arNextMode, info->cmd) != A_OK) {
        AR_DEBUG_PRINTF(ATH_DEBUG_ERR,("wext_ioctl: cmd=0x%x not allowed in this mode\n", info->cmd));
        return -EOPNOTSUPP;
    }

    if(arPriv->arNextMode != AP_NETWORK) {
    /*
     *  Static WEP Keys should be configured before setting the SSID
     */
    if (arPriv->arSsid[0] && erq->length) {
        return -EIO;
    }
    }

    if (ar->arWlanState == WLAN_DISABLED) {
        return -EIO;
    }

    index = erq->flags & IW_ENCODE_INDEX;

    if (index && (((index - 1) < WMI_MIN_KEY_INDEX) ||
                  ((index - 1) > WMI_MAX_KEY_INDEX)))
    {
        return -EIO;
    }

    if (erq->flags & IW_ENCODE_DISABLED) {
        /*
         * Encryption disabled
         */
        if (index) {
            /*
             * If key index was specified then clear the specified key
             */
            index--;
            A_MEMZERO(arPriv->arWepKeyList[index].arKey,
                      sizeof(arPriv->arWepKeyList[index].arKey));
            arPriv->arWepKeyList[index].arKeyLen = 0;
        }
        arPriv->arDot11AuthMode       = OPEN_AUTH;
        arPriv->arPairwiseCrypto      = NONE_CRYPT;
        arPriv->arGroupCrypto         = NONE_CRYPT;
        arPriv->arAuthMode            = WMI_NONE_AUTH;
    } else {
        /*
         * Enabling WEP encryption
         */
        if (index) {
            index--;                /* keyindex is off base 1 in iwconfig */
        }

        if (erq->flags & IW_ENCODE_OPEN) {
            auth |= OPEN_AUTH;
        }
        if (erq->flags & IW_ENCODE_RESTRICTED) {
            auth |= SHARED_AUTH;
        }

        if (!auth) {
            auth = OPEN_AUTH;
        }

        if (!ap->deKeySet) {
            arPriv->arDefTxKeyIndex = index;
        }

        if (erq->length) {
            if (!IEEE80211_IS_VALID_WEP_CIPHER_LEN(erq->length)) {
                return -EIO;
            }

            A_MEMZERO(arPriv->arWepKeyList[index].arKey,
                      sizeof(arPriv->arWepKeyList[index].arKey));
            A_MEMCPY(arPriv->arWepKeyList[index].arKey, keybuf, erq->length);
            arPriv->arWepKeyList[index].arKeyLen = erq->length;
            arPriv->arDot11AuthMode       = auth;
        } else {
            if (arPriv->arWepKeyList[index].arKeyLen == 0) {
                return -EIO;
            }
            arPriv->arDefTxKeyIndex = index;

            if(arPriv->arSsidLen && arPriv->arWepKeyList[index].arKeyLen) {
                wmi_addKey_cmd(arPriv->arWmi,
                               index,
                               WEP_CRYPT,
                               GROUP_USAGE | TX_USAGE,
                               arPriv->arWepKeyList[index].arKeyLen,
                               NULL,
                               arPriv->arWepKeyList[index].arKey, KEY_OP_INIT_VAL, NULL,
                               NO_SYNC_WMIFLAG);
            }
            ap->deKeySet = TRUE;
        }

        arPriv->arPairwiseCrypto      = WEP_CRYPT;
        arPriv->arGroupCrypto         = WEP_CRYPT;
        arPriv->arAuthMode            = WMI_NONE_AUTH;
    }

    if(arPriv->arNextMode != AP_NETWORK) {
    /*
     * profile has changed.  Erase ssid to signal change
     */
        A_MEMZERO(arPriv->arSsid, sizeof(arPriv->arSsid));
        arPriv->arSsidLen = 0;
    }
    arPriv->ap_profile_flag = 1; /* There is a change in profile */
    return 0;
}

static int
ar6000_ioctl_giwencode(struct net_device *dev,
              struct iw_request_info *info,
              struct iw_point *erq, char *key)
{
    AR_SOFTC_DEV_T *arPriv = (AR_SOFTC_DEV_T *)ar6k_priv(dev);
    AR_SOFTC_T     *ar     = arPriv->arSoftc;

    A_UINT8 keyIndex;
    struct ar_wep_key *wk;

    if (is_iwioctl_allowed(arPriv->arNextMode, info->cmd) != A_OK) {
        AR_DEBUG_PRINTF(ATH_DEBUG_ERR,("wext_ioctl: cmd=0x%x not allowed in this mode\n", info->cmd));
        return -EOPNOTSUPP;
    }

    if (ar->arWlanState == WLAN_DISABLED) {
        return -EIO;
    }

    if (arPriv->arPairwiseCrypto == NONE_CRYPT) {
        erq->length = 0;
        erq->flags = IW_ENCODE_DISABLED;
    } else {
        if (arPriv->arPairwiseCrypto == WEP_CRYPT) {
            /* get the keyIndex */
            keyIndex = erq->flags & IW_ENCODE_INDEX;
            if (0 == keyIndex) {
                keyIndex = arPriv->arDefTxKeyIndex;
            } else if ((keyIndex - 1 < WMI_MIN_KEY_INDEX) ||
                       (keyIndex - 1 > WMI_MAX_KEY_INDEX))
            {
                keyIndex = WMI_MIN_KEY_INDEX;
            } else {
                keyIndex--;
            }
            erq->flags = keyIndex + 1;
            erq->flags &= ~IW_ENCODE_DISABLED;
            wk = &arPriv->arWepKeyList[keyIndex];
            if (erq->length > wk->arKeyLen) {
                erq->length = wk->arKeyLen;
            }
            if (wk->arKeyLen) {
                A_MEMCPY(key, wk->arKey, erq->length);
            }
        } else {
            erq->flags &= ~IW_ENCODE_DISABLED;
            if (arPriv->arSta.user_saved_keys.keyOk) {
                erq->length = arPriv->arSta.user_saved_keys.ucast_ik.ik_keylen;
                if (erq->length) {
                    A_MEMCPY(key, arPriv->arSta.user_saved_keys.ucast_ik.ik_keydata, erq->length);
                }
            } else {
                erq->length = 1;    // not really printing any key but let iwconfig know enc is on
            }
        }

        if (arPriv->arDot11AuthMode & OPEN_AUTH) {
            erq->flags |= IW_ENCODE_OPEN;
        }
        if (arPriv->arDot11AuthMode & SHARED_AUTH) {
            erq->flags |= IW_ENCODE_RESTRICTED;
        }
    }

    return 0;
}

#if WIRELESS_EXT >= 18
/*
 * SIOCSIWGENIE
 */
static int
ar6000_ioctl_siwgenie(struct net_device *dev,
              struct iw_request_info *info,
              struct iw_point *erq, char *extra)
{
    AR_SOFTC_DEV_T *arPriv = (AR_SOFTC_DEV_T *)ar6k_priv(dev);
    AR_SOFTC_T     *ar     = arPriv->arSoftc;
    A_UCHAR *ie = extra;
    A_UINT16   ieLen = erq->length;
    const A_UINT8 wfa_oui[] = { 0x00, 0x50, 0xf2, 0x04 };
    const A_UINT8 wpa_oui[] = { 0x00, 0x50, 0xf2, 0x01 };

    if (ar->arWmiReady == FALSE) {
        return -EIO;
    }
    if (ieLen > IEEE80211_APPIE_FRAME_MAX_LEN) {
        return -EIO;
    }

    /* get the Information Element and check if it's a WPS IE */
    if (ieLen>=6 &&
            ((ie[0]==IEEE80211_ELEMID_VENDOR) && 
            memcmp(&ie[2], wfa_oui, sizeof(wfa_oui))==0)) {
        /* WPS IE detected, notify target */
        AR_DEBUG_PRINTF(ATH_DEBUG_INFO,("WPS IE detected -- setting WPS flag\n"));
        arPriv->arSta.arConnectCtrlFlags |= CONNECT_WPS_FLAG;
    } else {
        if ((ieLen>=1 && ie[0]==IEEE80211_ELEMID_RSN) ||
                (ieLen>=6 && ie[0]==IEEE80211_ELEMID_VENDOR && 
                memcmp(&ie[2], wpa_oui, sizeof(wpa_oui))==0)) { 
            ieLen = 0; /* Firmware will set for us. Clear the previous one */
        }
#ifdef CONFIG_WAPI
        else if (ieLen>=1 && ie[0]==IEEE80211_ELEMID_WAPI) {
            //A_PRINTF("Set WAPI IE\n");
        }
#endif
        arPriv->arSta.arConnectCtrlFlags &= ~CONNECT_WPS_FLAG;
    }

    wmi_set_appie_cmd(arPriv->arWmi, WMI_FRAME_ASSOC_REQ, ieLen, ie);
    return 0;
}


/*
 * SIOCGIWGENIE
 */
static int
ar6000_ioctl_giwgenie(struct net_device *dev,
              struct iw_request_info *info,
              struct iw_point *erq, char *extra)
{
    AR_SOFTC_DEV_T *arPriv = (AR_SOFTC_DEV_T *)ar6k_priv(dev);
    AR_SOFTC_T     *ar     = arPriv->arSoftc;


    if (ar->arWmiReady == FALSE) {
        return -EIO;
    }
    erq->length = 0;
    erq->flags = 0;

    return 0;
}

/*
 * SIOCSIWAUTH
 */
static int
ar6000_ioctl_siwauth(struct net_device *dev,
              struct iw_request_info *info,
              struct iw_param *data, char *extra)
{
    AR_SOFTC_DEV_T *arPriv = (AR_SOFTC_DEV_T *)ar6k_priv(dev);
    AR_SOFTC_T     *ar     = arPriv->arSoftc;


    A_BOOL profChanged;
    A_UINT16 param;
    A_INT32 ret;
    A_INT32 value;

    if (ar->arWmiReady == FALSE) {
        return -EIO;
    }

    if (ar->arWlanState == WLAN_DISABLED) {
        return -EIO;
    }

    param = data->flags & IW_AUTH_INDEX;
    value = data->value;
    profChanged = TRUE;
    ret = 0;

    switch (param) {
        case IW_AUTH_WPA_VERSION:
            if (value & IW_AUTH_WPA_VERSION_DISABLED) {
                arPriv->arAuthMode = WMI_NONE_AUTH;
            } else if (value & IW_AUTH_WPA_VERSION_WPA) {
                arPriv->arAuthMode = WMI_WPA_AUTH;
            } else if (value & IW_AUTH_WPA_VERSION_WPA2) {
                arPriv->arAuthMode = WMI_WPA2_AUTH;
            } else {
                ret = -1;
                profChanged    = FALSE;
            }
            break;
        case IW_AUTH_CIPHER_PAIRWISE:
            if (value & IW_AUTH_CIPHER_NONE) {
                arPriv->arPairwiseCrypto = NONE_CRYPT;
                arPriv->arPairwiseCryptoLen = 0;
            } else if (value & IW_AUTH_CIPHER_WEP40) {
                arPriv->arPairwiseCrypto = WEP_CRYPT;
                arPriv->arPairwiseCryptoLen = 5;
            } else if (value & IW_AUTH_CIPHER_TKIP) {
                arPriv->arPairwiseCrypto = TKIP_CRYPT;
                arPriv->arPairwiseCryptoLen = 0;
            } else if (value & IW_AUTH_CIPHER_CCMP) {
                arPriv->arPairwiseCrypto = AES_CRYPT;
                arPriv->arPairwiseCryptoLen = 0;
            } else if (value & IW_AUTH_CIPHER_WEP104) {
                arPriv->arPairwiseCrypto = WEP_CRYPT;
                arPriv->arPairwiseCryptoLen = 13;
#ifdef WAPI_ENABLE
            } else if (value & IW_AUTH_CIPHER_SMS4) {
                arPriv->arPairwiseCrypto = WAPI_CRYPT;
                arPriv->arPairwiseCryptoLen = 0;
#endif
            } else {
                ret = -1;
                profChanged    = FALSE;
            }
            break;
        case IW_AUTH_CIPHER_GROUP:
            if (value & IW_AUTH_CIPHER_NONE) {
                arPriv->arGroupCrypto = NONE_CRYPT;
                arPriv->arGroupCryptoLen = 0;
            } else if (value & IW_AUTH_CIPHER_WEP40) {
                arPriv->arGroupCrypto = WEP_CRYPT;
                arPriv->arGroupCryptoLen = 5;
            } else if (value & IW_AUTH_CIPHER_TKIP) {
                arPriv->arGroupCrypto = TKIP_CRYPT;
                arPriv->arGroupCryptoLen = 0;
            } else if (value & IW_AUTH_CIPHER_CCMP) {
                arPriv->arGroupCrypto = AES_CRYPT;
                arPriv->arGroupCryptoLen = 0;
            } else if (value & IW_AUTH_CIPHER_WEP104) {
                arPriv->arGroupCrypto = WEP_CRYPT;
                arPriv->arGroupCryptoLen = 13;
#ifdef WAPI_ENABLE
            } else if (value & IW_AUTH_CIPHER_SMS4) {
                arPriv->arGroupCrypto = WAPI_CRYPT;
                arPriv->arGroupCryptoLen = 0;
#endif
            } else {
                ret = -1;
                profChanged    = FALSE;
            }
            break;
        case IW_AUTH_KEY_MGMT:
            if (value & IW_AUTH_KEY_MGMT_PSK) {
                if (WMI_WPA_AUTH == arPriv->arAuthMode) {
                    arPriv->arAuthMode = WMI_WPA_PSK_AUTH;
                } else if (WMI_WPA2_AUTH == arPriv->arAuthMode) {
                    arPriv->arAuthMode = WMI_WPA2_PSK_AUTH;
                } else {
                    ret = -1;
                }
#define IW_AUTH_KEY_MGMT_CCKM       8
            } else if (value & IW_AUTH_KEY_MGMT_CCKM) {
                if (WMI_WPA_AUTH == arPriv->arAuthMode) {
                    arPriv->arAuthMode = WMI_WPA_AUTH_CCKM;
                } else if (WMI_WPA2_AUTH == arPriv->arAuthMode) {
                    arPriv->arAuthMode = WMI_WPA2_AUTH_CCKM;
                } else {
                    ret = -1;
                }
            } else if (!(value & IW_AUTH_KEY_MGMT_802_1X)) {
                arPriv->arAuthMode = WMI_NONE_AUTH;
            }
            break;
        case IW_AUTH_TKIP_COUNTERMEASURES:
            wmi_set_tkip_countermeasures_cmd(arPriv->arWmi, value);
            profChanged    = FALSE;
            break;
        case IW_AUTH_DROP_UNENCRYPTED:
            profChanged    = FALSE;
            break;
        case IW_AUTH_80211_AUTH_ALG:
            arPriv->arDot11AuthMode = 0;
            if (value & IW_AUTH_ALG_OPEN_SYSTEM) {
                arPriv->arDot11AuthMode  |= OPEN_AUTH;
            }
            if (value & IW_AUTH_ALG_SHARED_KEY) {
                arPriv->arDot11AuthMode  |= SHARED_AUTH;
            }
            if (value & IW_AUTH_ALG_LEAP) {
                arPriv->arDot11AuthMode   = LEAP_AUTH;
            }
            if(arPriv->arDot11AuthMode == 0) {
                ret = -1;
                profChanged    = FALSE;
            }
            break;
        case IW_AUTH_WPA_ENABLED:
            if (!value) {
                arPriv->arAuthMode = WMI_NONE_AUTH;
                /* when the supplicant is stopped, it calls this
                 * handler with value=0. The followings need to be
                 * reset if the STA were to connect again
                 * without security
                 */
                arPriv->arDot11AuthMode = OPEN_AUTH;
                arPriv->arPairwiseCrypto = NONE_CRYPT;
                arPriv->arPairwiseCryptoLen = 0;
                arPriv->arGroupCrypto = NONE_CRYPT;
                arPriv->arGroupCryptoLen = 0;
            }
            break;
        case IW_AUTH_RX_UNENCRYPTED_EAPOL:
            profChanged    = FALSE;
            break;
        case IW_AUTH_ROAMING_CONTROL:
            profChanged    = FALSE;
            break;
        case IW_AUTH_PRIVACY_INVOKED:
            if (!value) {
                arPriv->arPairwiseCrypto = NONE_CRYPT;
                arPriv->arPairwiseCryptoLen = 0;
                arPriv->arGroupCrypto = NONE_CRYPT;
                arPriv->arGroupCryptoLen = 0;
            }
            break;
        default:
           ret = -1;
           profChanged    = FALSE;
           break;
    }

    if (profChanged == TRUE) {
        /*
         * profile has changed.  Erase ssid to signal change
         */
        A_MEMZERO(arPriv->arSsid, sizeof(arPriv->arSsid));
        arPriv->arSsidLen = 0;
    }

    return ret;
}


/*
 * SIOCGIWAUTH
 */
static int
ar6000_ioctl_giwauth(struct net_device *dev,
              struct iw_request_info *info,
              struct iw_param *data, char *extra)
{
    AR_SOFTC_DEV_T *arPriv = (AR_SOFTC_DEV_T *)ar6k_priv(dev);
    AR_SOFTC_T     *ar     = arPriv->arSoftc;

    A_UINT16 param;
    A_INT32 ret;

    if (ar->arWmiReady == FALSE) {
        return -EIO;
    }

    if (ar->arWlanState == WLAN_DISABLED) {
        return -EIO;
    }

    param = data->flags & IW_AUTH_INDEX;
    ret = 0;
    data->value = 0;


    switch (param) {
        case IW_AUTH_WPA_VERSION:
            if (arPriv->arAuthMode == WMI_NONE_AUTH) {
                data->value |= IW_AUTH_WPA_VERSION_DISABLED;
            } else if (arPriv->arAuthMode == WMI_WPA_AUTH) {
                data->value |= IW_AUTH_WPA_VERSION_WPA;
            } else if (arPriv->arAuthMode == WMI_WPA2_AUTH) {
                data->value |= IW_AUTH_WPA_VERSION_WPA2;
            } else {
                ret = -1;
            }
            break;
        case IW_AUTH_CIPHER_PAIRWISE:
            if (arPriv->arPairwiseCrypto == NONE_CRYPT) {
                data->value |= IW_AUTH_CIPHER_NONE;
            } else if (arPriv->arPairwiseCrypto == WEP_CRYPT) {
                if (arPriv->arPairwiseCryptoLen == 13) {
                    data->value |= IW_AUTH_CIPHER_WEP104;
                } else {
                    data->value |= IW_AUTH_CIPHER_WEP40;
                }
            } else if (arPriv->arPairwiseCrypto == TKIP_CRYPT) {
                data->value |= IW_AUTH_CIPHER_TKIP;
            } else if (arPriv->arPairwiseCrypto == AES_CRYPT) {
                data->value |= IW_AUTH_CIPHER_CCMP;
            } else {
                ret = -1;
            }
            break;
        case IW_AUTH_CIPHER_GROUP:
            if (arPriv->arGroupCrypto == NONE_CRYPT) {
                    data->value |= IW_AUTH_CIPHER_NONE;
            } else if (arPriv->arGroupCrypto == WEP_CRYPT) {
                if (arPriv->arGroupCryptoLen == 13) {
                    data->value |= IW_AUTH_CIPHER_WEP104;
                } else {
                    data->value |= IW_AUTH_CIPHER_WEP40;
                }
            } else if (arPriv->arGroupCrypto == TKIP_CRYPT) {
                data->value |= IW_AUTH_CIPHER_TKIP;
            } else if (arPriv->arGroupCrypto == AES_CRYPT) {
                data->value |= IW_AUTH_CIPHER_CCMP;
            } else {
                ret = -1;
            }
            break;
        case IW_AUTH_KEY_MGMT:
            if ((arPriv->arAuthMode == WMI_WPA_PSK_AUTH) ||
                (arPriv->arAuthMode == WMI_WPA2_PSK_AUTH)) {
                data->value |= IW_AUTH_KEY_MGMT_PSK;
            } else if ((arPriv->arAuthMode == WMI_WPA_AUTH) ||
                       (arPriv->arAuthMode == WMI_WPA2_AUTH)) {
                data->value |= IW_AUTH_KEY_MGMT_802_1X;
            }
            break;
        case IW_AUTH_TKIP_COUNTERMEASURES:
            // TODO. Save countermeassure enable/disable
            data->value = 0;
            break;
        case IW_AUTH_DROP_UNENCRYPTED:
            break;
        case IW_AUTH_80211_AUTH_ALG:
            if (arPriv->arDot11AuthMode == OPEN_AUTH) {
                data->value |= IW_AUTH_ALG_OPEN_SYSTEM;
            } else if (arPriv->arDot11AuthMode == SHARED_AUTH) {
                data->value |= IW_AUTH_ALG_SHARED_KEY;
            } else if (arPriv->arDot11AuthMode == LEAP_AUTH) {
                data->value |= IW_AUTH_ALG_LEAP;
            } else {
                ret = -1;
            }
            break;
        case IW_AUTH_WPA_ENABLED:
            if (arPriv->arAuthMode == WMI_NONE_AUTH) {
                data->value = 0;
            } else {
                data->value = 1;
            }
            break;
        case IW_AUTH_RX_UNENCRYPTED_EAPOL:
            break;
        case IW_AUTH_ROAMING_CONTROL:
            break;
        case IW_AUTH_PRIVACY_INVOKED:
            if (arPriv->arPairwiseCrypto == NONE_CRYPT) {
                data->value = 0;
            } else {
                data->value = 1;
            }
            break;
        default:
           ret = -1;
           break;
    }

    return 0;
}

/*
 * SIOCSIWPMKSA
 */
static int
ar6000_ioctl_siwpmksa(struct net_device *dev,
              struct iw_request_info *info,
              struct iw_point *data, char *extra)
{
    AR_SOFTC_DEV_T *arPriv = (AR_SOFTC_DEV_T *)ar6k_priv(dev);
    AR_SOFTC_T     *ar     = arPriv->arSoftc;
    A_INT32 ret;
    A_STATUS status;
    struct iw_pmksa *pmksa;

    pmksa = (struct iw_pmksa *)extra;

    if (ar->arWmiReady == FALSE) {
        return -EIO;
    }

    ret = 0;
    status = A_OK;

    switch (pmksa->cmd) {
        case IW_PMKSA_ADD:
            status = wmi_setPmkid_cmd(arPriv->arWmi, (A_UINT8*)pmksa->bssid.sa_data, pmksa->pmkid, TRUE);
            break;
        case IW_PMKSA_REMOVE:
            status = wmi_setPmkid_cmd(arPriv->arWmi, (A_UINT8*)pmksa->bssid.sa_data, pmksa->pmkid, FALSE);
            break;
        case IW_PMKSA_FLUSH:
            if (arPriv->arConnected == TRUE) {
                status = wmi_setPmkid_cmd(arPriv->arWmi, arPriv->arBssid, NULL, 0);
            }
            break;
        default:
            ret=-1;
            break;
    }
    if (status != A_OK) {
        ret = -1;
    }

    return ret;
}

#ifdef WAPI_ENABLE

#define PN_INIT 0x5c365c36

static int ar6000_set_wapi_key(struct net_device *dev,
              struct iw_request_info *info,
              struct iw_point *erq, char *extra)
{
    AR_SOFTC_DEV_T *arPriv = (AR_SOFTC_DEV_T *)ar6k_priv(dev);
    struct iw_encode_ext *ext = (struct iw_encode_ext *)extra;
    KEY_USAGE   keyUsage = 0;
    A_INT32     keyLen;
    A_UINT8     *keyData;
    A_INT32     index;
    A_UINT32    *PN;
    A_INT32     i;
    A_STATUS    status;
    A_UINT8     wapiKeyRsc[16];
    CRYPTO_TYPE keyType = WAPI_CRYPT;
    const A_UINT8 broadcastMac[] = {0xff, 0xff, 0xff, 0xff, 0xff, 0xff};

    index = erq->flags & IW_ENCODE_INDEX;
    if (index && (((index - 1) < WMI_MIN_KEY_INDEX) ||
                ((index - 1) > WMI_MAX_KEY_INDEX))) {
        return -EIO;
    }

    index--;
    if (index < 0 || index > 4) {
        return -EIO;
    }
    keyData = (A_UINT8 *)(ext + 1);
    keyLen = erq->length - sizeof(struct iw_encode_ext);
    A_MEMCPY(wapiKeyRsc, ext->tx_seq, sizeof(wapiKeyRsc));

    if (A_MEMCMP(ext->addr.sa_data, broadcastMac, sizeof(broadcastMac)) == 0) {
        keyUsage |= GROUP_USAGE;
        PN = (A_UINT32 *)wapiKeyRsc;
        for (i = 0; i < 4; i++) {
            PN[i] = PN_INIT;
        }
    } else {
        keyUsage |= PAIRWISE_USAGE;
    }
    status = wmi_addKey_cmd(arPriv->arWmi,
                            index,
                            keyType,
                            keyUsage,
                            keyLen,
                            wapiKeyRsc,
                            keyData,
                            KEY_OP_INIT_WAPIPN,
                            NULL,
                            SYNC_BEFORE_WMIFLAG);
    if (A_OK != status) {
        return -EIO;
    }
    return 0;
}

#endif

/*
 * SIOCSIWENCODEEXT
 */
static int
ar6000_ioctl_siwencodeext(struct net_device *dev,
              struct iw_request_info *info,
              struct iw_point *erq, char *extra)
{
    AR_SOFTC_DEV_T *arPriv = (AR_SOFTC_DEV_T *)ar6k_priv(dev);
    AR_SOFTC_T     *ar     = arPriv->arSoftc;
    A_INT32 index;
    struct iw_encode_ext *ext;
    KEY_USAGE keyUsage;
    A_INT32 keyLen;
    A_UINT8 *keyData;
    A_UINT8 keyRsc[8];
    A_STATUS status;
    CRYPTO_TYPE keyType;
#ifdef USER_KEYS
    struct ieee80211req_key ik;
#endif /* USER_KEYS */
    AR_SOFTC_AP_T   *arAp = &arPriv->arAp;

    if (ar->arWlanState == WLAN_DISABLED) {
        return -EIO;
    }

#ifdef USER_KEYS
        arPriv->arSta.user_saved_keys.keyOk = FALSE;
#endif /* USER_KEYS */

    index = erq->flags & IW_ENCODE_INDEX;

    if (index && (((index - 1) < WMI_MIN_KEY_INDEX) ||
                  ((index - 1) > WMI_MAX_KEY_INDEX)))
    {
        return -EIO;
    }

    ext = (struct iw_encode_ext *)extra;
    if (erq->flags & IW_ENCODE_DISABLED) {
        /*
         * Encryption disabled
         */
        if (index) {
            /*
             * If key index was specified then clear the specified key
             */
            index--;
            A_MEMZERO(arPriv->arWepKeyList[index].arKey,
                      sizeof(arPriv->arWepKeyList[index].arKey));
            arPriv->arWepKeyList[index].arKeyLen = 0;
        }
    } else {
        /*
         * Enabling WEP encryption
         */
        if (index) {
            index--;                /* keyindex is off base 1 in iwconfig */
        }

        keyUsage = 0;
        keyLen = erq->length - sizeof(struct iw_encode_ext);

        if (ext->ext_flags & IW_ENCODE_EXT_SET_TX_KEY) {
            keyUsage = TX_USAGE;
            arPriv->arDefTxKeyIndex = index;
            // Just setting the key index
            if (keyLen == 0) {
                return 0;
            }
        }

        if (keyLen <= 0) {
            if (ext->alg == IW_ENCODE_ALG_KRK) {
                wmi_delete_krk_cmd(arPriv->arWmi);
            }
            return -EIO;
        }

        /* key follows iw_encode_ext */
        keyData = (A_UINT8 *)(ext + 1);

        switch (ext->alg) {
            case IW_ENCODE_ALG_WEP:
                keyType = WEP_CRYPT;
#ifdef USER_KEYS
                ik.ik_type = IEEE80211_CIPHER_WEP;
#endif /* USER_KEYS */
                if(arPriv->arNextMode == AP_NETWORK) {
                    arAp->ap_mode_bkey.ik_type = IEEE80211_CIPHER_WEP;
                }
                if (!IEEE80211_IS_VALID_WEP_CIPHER_LEN(keyLen)) {
                    return -EIO;
                }

                /* Check whether it is static wep. */
                if (!arPriv->arConnected) {
                    A_MEMZERO(arPriv->arWepKeyList[index].arKey,
                          sizeof(arPriv->arWepKeyList[index].arKey));
                    A_MEMCPY(arPriv->arWepKeyList[index].arKey, keyData, keyLen);
                    arPriv->arWepKeyList[index].arKeyLen = keyLen;

                    return 0;
                }
                break;
            case IW_ENCODE_ALG_TKIP:
                keyType = TKIP_CRYPT;
#ifdef USER_KEYS
                ik.ik_type = IEEE80211_CIPHER_TKIP;
#endif /* USER_KEYS */
                if(arPriv->arNextMode == AP_NETWORK) {
                    arAp->ap_mode_bkey.ik_type = IEEE80211_CIPHER_TKIP;
                }
                break;
            case IW_ENCODE_ALG_CCMP:
                keyType = AES_CRYPT;
#ifdef USER_KEYS
                ik.ik_type = IEEE80211_CIPHER_AES_CCM;
#endif /* USER_KEYS */
                if(arPriv->arNextMode == AP_NETWORK) {
                    arAp->ap_mode_bkey.ik_type = IEEE80211_CIPHER_AES_CCM;
                }
                break;
#ifdef WAPI_ENABLE
            case IW_ENCODE_ALG_SM4:
                return ar6000_set_wapi_key(dev, info, erq, extra);
#endif
            case IW_ENCODE_ALG_PMK:
                arPriv->arSta.arConnectCtrlFlags |= CONNECT_DO_WPA_OFFLOAD;
                return wmi_set_pmk_cmd(arPriv->arWmi, keyData);
            case IW_ENCODE_ALG_KRK:
                return wmi_add_krk_cmd(arPriv->arWmi, keyData);
            default:
                return -EIO;
        }


        if (ext->ext_flags & IW_ENCODE_EXT_GROUP_KEY) {
            keyUsage |= GROUP_USAGE;
            if(arPriv->arNextMode == AP_NETWORK) {
                keyUsage &= ~TX_USAGE; 
                arAp->ap_mode_bkey.ik_keyix = index;
                arAp->ap_mode_bkey.ik_keylen = keyLen;
                memcpy(arAp->ap_mode_bkey.ik_keydata, keyData, keyLen);
                memcpy(&arAp->ap_mode_bkey.ik_keyrsc, keyRsc, sizeof(keyRsc));
                memcpy(arAp->ap_mode_bkey.ik_macaddr, ext->addr.sa_data, ETH_ALEN);
            }
        } else {
            keyUsage |= PAIRWISE_USAGE;
        }

        if (ext->ext_flags & IW_ENCODE_EXT_RX_SEQ_VALID) {
            A_MEMCPY(keyRsc, ext->rx_seq, sizeof(keyRsc));
        } else {
            A_MEMZERO(keyRsc, sizeof(keyRsc));
        }

        if (((WMI_WPA_PSK_AUTH == arPriv->arAuthMode) || (WMI_WPA2_PSK_AUTH == arPriv->arAuthMode)) &&
            (GROUP_USAGE & keyUsage))
        {   
                A_UNTIMEOUT(&arPriv->arSta.disconnect_timer);
        }

         status = wmi_addKey_cmd(arPriv->arWmi, index, keyType, keyUsage,
                            keyLen, keyRsc,
                            keyData, KEY_OP_INIT_VAL,
                            (A_UINT8*)ext->addr.sa_data,
                            SYNC_BOTH_WMIFLAG);
         if (status != A_OK) {
            return -EIO;
         }

#ifdef USER_KEYS
        ik.ik_keyix = index;
        ik.ik_keylen = keyLen;
        memcpy(ik.ik_keydata, keyData, keyLen);
        memcpy(&ik.ik_keyrsc, keyRsc, sizeof(keyRsc));
        memcpy(ik.ik_macaddr, ext->addr.sa_data, ETH_ALEN);
        if (ext->ext_flags & IW_ENCODE_EXT_GROUP_KEY) {
            memcpy(&arPriv->arSta.user_saved_keys.bcast_ik, &ik,
                       sizeof(struct ieee80211req_key));
        } else {
            memcpy(&arPriv->arSta.user_saved_keys.ucast_ik, &ik,
                      sizeof(struct ieee80211req_key));
        }
        arPriv->arSta.user_saved_keys.keyOk = TRUE;
#endif /* USER_KEYS */
    }


    return 0;
}

/*
 * SIOCGIWENCODEEXT
 */
static int
ar6000_ioctl_giwencodeext(struct net_device *dev,
              struct iw_request_info *info,
              struct iw_point *erq, char *extra)
{
    AR_SOFTC_DEV_T *arPriv = (AR_SOFTC_DEV_T *)ar6k_priv(dev);
    AR_SOFTC_T     *ar     = arPriv->arSoftc;

    if (ar->arWlanState == WLAN_DISABLED) {
        return -EIO;
    }

    if (arPriv->arPairwiseCrypto == NONE_CRYPT) {
        erq->length = 0;
        erq->flags = IW_ENCODE_DISABLED;
    } else {
        erq->length = 0;
    }

    return 0;
}
#endif // WIRELESS_EXT >= 18

#if WIRELESS_EXT > 20
static int ar6000_ioctl_siwpower(struct net_device *dev,
                 struct iw_request_info *info,
                 union iwreq_data *wrqu, char *extra)
{
#ifndef ATH6K_CONFIG_OTA_MODE
    AR_SOFTC_DEV_T *arPriv = (AR_SOFTC_DEV_T *)ar6k_priv(dev);
    AR_SOFTC_T     *ar     = arPriv->arSoftc;
    WMI_POWER_MODE power_mode;

    if (ar->arWmiReady == FALSE) {
        return -EIO;
    }

    if (ar->arWlanState == WLAN_DISABLED) {
        return -EIO;
    }

    if (wrqu->power.disabled)
        power_mode = MAX_PERF_POWER;
    else
        power_mode = REC_POWER;

    if (wmi_powermode_cmd(arPriv->arWmi, power_mode) < 0)
        return -EIO;
#endif
    return 0;
}

static int ar6000_ioctl_giwpower(struct net_device *dev,
                 struct iw_request_info *info,
                 union iwreq_data *wrqu, char *extra)
{
    AR_SOFTC_DEV_T *arPriv = (AR_SOFTC_DEV_T *)ar6k_priv(dev);
    AR_SOFTC_T     *ar     = arPriv->arSoftc;
    WMI_POWER_MODE power_mode;

    if (ar->arWmiReady == FALSE) {
        return -EIO;
    }

    if (ar->arWlanState == WLAN_DISABLED) {
        return -EIO;
    }

    power_mode = wmi_get_power_mode_cmd(arPriv->arWmi);

    if (power_mode == MAX_PERF_POWER)
        wrqu->power.disabled = 1;
    else
        wrqu->power.disabled = 0;

    return 0;
}
#endif // WIRELESS_EXT > 20

/*
 * SIOCGIWNAME
 */
int
ar6000_ioctl_giwname(struct net_device *dev,
           struct iw_request_info *info,
           char *name, char *extra)
{
    A_UINT8 capability;
    AR_SOFTC_DEV_T *arPriv = (AR_SOFTC_DEV_T *)ar6k_priv(dev);
    AR_SOFTC_T     *ar     = arPriv->arSoftc;

    if (is_iwioctl_allowed(arPriv->arNextMode, info->cmd) != A_OK) {
        AR_DEBUG_PRINTF(ATH_DEBUG_ERR,("wext_ioctl: cmd=0x%x not allowed in this mode\n", info->cmd));
        return -EOPNOTSUPP;
    }

    if (ar->arWlanState == WLAN_DISABLED) {
        return -EIO;
    }

    capability = arPriv->arPhyCapability;
    if(arPriv->arNetworkType == INFRA_NETWORK && arPriv->arConnected) {
        bss_t *bss = wmi_find_node(arPriv->arWmi, arPriv->arBssid);
        if (bss) {
            capability = get_bss_phy_capability(bss);
            wmi_node_return(arPriv->arWmi, bss);
        }
    }
    switch (capability) {
    case (WMI_11A_CAPABILITY):
        strncpy(name, "AR6000 802.11a", IFNAMSIZ);
        break;
    case (WMI_11G_CAPABILITY):
        strncpy(name, "AR6000 802.11g", IFNAMSIZ);
        break;
    case (WMI_11AG_CAPABILITY):
        strncpy(name, "AR6000 802.11ag", IFNAMSIZ);
        break;
    case (WMI_11NA_CAPABILITY):
        strncpy(name, "AR6000 802.11na", IFNAMSIZ);
        break;
    case (WMI_11NG_CAPABILITY):
        strncpy(name, "AR6000 802.11ng", IFNAMSIZ);
        break;
    case (WMI_11NAG_CAPABILITY):
        strncpy(name, "AR6K 802.11nag", IFNAMSIZ);
        break;
    default:
        strncpy(name, "AR6000 802.11b", IFNAMSIZ);
        break;
    }

    return 0;
}

/*
 * SIOCSIWFREQ
 */
int
ar6000_ioctl_siwfreq(struct net_device *dev,
            struct iw_request_info *info,
            struct iw_freq *freq, char *extra)
{
    AR_SOFTC_DEV_T *arPriv = (AR_SOFTC_DEV_T *)ar6k_priv(dev);
    AR_SOFTC_T     *ar     = arPriv->arSoftc;

    if (is_iwioctl_allowed(arPriv->arNextMode, info->cmd) != A_OK) {
        AR_DEBUG_PRINTF(ATH_DEBUG_ERR,("wext_ioctl: cmd=0x%x not allowed in this mode\n", info->cmd));
        return -EOPNOTSUPP;
    }

    if (ar->arWlanState == WLAN_DISABLED) {
        return -EIO;
    }

    /*
     * We support limiting the channels via wmiconfig.
     *
     * We use this command to configure the channel hint for the connect cmd
     * so it is possible the target will end up connecting to a different
     * channel.
     */
    if (freq->e > 1) {
        return -EINVAL;
    } else if (freq->e == 1) {
         if ((arPriv->arPhyCapability == WMI_11NG_CAPABILITY) && (((freq->m / 100000) >= 5180) && ((freq->m / 100000) <= 5825))) {
            return -EINVAL;
         }
         arPriv->arChannelHint = freq->m / 100000;
    } else if(freq->m > 0) {
         if ((arPriv->arPhyCapability == WMI_11NG_CAPABILITY) && ((wlan_ieee2freq(freq->m) >=5180) && (wlan_ieee2freq(freq->m) <= 5825))) {
            return -EINVAL;
         }
            arPriv->arChannelHint = wlan_ieee2freq(freq->m);
    } else {
            /* Auto Channel Selection */
            arPriv->arChannelHint = 0;
    }
    
    arPriv->ap_profile_flag = 1; /* There is a change in profile */

    A_PRINTF("channel hint set to %d\n", arPriv->arChannelHint);
    return 0;
}

/*
 * SIOCGIWFREQ
 */
int
ar6000_ioctl_giwfreq(struct net_device *dev,
                struct iw_request_info *info,
                struct iw_freq *freq, char *extra)
{
    AR_SOFTC_DEV_T *arPriv = (AR_SOFTC_DEV_T *)ar6k_priv(dev);
    AR_SOFTC_T     *ar     = arPriv->arSoftc;

    if (is_iwioctl_allowed(arPriv->arNextMode, info->cmd) != A_OK) {
        AR_DEBUG_PRINTF(ATH_DEBUG_ERR,("wext_ioctl: cmd=0x%x not allowed in this mode\n", info->cmd));
        return -EOPNOTSUPP;
    }

    if (ar->arWlanState == WLAN_DISABLED) {
        return -EIO;
    }

    if (arPriv->arNetworkType == AP_NETWORK) {
        if(arPriv->arChannelHint) {
            freq->m = arPriv->arChannelHint * 100000;
        } else if(arPriv->arBssChannel) {
            freq->m = arPriv->arBssChannel * 100000;
        } else {
            return -EINVAL;
        }
    } else {
        if (arPriv->arConnected != TRUE) {
            return -EINVAL;
        } else {
            freq->m = arPriv->arBssChannel * 100000;
        }
    }

    freq->e = 1;

    return 0;
}

/*
 * SIOCSIWMODE
 */
int
ar6000_ioctl_siwmode(struct net_device *dev,
            struct iw_request_info *info,
            __u32 *mode, char *extra)
{
    AR_SOFTC_DEV_T *arPriv = (AR_SOFTC_DEV_T *)ar6k_priv(dev);
    AR_SOFTC_T     *ar     = arPriv->arSoftc;

    if (is_iwioctl_allowed(arPriv->arNextMode, info->cmd) != A_OK) {
        AR_DEBUG_PRINTF(ATH_DEBUG_ERR,("wext_ioctl: cmd=0x%x not allowed in this mode\n", info->cmd));
        return -EOPNOTSUPP;
    }

    if (ar->arWlanState == WLAN_DISABLED) {
        return -EIO;
    }

#if 0
    /*
     * clear SSID during mode switch in connected state
     */
    if(!(arPriv->arNetworkType == (((*mode) == IW_MODE_INFRA) ? INFRA_NETWORK : ADHOC_NETWORK)) 
                  && (arPriv->arConnected == TRUE) ){
        A_MEMZERO(arPriv->arSsid, sizeof(arPriv->arSsid));
        arPriv->arSsidLen = 0;
    }
#endif

    switch (*mode) {
    case IW_MODE_INFRA:
        arPriv->arNextMode = INFRA_NETWORK;
        break;
    case IW_MODE_ADHOC:
        arPriv->arNextMode = ADHOC_NETWORK;
        break;
    case IW_MODE_MASTER:
        arPriv->arNextMode = AP_NETWORK;
        break;
    default:
        return -EINVAL;
    }

    if (arPriv->arNetworkType != arPriv->arNextMode) {
        ar6000_init_mode_info(arPriv);
    }

    /* Update the arNetworkType */
    arPriv->arNetworkType = arPriv->arNextMode;

    return 0;
}

/*
 * SIOCGIWMODE
 */
int
ar6000_ioctl_giwmode(struct net_device *dev,
            struct iw_request_info *info,
            __u32 *mode, char *extra)
{
    AR_SOFTC_DEV_T *arPriv = (AR_SOFTC_DEV_T *)ar6k_priv(dev);
    AR_SOFTC_T     *ar     = arPriv->arSoftc;

    if (is_iwioctl_allowed(arPriv->arNextMode, info->cmd) != A_OK) {
        AR_DEBUG_PRINTF(ATH_DEBUG_ERR,("wext_ioctl: cmd=0x%x not allowed in this mode\n", info->cmd));
        return -EOPNOTSUPP;
    }

    if (ar->arWlanState == WLAN_DISABLED) {
        return -EIO;
    }

    switch (arPriv->arNetworkType) {
    case INFRA_NETWORK:
        *mode = IW_MODE_INFRA;
        break;
    case ADHOC_NETWORK:
        *mode = IW_MODE_ADHOC;
        break;
    case AP_NETWORK:
        *mode = IW_MODE_MASTER;
        break;
    default:
        return -EIO;
    }
    return 0;
}

/*
 * SIOCSIWSENS
 */
int
ar6000_ioctl_siwsens(struct net_device *dev,
            struct iw_request_info *info,
            struct iw_param *sens, char *extra)
{
    return 0;
}

/*
 * SIOCGIWSENS
 */
int
ar6000_ioctl_giwsens(struct net_device *dev,
            struct iw_request_info *info,
            struct iw_param *sens, char *extra)
{
    sens->value = 0;
    sens->fixed = 1;

    return 0;
}

/*
 * SIOCGIWRANGE
 */
int
ar6000_ioctl_giwrange(struct net_device *dev,
             struct iw_request_info *info,
             struct iw_point *data, char *extra)
{

    AR_SOFTC_DEV_T *arPriv = (AR_SOFTC_DEV_T *)ar6k_priv(dev);
    AR_SOFTC_T     *ar     = arPriv->arSoftc;
    AR_SOFTC_STA_T *arSta  = &arPriv->arSta;
    struct iw_range *range = (struct iw_range *) extra;
    int i, j, ret = 0;
    const A_INT32 rateTable[] = { 
        1000, 2000, 5500, 11000,
        6000, 9000, 12000, 18000, 24000, 36000, 48000, 54000,
        6500, 13000, 19500, 26000, 39000, 52000, 58500, 65000 };

    if (is_iwioctl_allowed(arPriv->arNextMode, info->cmd) != A_OK) {
        AR_DEBUG_PRINTF(ATH_DEBUG_ERR,("wext_ioctl: cmd=0x%x not allowed in this mode\n", info->cmd));
        return -EOPNOTSUPP;
    }

    if (ar->bIsDestroyProgress) {
        return -EBUSY;
    }

    if (ar->arWmiReady == FALSE) {
        return -EIO;
    }

    if (down_interruptible(&ar->arSem)) {
        return -ERESTARTSYS;
    }

    if (ar->bIsDestroyProgress) {
        up(&ar->arSem);
        return -EBUSY;
    }

    arSta->arNumChannels = -1;
    A_MEMZERO(arSta->arChannelList, sizeof (arSta->arChannelList));

    if (wmi_get_channelList_cmd(arPriv->arWmi) != A_OK) {
        up(&ar->arSem);
        return -EIO;
    }

    wait_event_interruptible_timeout(arPriv->arEvent, arSta->arNumChannels != -1, wmitimeout * HZ);

    if (signal_pending(current)) {
        up(&ar->arSem);
        return -EINTR;
    }

    data->length = sizeof(struct iw_range);
    A_MEMZERO(range, sizeof(struct iw_range));

    range->enc_capa |= IW_ENC_CAPA_WPA | IW_ENC_CAPA_WPA2;
    range->enc_capa |= IW_ENC_CAPA_CIPHER_TKIP|IW_ENC_CAPA_CIPHER_CCMP;
    /* Event capability (kernel + driver) */
    range->event_capa[0] = (IW_EVENT_CAPA_K_0 |
                            IW_EVENT_CAPA_MASK(SIOCGIWAP) |
                            IW_EVENT_CAPA_MASK(SIOCGIWSCAN));
    range->event_capa[1] = IW_EVENT_CAPA_K_1;
    range->event_capa[4] = (IW_EVENT_CAPA_MASK(IWEVCUSTOM) |
                            IW_EVENT_CAPA_MASK(IWEVREGISTERED) |
                            IW_EVENT_CAPA_MASK(IWEVEXPIRED) |
#if WIRELESS_EXT >= 18
                            IW_EVENT_CAPA_MASK(IWEVPMKIDCAND) |
                            IW_EVENT_CAPA_MASK(IWEVASSOCRESPIE) |
                            IW_EVENT_CAPA_MASK(IWEVASSOCREQIE) |
                            IW_EVENT_CAPA_MASK(IWEVGENIE) |
#endif
                            0);

    range->scan_capa = IW_SCAN_CAPA_ESSID | IW_SCAN_CAPA_CHANNEL;
    
    range->txpower_capa = 0;

    range->min_pmp = 1 * 1024;
    range->max_pmp = 65535 * 1024;
    range->min_pmt = 1 * 1024;
    range->max_pmt = 1000 * 1024;
    range->pmp_flags = IW_POWER_PERIOD;
    range->pmt_flags = IW_POWER_TIMEOUT;
    range->pm_capa = 0;

    range->we_version_compiled = WIRELESS_EXT;
    range->we_version_source = 13;

    range->retry_capa = IW_RETRY_LIMIT;
    range->retry_flags = IW_RETRY_LIMIT;
    range->min_retry = 0;
    range->max_retry = 255;

    range->num_frequency = range->num_channels = arSta->arNumChannels;
    for (i = 0; i < arSta->arNumChannels; i++) {
        range->freq[i].i = wlan_freq2ieee(arSta->arChannelList[i]);
        range->freq[i].m = arSta->arChannelList[i] * 100000;
        range->freq[i].e = 1;
         /*
         * Linux supports max of 32 channels, bail out once you
         * reach the max.
         */
        if (i == IW_MAX_FREQUENCIES) {
            break;
        }
    }

    /* Max quality is max field value minus noise floor */
    range->max_qual.qual  = 0xff - 161;

    /*
     * In order to use dBm measurements, 'level' must be lower
     * than any possible measurement (see iw_print_stats() in
     * wireless tools).  It's unclear how this is meant to be
     * done, but setting zero in these values forces dBm and
     * the actual numbers are not used.
     */
    range->max_qual.level = 0;
    range->max_qual.noise = 0;

    range->sensitivity = 3;

    range->max_encoding_tokens = 4;
    /* XXX query driver to find out supported key sizes */
    range->num_encoding_sizes = 3;
    range->encoding_size[0] = 5;        /* 40-bit */
    range->encoding_size[1] = 13;       /* 104-bit */
    range->encoding_size[2] = 16;       /* 128-bit */

    for (i=0, j=0; i<sizeof(rateTable)/sizeof(rateTable[0]) && j<IW_MAX_BITRATES; ++i) {
        A_INT8 rateIdx;
        if (wmi_validate_bitrate(arPriv->arWmi, rateTable[i], &rateIdx)==A_OK) {
            range->bitrate[j] = wmi_get_rate(i) * 1000;
            ++j;            
        }
    }
    range->num_bitrates = j;

    /* estimated maximum TCP throughput values (bps) */
    range->throughput = 22000000;

    range->min_rts = 0;
    range->max_rts = 2347;
    range->min_frag = 256;
    range->max_frag = 2346;

#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,27)
    if (arSta->wpaOffloadEnabled) {
        range->enc_capa |= IW_ENC_CAPA_4WAY_HANDSHAKE;
    }
#endif /* LINUX_VERSION_CODE >= 2.6.27 */

    up(&ar->arSem);

    return ret;
}

/*
 * SIOCSIWPRIV
 *
 */
int
ar6000_ioctl_siwpriv(struct net_device *dev,
              struct iw_request_info *info,
              struct iw_point *data, char *extra)
{
    int ret = -EOPNOTSUPP;
    AR_SOFTC_DEV_T *arPriv = (AR_SOFTC_DEV_T *)ar6k_priv(dev);
    AR_SOFTC_T     *ar = arPriv->arSoftc;
#ifdef ANDROID_ENV
    extern int android_ioctl_siwpriv(struct net_device *, struct iw_request_info *, struct iw_point *, char*);
    char cmd[5];
    if (data->pointer) {
        if (copy_from_user(cmd, data->pointer, sizeof(cmd))) 
            return -EIO;
    }
#endif

    if (!ar || 
            ( (!ar->arWmiReady || (ar->arWlanState != WLAN_ENABLED))
#ifdef ANDROID_ENV
            && (!data->pointer || strncasecmp(cmd, "START", 5)!=0)
#endif
            )
        ) {
        return -EIO;
    }

#ifdef ANDROID_ENV
    ret = android_ioctl_siwpriv(dev, info, data, extra);
    if (ret!=-EOPNOTSUPP) {
        return ret;
    }
#endif
    return ret;
}

/*
 * SIOCSIWAP
 * This ioctl is used to set the desired bssid for the connect command.
 */
int
ar6000_ioctl_siwap(struct net_device *dev,
              struct iw_request_info *info,
              struct sockaddr *ap_addr, char *extra)
{
    AR_SOFTC_DEV_T *arPriv = (AR_SOFTC_DEV_T *)ar6k_priv(dev);
    AR_SOFTC_STA_T *arSta     = &arPriv->arSta;
    AR_SOFTC_T     *ar     = arPriv->arSoftc;

    if (is_iwioctl_allowed(arPriv->arNextMode, info->cmd) != A_OK) {
        AR_DEBUG_PRINTF(ATH_DEBUG_ERR,("wext_ioctl: cmd=0x%x not allowed in this mode\n", info->cmd));
        return -EOPNOTSUPP;
    }

    if (ar->arWlanState == WLAN_DISABLED) {
        return -EIO;
    }

    if (ap_addr->sa_family != ARPHRD_ETHER) {
        return -EIO;
    }

    if (A_MEMCMP(&ap_addr->sa_data, bcast_mac, AR6000_ETH_ADDR_LEN) == 0) {
        A_MEMZERO(arSta->arReqBssid, sizeof(arSta->arReqBssid));
    } else {
        A_MEMCPY(arSta->arReqBssid, &ap_addr->sa_data,  sizeof(arSta->arReqBssid));
    }

    return 0;
}

/*
 * SIOCGIWAP
 */
int
ar6000_ioctl_giwap(struct net_device *dev,
              struct iw_request_info *info,
              struct sockaddr *ap_addr, char *extra)
{
    AR_SOFTC_DEV_T *arPriv = (AR_SOFTC_DEV_T *)ar6k_priv(dev);
    AR_SOFTC_T     *ar     = arPriv->arSoftc;

    if (is_iwioctl_allowed(arPriv->arNextMode, info->cmd) != A_OK) {
        AR_DEBUG_PRINTF(ATH_DEBUG_ERR,("wext_ioctl: cmd=0x%x not allowed in this mode\n", info->cmd));
        return -EOPNOTSUPP;
    }

    if (ar->arWlanState == WLAN_DISABLED) {
        return -EIO;
    }

    if (arPriv->arNetworkType == AP_NETWORK) {
        A_MEMCPY(&ap_addr->sa_data, dev->dev_addr, ATH_MAC_LEN);
        ap_addr->sa_family = ARPHRD_ETHER;
        return 0;
    }

    if (arPriv->arConnected != TRUE) {
        return -EINVAL;
    }

    A_MEMCPY(&ap_addr->sa_data, arPriv->arBssid, sizeof(arPriv->arBssid));
    ap_addr->sa_family = ARPHRD_ETHER;

    return 0;
}

#if (WIRELESS_EXT >= 18)
/*
 * SIOCSIWMLME
 */
int
ar6000_ioctl_siwmlme(struct net_device *dev,
            struct iw_request_info *info,
            struct iw_point *data, char *extra)
{
    AR_SOFTC_DEV_T *arPriv = (AR_SOFTC_DEV_T *)ar6k_priv(dev);
    AR_SOFTC_T     *ar     = arPriv->arSoftc;
    AR_SOFTC_STA_T *arSta  = &arPriv->arSta;

    if (is_iwioctl_allowed(arPriv->arNextMode, info->cmd) != A_OK) {
        AR_DEBUG_PRINTF(ATH_DEBUG_ERR,("wext_ioctl: cmd=0x%x not allowed in this mode\n", info->cmd));
        return -EOPNOTSUPP;
    }

    if (ar->bIsDestroyProgress) {
        return -EBUSY;
    }

    if (ar->arWlanState == WLAN_DISABLED) {
        return -EIO;
    }

    if (ar->arWmiReady == FALSE) {
        return -EIO;
    }

    if (down_interruptible(&ar->arSem)) {
        return -ERESTARTSYS;
    }

    if (data->pointer && data->length == sizeof(struct iw_mlme)) {

        A_UINT8 arNetworkType;
        struct iw_mlme mlme;

        if (copy_from_user(&mlme, data->pointer, sizeof(struct iw_mlme))) {
            up(&ar->arSem);
            return -EIO;
        }

        switch (mlme.cmd) {

            case IW_MLME_DEAUTH:
                /* fall through */
            case IW_MLME_DISASSOC:
                if ((arPriv->arConnected != TRUE) ||
                    (memcmp(arPriv->arBssid, mlme.addr.sa_data, 6) != 0)) {

                    up(&ar->arSem);
                    return -EINVAL;
                }
                wmi_setPmkid_cmd(arPriv->arWmi, arPriv->arBssid, NULL, 0);
                arNetworkType = arPriv->arNetworkType;
                ar6000_init_profile_info(arPriv);
                arPriv->arNetworkType = arNetworkType;
                reconnect_flag = 0;
                ar6000_disconnect(arPriv);
                A_MEMZERO(arPriv->arSsid, sizeof(arPriv->arSsid));
                arPriv->arSsidLen = 0;
                if (arSta->arSkipScan == FALSE) {
                    A_MEMZERO(arSta->arReqBssid, sizeof(arSta->arReqBssid));
                }
                break;

            case IW_MLME_AUTH:
                /* fall through */
            case IW_MLME_ASSOC:
                /* fall through */
            default:
                up(&ar->arSem);
                return -EOPNOTSUPP;
        }
    }

    up(&ar->arSem);
    return 0;
}
#endif /* WIRELESS_EXT >= 18 */

/*
 * SIOCGIWAPLIST
 */
int
ar6000_ioctl_iwaplist(struct net_device *dev,
            struct iw_request_info *info,
            struct iw_point *data, char *extra)
{
    return -EIO;            /* for now */
}

/*
 * SIOCSIWSCAN
 */
int
ar6000_ioctl_siwscan(struct net_device *dev,
                     struct iw_request_info *info,
                     struct iw_point *data, char *extra)
{
#define ACT_DWELLTIME_DEFAULT   105
#define HOME_TXDRAIN_TIME       100
#define SCAN_INT                HOME_TXDRAIN_TIME + ACT_DWELLTIME_DEFAULT
    AR_SOFTC_DEV_T *arPriv = (AR_SOFTC_DEV_T *)ar6k_priv(dev);
    AR_SOFTC_DEV_T *arApPriv = NULL;
    AR_SOFTC_T     *ar     = arPriv->arSoftc;
    AR_SOFTC_STA_T *arSta  = &arPriv->arSta;
    A_UINT16 connChannel = 0;
    int ret = 0;
    struct iw_scan_req *req = (struct iw_scan_req *) extra;
    A_INT8 numChan = 0;
    A_UINT16 channelList[IW_MAX_FREQUENCIES], *pChannelList = NULL;

    if (is_iwioctl_allowed(arPriv->arNextMode, info->cmd) != A_OK) {
        AR_DEBUG_PRINTF(ATH_DEBUG_ERR,("wext_ioctl: cmd=0x%x not allowed in this mode\n", info->cmd));
        return -EOPNOTSUPP;
    }

    if (ar->arWmiReady == FALSE) {
        return -EIO;
    }

    if (ar->arWlanState == WLAN_DISABLED) {
        return -EIO;
    }

    if (data->length < sizeof(struct iw_scan_req)) {
        req = NULL;
    }

    /* If scan is issued in the middle of ongoing scan or connect,
       dont issue another one */
    if ( arSta->scan_triggered > 0 ) {
        ++arSta->scan_triggered;
        if (arSta->scan_triggered < 5) {
            return 0;
        } else {
            AR_DEBUG_PRINTF(ATH_DEBUG_WLAN_SCAN,("Scan request is triggered over 5 times. Not scan complete event\n"));
        }
    } 

    if (!arSta->arUserBssFilter) {
        if (wmi_bssfilter_cmd(arPriv->arWmi, ALL_BSS_FILTER, 0) != A_OK) {
            return -EIO;
        }
    }

    if (arPriv->arConnected) {
        if  (wmi_get_stats_cmd(arPriv->arWmi) != A_OK) {
            return -EIO;
        }
    }

#if WIRELESS_EXT >= 18
    if ( req && (data->flags & IW_SCAN_THIS_ESSID) == IW_SCAN_THIS_ESSID ) {   
        if (wmi_probedSsid_cmd(arPriv->arWmi, 0,
             SPECIFIC_SSID_FLAG, req->essid_len, req->essid) == A_OK) {
            if (!arSta->scanSpecificSsid) {
                arSta->scanSpecificSsid = 1;
            }
        }
    } else {
        A_UINT8 idx;
        for (idx=0; idx<arSta->scanSpecificSsid; ++idx) {
            /* index 0 always reserves for broadcast SSID*/
            A_UINT8 flag = (idx==0) ? ANY_SSID_FLAG : DISABLE_SSID_FLAG;
            wmi_probedSsid_cmd(arPriv->arWmi, idx, flag, 0, NULL);
        }
        arSta->scanSpecificSsid = 0;
    }

    if ( req && (data->flags & IW_SCAN_THIS_FREQ) == IW_SCAN_THIS_FREQ && req->num_channels>0) {
        A_UINT8 i;        
        for (i=0; i<req->num_channels; ++i) {
            struct iw_freq *freq = &req->channel_list[i];
            A_UINT16 *dst = &channelList[numChan];
            if (freq->e == 1) {
                /* freq->m == (Freq HZ value) divided by (10 ^ freq->e) */
                *dst = freq->m / 100000;
                ++numChan;
            } else if (freq->e < 1 && freq->m)  {
                /* It is a channel number if freq->e == 0 */
                *dst = wlan_ieee2freq(freq->m);
                ++numChan;
            }
        }
        if (numChan > 0) {
            pChannelList = channelList;
        }
    }
#endif /* WIRELESS_EXT >= 18 */

    /* AP-STA concurrency. Allow scan only on connected channel when AP and STA
     * both are functional.
     */
    GET_CONN_AP_PRIV(ar,arApPriv);
    if((arApPriv) && (arPriv->arConnected)){
          connChannel = arPriv->arBssChannel; 
         if (wmi_startscan_cmd(arPriv->arWmi, WMI_LONG_SCAN, FALSE, FALSE, \
                     0, 0, 1,&connChannel) != A_OK) {
             ret = -EIO;
         }
    } else {
        if (wmi_startscan_cmd(arPriv->arWmi, WMI_LONG_SCAN, FALSE, FALSE, \
                    0, 0, numChan, pChannelList) != A_OK) {
            ret = -EIO;
        }
    }
    if (ret == 0) {
        arSta->scan_triggered = 1;
    }

    return ret;
#undef  ACT_DWELLTIME_DEFAULT
#undef HOME_TXDRAIN_TIME
#undef SCAN_INT
}


/*
 * Units are in db above the noise floor. That means the
 * rssi values reported in the tx/rx descriptors in the
 * driver are the SNR expressed in db.
 *
 * If you assume that the noise floor is -95, which is an
 * excellent assumption 99.5 % of the time, then you can
 * derive the absolute signal level (i.e. -95 + rssi).
 * There are some other slight factors to take into account
 * depending on whether the rssi measurement is from 11b,
 * 11g, or 11a.   These differences are at most 2db and
 * can be documented.
 *
 * NB: various calculations are based on the orinoco/wavelan
 *     drivers for compatibility
 */
static void
ar6000_set_quality(struct iw_quality *iq, A_INT8 rssi)
{
    if (rssi < 0) {
        iq->qual = 0;
    } else {
        iq->qual = rssi;
    }

    /* NB: max is 94 because noise is hardcoded to 161 */
    if (iq->qual > 94)
        iq->qual = 94;

    iq->noise = 161;        /* -95dBm */
    iq->level = iq->noise + iq->qual;
    iq->updated = 7;
}


int
ar6000_ioctl_siwcommit(struct net_device *dev,
                     struct iw_request_info *info,
                     struct iw_point *data, char *extra)
{

    AR_SOFTC_DEV_T *arPriv = (AR_SOFTC_DEV_T *)ar6k_priv(dev);
    AR_SOFTC_T     *ar     = arPriv->arSoftc;

    if (is_iwioctl_allowed(arPriv->arNextMode, info->cmd) != A_OK) {
        AR_DEBUG_PRINTF(ATH_DEBUG_ERR,("wext_ioctl: cmd=0x%x not allowed in this mode\n", info->cmd));
        return -EOPNOTSUPP;
    }

    if (ar->arWmiReady == FALSE) {
        return -EIO;
    }

    if (ar->arWlanState == WLAN_DISABLED) {
        return -EIO;
    }

    AR_DEBUG_PRINTF(ATH_DEBUG_WLAN_CONNECT,("AP: SSID %s freq %d authmode %d dot11 auth %d"\
                    " PW crypto %d GRP crypto %d\n",
                    arPriv->arSsid, arPriv->arChannelHint,
                    arPriv->arAuthMode, arPriv->arDot11AuthMode,
                    arPriv->arPairwiseCrypto, arPriv->arGroupCrypto));

    /* Stop getting pkts from upper stack */
    netif_stop_queue(arPriv->arNetDev);
    /* Flush the Tx queues */
    ar6000_TxDataCleanup(ar);
    /* Start getting pkts from upper stack */
    netif_wake_queue(arPriv->arNetDev);

    ar6000_ap_mode_profile_commit(arPriv);

    return 0;
}

#define W_PROTO(_x) wait_ ## _x
#define WAIT_HANDLER_IMPL(_x, type) \
int wait_ ## _x (struct net_device *dev, struct iw_request_info *info, type wrqu, char *extra) {\
    int ret; \
    dev_hold(dev); \
    rtnl_unlock(); \
    ret = _x(dev, info, wrqu, extra); \
    rtnl_lock(); \
    dev_put(dev); \
    return ret;\
}

WAIT_HANDLER_IMPL(ar6000_ioctl_siwessid, struct iw_point *)
WAIT_HANDLER_IMPL(ar6000_ioctl_giwrate, struct iw_param *)
WAIT_HANDLER_IMPL(ar6000_ioctl_giwtxpow, struct iw_param *)
WAIT_HANDLER_IMPL(ar6000_ioctl_giwrange, struct iw_point*)

/* Structures to export the Wireless Handlers */
static const iw_handler ath_handlers[] = {
    (iw_handler) ar6000_ioctl_siwcommit,        /* SIOCSIWCOMMIT */
    (iw_handler) ar6000_ioctl_giwname,          /* SIOCGIWNAME */
    (iw_handler) NULL,                          /* SIOCSIWNWID */
    (iw_handler) NULL,                          /* SIOCGIWNWID */
    (iw_handler) ar6000_ioctl_siwfreq,          /* SIOCSIWFREQ */
    (iw_handler) ar6000_ioctl_giwfreq,          /* SIOCGIWFREQ */
    (iw_handler) ar6000_ioctl_siwmode,          /* SIOCSIWMODE */
    (iw_handler) ar6000_ioctl_giwmode,          /* SIOCGIWMODE */
    (iw_handler) ar6000_ioctl_siwsens,          /* SIOCSIWSENS */
    (iw_handler) ar6000_ioctl_giwsens,          /* SIOCGIWSENS */
    (iw_handler) NULL /* not _used */,          /* SIOCSIWRANGE */
    (iw_handler) W_PROTO(ar6000_ioctl_giwrange),/* SIOCGIWRANGE */
    (iw_handler) ar6000_ioctl_siwpriv,         /* SIOCSIWPRIV */
    (iw_handler) NULL /* kernel code */,        /* SIOCGIWPRIV */
    (iw_handler) NULL /* not used */,           /* SIOCSIWSTATS */
    (iw_handler) NULL /* kernel code */,        /* SIOCGIWSTATS */
    (iw_handler) NULL,                          /* SIOCSIWSPY */
    (iw_handler) NULL,                          /* SIOCGIWSPY */
    (iw_handler) NULL,                          /* SIOCSIWTHRSPY */
    (iw_handler) NULL,                          /* SIOCGIWTHRSPY */
    (iw_handler) ar6000_ioctl_siwap,            /* SIOCSIWAP */
    (iw_handler) ar6000_ioctl_giwap,            /* SIOCGIWAP */
#if (WIRELESS_EXT >= 18)
    (iw_handler) ar6000_ioctl_siwmlme,          /* SIOCSIWMLME */
#else
    (iw_handler) NULL,                          /* -- hole -- */
#endif  /* WIRELESS_EXT >= 18 */
    (iw_handler) ar6000_ioctl_iwaplist,         /* SIOCGIWAPLIST */
    (iw_handler) ar6000_ioctl_siwscan,          /* SIOCSIWSCAN */
    (iw_handler) ar6000_ioctl_giwscan,          /* SIOCGIWSCAN */
    (iw_handler) W_PROTO(ar6000_ioctl_siwessid),/* SIOCSIWESSID */
    (iw_handler) ar6000_ioctl_giwessid,         /* SIOCGIWESSID */
    (iw_handler) NULL,                          /* SIOCSIWNICKN */
    (iw_handler) NULL,                          /* SIOCGIWNICKN */
    (iw_handler) NULL,                          /* -- hole -- */
    (iw_handler) NULL,                          /* -- hole -- */
    (iw_handler) ar6000_ioctl_siwrate,          /* SIOCSIWRATE */
    (iw_handler) W_PROTO(ar6000_ioctl_giwrate), /* SIOCGIWRATE */
    (iw_handler) NULL,                          /* SIOCSIWRTS */
    (iw_handler) NULL,                          /* SIOCGIWRTS */
    (iw_handler) NULL,                          /* SIOCSIWFRAG */
    (iw_handler) NULL,                          /* SIOCGIWFRAG */
    (iw_handler) ar6000_ioctl_siwtxpow,         /* SIOCSIWTXPOW */
    (iw_handler) W_PROTO(ar6000_ioctl_giwtxpow),/* SIOCGIWTXPOW */
    (iw_handler) ar6000_ioctl_siwretry,         /* SIOCSIWRETRY */
    (iw_handler) ar6000_ioctl_giwretry,         /* SIOCGIWRETRY */
    (iw_handler) ar6000_ioctl_siwencode,        /* SIOCSIWENCODE */
    (iw_handler) ar6000_ioctl_giwencode,        /* SIOCGIWENCODE */
#if WIRELESS_EXT > 20
    (iw_handler) ar6000_ioctl_siwpower,         /* SIOCSIWPOWER */
    (iw_handler) ar6000_ioctl_giwpower,         /* SIOCGIWPOWER */
#endif // WIRELESS_EXT > 20
#if WIRELESS_EXT >= 18
    (iw_handler) NULL,                          /* -- hole -- */
    (iw_handler) NULL,                          /* -- hole -- */
    (iw_handler) ar6000_ioctl_siwgenie,         /* SIOCSIWGENIE */
    (iw_handler) ar6000_ioctl_giwgenie,         /* SIOCGIWGENIE */
    (iw_handler) ar6000_ioctl_siwauth,          /* SIOCSIWAUTH */
    (iw_handler) ar6000_ioctl_giwauth,          /* SIOCGIWAUTH */
    (iw_handler) ar6000_ioctl_siwencodeext,     /* SIOCSIWENCODEEXT */
    (iw_handler) ar6000_ioctl_giwencodeext,     /* SIOCGIWENCODEEXT */
    (iw_handler) ar6000_ioctl_siwpmksa,         /* SIOCSIWPMKSA */
#endif // WIRELESS_EXT >= 18
};

struct iw_handler_def ath_iw_handler_def = {
    .standard         = (iw_handler *)ath_handlers,
    .num_standard     = ARRAY_SIZE(ath_handlers),
//    .private          = NULL,
//    .num_private      = 0,
};
