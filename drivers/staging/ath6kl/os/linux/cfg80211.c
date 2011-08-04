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

#include <linux/wireless.h>
#include <linux/ieee80211.h>
#include <net/cfg80211.h>

#include "ar6000_drv.h"


extern A_WAITQUEUE_HEAD arEvent;
extern unsigned int wmitimeout;
extern int reconnect_flag;


#define RATETAB_ENT(_rate, _rateid, _flags) {   \
    .bitrate    = (_rate),                  \
    .flags      = (_flags),                 \
    .hw_value   = (_rateid),                \
}

#define CHAN2G(_channel, _freq, _flags) {   \
    .band           = IEEE80211_BAND_2GHZ,  \
    .hw_value       = (_channel),           \
    .center_freq    = (_freq),              \
    .flags          = (_flags),             \
    .max_antenna_gain   = 0,                \
    .max_power      = 30,                   \
}

#define CHAN5G(_channel, _flags) {              \
    .band           = IEEE80211_BAND_5GHZ,      \
    .hw_value       = (_channel),               \
    .center_freq    = 5000 + (5 * (_channel)),  \
    .flags          = (_flags),                 \
    .max_antenna_gain   = 0,                    \
    .max_power      = 30,                       \
}

static struct
ieee80211_rate ar6k_rates[] = {
    RATETAB_ENT(10,  0x1,   0),
    RATETAB_ENT(20,  0x2,   0),
    RATETAB_ENT(55,  0x4,   0),
    RATETAB_ENT(110, 0x8,   0),
    RATETAB_ENT(60,  0x10,  0),
    RATETAB_ENT(90,  0x20,  0),
    RATETAB_ENT(120, 0x40,  0),
    RATETAB_ENT(180, 0x80,  0),
    RATETAB_ENT(240, 0x100, 0),
    RATETAB_ENT(360, 0x200, 0),
    RATETAB_ENT(480, 0x400, 0),
    RATETAB_ENT(540, 0x800, 0),
};

#define ar6k_a_rates     (ar6k_rates + 4)
#define ar6k_a_rates_size    8
#define ar6k_g_rates     (ar6k_rates + 0)
#define ar6k_g_rates_size    12

static struct
ieee80211_channel ar6k_2ghz_channels[] = {
    CHAN2G(1, 2412, 0),
    CHAN2G(2, 2417, 0),
    CHAN2G(3, 2422, 0),
    CHAN2G(4, 2427, 0),
    CHAN2G(5, 2432, 0),
    CHAN2G(6, 2437, 0),
    CHAN2G(7, 2442, 0),
    CHAN2G(8, 2447, 0),
    CHAN2G(9, 2452, 0),
    CHAN2G(10, 2457, 0),
    CHAN2G(11, 2462, 0),
    CHAN2G(12, 2467, 0),
    CHAN2G(13, 2472, 0),
    CHAN2G(14, 2484, 0),
};

static struct
ieee80211_channel ar6k_5ghz_a_channels[] = {
    CHAN5G(34, 0),      CHAN5G(36, 0),
    CHAN5G(38, 0),      CHAN5G(40, 0),
    CHAN5G(42, 0),      CHAN5G(44, 0),
    CHAN5G(46, 0),      CHAN5G(48, 0),
    CHAN5G(52, 0),      CHAN5G(56, 0),
    CHAN5G(60, 0),      CHAN5G(64, 0),
    CHAN5G(100, 0),     CHAN5G(104, 0),
    CHAN5G(108, 0),     CHAN5G(112, 0),
    CHAN5G(116, 0),     CHAN5G(120, 0),
    CHAN5G(124, 0),     CHAN5G(128, 0),
    CHAN5G(132, 0),     CHAN5G(136, 0),
    CHAN5G(140, 0),     CHAN5G(149, 0),
    CHAN5G(153, 0),     CHAN5G(157, 0),
    CHAN5G(161, 0),     CHAN5G(165, 0),
    CHAN5G(184, 0),     CHAN5G(188, 0),
    CHAN5G(192, 0),     CHAN5G(196, 0),
    CHAN5G(200, 0),     CHAN5G(204, 0),
    CHAN5G(208, 0),     CHAN5G(212, 0),
    CHAN5G(216, 0),
};

static struct
ieee80211_supported_band ar6k_band_2ghz = {
    .n_channels = ARRAY_SIZE(ar6k_2ghz_channels),
    .channels = ar6k_2ghz_channels,
    .n_bitrates = ar6k_g_rates_size,
    .bitrates = ar6k_g_rates,
};

static struct
ieee80211_supported_band ar6k_band_5ghz = {
    .n_channels = ARRAY_SIZE(ar6k_5ghz_a_channels),
    .channels = ar6k_5ghz_a_channels,
    .n_bitrates = ar6k_a_rates_size,
    .bitrates = ar6k_a_rates,
};

static int
ar6k_set_wpa_version(struct ar6_softc *ar, enum nl80211_wpa_versions wpa_version)
{

    AR_DEBUG_PRINTF(ATH_DEBUG_INFO, ("%s: %u\n", __func__, wpa_version));

    if (!wpa_version) {
        ar->arAuthMode = NONE_AUTH;
    } else if (wpa_version & NL80211_WPA_VERSION_1) {
        ar->arAuthMode = WPA_AUTH;
    } else if (wpa_version & NL80211_WPA_VERSION_2) {
        ar->arAuthMode = WPA2_AUTH;
    } else {
        AR_DEBUG_PRINTF(ATH_DEBUG_ERR,
                        ("%s: %u not spported\n", __func__, wpa_version));
        return -ENOTSUPP;
    }

    return 0;
}

static int
ar6k_set_auth_type(struct ar6_softc *ar, enum nl80211_auth_type auth_type)
{

    AR_DEBUG_PRINTF(ATH_DEBUG_INFO, ("%s: 0x%x\n", __func__, auth_type));

    switch (auth_type) {
    case NL80211_AUTHTYPE_OPEN_SYSTEM:
        ar->arDot11AuthMode = OPEN_AUTH;
        break;
    case NL80211_AUTHTYPE_SHARED_KEY:
        ar->arDot11AuthMode = SHARED_AUTH;
        break;
    case NL80211_AUTHTYPE_NETWORK_EAP:
        ar->arDot11AuthMode = LEAP_AUTH;
        break;

    case NL80211_AUTHTYPE_AUTOMATIC:
        ar->arDot11AuthMode = OPEN_AUTH;
        ar->arAutoAuthStage = AUTH_OPEN_IN_PROGRESS;
        break;

    default:
        ar->arDot11AuthMode = OPEN_AUTH;
        AR_DEBUG_PRINTF(ATH_DEBUG_INFO,
                        ("%s: 0x%x not spported\n", __func__, auth_type));
        return -ENOTSUPP;
    }

    return 0;
}

static int
ar6k_set_cipher(struct ar6_softc *ar, u32 cipher, bool ucast)
{
    u8 *ar_cipher = ucast ? &ar->arPairwiseCrypto :
                                &ar->arGroupCrypto;
    u8 *ar_cipher_len = ucast ? &ar->arPairwiseCryptoLen :
                                    &ar->arGroupCryptoLen;

    AR_DEBUG_PRINTF(ATH_DEBUG_INFO,
                    ("%s: cipher 0x%x, ucast %u\n", __func__, cipher, ucast));

    switch (cipher) {
    case 0:
    case IW_AUTH_CIPHER_NONE:
        *ar_cipher = NONE_CRYPT;
        *ar_cipher_len = 0;
        break;
    case WLAN_CIPHER_SUITE_WEP40:
        *ar_cipher = WEP_CRYPT;
        *ar_cipher_len = 5;
        break;
    case WLAN_CIPHER_SUITE_WEP104:
        *ar_cipher = WEP_CRYPT;
        *ar_cipher_len = 13;
        break;
    case WLAN_CIPHER_SUITE_TKIP:
        *ar_cipher = TKIP_CRYPT;
        *ar_cipher_len = 0;
        break;
    case WLAN_CIPHER_SUITE_CCMP:
        *ar_cipher = AES_CRYPT;
        *ar_cipher_len = 0;
        break;
    default:
        AR_DEBUG_PRINTF(ATH_DEBUG_ERR,
                        ("%s: cipher 0x%x not supported\n", __func__, cipher));
        return -ENOTSUPP;
    }

    return 0;
}

static void
ar6k_set_key_mgmt(struct ar6_softc *ar, u32 key_mgmt)
{
    AR_DEBUG_PRINTF(ATH_DEBUG_INFO, ("%s: 0x%x\n", __func__, key_mgmt));

    if (WLAN_AKM_SUITE_PSK == key_mgmt) {
        if (WPA_AUTH == ar->arAuthMode) {
            ar->arAuthMode = WPA_PSK_AUTH;
        } else if (WPA2_AUTH == ar->arAuthMode) {
            ar->arAuthMode = WPA2_PSK_AUTH;
        }
    } else if (WLAN_AKM_SUITE_8021X != key_mgmt) {
        ar->arAuthMode = NONE_AUTH;
    }
}

static int
ar6k_cfg80211_connect(struct wiphy *wiphy, struct net_device *dev,
                      struct cfg80211_connect_params *sme)
{
    struct ar6_softc *ar = ar6k_priv(dev);
    int status;

    AR_DEBUG_PRINTF(ATH_DEBUG_INFO, ("%s: \n", __func__));
    ar->smeState = SME_CONNECTING;

    if(ar->arWmiReady == false) {
        AR_DEBUG_PRINTF(ATH_DEBUG_ERR, ("%s: Wmi not ready yet\n", __func__));
        return -EIO;
    }

    if(ar->arWlanState == WLAN_DISABLED) {
        AR_DEBUG_PRINTF(ATH_DEBUG_ERR, ("%s: Wlan disabled\n", __func__));
        return -EIO;
    }

    if(ar->bIsDestroyProgress) {
        AR_DEBUG_PRINTF(ATH_DEBUG_ERR, ("%s: destroy in progress\n", __func__));
        return -EBUSY;
    }

    if(!sme->ssid_len || IEEE80211_MAX_SSID_LEN < sme->ssid_len) {
        AR_DEBUG_PRINTF(ATH_DEBUG_ERR, ("%s: ssid invalid\n", __func__));
        return -EINVAL;
    }

    if(ar->arSkipScan == true &&
       ((sme->channel && sme->channel->center_freq == 0) ||
        (sme->bssid && !sme->bssid[0] && !sme->bssid[1] && !sme->bssid[2] &&
         !sme->bssid[3] && !sme->bssid[4] && !sme->bssid[5])))
    {
        AR_DEBUG_PRINTF(ATH_DEBUG_ERR, ("%s:SkipScan: channel or bssid invalid\n", __func__));
        return -EINVAL;
    }

    if(down_interruptible(&ar->arSem)) {
        AR_DEBUG_PRINTF(ATH_DEBUG_ERR, ("%s: busy, couldn't get access\n", __func__));
        return -ERESTARTSYS;
    }

    if(ar->bIsDestroyProgress) {
        AR_DEBUG_PRINTF(ATH_DEBUG_ERR, ("%s: busy, destroy in progress\n", __func__));
        up(&ar->arSem);
        return -EBUSY;
    }

    if(ar->arTxPending[wmi_get_control_ep(ar->arWmi)]) {
        /*
        * sleep until the command queue drains
        */
        wait_event_interruptible_timeout(arEvent,
        ar->arTxPending[wmi_get_control_ep(ar->arWmi)] == 0, wmitimeout * HZ);
        if (signal_pending(current)) {
            AR_DEBUG_PRINTF(ATH_DEBUG_ERR, ("%s: cmd queue drain timeout\n", __func__));
            up(&ar->arSem);
            return -EINTR;
        }
    }

    if(ar->arConnected == true &&
       ar->arSsidLen == sme->ssid_len &&
       !memcmp(ar->arSsid, sme->ssid, ar->arSsidLen)) {
        reconnect_flag = true;
        status = wmi_reconnect_cmd(ar->arWmi,
                                   ar->arReqBssid,
                                   ar->arChannelHint);

        up(&ar->arSem);
        if (status) {
            AR_DEBUG_PRINTF(ATH_DEBUG_ERR, ("%s: wmi_reconnect_cmd failed\n", __func__));
            return -EIO;
        }
        return 0;
    } else if(ar->arSsidLen == sme->ssid_len &&
              !memcmp(ar->arSsid, sme->ssid, ar->arSsidLen)) {
	    ar6000_disconnect(ar);
    }

    A_MEMZERO(ar->arSsid, sizeof(ar->arSsid));
    ar->arSsidLen = sme->ssid_len;
    memcpy(ar->arSsid, sme->ssid, sme->ssid_len);

    if(sme->channel){
        ar->arChannelHint = sme->channel->center_freq;
    }

    A_MEMZERO(ar->arReqBssid, sizeof(ar->arReqBssid));
    if(sme->bssid){
        if(memcmp(&sme->bssid, bcast_mac, AR6000_ETH_ADDR_LEN)) {
            memcpy(ar->arReqBssid, sme->bssid, sizeof(ar->arReqBssid));
        }
    }

    ar6k_set_wpa_version(ar, sme->crypto.wpa_versions);
    ar6k_set_auth_type(ar, sme->auth_type);

    if(sme->crypto.n_ciphers_pairwise) {
        ar6k_set_cipher(ar, sme->crypto.ciphers_pairwise[0], true);
    } else {
        ar6k_set_cipher(ar, IW_AUTH_CIPHER_NONE, true);
    }
    ar6k_set_cipher(ar, sme->crypto.cipher_group, false);

    if(sme->crypto.n_akm_suites) {
        ar6k_set_key_mgmt(ar, sme->crypto.akm_suites[0]);
    }

    if((sme->key_len) &&
       (NONE_AUTH == ar->arAuthMode) &&
        (WEP_CRYPT == ar->arPairwiseCrypto)) {
        struct ar_key *key = NULL;

        if(sme->key_idx < WMI_MIN_KEY_INDEX || sme->key_idx > WMI_MAX_KEY_INDEX) {
            AR_DEBUG_PRINTF(ATH_DEBUG_ERR,
                            ("%s: key index %d out of bounds\n", __func__, sme->key_idx));
            up(&ar->arSem);
            return -ENOENT;
        }

        key = &ar->keys[sme->key_idx];
        key->key_len = sme->key_len;
        memcpy(key->key, sme->key, key->key_len);
        key->cipher = ar->arPairwiseCrypto;
        ar->arDefTxKeyIndex = sme->key_idx;

        wmi_addKey_cmd(ar->arWmi, sme->key_idx,
                    ar->arPairwiseCrypto,
                    GROUP_USAGE | TX_USAGE,
                    key->key_len,
                    NULL,
                    key->key, KEY_OP_INIT_VAL, NULL,
                    NO_SYNC_WMIFLAG);
    }

    if (!ar->arUserBssFilter) {
        if (wmi_bssfilter_cmd(ar->arWmi, ALL_BSS_FILTER, 0) != 0) {
            AR_DEBUG_PRINTF(ATH_DEBUG_ERR, ("%s: Couldn't set bss filtering\n", __func__));
            up(&ar->arSem);
            return -EIO;
        }
    }

    ar->arNetworkType = ar->arNextMode;

    AR_DEBUG_PRINTF(ATH_DEBUG_INFO, ("%s: Connect called with authmode %d dot11 auth %d"\
                    " PW crypto %d PW crypto Len %d GRP crypto %d"\
                    " GRP crypto Len %d channel hint %u\n",
                    __func__, ar->arAuthMode, ar->arDot11AuthMode,
                    ar->arPairwiseCrypto, ar->arPairwiseCryptoLen,
                    ar->arGroupCrypto, ar->arGroupCryptoLen, ar->arChannelHint));

    reconnect_flag = 0;
    status = wmi_connect_cmd(ar->arWmi, ar->arNetworkType,
                            ar->arDot11AuthMode, ar->arAuthMode,
                            ar->arPairwiseCrypto, ar->arPairwiseCryptoLen,
                            ar->arGroupCrypto,ar->arGroupCryptoLen,
                            ar->arSsidLen, ar->arSsid,
                            ar->arReqBssid, ar->arChannelHint,
                            ar->arConnectCtrlFlags);

    up(&ar->arSem);

    if (A_EINVAL == status) {
        A_MEMZERO(ar->arSsid, sizeof(ar->arSsid));
        ar->arSsidLen = 0;
        AR_DEBUG_PRINTF(ATH_DEBUG_ERR, ("%s: Invalid request\n", __func__));
        return -ENOENT;
    } else if (status) {
        AR_DEBUG_PRINTF(ATH_DEBUG_ERR, ("%s: wmi_connect_cmd failed\n", __func__));
        return -EIO;
    }

    if ((!(ar->arConnectCtrlFlags & CONNECT_DO_WPA_OFFLOAD)) &&
        ((WPA_PSK_AUTH == ar->arAuthMode) || (WPA2_PSK_AUTH == ar->arAuthMode)))
    {
        A_TIMEOUT_MS(&ar->disconnect_timer, A_DISCONNECT_TIMER_INTERVAL, 0);
    }

    ar->arConnectCtrlFlags &= ~CONNECT_DO_WPA_OFFLOAD;
    ar->arConnectPending = true;

    return 0;
}

void
ar6k_cfg80211_connect_event(struct ar6_softc *ar, u16 channel,
                u8 *bssid, u16 listenInterval,
                u16 beaconInterval,NETWORK_TYPE networkType,
                u8 beaconIeLen, u8 assocReqLen,
                u8 assocRespLen, u8 *assocInfo)
{
    u16 size = 0;
    u16 capability = 0;
    struct cfg80211_bss *bss = NULL;
    struct ieee80211_mgmt *mgmt = NULL;
    struct ieee80211_channel *ibss_channel = NULL;
    s32 signal = 50 * 100;
    u8 ie_buf_len = 0;
    unsigned char ie_buf[256];
    unsigned char *ptr_ie_buf = ie_buf;
    unsigned char *ieeemgmtbuf = NULL;
    u8 source_mac[ATH_MAC_LEN];

    u8 assocReqIeOffset = sizeof(u16)  +  /* capinfo*/
                               sizeof(u16);    /* listen interval */
    u8 assocRespIeOffset = sizeof(u16) +  /* capinfo*/
                                sizeof(u16) +  /* status Code */
                                sizeof(u16);   /* associd */
    u8 *assocReqIe = assocInfo + beaconIeLen + assocReqIeOffset;
    u8 *assocRespIe = assocInfo + beaconIeLen + assocReqLen + assocRespIeOffset;

    AR_DEBUG_PRINTF(ATH_DEBUG_INFO, ("%s: \n", __func__));

    assocReqLen -= assocReqIeOffset;
    assocRespLen -= assocRespIeOffset;

    ar->arAutoAuthStage = AUTH_IDLE;

    if((ADHOC_NETWORK & networkType)) {
        if(NL80211_IFTYPE_ADHOC != ar->wdev->iftype) {
            AR_DEBUG_PRINTF(ATH_DEBUG_INFO,
                            ("%s: ath6k not in ibss mode\n", __func__));
            return;
        }
    }

    if((INFRA_NETWORK & networkType)) {
        if(NL80211_IFTYPE_STATION != ar->wdev->iftype) {
            AR_DEBUG_PRINTF(ATH_DEBUG_INFO,
                            ("%s: ath6k not in station mode\n", __func__));
            return;
        }
    }

    /* Before informing the join/connect event, make sure that
     * bss entry is present in scan list, if it not present
     * construct and insert into scan list, otherwise that
     * event will be dropped on the way by cfg80211, due to
     * this keys will not be plumbed in case of WEP and
     * application will not be aware of join/connect status. */
    bss = cfg80211_get_bss(ar->wdev->wiphy, NULL, bssid,
                           ar->wdev->ssid, ar->wdev->ssid_len,
                           ((ADHOC_NETWORK & networkType) ? WLAN_CAPABILITY_IBSS : WLAN_CAPABILITY_ESS),
                           ((ADHOC_NETWORK & networkType) ? WLAN_CAPABILITY_IBSS : WLAN_CAPABILITY_ESS));

    /*
     * Earlier we were updating the cfg about bss by making a beacon frame
     * only if the entry for bss is not there. This can have some issue if
     * ROAM event is generated and a heavy traffic is ongoing. The ROAM
     * event is handled through a work queue and by the time it really gets
     * handled, BSS would have been aged out. So it is better to update the
     * cfg about BSS irrespective of its entry being present right now or
     * not.
     */

    if (ADHOC_NETWORK & networkType) {
            /* construct 802.11 mgmt beacon */
            if(ptr_ie_buf) {
		    *ptr_ie_buf++ = WLAN_EID_SSID;
		    *ptr_ie_buf++ = ar->arSsidLen;
		    memcpy(ptr_ie_buf, ar->arSsid, ar->arSsidLen);
		    ptr_ie_buf +=ar->arSsidLen;

		    *ptr_ie_buf++ = WLAN_EID_IBSS_PARAMS;
		    *ptr_ie_buf++ = 2; /* length */
		    *ptr_ie_buf++ = 0; /* ATIM window */
		    *ptr_ie_buf++ = 0; /* ATIM window */

		    /* TODO: update ibss params and include supported rates,
		     * DS param set, extened support rates, wmm. */

		    ie_buf_len = ptr_ie_buf - ie_buf;
            }

            capability |= IEEE80211_CAPINFO_IBSS;
            if(WEP_CRYPT == ar->arPairwiseCrypto) {
		    capability |= IEEE80211_CAPINFO_PRIVACY;
            }
            memcpy(source_mac, ar->arNetDev->dev_addr, ATH_MAC_LEN);
            ptr_ie_buf = ie_buf;
    } else {
            capability = *(u16 *)(&assocInfo[beaconIeLen]);
            memcpy(source_mac, bssid, ATH_MAC_LEN);
            ptr_ie_buf = assocReqIe;
            ie_buf_len = assocReqLen;
    }

    size = offsetof(struct ieee80211_mgmt, u)
	    + sizeof(mgmt->u.beacon)
	    + ie_buf_len;

    ieeemgmtbuf = A_MALLOC_NOWAIT(size);
    if(!ieeemgmtbuf) {
            AR_DEBUG_PRINTF(ATH_DEBUG_ERR,
                            ("%s: ieeeMgmtbuf alloc error\n", __func__));
	    cfg80211_put_bss(bss);
            return;
    }

    A_MEMZERO(ieeemgmtbuf, size);
    mgmt = (struct ieee80211_mgmt *)ieeemgmtbuf;
    mgmt->frame_control = (IEEE80211_FTYPE_MGMT | IEEE80211_STYPE_BEACON);
    memcpy(mgmt->da, bcast_mac, ATH_MAC_LEN);
    memcpy(mgmt->sa, source_mac, ATH_MAC_LEN);
    memcpy(mgmt->bssid, bssid, ATH_MAC_LEN);
    mgmt->u.beacon.beacon_int = beaconInterval;
    mgmt->u.beacon.capab_info = capability;
    memcpy(mgmt->u.beacon.variable, ptr_ie_buf, ie_buf_len);

    ibss_channel = ieee80211_get_channel(ar->wdev->wiphy, (int)channel);

    AR_DEBUG_PRINTF(ATH_DEBUG_INFO,
		    ("%s: inform bss with bssid %pM channel %d beaconInterval %d "
		     "capability 0x%x\n", __func__, mgmt->bssid,
		     ibss_channel->hw_value, beaconInterval, capability));

    bss = cfg80211_inform_bss_frame(ar->wdev->wiphy,
				    ibss_channel, mgmt,
				    le16_to_cpu(size),
				    signal, GFP_KERNEL);
    kfree(ieeemgmtbuf);
    cfg80211_put_bss(bss);

    if((ADHOC_NETWORK & networkType)) {
        cfg80211_ibss_joined(ar->arNetDev, bssid, GFP_KERNEL);
        return;
    }

    if (false == ar->arConnected) {
        /* inform connect result to cfg80211 */
        ar->smeState = SME_DISCONNECTED;
        cfg80211_connect_result(ar->arNetDev, bssid,
                                assocReqIe, assocReqLen,
                                assocRespIe, assocRespLen,
                                WLAN_STATUS_SUCCESS, GFP_KERNEL);
    } else {
        /* inform roam event to cfg80211 */
	cfg80211_roamed(ar->arNetDev, ibss_channel, bssid,
                        assocReqIe, assocReqLen,
                        assocRespIe, assocRespLen,
                        GFP_KERNEL);
    }
}

static int
ar6k_cfg80211_disconnect(struct wiphy *wiphy, struct net_device *dev,
                        u16 reason_code)
{
    struct ar6_softc *ar = (struct ar6_softc *)ar6k_priv(dev);

    AR_DEBUG_PRINTF(ATH_DEBUG_INFO, ("%s: reason=%u\n", __func__, reason_code));

    if(ar->arWmiReady == false) {
        AR_DEBUG_PRINTF(ATH_DEBUG_ERR, ("%s: Wmi not ready\n", __func__));
        return -EIO;
    }

    if(ar->arWlanState == WLAN_DISABLED) {
        AR_DEBUG_PRINTF(ATH_DEBUG_ERR, ("%s: Wlan disabled\n", __func__));
        return -EIO;
    }

    if(ar->bIsDestroyProgress) {
        AR_DEBUG_PRINTF(ATH_DEBUG_ERR, ("%s: busy, destroy in progress\n", __func__));
        return -EBUSY;
    }

    if(down_interruptible(&ar->arSem)) {
        AR_DEBUG_PRINTF(ATH_DEBUG_ERR, ("%s: busy, couldn't get access\n", __func__));
        return -ERESTARTSYS;
    }

    reconnect_flag = 0;
    ar6000_disconnect(ar);
    A_MEMZERO(ar->arSsid, sizeof(ar->arSsid));
    ar->arSsidLen = 0;

    if (ar->arSkipScan == false) {
        A_MEMZERO(ar->arReqBssid, sizeof(ar->arReqBssid));
    }

    up(&ar->arSem);

    return 0;
}

void
ar6k_cfg80211_disconnect_event(struct ar6_softc *ar, u8 reason,
                               u8 *bssid, u8 assocRespLen,
                               u8 *assocInfo, u16 protocolReasonStatus)
{

    u16 status;

    AR_DEBUG_PRINTF(ATH_DEBUG_INFO, ("%s: reason=%u\n", __func__, reason));

    if (ar->scan_request) {
	cfg80211_scan_done(ar->scan_request, true);
        ar->scan_request = NULL;
    }
    if((ADHOC_NETWORK & ar->arNetworkType)) {
        if(NL80211_IFTYPE_ADHOC != ar->wdev->iftype) {
            AR_DEBUG_PRINTF(ATH_DEBUG_INFO,
                            ("%s: ath6k not in ibss mode\n", __func__));
            return;
        }
        A_MEMZERO(bssid, ETH_ALEN);
        cfg80211_ibss_joined(ar->arNetDev, bssid, GFP_KERNEL);
        return;
    }

    if((INFRA_NETWORK & ar->arNetworkType)) {
        if(NL80211_IFTYPE_STATION != ar->wdev->iftype) {
            AR_DEBUG_PRINTF(ATH_DEBUG_INFO,
                            ("%s: ath6k not in station mode\n", __func__));
            return;
        }
    }

    if(true == ar->arConnectPending) {
        if(NO_NETWORK_AVAIL == reason) {
            /* connect cmd failed */
            wmi_disconnect_cmd(ar->arWmi);
        } else if (reason == DISCONNECT_CMD) {
		if (ar->arAutoAuthStage) {
			/*
			 * If the current auth algorithm is open try shared
			 * and make autoAuthStage idle. We do not make it
			 * leap for now being.
			 */
			if (ar->arDot11AuthMode == OPEN_AUTH) {
				struct ar_key *key = NULL;
				key = &ar->keys[ar->arDefTxKeyIndex];
				if (down_interruptible(&ar->arSem)) {
					AR_DEBUG_PRINTF(ATH_DEBUG_ERR, ("%s: busy, couldn't get access\n", __func__));
					return;
				}


				ar->arDot11AuthMode = SHARED_AUTH;
				ar->arAutoAuthStage = AUTH_IDLE;

				wmi_addKey_cmd(ar->arWmi, ar->arDefTxKeyIndex,
						ar->arPairwiseCrypto,
						GROUP_USAGE | TX_USAGE,
						key->key_len,
						NULL,
						key->key, KEY_OP_INIT_VAL, NULL,
						NO_SYNC_WMIFLAG);

				status = wmi_connect_cmd(ar->arWmi,
							 ar->arNetworkType,
							 ar->arDot11AuthMode,
							 ar->arAuthMode,
							 ar->arPairwiseCrypto,
							 ar->arPairwiseCryptoLen,
							 ar->arGroupCrypto,
							 ar->arGroupCryptoLen,
							 ar->arSsidLen,
							 ar->arSsid,
							 ar->arReqBssid,
							 ar->arChannelHint,
							 ar->arConnectCtrlFlags);
				up(&ar->arSem);

			} else if (ar->arDot11AuthMode == SHARED_AUTH) {
				/* should not reach here */
			}
		} else {
			ar->arConnectPending = false;
			if (ar->smeState == SME_CONNECTING) {
				cfg80211_connect_result(ar->arNetDev, bssid,
							NULL, 0,
							NULL, 0,
							WLAN_STATUS_UNSPECIFIED_FAILURE,
							GFP_KERNEL);
			} else {
				cfg80211_disconnected(ar->arNetDev,
						      reason,
						      NULL, 0,
						      GFP_KERNEL);
			}
			ar->smeState = SME_DISCONNECTED;
		}
	}
    } else {
	    if (reason != DISCONNECT_CMD)
		    wmi_disconnect_cmd(ar->arWmi);
    }
}

void
ar6k_cfg80211_scan_node(void *arg, bss_t *ni)
{
    struct wiphy *wiphy = (struct wiphy *)arg;
    u16 size;
    unsigned char *ieeemgmtbuf = NULL;
    struct ieee80211_mgmt *mgmt;
    struct ieee80211_channel *channel;
    struct ieee80211_supported_band *band;
    struct ieee80211_common_ie  *cie;
    s32 signal;
    int freq;

    cie = &ni->ni_cie;

#define CHAN_IS_11A(x)  (!((x >= 2412) && (x <= 2484)))
    if(CHAN_IS_11A(cie->ie_chan)) {
        /* 11a */
        band = wiphy->bands[IEEE80211_BAND_5GHZ];
    } else if((cie->ie_erp) || (cie->ie_xrates)) {
        /* 11g */
        band = wiphy->bands[IEEE80211_BAND_2GHZ];
    } else {
        /* 11b */
        band = wiphy->bands[IEEE80211_BAND_2GHZ];
    }

    size = ni->ni_framelen + offsetof(struct ieee80211_mgmt, u);
    ieeemgmtbuf = A_MALLOC_NOWAIT(size);
    if(!ieeemgmtbuf)
    {
        AR_DEBUG_PRINTF(ATH_DEBUG_ERR, ("%s: ieeeMgmtbuf alloc error\n", __func__));
        return;
    }

    /* Note:
       TODO: Update target to include 802.11 mac header while sending bss info.
       Target removes 802.11 mac header while sending the bss info to host,
       cfg80211 needs it, for time being just filling the da, sa and bssid fields alone.
    */
    mgmt = (struct ieee80211_mgmt *)ieeemgmtbuf;
    memcpy(mgmt->da, bcast_mac, ATH_MAC_LEN);
    memcpy(mgmt->sa, ni->ni_macaddr, ATH_MAC_LEN);
    memcpy(mgmt->bssid, ni->ni_macaddr, ATH_MAC_LEN);
    memcpy(ieeemgmtbuf + offsetof(struct ieee80211_mgmt, u),
             ni->ni_buf, ni->ni_framelen);

    freq    = cie->ie_chan;
    channel = ieee80211_get_channel(wiphy, freq);
    signal  = ni->ni_snr * 100;

	AR_DEBUG_PRINTF(ATH_DEBUG_INFO,
		("%s: bssid %pM channel %d freq %d size %d\n", __func__,
			mgmt->bssid, channel->hw_value, freq, size));
    cfg80211_inform_bss_frame(wiphy, channel, mgmt,
                              le16_to_cpu(size),
                              signal, GFP_KERNEL);

    kfree (ieeemgmtbuf);
}

static int
ar6k_cfg80211_scan(struct wiphy *wiphy, struct net_device *ndev,
                   struct cfg80211_scan_request *request)
{
    struct ar6_softc *ar = (struct ar6_softc *)ar6k_priv(ndev);
    int ret = 0;
    u32 forceFgScan = 0;

    AR_DEBUG_PRINTF(ATH_DEBUG_INFO, ("%s: \n", __func__));

    if(ar->arWmiReady == false) {
        AR_DEBUG_PRINTF(ATH_DEBUG_ERR, ("%s: Wmi not ready\n", __func__));
        return -EIO;
    }

    if(ar->arWlanState == WLAN_DISABLED) {
        AR_DEBUG_PRINTF(ATH_DEBUG_ERR, ("%s: Wlan disabled\n", __func__));
        return -EIO;
    }

    if (!ar->arUserBssFilter) {
        if (wmi_bssfilter_cmd(ar->arWmi,
                             (ar->arConnected ? ALL_BUT_BSS_FILTER : ALL_BSS_FILTER),
                             0) != 0) {
            AR_DEBUG_PRINTF(ATH_DEBUG_ERR, ("%s: Couldn't set bss filtering\n", __func__));
            return -EIO;
        }
    }

    if(request->n_ssids &&
       request->ssids[0].ssid_len) {
        u8 i;

        if(request->n_ssids > (MAX_PROBED_SSID_INDEX - 1)) {
            request->n_ssids = MAX_PROBED_SSID_INDEX - 1;
        }

        for (i = 0; i < request->n_ssids; i++) {
            wmi_probedSsid_cmd(ar->arWmi, i+1, SPECIFIC_SSID_FLAG,
                               request->ssids[i].ssid_len,
                               request->ssids[i].ssid);
        }
    }

    if(ar->arConnected) {
        forceFgScan = 1;
    }

    if(wmi_startscan_cmd(ar->arWmi, WMI_LONG_SCAN, forceFgScan, false, \
                         0, 0, 0, NULL) != 0) {
        AR_DEBUG_PRINTF(ATH_DEBUG_ERR, ("%s: wmi_startscan_cmd failed\n", __func__));
        ret = -EIO;
    }

    ar->scan_request = request;

    return ret;
}

void
ar6k_cfg80211_scanComplete_event(struct ar6_softc *ar, int status)
{

    AR_DEBUG_PRINTF(ATH_DEBUG_INFO, ("%s: status %d\n", __func__, status));

    if(ar->scan_request)
    {
        /* Translate data to cfg80211 mgmt format */
	if (ar->arWmi)
		wmi_iterate_nodes(ar->arWmi, ar6k_cfg80211_scan_node, ar->wdev->wiphy);

        cfg80211_scan_done(ar->scan_request,
            ((status & A_ECANCELED) || (status & A_EBUSY)) ? true : false);

        if(ar->scan_request->n_ssids &&
           ar->scan_request->ssids[0].ssid_len) {
            u8 i;

            for (i = 0; i < ar->scan_request->n_ssids; i++) {
                wmi_probedSsid_cmd(ar->arWmi, i+1, DISABLE_SSID_FLAG,
                                   0, NULL);
            }
        }
        ar->scan_request = NULL;
    }
}

static int
ar6k_cfg80211_add_key(struct wiphy *wiphy, struct net_device *ndev,
                      u8 key_index, bool pairwise, const u8 *mac_addr,
                      struct key_params *params)
{
    struct ar6_softc *ar = (struct ar6_softc *)ar6k_priv(ndev);
    struct ar_key *key = NULL;
    u8 key_usage;
    u8 key_type;
    int status = 0;

    AR_DEBUG_PRINTF(ATH_DEBUG_INFO, ("%s:\n", __func__));

    if(ar->arWmiReady == false) {
        AR_DEBUG_PRINTF(ATH_DEBUG_ERR, ("%s: Wmi not ready\n", __func__));
        return -EIO;
    }

    if(ar->arWlanState == WLAN_DISABLED) {
        AR_DEBUG_PRINTF(ATH_DEBUG_ERR, ("%s: Wlan disabled\n", __func__));
        return -EIO;
    }

    if(key_index < WMI_MIN_KEY_INDEX || key_index > WMI_MAX_KEY_INDEX) {
        AR_DEBUG_PRINTF(ATH_DEBUG_INFO,
                        ("%s: key index %d out of bounds\n", __func__, key_index));
        return -ENOENT;
    }

    key = &ar->keys[key_index];
    A_MEMZERO(key, sizeof(struct ar_key));

    if(!mac_addr || is_broadcast_ether_addr(mac_addr)) {
        key_usage = GROUP_USAGE;
    } else {
        key_usage = PAIRWISE_USAGE;
    }

    if(params) {
        if(params->key_len > WLAN_MAX_KEY_LEN ||
            params->seq_len > IW_ENCODE_SEQ_MAX_SIZE)
            return -EINVAL;

        key->key_len = params->key_len;
        memcpy(key->key, params->key, key->key_len);
        key->seq_len = params->seq_len;
        memcpy(key->seq, params->seq, key->seq_len);
        key->cipher = params->cipher;
    }

    switch (key->cipher) {
    case WLAN_CIPHER_SUITE_WEP40:
    case WLAN_CIPHER_SUITE_WEP104:
        key_type = WEP_CRYPT;
        break;

    case WLAN_CIPHER_SUITE_TKIP:
        key_type = TKIP_CRYPT;
        break;

    case WLAN_CIPHER_SUITE_CCMP:
        key_type = AES_CRYPT;
        break;

    default:
        return -ENOTSUPP;
    }

    if (((WPA_PSK_AUTH == ar->arAuthMode) || (WPA2_PSK_AUTH == ar->arAuthMode)) &&
        (GROUP_USAGE & key_usage))
    {
        A_UNTIMEOUT(&ar->disconnect_timer);
    }

    AR_DEBUG_PRINTF(ATH_DEBUG_INFO,
                    ("%s: index %d, key_len %d, key_type 0x%x,"\
                    " key_usage 0x%x, seq_len %d\n",
                    __func__, key_index, key->key_len, key_type,
                    key_usage, key->seq_len));

    ar->arDefTxKeyIndex = key_index;
    status = wmi_addKey_cmd(ar->arWmi, ar->arDefTxKeyIndex, key_type, key_usage,
                    key->key_len, key->seq, key->key, KEY_OP_INIT_VAL,
                    (u8 *)mac_addr, SYNC_BOTH_WMIFLAG);


    if (status) {
        return -EIO;
    }

    return 0;
}

static int
ar6k_cfg80211_del_key(struct wiphy *wiphy, struct net_device *ndev,
                      u8 key_index, bool pairwise, const u8 *mac_addr)
{
    struct ar6_softc *ar = (struct ar6_softc *)ar6k_priv(ndev);

    AR_DEBUG_PRINTF(ATH_DEBUG_INFO, ("%s: index %d\n", __func__, key_index));

    if(ar->arWmiReady == false) {
        AR_DEBUG_PRINTF(ATH_DEBUG_ERR, ("%s: Wmi not ready\n", __func__));
        return -EIO;
    }

    if(ar->arWlanState == WLAN_DISABLED) {
        AR_DEBUG_PRINTF(ATH_DEBUG_ERR, ("%s: Wlan disabled\n", __func__));
        return -EIO;
    }

    if(key_index < WMI_MIN_KEY_INDEX || key_index > WMI_MAX_KEY_INDEX) {
        AR_DEBUG_PRINTF(ATH_DEBUG_INFO,
                        ("%s: key index %d out of bounds\n", __func__, key_index));
        return -ENOENT;
    }

    if(!ar->keys[key_index].key_len) {
        AR_DEBUG_PRINTF(ATH_DEBUG_INFO, ("%s: index %d is empty\n", __func__, key_index));
        return 0;
    }

    ar->keys[key_index].key_len = 0;

    return wmi_deleteKey_cmd(ar->arWmi, key_index);
}


static int
ar6k_cfg80211_get_key(struct wiphy *wiphy, struct net_device *ndev,
                      u8 key_index, bool pairwise, const u8 *mac_addr,
                      void *cookie,
                      void (*callback)(void *cookie, struct key_params*))
{
    struct ar6_softc *ar = (struct ar6_softc *)ar6k_priv(ndev);
    struct ar_key *key = NULL;
    struct key_params params;

    AR_DEBUG_PRINTF(ATH_DEBUG_INFO, ("%s: index %d\n", __func__, key_index));

    if(ar->arWmiReady == false) {
        AR_DEBUG_PRINTF(ATH_DEBUG_ERR, ("%s: Wmi not ready\n", __func__));
        return -EIO;
    }

    if(ar->arWlanState == WLAN_DISABLED) {
        AR_DEBUG_PRINTF(ATH_DEBUG_ERR, ("%s: Wlan disabled\n", __func__));
        return -EIO;
    }

    if(key_index < WMI_MIN_KEY_INDEX || key_index > WMI_MAX_KEY_INDEX) {
        AR_DEBUG_PRINTF(ATH_DEBUG_INFO,
                        ("%s: key index %d out of bounds\n", __func__, key_index));
        return -ENOENT;
    }

    key = &ar->keys[key_index];
    A_MEMZERO(&params, sizeof(params));
    params.cipher = key->cipher;
    params.key_len = key->key_len;
    params.seq_len = key->seq_len;
    params.seq = key->seq;
    params.key = key->key;

    callback(cookie, &params);

    return key->key_len ? 0 : -ENOENT;
}


static int
ar6k_cfg80211_set_default_key(struct wiphy *wiphy, struct net_device *ndev,
                              u8 key_index, bool unicast, bool multicast)
{
    struct ar6_softc *ar = (struct ar6_softc *)ar6k_priv(ndev);
    struct ar_key *key = NULL;
    int status = 0;
    u8 key_usage;

    AR_DEBUG_PRINTF(ATH_DEBUG_INFO, ("%s: index %d\n", __func__, key_index));

    if(ar->arWmiReady == false) {
        AR_DEBUG_PRINTF(ATH_DEBUG_ERR, ("%s: Wmi not ready\n", __func__));
        return -EIO;
    }

    if(ar->arWlanState == WLAN_DISABLED) {
        AR_DEBUG_PRINTF(ATH_DEBUG_ERR, ("%s: Wlan disabled\n", __func__));
        return -EIO;
    }

    if(key_index < WMI_MIN_KEY_INDEX || key_index > WMI_MAX_KEY_INDEX) {
        AR_DEBUG_PRINTF(ATH_DEBUG_INFO,
                        ("%s: key index %d out of bounds\n",
                        __func__, key_index));
        return -ENOENT;
    }

    if(!ar->keys[key_index].key_len) {
        AR_DEBUG_PRINTF(ATH_DEBUG_INFO, ("%s: invalid key index %d\n",
                        __func__, key_index));
        return -EINVAL;
    }

    ar->arDefTxKeyIndex = key_index;
    key = &ar->keys[ar->arDefTxKeyIndex];
    key_usage = GROUP_USAGE;
    if (WEP_CRYPT == ar->arPairwiseCrypto) {
        key_usage |= TX_USAGE;
    }

    status = wmi_addKey_cmd(ar->arWmi, ar->arDefTxKeyIndex,
                            ar->arPairwiseCrypto, key_usage,
                            key->key_len, key->seq, key->key, KEY_OP_INIT_VAL,
                            NULL, SYNC_BOTH_WMIFLAG);
    if (status) {
        return -EIO;
    }

    return 0;
}

static int
ar6k_cfg80211_set_default_mgmt_key(struct wiphy *wiphy, struct net_device *ndev,
                                   u8 key_index)
{
    struct ar6_softc *ar = (struct ar6_softc *)ar6k_priv(ndev);

    AR_DEBUG_PRINTF(ATH_DEBUG_INFO, ("%s: index %d\n", __func__, key_index));

    if(ar->arWmiReady == false) {
        AR_DEBUG_PRINTF(ATH_DEBUG_ERR, ("%s: Wmi not ready\n", __func__));
        return -EIO;
    }

    if(ar->arWlanState == WLAN_DISABLED) {
        AR_DEBUG_PRINTF(ATH_DEBUG_ERR, ("%s: Wlan disabled\n", __func__));
        return -EIO;
    }

    AR_DEBUG_PRINTF(ATH_DEBUG_INFO, ("%s: not supported\n", __func__));
    return -ENOTSUPP;
}

void
ar6k_cfg80211_tkip_micerr_event(struct ar6_softc *ar, u8 keyid, bool ismcast)
{
    AR_DEBUG_PRINTF(ATH_DEBUG_INFO,
                    ("%s: keyid %d, ismcast %d\n", __func__, keyid, ismcast));

    cfg80211_michael_mic_failure(ar->arNetDev, ar->arBssid,
                                 (ismcast ? NL80211_KEYTYPE_GROUP : NL80211_KEYTYPE_PAIRWISE),
                                 keyid, NULL, GFP_KERNEL);
}

static int
ar6k_cfg80211_set_wiphy_params(struct wiphy *wiphy, u32 changed)
{
    struct ar6_softc *ar = (struct ar6_softc *)wiphy_priv(wiphy);

    AR_DEBUG_PRINTF(ATH_DEBUG_INFO, ("%s: changed 0x%x\n", __func__, changed));

    if(ar->arWmiReady == false) {
        AR_DEBUG_PRINTF(ATH_DEBUG_ERR, ("%s: Wmi not ready\n", __func__));
        return -EIO;
    }

    if(ar->arWlanState == WLAN_DISABLED) {
        AR_DEBUG_PRINTF(ATH_DEBUG_ERR, ("%s: Wlan disabled\n", __func__));
        return -EIO;
    }

    if (changed & WIPHY_PARAM_RTS_THRESHOLD) {
        if (wmi_set_rts_cmd(ar->arWmi,wiphy->rts_threshold) != 0){
            AR_DEBUG_PRINTF(ATH_DEBUG_ERR, ("%s: wmi_set_rts_cmd failed\n", __func__));
            return -EIO;
        }
    }

    return 0;
}

static int
ar6k_cfg80211_set_bitrate_mask(struct wiphy *wiphy, struct net_device *dev,
                               const u8 *peer,
                               const struct cfg80211_bitrate_mask *mask)
{
    AR_DEBUG_PRINTF(ATH_DEBUG_ERR, ("Setting rates: Not supported\n"));
    return -EIO;
}

/* The type nl80211_tx_power_setting replaces the following data type from 2.6.36 onwards */
static int
ar6k_cfg80211_set_txpower(struct wiphy *wiphy, enum nl80211_tx_power_setting type, int dbm)
{
    struct ar6_softc *ar = (struct ar6_softc *)wiphy_priv(wiphy);
    u8 ar_dbm;

    AR_DEBUG_PRINTF(ATH_DEBUG_INFO, ("%s: type 0x%x, dbm %d\n", __func__, type, dbm));

    if(ar->arWmiReady == false) {
        AR_DEBUG_PRINTF(ATH_DEBUG_ERR, ("%s: Wmi not ready\n", __func__));
        return -EIO;
    }

    if(ar->arWlanState == WLAN_DISABLED) {
        AR_DEBUG_PRINTF(ATH_DEBUG_ERR, ("%s: Wlan disabled\n", __func__));
        return -EIO;
    }

    ar->arTxPwrSet = false;
    switch(type) {
    case NL80211_TX_POWER_AUTOMATIC:
        return 0;
    case NL80211_TX_POWER_LIMITED:
        ar->arTxPwr = ar_dbm = dbm;
        ar->arTxPwrSet = true;
        break;
    default:
        AR_DEBUG_PRINTF(ATH_DEBUG_INFO, ("%s: type 0x%x not supported\n", __func__, type));
        return -EOPNOTSUPP;
    }

    wmi_set_txPwr_cmd(ar->arWmi, ar_dbm);

    return 0;
}

static int
ar6k_cfg80211_get_txpower(struct wiphy *wiphy, int *dbm)
{
    struct ar6_softc *ar = (struct ar6_softc *)wiphy_priv(wiphy);

    AR_DEBUG_PRINTF(ATH_DEBUG_INFO, ("%s: \n", __func__));

    if(ar->arWmiReady == false) {
        AR_DEBUG_PRINTF(ATH_DEBUG_ERR, ("%s: Wmi not ready\n", __func__));
        return -EIO;
    }

    if(ar->arWlanState == WLAN_DISABLED) {
        AR_DEBUG_PRINTF(ATH_DEBUG_ERR, ("%s: Wlan disabled\n", __func__));
        return -EIO;
    }

    if((ar->arConnected == true)) {
        ar->arTxPwr = 0;

        if(wmi_get_txPwr_cmd(ar->arWmi) != 0) {
            AR_DEBUG_PRINTF(ATH_DEBUG_ERR, ("%s: wmi_get_txPwr_cmd failed\n", __func__));
            return -EIO;
        }

        wait_event_interruptible_timeout(arEvent, ar->arTxPwr != 0, 5 * HZ);

        if(signal_pending(current)) {
            AR_DEBUG_PRINTF(ATH_DEBUG_ERR, ("%s: Target did not respond\n", __func__));
            return -EINTR;
        }
    }

    *dbm = ar->arTxPwr;
    return 0;
}

static int
ar6k_cfg80211_set_power_mgmt(struct wiphy *wiphy,
                             struct net_device *dev,
                             bool pmgmt, int timeout)
{
    struct ar6_softc *ar = ar6k_priv(dev);
    WMI_POWER_MODE_CMD pwrMode;

    AR_DEBUG_PRINTF(ATH_DEBUG_INFO, ("%s: pmgmt %d, timeout %d\n", __func__, pmgmt, timeout));

    if(ar->arWmiReady == false) {
        AR_DEBUG_PRINTF(ATH_DEBUG_ERR, ("%s: Wmi not ready\n", __func__));
        return -EIO;
    }

    if(ar->arWlanState == WLAN_DISABLED) {
        AR_DEBUG_PRINTF(ATH_DEBUG_ERR, ("%s: Wlan disabled\n", __func__));
        return -EIO;
    }

    if(pmgmt) {
        AR_DEBUG_PRINTF(ATH_DEBUG_INFO, ("%s: Max Perf\n", __func__));
        pwrMode.powerMode = REC_POWER;
    } else {
        AR_DEBUG_PRINTF(ATH_DEBUG_INFO, ("%s: Rec Power\n", __func__));
        pwrMode.powerMode = MAX_PERF_POWER;
    }

    if(wmi_powermode_cmd(ar->arWmi, pwrMode.powerMode) != 0) {
        AR_DEBUG_PRINTF(ATH_DEBUG_ERR, ("%s: wmi_powermode_cmd failed\n", __func__));
        return -EIO;
    }

    return 0;
}

static struct net_device *
ar6k_cfg80211_add_virtual_intf(struct wiphy *wiphy, char *name,
            				    enum nl80211_iftype type, u32 *flags,
            				    struct vif_params *params)
{

    AR_DEBUG_PRINTF(ATH_DEBUG_INFO, ("%s: not supported\n", __func__));

    /* Multiple virtual interface is not supported.
     * The default interface supports STA and IBSS type
     */
    return ERR_PTR(-EOPNOTSUPP);
}

static int
ar6k_cfg80211_del_virtual_intf(struct wiphy *wiphy, struct net_device *dev)
{

    AR_DEBUG_PRINTF(ATH_DEBUG_INFO, ("%s: not supported\n", __func__));

    /* Multiple virtual interface is not supported.
     * The default interface supports STA and IBSS type
     */
    return -EOPNOTSUPP;
}

static int
ar6k_cfg80211_change_iface(struct wiphy *wiphy, struct net_device *ndev,
                           enum nl80211_iftype type, u32 *flags,
                           struct vif_params *params)
{
    struct ar6_softc *ar = ar6k_priv(ndev);
    struct wireless_dev *wdev = ar->wdev;

    AR_DEBUG_PRINTF(ATH_DEBUG_INFO, ("%s: type %u\n", __func__, type));

    if(ar->arWmiReady == false) {
        AR_DEBUG_PRINTF(ATH_DEBUG_ERR, ("%s: Wmi not ready\n", __func__));
        return -EIO;
    }

    if(ar->arWlanState == WLAN_DISABLED) {
        AR_DEBUG_PRINTF(ATH_DEBUG_ERR, ("%s: Wlan disabled\n", __func__));
        return -EIO;
    }

    switch (type) {
    case NL80211_IFTYPE_STATION:
        ar->arNextMode = INFRA_NETWORK;
        break;
    case NL80211_IFTYPE_ADHOC:
        ar->arNextMode = ADHOC_NETWORK;
        break;
    default:
        AR_DEBUG_PRINTF(ATH_DEBUG_ERR, ("%s: type %u\n", __func__, type));
        return -EOPNOTSUPP;
    }

    wdev->iftype = type;

    return 0;
}

static int
ar6k_cfg80211_join_ibss(struct wiphy *wiphy, struct net_device *dev,
                        struct cfg80211_ibss_params *ibss_param)
{
    struct ar6_softc *ar = ar6k_priv(dev);
    int status;

    AR_DEBUG_PRINTF(ATH_DEBUG_INFO, ("%s: \n", __func__));

    if(ar->arWmiReady == false) {
        AR_DEBUG_PRINTF(ATH_DEBUG_ERR, ("%s: Wmi not ready\n", __func__));
        return -EIO;
    }

    if(ar->arWlanState == WLAN_DISABLED) {
        AR_DEBUG_PRINTF(ATH_DEBUG_ERR, ("%s: Wlan disabled\n", __func__));
        return -EIO;
    }

    if(!ibss_param->ssid_len || IEEE80211_MAX_SSID_LEN < ibss_param->ssid_len) {
        AR_DEBUG_PRINTF(ATH_DEBUG_ERR, ("%s: ssid invalid\n", __func__));
        return -EINVAL;
    }

    ar->arSsidLen = ibss_param->ssid_len;
    memcpy(ar->arSsid, ibss_param->ssid, ar->arSsidLen);

    if(ibss_param->channel) {
        ar->arChannelHint = ibss_param->channel->center_freq;
    }

    if(ibss_param->channel_fixed) {
        /* TODO: channel_fixed: The channel should be fixed, do not search for
         * IBSSs to join on other channels. Target firmware does not support this
         * feature, needs to be updated.*/
    }

    A_MEMZERO(ar->arReqBssid, sizeof(ar->arReqBssid));
    if(ibss_param->bssid) {
        if(memcmp(&ibss_param->bssid, bcast_mac, AR6000_ETH_ADDR_LEN)) {
            memcpy(ar->arReqBssid, ibss_param->bssid, sizeof(ar->arReqBssid));
        }
    }

    ar6k_set_wpa_version(ar, 0);
    ar6k_set_auth_type(ar, NL80211_AUTHTYPE_OPEN_SYSTEM);

    if(ibss_param->privacy) {
        ar6k_set_cipher(ar, WLAN_CIPHER_SUITE_WEP40, true);
        ar6k_set_cipher(ar, WLAN_CIPHER_SUITE_WEP40, false);
    } else {
        ar6k_set_cipher(ar, IW_AUTH_CIPHER_NONE, true);
        ar6k_set_cipher(ar, IW_AUTH_CIPHER_NONE, false);
    }

    ar->arNetworkType = ar->arNextMode;

    AR_DEBUG_PRINTF(ATH_DEBUG_INFO, ("%s: Connect called with authmode %d dot11 auth %d"\
                    " PW crypto %d PW crypto Len %d GRP crypto %d"\
                    " GRP crypto Len %d channel hint %u\n",
                    __func__, ar->arAuthMode, ar->arDot11AuthMode,
                    ar->arPairwiseCrypto, ar->arPairwiseCryptoLen,
                    ar->arGroupCrypto, ar->arGroupCryptoLen, ar->arChannelHint));

    status = wmi_connect_cmd(ar->arWmi, ar->arNetworkType,
                            ar->arDot11AuthMode, ar->arAuthMode,
                            ar->arPairwiseCrypto, ar->arPairwiseCryptoLen,
                            ar->arGroupCrypto,ar->arGroupCryptoLen,
                            ar->arSsidLen, ar->arSsid,
                            ar->arReqBssid, ar->arChannelHint,
                            ar->arConnectCtrlFlags);
    ar->arConnectPending = true;

    return 0;
}

static int
ar6k_cfg80211_leave_ibss(struct wiphy *wiphy, struct net_device *dev)
{
    struct ar6_softc *ar = (struct ar6_softc *)ar6k_priv(dev);

    AR_DEBUG_PRINTF(ATH_DEBUG_INFO, ("%s: \n", __func__));

    if(ar->arWmiReady == false) {
        AR_DEBUG_PRINTF(ATH_DEBUG_ERR, ("%s: Wmi not ready\n", __func__));
        return -EIO;
    }

    if(ar->arWlanState == WLAN_DISABLED) {
        AR_DEBUG_PRINTF(ATH_DEBUG_ERR, ("%s: Wlan disabled\n", __func__));
        return -EIO;
    }

    ar6000_disconnect(ar);
    A_MEMZERO(ar->arSsid, sizeof(ar->arSsid));
    ar->arSsidLen = 0;

    return 0;
}


static const
u32 cipher_suites[] = {
    WLAN_CIPHER_SUITE_WEP40,
    WLAN_CIPHER_SUITE_WEP104,
    WLAN_CIPHER_SUITE_TKIP,
    WLAN_CIPHER_SUITE_CCMP,
};

bool is_rate_legacy(s32 rate)
{
	static const s32 legacy[] = { 1000, 2000, 5500, 11000,
				      6000, 9000, 12000, 18000, 24000,
				      36000, 48000, 54000 };
	u8 i;

	for (i = 0; i < ARRAY_SIZE(legacy); i++) {
		if (rate == legacy[i])
			return true;
	}

	return false;
}

bool is_rate_ht20(s32 rate, u8 *mcs, bool *sgi)
{
	static const s32 ht20[] = { 6500, 13000, 19500, 26000, 39000,
				    52000, 58500, 65000, 72200 };
	u8 i;

	for (i = 0; i < ARRAY_SIZE(ht20); i++) {
		if (rate == ht20[i]) {
			if (i == ARRAY_SIZE(ht20) - 1)
				/* last rate uses sgi */
				*sgi = true;
			else
				*sgi = false;

			*mcs = i;
			return true;
		}
	}
	return false;
}

bool is_rate_ht40(s32 rate, u8 *mcs, bool *sgi)
{
	static const s32 ht40[] = { 13500, 27000, 40500, 54000,
				    81000, 108000, 121500, 135000,
				    150000 };
	u8 i;

	for (i = 0; i < ARRAY_SIZE(ht40); i++) {
		if (rate == ht40[i]) {
			if (i == ARRAY_SIZE(ht40) - 1)
				/* last rate uses sgi */
				*sgi = true;
			else
				*sgi = false;

			*mcs = i;
			return true;
		}
	}

	return false;
}

static int ar6k_get_station(struct wiphy *wiphy, struct net_device *dev,
			    u8 *mac, struct station_info *sinfo)
{
	struct ar6_softc *ar = ar6k_priv(dev);
	long left;
	bool sgi;
	s32 rate;
	int ret;
	u8 mcs;

	if (memcmp(mac, ar->arBssid, ETH_ALEN) != 0)
		return -ENOENT;

	if (down_interruptible(&ar->arSem))
		return -EBUSY;

	ar->statsUpdatePending = true;

	ret = wmi_get_stats_cmd(ar->arWmi);

	if (ret != 0) {
		up(&ar->arSem);
		return -EIO;
	}

	left = wait_event_interruptible_timeout(arEvent,
						ar->statsUpdatePending == false,
						wmitimeout * HZ);

	up(&ar->arSem);

	if (left == 0)
		return -ETIMEDOUT;
	else if (left < 0)
		return left;

	if (ar->arTargetStats.rx_bytes) {
		sinfo->rx_bytes = ar->arTargetStats.rx_bytes;
		sinfo->filled |= STATION_INFO_RX_BYTES;
		sinfo->rx_packets = ar->arTargetStats.rx_packets;
		sinfo->filled |= STATION_INFO_RX_PACKETS;
	}

	if (ar->arTargetStats.tx_bytes) {
		sinfo->tx_bytes = ar->arTargetStats.tx_bytes;
		sinfo->filled |= STATION_INFO_TX_BYTES;
		sinfo->tx_packets = ar->arTargetStats.tx_packets;
		sinfo->filled |= STATION_INFO_TX_PACKETS;
	}

	sinfo->signal = ar->arTargetStats.cs_rssi;
	sinfo->filled |= STATION_INFO_SIGNAL;

	rate = ar->arTargetStats.tx_unicast_rate;

	if (is_rate_legacy(rate)) {
		sinfo->txrate.legacy = rate / 100;
	} else if (is_rate_ht20(rate, &mcs, &sgi)) {
		if (sgi) {
			sinfo->txrate.flags |= RATE_INFO_FLAGS_SHORT_GI;
			sinfo->txrate.mcs = mcs - 1;
		} else {
			sinfo->txrate.mcs = mcs;
		}

		sinfo->txrate.flags |= RATE_INFO_FLAGS_MCS;
	} else if (is_rate_ht40(rate, &mcs, &sgi)) {
		if (sgi) {
			sinfo->txrate.flags |= RATE_INFO_FLAGS_SHORT_GI;
			sinfo->txrate.mcs = mcs - 1;
		} else {
			sinfo->txrate.mcs = mcs;
		}

		sinfo->txrate.flags |= RATE_INFO_FLAGS_40_MHZ_WIDTH;
		sinfo->txrate.flags |= RATE_INFO_FLAGS_MCS;
	} else {
		WARN(1, "invalid rate: %d", rate);
		return 0;
	}

	sinfo->filled |= STATION_INFO_TX_BITRATE;

	return 0;
}

static struct
cfg80211_ops ar6k_cfg80211_ops = {
    .change_virtual_intf = ar6k_cfg80211_change_iface,
    .add_virtual_intf = ar6k_cfg80211_add_virtual_intf,
    .del_virtual_intf = ar6k_cfg80211_del_virtual_intf,
    .scan = ar6k_cfg80211_scan,
    .connect = ar6k_cfg80211_connect,
    .disconnect = ar6k_cfg80211_disconnect,
    .add_key = ar6k_cfg80211_add_key,
    .get_key = ar6k_cfg80211_get_key,
    .del_key = ar6k_cfg80211_del_key,
    .set_default_key = ar6k_cfg80211_set_default_key,
    .set_default_mgmt_key = ar6k_cfg80211_set_default_mgmt_key,
    .set_wiphy_params = ar6k_cfg80211_set_wiphy_params,
    .set_bitrate_mask = ar6k_cfg80211_set_bitrate_mask,
    .set_tx_power = ar6k_cfg80211_set_txpower,
    .get_tx_power = ar6k_cfg80211_get_txpower,
    .set_power_mgmt = ar6k_cfg80211_set_power_mgmt,
    .join_ibss = ar6k_cfg80211_join_ibss,
    .leave_ibss = ar6k_cfg80211_leave_ibss,
    .get_station = ar6k_get_station,
};

struct wireless_dev *
ar6k_cfg80211_init(struct device *dev)
{
    int ret = 0;
    struct wireless_dev *wdev;

    AR_DEBUG_PRINTF(ATH_DEBUG_INFO, ("%s: \n", __func__));

    wdev = kzalloc(sizeof(struct wireless_dev), GFP_KERNEL);
    if(!wdev) {
        AR_DEBUG_PRINTF(ATH_DEBUG_ERR,
                        ("%s: Couldn't allocate wireless device\n", __func__));
        return ERR_PTR(-ENOMEM);
    }

    /* create a new wiphy for use with cfg80211 */
    wdev->wiphy = wiphy_new(&ar6k_cfg80211_ops, sizeof(struct ar6_softc));
    if(!wdev->wiphy) {
        AR_DEBUG_PRINTF(ATH_DEBUG_ERR,
                        ("%s: Couldn't allocate wiphy device\n", __func__));
        kfree(wdev);
        return ERR_PTR(-ENOMEM);
    }

    /* set device pointer for wiphy */
    set_wiphy_dev(wdev->wiphy, dev);

    wdev->wiphy->interface_modes = BIT(NL80211_IFTYPE_STATION) |
                                   BIT(NL80211_IFTYPE_ADHOC);
    /* max num of ssids that can be probed during scanning */
    wdev->wiphy->max_scan_ssids = MAX_PROBED_SSID_INDEX;
    wdev->wiphy->bands[IEEE80211_BAND_2GHZ] = &ar6k_band_2ghz;
    wdev->wiphy->bands[IEEE80211_BAND_5GHZ] = &ar6k_band_5ghz;
    wdev->wiphy->signal_type = CFG80211_SIGNAL_TYPE_MBM;

    wdev->wiphy->cipher_suites = cipher_suites;
    wdev->wiphy->n_cipher_suites = ARRAY_SIZE(cipher_suites);

    ret = wiphy_register(wdev->wiphy);
    if(ret < 0) {
        AR_DEBUG_PRINTF(ATH_DEBUG_ERR,
                        ("%s: Couldn't register wiphy device\n", __func__));
        wiphy_free(wdev->wiphy);
        return ERR_PTR(ret);
    }

    return wdev;
}

void
ar6k_cfg80211_deinit(struct ar6_softc *ar)
{
    struct wireless_dev *wdev = ar->wdev;

    AR_DEBUG_PRINTF(ATH_DEBUG_INFO, ("%s: \n", __func__));

    if(ar->scan_request) {
        cfg80211_scan_done(ar->scan_request, true);
        ar->scan_request = NULL;
    }

    if(!wdev)
        return;

    wiphy_unregister(wdev->wiphy);
    wiphy_free(wdev->wiphy);
    kfree(wdev);
}







