//------------------------------------------------------------------------------
// Copyright (c) 2004-2010 Atheros Corporation.  All rights reserved.
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
//------------------------------------------------------------------------------
//==============================================================================
// This module implements the hardware independent layer of the
// Wireless Module Interface (WMI) protocol.
//
// Author(s): ="Atheros"
//==============================================================================

#include <a_config.h>
#include <athdefs.h>
#include <a_types.h>
#include <a_osapi.h>
#include "htc.h"
#include "htc_api.h"
#include "wmi.h"
#include <wlan_api.h>
#include <wmi_api.h>
#include <ieee80211.h>
#include <ieee80211_node.h>
#include "dset_api.h"
#include "gpio_api.h"
#include "wmi_host.h"
#include "a_drv.h"
#include "a_drv_api.h"
#define ATH_MODULE_NAME wmi
#include "a_debug.h"
#include "dbglog_api.h"
#include "roaming.h"

#define ATH_DEBUG_WMI ATH_DEBUG_MAKE_MODULE_MASK(0)

#ifdef ATH_DEBUG_MODULE

static struct ath_debug_mask_description wmi_debug_desc[] = {
    { ATH_DEBUG_WMI , "General WMI Tracing"},
};

ATH_DEBUG_INSTANTIATE_MODULE_VAR(wmi,
                                 "wmi",
                                 "Wireless Module Interface",
                                 ATH_DEBUG_MASK_DEFAULTS,
                                 ATH_DEBUG_DESCRIPTION_COUNT(wmi_debug_desc),
                                 wmi_debug_desc);

#endif

#ifndef REXOS
#define DBGARG      _A_FUNCNAME_
#define DBGFMT      "%s() : "
#define DBG_WMI     ATH_DEBUG_WMI
#define DBG_ERROR   ATH_DEBUG_ERR
#define DBG_WMI2    ATH_DEBUG_WMI
#define A_DPRINTF   AR_DEBUG_PRINTF
#endif

static int wmi_ready_event_rx(struct wmi_t *wmip, u8 *datap, int len);

static int wmi_connect_event_rx(struct wmi_t *wmip, u8 *datap,
                                     int len);
static int wmi_disconnect_event_rx(struct wmi_t *wmip, u8 *datap,
                                        int len);

static int wmi_tkip_micerr_event_rx(struct wmi_t *wmip, u8 *datap,
                                        int len);
static int wmi_bssInfo_event_rx(struct wmi_t *wmip, u8 *datap,
                                     int len);
static int wmi_opt_frame_event_rx(struct wmi_t *wmip, u8 *datap,
                                       int len);
static int wmi_pstream_timeout_event_rx(struct wmi_t *wmip, u8 *datap,
                                     int len);
static int wmi_sync_point(struct wmi_t *wmip);

static int wmi_bitrate_reply_rx(struct wmi_t *wmip, u8 *datap,
                                     int len);
static int wmi_ratemask_reply_rx(struct wmi_t *wmip, u8 *datap,
                                     int len);
static int wmi_channelList_reply_rx(struct wmi_t *wmip, u8 *datap,
                                         int len);
static int wmi_regDomain_event_rx(struct wmi_t *wmip, u8 *datap,
                                       int len);
static int wmi_txPwr_reply_rx(struct wmi_t *wmip, u8 *datap, int len);
static int wmi_neighborReport_event_rx(struct wmi_t *wmip, u8 *datap,
                                             int len);

static int wmi_dset_open_req_rx(struct wmi_t *wmip, u8 *datap,
                                     int len);
#ifdef CONFIG_HOST_DSET_SUPPORT
static int wmi_dset_close_rx(struct wmi_t *wmip, u8 *datap, int len);
static int wmi_dset_data_req_rx(struct wmi_t *wmip, u8 *datap,
                                     int len);
#endif /* CONFIG_HOST_DSET_SUPPORT */


static int wmi_scanComplete_rx(struct wmi_t *wmip, u8 *datap,
                                     int len);
static int wmi_errorEvent_rx(struct wmi_t *wmip, u8 *datap, int len);
static int wmi_statsEvent_rx(struct wmi_t *wmip, u8 *datap, int len);
static int wmi_rssiThresholdEvent_rx(struct wmi_t *wmip, u8 *datap, int len);
static int wmi_hbChallengeResp_rx(struct wmi_t *wmip, u8 *datap, int len);
static int wmi_reportErrorEvent_rx(struct wmi_t *wmip, u8 *datap, int len);
static int wmi_cac_event_rx(struct wmi_t *wmip, u8 *datap, int len);
static int wmi_channel_change_event_rx(struct wmi_t *wmip, u8 *datap, int len);
static int wmi_roam_tbl_event_rx(struct wmi_t *wmip, u8 *datap,
                                      int len);
static int wmi_roam_data_event_rx(struct wmi_t *wmip, u8 *datap,
                                      int len);
static int wmi_get_wow_list_event_rx(struct wmi_t *wmip, u8 *datap,
                                      int len);
static int
wmi_get_pmkid_list_event_rx(struct wmi_t *wmip, u8 *datap, u32 len);

static int
wmi_set_params_event_rx(struct wmi_t *wmip, u8 *datap, u32 len);

static int
wmi_acm_reject_event_rx(struct wmi_t *wmip, u8 *datap, u32 len);

#ifdef CONFIG_HOST_GPIO_SUPPORT
static int wmi_gpio_intr_rx(struct wmi_t *wmip, u8 *datap, int len);
static int wmi_gpio_data_rx(struct wmi_t *wmip, u8 *datap, int len);
static int wmi_gpio_ack_rx(struct wmi_t *wmip, u8 *datap, int len);
#endif /* CONFIG_HOST_GPIO_SUPPORT */

#ifdef CONFIG_HOST_TCMD_SUPPORT
static int
wmi_tcmd_test_report_rx(struct wmi_t *wmip, u8 *datap, int len);
#endif

static int
wmi_txRetryErrEvent_rx(struct wmi_t *wmip, u8 *datap, int len);

static int
wmi_snrThresholdEvent_rx(struct wmi_t *wmip, u8 *datap, int len);

static int
wmi_lqThresholdEvent_rx(struct wmi_t *wmip, u8 *datap, int len);

static bool
wmi_is_bitrate_index_valid(struct wmi_t *wmip, s32 rateIndex);

static int
wmi_aplistEvent_rx(struct wmi_t *wmip, u8 *datap, int len);

static int
wmi_dbglog_event_rx(struct wmi_t *wmip, u8 *datap, int len);

static int wmi_keepalive_reply_rx(struct wmi_t *wmip, u8 *datap, int len);

int wmi_cmd_send_xtnd(struct wmi_t *wmip, void *osbuf, WMIX_COMMAND_ID cmdId,
                  WMI_SYNC_FLAG syncflag);

u8 ar6000_get_upper_threshold(s16 rssi, SQ_THRESHOLD_PARAMS *sq_thresh, u32 size);
u8 ar6000_get_lower_threshold(s16 rssi, SQ_THRESHOLD_PARAMS *sq_thresh, u32 size);

void wmi_cache_configure_rssithreshold(struct wmi_t *wmip, WMI_RSSI_THRESHOLD_PARAMS_CMD *rssiCmd);
void wmi_cache_configure_snrthreshold(struct wmi_t *wmip, WMI_SNR_THRESHOLD_PARAMS_CMD *snrCmd);
static int wmi_send_rssi_threshold_params(struct wmi_t *wmip,
                              WMI_RSSI_THRESHOLD_PARAMS_CMD *rssiCmd);
static int wmi_send_snr_threshold_params(struct wmi_t *wmip,
                             WMI_SNR_THRESHOLD_PARAMS_CMD *snrCmd);
#if defined(CONFIG_TARGET_PROFILE_SUPPORT)
static int
wmi_prof_count_rx(struct wmi_t *wmip, u8 *datap, int len);
#endif /* CONFIG_TARGET_PROFILE_SUPPORT */

static int wmi_pspoll_event_rx(struct wmi_t *wmip, u8 *datap,
                                     int len);
static int wmi_dtimexpiry_event_rx(struct wmi_t *wmip, u8 *datap,
                                     int len);

static int wmi_peer_node_event_rx (struct wmi_t *wmip, u8 *datap,
                                        int len);
#ifdef ATH_AR6K_11N_SUPPORT
static int wmi_addba_req_event_rx(struct wmi_t *, u8 *, int);
static int wmi_addba_resp_event_rx(struct wmi_t *, u8 *, int);
static int wmi_delba_req_event_rx(struct wmi_t *, u8 *, int);
static int wmi_btcoex_config_event_rx(struct wmi_t *wmip, u8 *datap, int len);
static int wmi_btcoex_stats_event_rx(struct wmi_t *wmip, u8 *datap, int len);
#endif
static int wmi_hci_event_rx(struct wmi_t *, u8 *, int);

#ifdef WAPI_ENABLE
static int wmi_wapi_rekey_event_rx(struct wmi_t *wmip, u8 *datap,
                                     int len);
#endif

#if defined(UNDER_CE)
#if defined(NDIS51_MINIPORT)
unsigned int processDot11Hdr = 0;
#else
unsigned int processDot11Hdr = 1;
#endif
#else
extern unsigned int processDot11Hdr;
#endif

int wps_enable;
static const s32 wmi_rateTable[][2] = {
  //{W/O SGI, with SGI}
    {1000, 1000},
    {2000, 2000},
    {5500, 5500},
    {11000, 11000},
    {6000, 6000},
    {9000, 9000},
    {12000, 12000},
    {18000, 18000},
    {24000, 24000},
    {36000, 36000},
    {48000, 48000},
    {54000, 54000},
    {6500, 7200},
    {13000, 14400},
    {19500, 21700},
    {26000, 28900},
    {39000, 43300},
    {52000, 57800},
    {58500, 65000},
    {65000, 72200},
    {13500, 15000},
    {27000, 30000},
    {40500, 45000},
    {54000, 60000},
    {81000, 90000},
    {108000, 120000},
    {121500, 135000},
    {135000, 150000},
    {0, 0}};

#define MODE_A_SUPPORT_RATE_START       ((s32) 4)
#define MODE_A_SUPPORT_RATE_STOP        ((s32) 11)

#define MODE_GONLY_SUPPORT_RATE_START   MODE_A_SUPPORT_RATE_START
#define MODE_GONLY_SUPPORT_RATE_STOP    MODE_A_SUPPORT_RATE_STOP

#define MODE_B_SUPPORT_RATE_START       ((s32) 0)
#define MODE_B_SUPPORT_RATE_STOP        ((s32) 3)

#define MODE_G_SUPPORT_RATE_START       ((s32) 0)
#define MODE_G_SUPPORT_RATE_STOP        ((s32) 11)

#define MODE_GHT20_SUPPORT_RATE_START   ((s32) 0)
#define MODE_GHT20_SUPPORT_RATE_STOP    ((s32) 19)

#define MAX_NUMBER_OF_SUPPORT_RATES     (MODE_GHT20_SUPPORT_RATE_STOP + 1)

/* 802.1d to AC mapping. Refer pg 57 of WMM-test-plan-v1.2 */
const u8 up_to_ac[]= {
                WMM_AC_BE,
                WMM_AC_BK,
                WMM_AC_BK,
                WMM_AC_BE,
                WMM_AC_VI,
                WMM_AC_VI,
                WMM_AC_VO,
                WMM_AC_VO,
            };

#include "athstartpack.h"

/* This stuff is used when we want a simple layer-3 visibility */
typedef PREPACK struct _iphdr {
    u8 ip_ver_hdrlen;          /* version and hdr length */
    u8 ip_tos;                 /* type of service */
    u16 ip_len;                 /* total length */
    u16 ip_id;                  /* identification */
    s16 ip_off;                 /* fragment offset field */
#define IP_DF 0x4000                    /* dont fragment flag */
#define IP_MF 0x2000                    /* more fragments flag */
#define IP_OFFMASK 0x1fff               /* mask for fragmenting bits */
    u8 ip_ttl;                 /* time to live */
    u8 ip_p;                   /* protocol */
    u16 ip_sum;                 /* checksum */
    u8 ip_src[4];              /* source and dest address */
    u8 ip_dst[4];
} POSTPACK iphdr;

#include "athendpack.h"

static s16 rssi_event_value = 0;
static s16 snr_event_value = 0;

bool is_probe_ssid = false;

void *
wmi_init(void *devt)
{
    struct wmi_t *wmip;

    A_REGISTER_MODULE_DEBUG_INFO(wmi);

    wmip = A_MALLOC (sizeof(struct wmi_t));
    if (wmip == NULL) {
        return (NULL);
    }
    A_MEMZERO(wmip, sizeof(struct wmi_t ));
#ifdef THREAD_X
    INIT_WMI_LOCK(wmip);
#else
	A_MUTEX_INIT(&wmip->wmi_lock);
#endif
    wmip->wmi_devt = devt;
    wlan_node_table_init(wmip, &wmip->wmi_scan_table);
    wmi_qos_state_init(wmip);

    wmip->wmi_powerMode = REC_POWER;
    wmip->wmi_phyMode = WMI_11G_MODE;

    wmip->wmi_pair_crypto_type  = NONE_CRYPT;
    wmip->wmi_grp_crypto_type   = NONE_CRYPT;

    wmip->wmi_ht_allowed[A_BAND_24GHZ] = 1;
    wmip->wmi_ht_allowed[A_BAND_5GHZ] = 1;

    return (wmip);
}

void
wmi_qos_state_init(struct wmi_t *wmip)
{
    u8 i;

    if (wmip == NULL) {
        return;
    }
    LOCK_WMI(wmip);

    /* Initialize QoS States */
    wmip->wmi_numQoSStream = 0;

    wmip->wmi_fatPipeExists = 0;

    for (i=0; i < WMM_NUM_AC; i++) {
        wmip->wmi_streamExistsForAC[i]=0;
    }

    UNLOCK_WMI(wmip);

    A_WMI_SET_NUMDATAENDPTS(wmip->wmi_devt, 1);
}

void
wmi_set_control_ep(struct wmi_t * wmip, HTC_ENDPOINT_ID eid)
{
    A_ASSERT( eid != ENDPOINT_UNUSED);
    wmip->wmi_endpoint_id = eid;
}

HTC_ENDPOINT_ID
wmi_get_control_ep(struct wmi_t * wmip)
{
    return(wmip->wmi_endpoint_id);
}

void
wmi_shutdown(struct wmi_t *wmip)
{
    if (wmip != NULL) {
        wlan_node_table_cleanup(&wmip->wmi_scan_table);
        if (A_IS_MUTEX_VALID(&wmip->wmi_lock)) {
#ifdef THREAD_X
            DELETE_WMI_LOCK(&wmip);
#else
            A_MUTEX_DELETE(&wmip->wmi_lock);
#endif
        }
        A_FREE(wmip);
    }
}

/*
 *  performs DIX to 802.3 encapsulation for transmit packets.
 *  uses passed in buffer.  Returns buffer or NULL if failed.
 *  Assumes the entire DIX header is contigous and that there is
 *  enough room in the buffer for a 802.3 mac header and LLC+SNAP headers.
 */
int
wmi_dix_2_dot3(struct wmi_t *wmip, void *osbuf)
{
    u8 *datap;
    u16 typeorlen;
    ATH_MAC_HDR      macHdr;
    ATH_LLC_SNAP_HDR *llcHdr;

    A_ASSERT(osbuf != NULL);

    if (A_NETBUF_HEADROOM(osbuf) <
        (sizeof(ATH_LLC_SNAP_HDR) + sizeof(WMI_DATA_HDR)))
    {
        return A_NO_MEMORY;
    }

    datap = A_NETBUF_DATA(osbuf);

    typeorlen = *(u16 *)(datap + ATH_MAC_LEN + ATH_MAC_LEN);

    if (!IS_ETHERTYPE(A_BE2CPU16(typeorlen))) {
        /*
         * packet is already in 802.3 format - return success
         */
        A_DPRINTF(DBG_WMI, (DBGFMT "packet already 802.3\n", DBGARG));
        return (0);
    }

    /*
     * Save mac fields and length to be inserted later
     */
    memcpy(macHdr.dstMac, datap, ATH_MAC_LEN);
    memcpy(macHdr.srcMac, datap + ATH_MAC_LEN, ATH_MAC_LEN);
    macHdr.typeOrLen = A_CPU2BE16(A_NETBUF_LEN(osbuf) - sizeof(ATH_MAC_HDR) +
                                  sizeof(ATH_LLC_SNAP_HDR));

    /*
     * Make room for LLC+SNAP headers
     */
    if (A_NETBUF_PUSH(osbuf, sizeof(ATH_LLC_SNAP_HDR)) != 0) {
        return A_NO_MEMORY;
    }
    datap = A_NETBUF_DATA(osbuf);

    memcpy(datap, &macHdr, sizeof (ATH_MAC_HDR));

    llcHdr = (ATH_LLC_SNAP_HDR *)(datap + sizeof(ATH_MAC_HDR));
    llcHdr->dsap      = 0xAA;
    llcHdr->ssap      = 0xAA;
    llcHdr->cntl      = 0x03;
    llcHdr->orgCode[0] = 0x0;
    llcHdr->orgCode[1] = 0x0;
    llcHdr->orgCode[2] = 0x0;
    llcHdr->etherType = typeorlen;

    return (0);
}

int wmi_meta_add(struct wmi_t *wmip, void *osbuf, u8 *pVersion,void *pTxMetaS)
{
    switch(*pVersion){
	case 0:
		return (0);
    	case WMI_META_VERSION_1:
	        {
        	WMI_TX_META_V1     *pV1= NULL;
        	A_ASSERT(osbuf != NULL);
		if (A_NETBUF_PUSH(osbuf, WMI_MAX_TX_META_SZ) != 0) {
            		return A_NO_MEMORY;
        	}

        	pV1 = (WMI_TX_META_V1 *)A_NETBUF_DATA(osbuf);
        	/* the pktID is used in conjunction with txComplete messages
        	* allowing the target to notify which tx requests have been
        	* completed and how. */
        	pV1->pktID = 0;
        	/* the ratePolicyID allows the host to specify which rate policy
        	* to use for transmitting this packet. 0 means use default behavior. */
        	pV1->ratePolicyID = 0;
        	A_ASSERT(pVersion != NULL);
        	/* the version must be used to populate the meta field of the WMI_DATA_HDR */
        	*pVersion = WMI_META_VERSION_1;
		return (0);
    		}
#ifdef CONFIG_CHECKSUM_OFFLOAD
	case WMI_META_VERSION_2:
		{
     		WMI_TX_META_V2 *pV2 ;
        	A_ASSERT(osbuf != NULL);
		if (A_NETBUF_PUSH(osbuf, WMI_MAX_TX_META_SZ) != 0) {
            		return A_NO_MEMORY;
        	}
         	pV2 = (WMI_TX_META_V2 *)A_NETBUF_DATA(osbuf);
         	memcpy(pV2,(WMI_TX_META_V2 *)pTxMetaS,sizeof(WMI_TX_META_V2));
		return (0);
    		}
#endif
	default:
		return (0);
    }
}

/* Adds a WMI data header */
int
wmi_data_hdr_add(struct wmi_t *wmip, void *osbuf, u8 msgType, bool bMoreData,
                    WMI_DATA_HDR_DATA_TYPE data_type,u8 metaVersion, void *pTxMetaS)
{
    WMI_DATA_HDR     *dtHdr;
//    u8 metaVersion = 0;
    int status;

    A_ASSERT(osbuf != NULL);

    /* adds the meta data field after the wmi data hdr. If metaVersion
     * is returns 0 then no meta field was added. */
    if ((status = wmi_meta_add(wmip, osbuf, &metaVersion,pTxMetaS)) != 0) {
        return status;
    }

    if (A_NETBUF_PUSH(osbuf, sizeof(WMI_DATA_HDR)) != 0) {
        return A_NO_MEMORY;
    }

    dtHdr = (WMI_DATA_HDR *)A_NETBUF_DATA(osbuf);
    A_MEMZERO(dtHdr, sizeof(WMI_DATA_HDR));

    WMI_DATA_HDR_SET_MSG_TYPE(dtHdr, msgType);
    WMI_DATA_HDR_SET_DATA_TYPE(dtHdr, data_type);

    if (bMoreData) {
        WMI_DATA_HDR_SET_MORE_BIT(dtHdr);
    }

    WMI_DATA_HDR_SET_META(dtHdr, metaVersion);
    //dtHdr->rssi = 0;

    return (0);
}


u8 wmi_implicit_create_pstream(struct wmi_t *wmip, void *osbuf, u32 layer2Priority, bool wmmEnabled)
{
    u8 *datap;
    u8 trafficClass = WMM_AC_BE;
    u16 ipType = IP_ETHERTYPE;
    WMI_DATA_HDR    *dtHdr;
    u8 streamExists = 0;
    u8 userPriority;
    u32 hdrsize, metasize;
    ATH_LLC_SNAP_HDR    *llcHdr;

    WMI_CREATE_PSTREAM_CMD  cmd;

    A_ASSERT(osbuf != NULL);

    //
    // Initialize header size
    //
    hdrsize = 0;

    datap = A_NETBUF_DATA(osbuf);
    dtHdr = (WMI_DATA_HDR *)datap;
    metasize = (WMI_DATA_HDR_GET_META(dtHdr))? WMI_MAX_TX_META_SZ : 0;

    if (!wmmEnabled)
    {
            /* If WMM is disabled all traffic goes as BE traffic */
        userPriority = 0;
    }
    else
    {
        if (processDot11Hdr)
        {
             hdrsize = A_ROUND_UP(sizeof(struct ieee80211_qosframe),sizeof(u32));
             llcHdr = (ATH_LLC_SNAP_HDR *)(datap + sizeof(WMI_DATA_HDR) + metasize +
                          hdrsize);


        }
        else
        {
            llcHdr = (ATH_LLC_SNAP_HDR *)(datap + sizeof(WMI_DATA_HDR) + metasize +
                          sizeof(ATH_MAC_HDR));
        }

        if (llcHdr->etherType == A_CPU2BE16(ipType))
        {
            /* Extract the endpoint info from the TOS field in the IP header */

            userPriority = wmi_determine_userPriority (((u8 *)llcHdr) + sizeof(ATH_LLC_SNAP_HDR),layer2Priority);
        }
        else
        {
            userPriority = layer2Priority & 0x7;
        }
    }


    /* workaround for WMM S5 */
    if ((WMM_AC_VI == wmip->wmi_traffic_class) && ((5 == userPriority) || (4 == userPriority)))
    {
        userPriority = 1;
    }

    trafficClass = convert_userPriority_to_trafficClass(userPriority);

    WMI_DATA_HDR_SET_UP(dtHdr, userPriority);
    /* lower 3-bits are 802.1d priority */
    //dtHdr->info |= (userPriority & WMI_DATA_HDR_UP_MASK) << WMI_DATA_HDR_UP_SHIFT;

    LOCK_WMI(wmip);
    streamExists = wmip->wmi_fatPipeExists;
    UNLOCK_WMI(wmip);

    if (!(streamExists & (1 << trafficClass)))
    {

        A_MEMZERO(&cmd, sizeof(cmd));
        cmd.trafficClass = trafficClass;
        cmd.userPriority = userPriority;
        cmd.inactivityInt = WMI_IMPLICIT_PSTREAM_INACTIVITY_INT;
            /* Implicit streams are created with TSID 0xFF */

        cmd.tsid = WMI_IMPLICIT_PSTREAM;
        wmi_create_pstream_cmd(wmip, &cmd);
    }

    return trafficClass;
}

int
wmi_dot11_hdr_add (struct wmi_t *wmip, void *osbuf, NETWORK_TYPE mode)
{
    u8 *datap;
    u16 typeorlen;
    ATH_MAC_HDR      macHdr;
    ATH_LLC_SNAP_HDR *llcHdr;
    struct           ieee80211_frame *wh;
    u32 hdrsize;

    A_ASSERT(osbuf != NULL);

    if (A_NETBUF_HEADROOM(osbuf) <
        (sizeof(struct ieee80211_qosframe) +  sizeof(ATH_LLC_SNAP_HDR) + sizeof(WMI_DATA_HDR)))
    {
        return A_NO_MEMORY;
    }

    datap = A_NETBUF_DATA(osbuf);

    typeorlen = *(u16 *)(datap + ATH_MAC_LEN + ATH_MAC_LEN);

    if (!IS_ETHERTYPE(A_BE2CPU16(typeorlen))) {
/*
         * packet is already in 802.3 format - return success
         */
        A_DPRINTF(DBG_WMI, (DBGFMT "packet already 802.3\n", DBGARG));
        goto AddDot11Hdr;
    }

    /*
     * Save mac fields and length to be inserted later
     */
    memcpy(macHdr.dstMac, datap, ATH_MAC_LEN);
    memcpy(macHdr.srcMac, datap + ATH_MAC_LEN, ATH_MAC_LEN);
    macHdr.typeOrLen = A_CPU2BE16(A_NETBUF_LEN(osbuf) - sizeof(ATH_MAC_HDR) +
                                  sizeof(ATH_LLC_SNAP_HDR));

    // Remove the Ethernet hdr
    A_NETBUF_PULL(osbuf, sizeof(ATH_MAC_HDR));
    /*
     * Make room for LLC+SNAP headers
     */
    if (A_NETBUF_PUSH(osbuf, sizeof(ATH_LLC_SNAP_HDR)) != 0) {
        return A_NO_MEMORY;
    }
    datap = A_NETBUF_DATA(osbuf);

    llcHdr = (ATH_LLC_SNAP_HDR *)(datap);
    llcHdr->dsap       = 0xAA;
    llcHdr->ssap       = 0xAA;
    llcHdr->cntl       = 0x03;
    llcHdr->orgCode[0] = 0x0;
    llcHdr->orgCode[1] = 0x0;
    llcHdr->orgCode[2] = 0x0;
    llcHdr->etherType  = typeorlen;

AddDot11Hdr:
    /* Make room for 802.11 hdr */
    if (wmip->wmi_is_wmm_enabled)
    {
        hdrsize = A_ROUND_UP(sizeof(struct ieee80211_qosframe),sizeof(u32));
        if (A_NETBUF_PUSH(osbuf, hdrsize) != 0)
        {
            return A_NO_MEMORY;
        }
        wh = (struct ieee80211_frame *) A_NETBUF_DATA(osbuf);
        wh->i_fc[0] = IEEE80211_FC0_SUBTYPE_QOS;
    }
    else
    {
        hdrsize = A_ROUND_UP(sizeof(struct ieee80211_frame),sizeof(u32));
        if (A_NETBUF_PUSH(osbuf, hdrsize) != 0)
        {
            return A_NO_MEMORY;
        }
        wh = (struct ieee80211_frame *) A_NETBUF_DATA(osbuf);
        wh->i_fc[0] = IEEE80211_FC0_SUBTYPE_DATA;
    }
    /* Setup the SA & DA */
    IEEE80211_ADDR_COPY(wh->i_addr2, macHdr.srcMac);

    if (mode == INFRA_NETWORK) {
        IEEE80211_ADDR_COPY(wh->i_addr3, macHdr.dstMac);
    }
    else if (mode == ADHOC_NETWORK) {
        IEEE80211_ADDR_COPY(wh->i_addr1, macHdr.dstMac);
    }

    return (0);
}

int
wmi_dot11_hdr_remove(struct wmi_t *wmip, void *osbuf)
{
    u8 *datap;
    struct           ieee80211_frame *pwh,wh;
    u8 type,subtype;
    ATH_LLC_SNAP_HDR *llcHdr;
    ATH_MAC_HDR      macHdr;
    u32 hdrsize;

    A_ASSERT(osbuf != NULL);
    datap = A_NETBUF_DATA(osbuf);

    pwh = (struct ieee80211_frame *)datap;
    type = pwh->i_fc[0] & IEEE80211_FC0_TYPE_MASK;
    subtype = pwh->i_fc[0] & IEEE80211_FC0_SUBTYPE_MASK;

    memcpy((u8 *)&wh, datap, sizeof(struct ieee80211_frame));

    /* strip off the 802.11 hdr*/
    if (subtype == IEEE80211_FC0_SUBTYPE_QOS) {
        hdrsize = A_ROUND_UP(sizeof(struct ieee80211_qosframe),sizeof(u32));
        A_NETBUF_PULL(osbuf, hdrsize);
    } else if (subtype == IEEE80211_FC0_SUBTYPE_DATA) {
        A_NETBUF_PULL(osbuf, sizeof(struct ieee80211_frame));
    }

    datap = A_NETBUF_DATA(osbuf);
    llcHdr = (ATH_LLC_SNAP_HDR *)(datap);

    macHdr.typeOrLen = llcHdr->etherType;
    A_MEMZERO(macHdr.dstMac, sizeof(macHdr.dstMac));
    A_MEMZERO(macHdr.srcMac, sizeof(macHdr.srcMac));

    switch (wh.i_fc[1] & IEEE80211_FC1_DIR_MASK) {
    case IEEE80211_FC1_DIR_NODS:
        IEEE80211_ADDR_COPY(macHdr.dstMac, wh.i_addr1);
        IEEE80211_ADDR_COPY(macHdr.srcMac, wh.i_addr2);
        break;
    case IEEE80211_FC1_DIR_TODS:
        IEEE80211_ADDR_COPY(macHdr.dstMac, wh.i_addr3);
        IEEE80211_ADDR_COPY(macHdr.srcMac, wh.i_addr2);
        break;
    case IEEE80211_FC1_DIR_FROMDS:
        IEEE80211_ADDR_COPY(macHdr.dstMac, wh.i_addr1);
        IEEE80211_ADDR_COPY(macHdr.srcMac, wh.i_addr3);
        break;
    case IEEE80211_FC1_DIR_DSTODS:
        break;
    }

    // Remove the LLC Hdr.
    A_NETBUF_PULL(osbuf, sizeof(ATH_LLC_SNAP_HDR));

    // Insert the ATH MAC hdr.

    A_NETBUF_PUSH(osbuf, sizeof(ATH_MAC_HDR));
    datap = A_NETBUF_DATA(osbuf);

    memcpy (datap, &macHdr, sizeof(ATH_MAC_HDR));

    return 0;
}

/*
 *  performs 802.3 to DIX encapsulation for received packets.
 *  Assumes the entire 802.3 header is contigous.
 */
int
wmi_dot3_2_dix(void *osbuf)
{
    u8 *datap;
    ATH_MAC_HDR      macHdr;
    ATH_LLC_SNAP_HDR *llcHdr;

    A_ASSERT(osbuf != NULL);
    datap = A_NETBUF_DATA(osbuf);

    memcpy(&macHdr, datap, sizeof(ATH_MAC_HDR));
    llcHdr = (ATH_LLC_SNAP_HDR *)(datap + sizeof(ATH_MAC_HDR));
    macHdr.typeOrLen = llcHdr->etherType;

    if (A_NETBUF_PULL(osbuf, sizeof(ATH_LLC_SNAP_HDR)) != 0) {
        return A_NO_MEMORY;
    }

    datap = A_NETBUF_DATA(osbuf);

    memcpy(datap, &macHdr, sizeof (ATH_MAC_HDR));

    return (0);
}

/*
 * Removes a WMI data header
 */
int
wmi_data_hdr_remove(struct wmi_t *wmip, void *osbuf)
{
    A_ASSERT(osbuf != NULL);

    return (A_NETBUF_PULL(osbuf, sizeof(WMI_DATA_HDR)));
}

void
wmi_iterate_nodes(struct wmi_t *wmip, wlan_node_iter_func *f, void *arg)
{
    wlan_iterate_nodes(&wmip->wmi_scan_table, f, arg);
}

/*
 * WMI Extended Event received from Target.
 */
int
wmi_control_rx_xtnd(struct wmi_t *wmip, void *osbuf)
{
    WMIX_CMD_HDR *cmd;
    u16 id;
    u8 *datap;
    u32 len;
    int status = 0;

    if (A_NETBUF_LEN(osbuf) < sizeof(WMIX_CMD_HDR)) {
        A_DPRINTF(DBG_WMI, (DBGFMT "bad packet 1\n", DBGARG));
        wmip->wmi_stats.cmd_len_err++;
        return A_ERROR;
    }

    cmd = (WMIX_CMD_HDR *)A_NETBUF_DATA(osbuf);
    id = cmd->commandId;

    if (A_NETBUF_PULL(osbuf, sizeof(WMIX_CMD_HDR)) != 0) {
        A_DPRINTF(DBG_WMI, (DBGFMT "bad packet 2\n", DBGARG));
        wmip->wmi_stats.cmd_len_err++;
        return A_ERROR;
    }

    datap = A_NETBUF_DATA(osbuf);
    len = A_NETBUF_LEN(osbuf);

    switch (id) {
    case (WMIX_DSETOPENREQ_EVENTID):
        status = wmi_dset_open_req_rx(wmip, datap, len);
        break;
#ifdef CONFIG_HOST_DSET_SUPPORT
    case (WMIX_DSETCLOSE_EVENTID):
        status = wmi_dset_close_rx(wmip, datap, len);
        break;
    case (WMIX_DSETDATAREQ_EVENTID):
        status = wmi_dset_data_req_rx(wmip, datap, len);
        break;
#endif /* CONFIG_HOST_DSET_SUPPORT */
#ifdef CONFIG_HOST_GPIO_SUPPORT
    case (WMIX_GPIO_INTR_EVENTID):
        wmi_gpio_intr_rx(wmip, datap, len);
        break;
    case (WMIX_GPIO_DATA_EVENTID):
        wmi_gpio_data_rx(wmip, datap, len);
        break;
    case (WMIX_GPIO_ACK_EVENTID):
        wmi_gpio_ack_rx(wmip, datap, len);
        break;
#endif /* CONFIG_HOST_GPIO_SUPPORT */
    case (WMIX_HB_CHALLENGE_RESP_EVENTID):
        wmi_hbChallengeResp_rx(wmip, datap, len);
        break;
    case (WMIX_DBGLOG_EVENTID):
        wmi_dbglog_event_rx(wmip, datap, len);
        break;
#if defined(CONFIG_TARGET_PROFILE_SUPPORT)
    case (WMIX_PROF_COUNT_EVENTID):
        wmi_prof_count_rx(wmip, datap, len);
        break;
#endif /* CONFIG_TARGET_PROFILE_SUPPORT */
    default:
        A_DPRINTF(DBG_WMI|DBG_ERROR,
            (DBGFMT "Unknown id 0x%x\n", DBGARG, id));
        wmip->wmi_stats.cmd_id_err++;
        status = A_ERROR;
        break;
    }

    return status;
}

/*
 * Control Path
 */
u32 cmdRecvNum;

int
wmi_control_rx(struct wmi_t *wmip, void *osbuf)
{
    WMI_CMD_HDR *cmd;
    u16 id;
    u8 *datap;
    u32 len, i, loggingReq;
    int status = 0;

    A_ASSERT(osbuf != NULL);
    if (A_NETBUF_LEN(osbuf) < sizeof(WMI_CMD_HDR)) {
        A_NETBUF_FREE(osbuf);
        A_DPRINTF(DBG_WMI, (DBGFMT "bad packet 1\n", DBGARG));
        wmip->wmi_stats.cmd_len_err++;
        return A_ERROR;
    }

    cmd = (WMI_CMD_HDR *)A_NETBUF_DATA(osbuf);
    id = cmd->commandId;

    if (A_NETBUF_PULL(osbuf, sizeof(WMI_CMD_HDR)) != 0) {
        A_NETBUF_FREE(osbuf);
        A_DPRINTF(DBG_WMI, (DBGFMT "bad packet 2\n", DBGARG));
        wmip->wmi_stats.cmd_len_err++;
        return A_ERROR;
    }

    datap = A_NETBUF_DATA(osbuf);
    len = A_NETBUF_LEN(osbuf);

    loggingReq = 0;

    ar6000_get_driver_cfg(wmip->wmi_devt,
                    AR6000_DRIVER_CFG_LOG_RAW_WMI_MSGS,
                    &loggingReq);

    if(loggingReq) {
        AR_DEBUG_PRINTF(ATH_DEBUG_WMI, ("WMI %d \n",id));
        AR_DEBUG_PRINTF(ATH_DEBUG_WMI, ("WMI recv, MsgNo %d : ", cmdRecvNum));
        for(i = 0; i < len; i++)
            AR_DEBUG_PRINTF(ATH_DEBUG_WMI, ("%x ", datap[i]));
        AR_DEBUG_PRINTF(ATH_DEBUG_WMI, ("\n"));
    }

    LOCK_WMI(wmip);
    cmdRecvNum++;
    UNLOCK_WMI(wmip);

    switch (id) {
    case (WMI_GET_BITRATE_CMDID):
        A_DPRINTF(DBG_WMI, (DBGFMT "WMI_GET_BITRATE_CMDID\n", DBGARG));
        status = wmi_bitrate_reply_rx(wmip, datap, len);
        break;
    case (WMI_GET_CHANNEL_LIST_CMDID):
        A_DPRINTF(DBG_WMI, (DBGFMT "WMI_GET_CHANNEL_LIST_CMDID\n", DBGARG));
        status = wmi_channelList_reply_rx(wmip, datap, len);
        break;
    case (WMI_GET_TX_PWR_CMDID):
        A_DPRINTF(DBG_WMI, (DBGFMT "WMI_GET_TX_PWR_CMDID\n", DBGARG));
        status = wmi_txPwr_reply_rx(wmip, datap, len);
        break;
    case (WMI_READY_EVENTID):
        A_DPRINTF(DBG_WMI, (DBGFMT "WMI_READY_EVENTID\n", DBGARG));
        status = wmi_ready_event_rx(wmip, datap, len);
        A_WMI_SEND_EVENT_TO_APP(wmip->wmi_devt, id, datap, len);
        A_WMI_DBGLOG_INIT_DONE(wmip->wmi_devt);
        break;
    case (WMI_CONNECT_EVENTID):
        A_DPRINTF(DBG_WMI, (DBGFMT "WMI_CONNECT_EVENTID\n", DBGARG));
        status = wmi_connect_event_rx(wmip, datap, len);
        A_WMI_SEND_GENERIC_EVENT_TO_APP(wmip->wmi_devt, id, datap, len);
        break;
    case (WMI_DISCONNECT_EVENTID):
        A_DPRINTF(DBG_WMI, (DBGFMT "WMI_DISCONNECT_EVENTID\n", DBGARG));
        status = wmi_disconnect_event_rx(wmip, datap, len);
        A_WMI_SEND_EVENT_TO_APP(wmip->wmi_devt, id, datap, len);
        break;
    case (WMI_PEER_NODE_EVENTID):
        A_DPRINTF (DBG_WMI, (DBGFMT "WMI_PEER_NODE_EVENTID\n", DBGARG));
        status = wmi_peer_node_event_rx(wmip, datap, len);
        A_WMI_SEND_EVENT_TO_APP(wmip->wmi_devt, id, datap, len);
        break;
    case (WMI_TKIP_MICERR_EVENTID):
        A_DPRINTF(DBG_WMI, (DBGFMT "WMI_TKIP_MICERR_EVENTID\n", DBGARG));
        status = wmi_tkip_micerr_event_rx(wmip, datap, len);
        break;
    case (WMI_BSSINFO_EVENTID):
        A_DPRINTF(DBG_WMI, (DBGFMT "WMI_BSSINFO_EVENTID\n", DBGARG));
        {
            /*
             * convert WMI_BSS_INFO_HDR2 to WMI_BSS_INFO_HDR
             * Take a local copy of the WMI_BSS_INFO_HDR2 from the wmi buffer
             * and reconstruct the WMI_BSS_INFO_HDR in its place
            */
            WMI_BSS_INFO_HDR2 bih2;
            WMI_BSS_INFO_HDR *bih;
            memcpy(&bih2, datap, sizeof(WMI_BSS_INFO_HDR2));

            A_NETBUF_PUSH(osbuf, 4);
            datap = A_NETBUF_DATA(osbuf);
            len = A_NETBUF_LEN(osbuf);
            bih = (WMI_BSS_INFO_HDR *)datap;

            bih->channel = bih2.channel;
            bih->frameType = bih2.frameType;
            bih->snr = bih2.snr;
            bih->rssi = bih2.snr - 95;
            bih->ieMask = bih2.ieMask;
            memcpy(bih->bssid, bih2.bssid, ATH_MAC_LEN);

            status = wmi_bssInfo_event_rx(wmip, datap, len);
            A_WMI_SEND_GENERIC_EVENT_TO_APP(wmip->wmi_devt, id, datap, len);
        }
        break;
    case (WMI_REGDOMAIN_EVENTID):
        A_DPRINTF(DBG_WMI, (DBGFMT "WMI_REGDOMAIN_EVENTID\n", DBGARG));
        status = wmi_regDomain_event_rx(wmip, datap, len);
        break;
    case (WMI_PSTREAM_TIMEOUT_EVENTID):
        A_DPRINTF(DBG_WMI, (DBGFMT "WMI_PSTREAM_TIMEOUT_EVENTID\n", DBGARG));
        status = wmi_pstream_timeout_event_rx(wmip, datap, len);
            /* pstreams are fatpipe abstractions that get implicitly created.
             * User apps only deal with thinstreams. creation of a thinstream
             * by the user or data traffic flow in an AC triggers implicit
             * pstream creation. Do we need to send this event to App..?
             * no harm in sending it.
             */
        A_WMI_SEND_EVENT_TO_APP(wmip->wmi_devt, id, datap, len);
        break;
    case (WMI_NEIGHBOR_REPORT_EVENTID):
        A_DPRINTF(DBG_WMI, (DBGFMT "WMI_NEIGHBOR_REPORT_EVENTID\n", DBGARG));
        status = wmi_neighborReport_event_rx(wmip, datap, len);
        break;
    case (WMI_SCAN_COMPLETE_EVENTID):
        A_DPRINTF(DBG_WMI, (DBGFMT "WMI_SCAN_COMPLETE_EVENTID\n", DBGARG));
        status = wmi_scanComplete_rx(wmip, datap, len);
        A_WMI_SEND_EVENT_TO_APP(wmip->wmi_devt, id, datap, len);
        break;
    case (WMI_CMDERROR_EVENTID):
        A_DPRINTF(DBG_WMI, (DBGFMT "WMI_CMDERROR_EVENTID\n", DBGARG));
        status = wmi_errorEvent_rx(wmip, datap, len);
        break;
    case (WMI_REPORT_STATISTICS_EVENTID):
        A_DPRINTF(DBG_WMI, (DBGFMT "WMI_REPORT_STATISTICS_EVENTID\n", DBGARG));
        status = wmi_statsEvent_rx(wmip, datap, len);
        break;
    case (WMI_RSSI_THRESHOLD_EVENTID):
        A_DPRINTF(DBG_WMI, (DBGFMT "WMI_RSSI_THRESHOLD_EVENTID\n", DBGARG));
        status = wmi_rssiThresholdEvent_rx(wmip, datap, len);
        break;
    case (WMI_ERROR_REPORT_EVENTID):
        A_DPRINTF(DBG_WMI, (DBGFMT "WMI_ERROR_REPORT_EVENTID\n", DBGARG));
        status = wmi_reportErrorEvent_rx(wmip, datap, len);
        A_WMI_SEND_EVENT_TO_APP(wmip->wmi_devt, id, datap, len);
        break;
    case (WMI_OPT_RX_FRAME_EVENTID):
        A_DPRINTF(DBG_WMI, (DBGFMT "WMI_OPT_RX_FRAME_EVENTID\n", DBGARG));
        status = wmi_opt_frame_event_rx(wmip, datap, len);
        break;
    case (WMI_REPORT_ROAM_TBL_EVENTID):
        A_DPRINTF(DBG_WMI, (DBGFMT "WMI_REPORT_ROAM_TBL_EVENTID\n", DBGARG));
        status = wmi_roam_tbl_event_rx(wmip, datap, len);
        break;
    case (WMI_EXTENSION_EVENTID):
        A_DPRINTF(DBG_WMI, (DBGFMT "WMI_EXTENSION_EVENTID\n", DBGARG));
        status = wmi_control_rx_xtnd(wmip, osbuf);
        break;
    case (WMI_CAC_EVENTID):
        A_DPRINTF(DBG_WMI, (DBGFMT "WMI_CAC_EVENTID\n", DBGARG));
        status = wmi_cac_event_rx(wmip, datap, len);
        break;
    case (WMI_CHANNEL_CHANGE_EVENTID):
        A_DPRINTF(DBG_WMI, (DBGFMT "WMI_CHANNEL_CHANGE_EVENTID\n", DBGARG));
        status = wmi_channel_change_event_rx(wmip, datap, len);
        break;
    case (WMI_REPORT_ROAM_DATA_EVENTID):
        A_DPRINTF(DBG_WMI, (DBGFMT "WMI_REPORT_ROAM_DATA_EVENTID\n", DBGARG));
        status = wmi_roam_data_event_rx(wmip, datap, len);
        break;
#ifdef CONFIG_HOST_TCMD_SUPPORT
    case (WMI_TEST_EVENTID):
        A_DPRINTF(DBG_WMI, (DBGFMT "WMI_TEST_EVENTID\n", DBGARG));
        status = wmi_tcmd_test_report_rx(wmip, datap, len);
        break;
#endif
    case (WMI_GET_FIXRATES_CMDID):
        A_DPRINTF(DBG_WMI, (DBGFMT "WMI_GET_FIXRATES_CMDID\n", DBGARG));
        status = wmi_ratemask_reply_rx(wmip, datap, len);
        break;
    case (WMI_TX_RETRY_ERR_EVENTID):
        A_DPRINTF(DBG_WMI, (DBGFMT "WMI_TX_RETRY_ERR_EVENTID\n", DBGARG));
        status = wmi_txRetryErrEvent_rx(wmip, datap, len);
        A_WMI_SEND_EVENT_TO_APP(wmip->wmi_devt, id, datap, len);
        break;
    case (WMI_SNR_THRESHOLD_EVENTID):
        A_DPRINTF(DBG_WMI, (DBGFMT "WMI_SNR_THRESHOLD_EVENTID\n", DBGARG));
        status = wmi_snrThresholdEvent_rx(wmip, datap, len);
        break;
    case (WMI_LQ_THRESHOLD_EVENTID):
        A_DPRINTF(DBG_WMI, (DBGFMT "WMI_LQ_THRESHOLD_EVENTID\n", DBGARG));
        status = wmi_lqThresholdEvent_rx(wmip, datap, len);
        A_WMI_SEND_EVENT_TO_APP(wmip->wmi_devt, id, datap, len);
        break;
    case (WMI_APLIST_EVENTID):
        AR_DEBUG_PRINTF(ATH_DEBUG_WMI, ("Received APLIST Event\n"));
        status = wmi_aplistEvent_rx(wmip, datap, len);
        break;
    case (WMI_GET_KEEPALIVE_CMDID):
        A_DPRINTF(DBG_WMI, (DBGFMT "WMI_GET_KEEPALIVE_CMDID\n", DBGARG));
        status = wmi_keepalive_reply_rx(wmip, datap, len);
        break;
    case (WMI_GET_WOW_LIST_EVENTID):
        status = wmi_get_wow_list_event_rx(wmip, datap, len);
        break;
    case (WMI_GET_PMKID_LIST_EVENTID):
        A_DPRINTF(DBG_WMI, (DBGFMT "WMI_GET_PMKID_LIST Event\n", DBGARG));
        status = wmi_get_pmkid_list_event_rx(wmip, datap, len);
        break;
    case (WMI_PSPOLL_EVENTID):
        A_DPRINTF(DBG_WMI, (DBGFMT "WMI_PSPOLL_EVENT\n", DBGARG));
        status = wmi_pspoll_event_rx(wmip, datap, len);
        break;
    case (WMI_DTIMEXPIRY_EVENTID):
        A_DPRINTF(DBG_WMI, (DBGFMT "WMI_DTIMEXPIRY_EVENT\n", DBGARG));
        status = wmi_dtimexpiry_event_rx(wmip, datap, len);
        break;
    case (WMI_SET_PARAMS_REPLY_EVENTID):
        A_DPRINTF(DBG_WMI, (DBGFMT "WMI_SET_PARAMS_REPLY Event\n", DBGARG));
        status = wmi_set_params_event_rx(wmip, datap, len);
        break;
    case (WMI_ACM_REJECT_EVENTID):
        A_DPRINTF(DBG_WMI, (DBGFMT "WMI_SET_PARAMS_REPLY Event\n", DBGARG));
        status = wmi_acm_reject_event_rx(wmip, datap, len);
        break;		
#ifdef ATH_AR6K_11N_SUPPORT
    case (WMI_ADDBA_REQ_EVENTID):
        status = wmi_addba_req_event_rx(wmip, datap, len);
        break;
    case (WMI_ADDBA_RESP_EVENTID):
        status = wmi_addba_resp_event_rx(wmip, datap, len);
        break;
    case (WMI_DELBA_REQ_EVENTID):
        status = wmi_delba_req_event_rx(wmip, datap, len);
        break;
	case (WMI_REPORT_BTCOEX_CONFIG_EVENTID):
	    A_DPRINTF(DBG_WMI, (DBGFMT "WMI_BTCOEX_CONFIG_EVENTID", DBGARG));
    	status = wmi_btcoex_config_event_rx(wmip, datap, len);
	    break;
	case (WMI_REPORT_BTCOEX_STATS_EVENTID):
	    A_DPRINTF(DBG_WMI, (DBGFMT "WMI_BTCOEX_STATS_EVENTID", DBGARG));
    	status = wmi_btcoex_stats_event_rx(wmip, datap, len);
	    break;
#endif
    case (WMI_TX_COMPLETE_EVENTID):
        {
            int index;
            TX_COMPLETE_MSG_V1 *pV1;
            WMI_TX_COMPLETE_EVENT *pEv = (WMI_TX_COMPLETE_EVENT *)datap;
            A_PRINTF("comp: %d %d %d\n", pEv->numMessages, pEv->msgLen, pEv->msgType);

            for(index = 0 ; index < pEv->numMessages ; index++) {
                pV1 = (TX_COMPLETE_MSG_V1 *)(datap + sizeof(WMI_TX_COMPLETE_EVENT) + index*sizeof(TX_COMPLETE_MSG_V1));
                A_PRINTF("msg: %d %d %d %d\n", pV1->status, pV1->pktID, pV1->rateIdx, pV1->ackFailures);
            }
        }
        break;
    case (WMI_HCI_EVENT_EVENTID):
        status = wmi_hci_event_rx(wmip, datap, len);
        break;
#ifdef WAPI_ENABLE
    case (WMI_WAPI_REKEY_EVENTID):
        A_DPRINTF(DBG_WMI, (DBGFMT "WMI_WAPI_REKEY_EVENTID", DBGARG));
        status = wmi_wapi_rekey_event_rx(wmip, datap, len);
        break;
#endif
    default:
        A_DPRINTF(DBG_WMI|DBG_ERROR,
            (DBGFMT "Unknown id 0x%x\n", DBGARG, id));
        wmip->wmi_stats.cmd_id_err++;
        status = A_ERROR;
        break;
    }

    A_NETBUF_FREE(osbuf);

    return status;
}

/* Send a "simple" wmi command -- one with no arguments */
static int
wmi_simple_cmd(struct wmi_t *wmip, WMI_COMMAND_ID cmdid)
{
    void *osbuf;

    osbuf = A_NETBUF_ALLOC(0);
    if (osbuf == NULL) {
        return A_NO_MEMORY;
    }

    return (wmi_cmd_send(wmip, osbuf, cmdid, NO_SYNC_WMIFLAG));
}

/* Send a "simple" extended wmi command -- one with no arguments.
   Enabling this command only if GPIO or profiling support is enabled.
   This is to suppress warnings on some platforms */
#if defined(CONFIG_HOST_GPIO_SUPPORT) || defined(CONFIG_TARGET_PROFILE_SUPPORT)
static int
wmi_simple_cmd_xtnd(struct wmi_t *wmip, WMIX_COMMAND_ID cmdid)
{
    void *osbuf;

    osbuf = A_NETBUF_ALLOC(0);
    if (osbuf == NULL) {
        return A_NO_MEMORY;
    }

    return (wmi_cmd_send_xtnd(wmip, osbuf, cmdid, NO_SYNC_WMIFLAG));
}
#endif

static int
wmi_ready_event_rx(struct wmi_t *wmip, u8 *datap, int len)
{
    WMI_READY_EVENT *ev = (WMI_READY_EVENT *)datap;

    if (len < sizeof(WMI_READY_EVENT)) {
        return A_EINVAL;
    }
    A_DPRINTF(DBG_WMI, (DBGFMT "Enter\n", DBGARG));
    wmip->wmi_ready = true;
    A_WMI_READY_EVENT(wmip->wmi_devt, ev->macaddr, ev->phyCapability,
                      ev->sw_version, ev->abi_version);

    return 0;
}

#define LE_READ_4(p)                            \
    ((u32)                            \
     ((((u8 *)(p))[0]      ) | (((u8 *)(p))[1] <<  8) | \
      (((u8 *)(p))[2] << 16) | (((u8 *)(p))[3] << 24)))

static int __inline
iswmmoui(const u8 *frm)
{
    return frm[1] > 3 && LE_READ_4(frm+2) == ((WMM_OUI_TYPE<<24)|WMM_OUI);
}

static int __inline
iswmmparam(const u8 *frm)
{
    return frm[1] > 5 && frm[6] == WMM_PARAM_OUI_SUBTYPE;
}


static int
wmi_connect_event_rx(struct wmi_t *wmip, u8 *datap, int len)
{
    WMI_CONNECT_EVENT *ev;
    u8 *pie,*peie;

    if (len < sizeof(WMI_CONNECT_EVENT))
    {
        return A_EINVAL;
    }
    ev = (WMI_CONNECT_EVENT *)datap;

    A_DPRINTF(DBG_WMI,
        (DBGFMT "freq %d bssid %2.2x:%2.2x:%2.2x:%2.2x:%2.2x:%2.2x\n",
        DBGARG, ev->channel,
        ev->bssid[0], ev->bssid[1], ev->bssid[2],
        ev->bssid[3], ev->bssid[4], ev->bssid[5]));

    memcpy(wmip->wmi_bssid, ev->bssid, ATH_MAC_LEN);

    /* initialize pointer to start of assoc rsp IEs */
    pie = ev->assocInfo + ev->beaconIeLen + ev->assocReqLen +
                            sizeof(u16)  +  /* capinfo*/
                            sizeof(u16)  +  /* status Code */
                            sizeof(u16)  ;  /* associd */

    /* initialize pointer to end of assoc rsp IEs */
    peie = ev->assocInfo + ev->beaconIeLen + ev->assocReqLen + ev->assocRespLen;

    while (pie < peie)
    {
        switch (*pie)
        {
            case IEEE80211_ELEMID_VENDOR:
                if (iswmmoui(pie))
                {
                    if(iswmmparam (pie))
                    {
                        wmip->wmi_is_wmm_enabled = true;
                    }
                }
            break;
        }

        if (wmip->wmi_is_wmm_enabled)
        {
            break;
        }
        pie += pie[1] + 2;
    }

    A_WMI_CONNECT_EVENT(wmip->wmi_devt, ev->channel, ev->bssid,
                         ev->listenInterval, ev->beaconInterval,
                         (NETWORK_TYPE) ev->networkType, ev->beaconIeLen,
                         ev->assocReqLen, ev->assocRespLen,
                         ev->assocInfo);

    return 0;
}

static int
wmi_regDomain_event_rx(struct wmi_t *wmip, u8 *datap, int len)
{
    WMI_REG_DOMAIN_EVENT *ev;

    if (len < sizeof(*ev)) {
        return A_EINVAL;
    }
    ev = (WMI_REG_DOMAIN_EVENT *)datap;

    A_WMI_REGDOMAIN_EVENT(wmip->wmi_devt, ev->regDomain);

    return 0;
}

static int
wmi_neighborReport_event_rx(struct wmi_t *wmip, u8 *datap, int len)
{
    WMI_NEIGHBOR_REPORT_EVENT *ev;
    int numAps;

    if (len < sizeof(*ev)) {
        return A_EINVAL;
    }
    ev = (WMI_NEIGHBOR_REPORT_EVENT *)datap;
    numAps = ev->numberOfAps;

    if (len < (int)(sizeof(*ev) + ((numAps - 1) * sizeof(WMI_NEIGHBOR_INFO)))) {
        return A_EINVAL;
    }

    A_WMI_NEIGHBORREPORT_EVENT(wmip->wmi_devt, numAps, ev->neighbor);

    return 0;
}

static int
wmi_disconnect_event_rx(struct wmi_t *wmip, u8 *datap, int len)
{
    WMI_DISCONNECT_EVENT *ev;
    wmip->wmi_traffic_class = 100;

    if (len < sizeof(WMI_DISCONNECT_EVENT)) {
        return A_EINVAL;
    }
    A_DPRINTF(DBG_WMI, (DBGFMT "Enter\n", DBGARG));

    ev = (WMI_DISCONNECT_EVENT *)datap;

    A_MEMZERO(wmip->wmi_bssid, sizeof(wmip->wmi_bssid));

    wmip->wmi_is_wmm_enabled = false;
    wmip->wmi_pair_crypto_type = NONE_CRYPT;
    wmip->wmi_grp_crypto_type = NONE_CRYPT;

    A_WMI_DISCONNECT_EVENT(wmip->wmi_devt, ev->disconnectReason, ev->bssid,
                            ev->assocRespLen, ev->assocInfo, ev->protocolReasonStatus);

    return 0;
}

static int
wmi_peer_node_event_rx(struct wmi_t *wmip, u8 *datap, int len)
{
    WMI_PEER_NODE_EVENT *ev;

    if (len < sizeof(WMI_PEER_NODE_EVENT)) {
        return A_EINVAL;
    }
    ev = (WMI_PEER_NODE_EVENT *)datap;
    if (ev->eventCode == PEER_NODE_JOIN_EVENT) {
        A_DPRINTF (DBG_WMI, (DBGFMT "Joined node with Macaddr: ", DBGARG));
    } else if(ev->eventCode == PEER_NODE_LEAVE_EVENT) {
        A_DPRINTF (DBG_WMI, (DBGFMT "left node with Macaddr: ", DBGARG));
    }

    A_WMI_PEER_EVENT (wmip->wmi_devt, ev->eventCode, ev->peerMacAddr);

    return 0;
}

static int
wmi_tkip_micerr_event_rx(struct wmi_t *wmip, u8 *datap, int len)
{
    WMI_TKIP_MICERR_EVENT *ev;

    if (len < sizeof(*ev)) {
        return A_EINVAL;
    }
    A_DPRINTF(DBG_WMI, (DBGFMT "Enter\n", DBGARG));

    ev = (WMI_TKIP_MICERR_EVENT *)datap;
    A_WMI_TKIP_MICERR_EVENT(wmip->wmi_devt, ev->keyid, ev->ismcast);

    return 0;
}

static int
wmi_bssInfo_event_rx(struct wmi_t *wmip, u8 *datap, int len)
{
    bss_t *bss = NULL;
    WMI_BSS_INFO_HDR *bih;
    u8 *buf;
    u32 nodeCachingAllowed = 1;
    u8 cached_ssid_len = 0;
    u8 cached_ssid_buf[IEEE80211_NWID_LEN] = {0};
    u8 beacon_ssid_len = 0;

    if (len <= sizeof(WMI_BSS_INFO_HDR)) {
        return A_EINVAL;
    }

    bih = (WMI_BSS_INFO_HDR *)datap;
    bss = wlan_find_node(&wmip->wmi_scan_table, bih->bssid);

    if (bih->rssi > 0) {
        if (NULL == bss)
            return 0;  //no node found in the table, just drop the node with incorrect RSSI
        else
            bih->rssi = bss->ni_rssi; //Adjust RSSI in datap in case it is used in A_WMI_BSSINFO_EVENT_RX
    }

    A_WMI_BSSINFO_EVENT_RX(wmip->wmi_devt, datap, len);
    /* What is driver config for wlan node caching? */
    if(ar6000_get_driver_cfg(wmip->wmi_devt,
                    AR6000_DRIVER_CFG_GET_WLANNODECACHING,
                    &nodeCachingAllowed) != 0) {
        wmi_node_return(wmip, bss);
        return A_EINVAL;
    }

    if(!nodeCachingAllowed) {
        wmi_node_return(wmip, bss);
        return 0;
    }

    buf = datap + sizeof(WMI_BSS_INFO_HDR);
    len -= sizeof(WMI_BSS_INFO_HDR);

	A_DPRINTF(DBG_WMI2, (DBGFMT "bssInfo event - ch %u, rssi %02x, "
		"bssid \"%pM\"\n", DBGARG, bih->channel,
		(unsigned char) bih->rssi, bih->bssid));

    if(wps_enable && (bih->frameType == PROBERESP_FTYPE) ) {
        wmi_node_return(wmip, bss);
        return 0;
    }

    if (bss != NULL) {
        /*
         * Free up the node.  Not the most efficient process given
         * we are about to allocate a new node but it is simple and should be
         * adequate.
         */

        /* In case of hidden AP, beacon will not have ssid,
         * but a directed probe response will have it,
         * so cache the probe-resp-ssid if already present. */
        if ((true == is_probe_ssid) && (BEACON_FTYPE == bih->frameType))
        {
            u8 *ie_ssid;

            ie_ssid = bss->ni_cie.ie_ssid;
            if(ie_ssid && (ie_ssid[1] <= IEEE80211_NWID_LEN) && (ie_ssid[2] != 0))
            {
                cached_ssid_len = ie_ssid[1];
                memcpy(cached_ssid_buf, ie_ssid + 2, cached_ssid_len);
            }
        }

        /*
         * Use the current average rssi of associated AP base on assumpiton
         * 1. Most os with GUI will update RSSI by wmi_get_stats_cmd() periodically
         * 2. wmi_get_stats_cmd(..) will be called when calling wmi_startscan_cmd(...)
         * The average value of RSSI give end-user better feeling for instance value of scan result
         * It also sync up RSSI info in GUI between scan result and RSSI signal icon
         */
        if (IEEE80211_ADDR_EQ(wmip->wmi_bssid, bih->bssid)) {
            bih->rssi = bss->ni_rssi;
            bih->snr  = bss->ni_snr;
        }

        wlan_node_reclaim(&wmip->wmi_scan_table, bss);
    }

    /*  beacon/probe response frame format
     *  [8] time stamp
     *  [2] beacon interval
     *  [2] capability information
     *  [tlv] ssid */
    beacon_ssid_len = buf[SSID_IE_LEN_INDEX];

    /* If ssid is cached for this hidden AP, then change buffer len accordingly. */
    if ((true == is_probe_ssid) && (BEACON_FTYPE == bih->frameType) &&
        (0 != cached_ssid_len) &&
        (0 == beacon_ssid_len || (cached_ssid_len > beacon_ssid_len && 0 == buf[SSID_IE_LEN_INDEX + 1])))
    {
        len += (cached_ssid_len - beacon_ssid_len);
    }

    bss = wlan_node_alloc(&wmip->wmi_scan_table, len);
    if (bss == NULL) {
        return A_NO_MEMORY;
    }

    bss->ni_snr        = bih->snr;
    bss->ni_rssi       = bih->rssi;
    A_ASSERT(bss->ni_buf != NULL);

    /* In case of hidden AP, beacon will not have ssid,
     * but a directed probe response will have it,
     * so place the cached-ssid(probe-resp) in the bssinfo. */
    if ((true == is_probe_ssid) && (BEACON_FTYPE == bih->frameType) &&
         (0 != cached_ssid_len) &&
         (0 == beacon_ssid_len || (beacon_ssid_len && 0 == buf[SSID_IE_LEN_INDEX + 1])))
    {
        u8 *ni_buf = bss->ni_buf;
        int buf_len = len;

        /* copy the first 14 bytes such as
         * time-stamp(8), beacon-interval(2), cap-info(2), ssid-id(1), ssid-len(1). */
        memcpy(ni_buf, buf, SSID_IE_LEN_INDEX + 1);

        ni_buf[SSID_IE_LEN_INDEX] = cached_ssid_len;
        ni_buf += (SSID_IE_LEN_INDEX + 1);

        buf += (SSID_IE_LEN_INDEX + 1);
        buf_len -= (SSID_IE_LEN_INDEX + 1);

        /* copy the cached ssid */
        memcpy(ni_buf, cached_ssid_buf, cached_ssid_len);
        ni_buf += cached_ssid_len;

        buf += beacon_ssid_len;
        buf_len -= beacon_ssid_len;

        if (cached_ssid_len > beacon_ssid_len)
            buf_len -= (cached_ssid_len - beacon_ssid_len);

        /* now copy the rest of bytes */
        memcpy(ni_buf, buf, buf_len);
    }
    else
        memcpy(bss->ni_buf, buf, len);

    bss->ni_framelen = len;
    if (wlan_parse_beacon(bss->ni_buf, len, &bss->ni_cie) != 0) {
        wlan_node_free(bss);
        return A_EINVAL;
    }

    /*
     * Update the frequency in ie_chan, overwriting of channel number
     * which is done in wlan_parse_beacon
     */
    bss->ni_cie.ie_chan = bih->channel;
    wlan_setup_node(&wmip->wmi_scan_table, bss, bih->bssid);

    return 0;
}

static int
wmi_opt_frame_event_rx(struct wmi_t *wmip, u8 *datap, int len)
{
    bss_t *bss;
    WMI_OPT_RX_INFO_HDR *bih;
    u8 *buf;

    if (len <= sizeof(WMI_OPT_RX_INFO_HDR)) {
        return A_EINVAL;
    }

    bih = (WMI_OPT_RX_INFO_HDR *)datap;
    buf = datap + sizeof(WMI_OPT_RX_INFO_HDR);
    len -= sizeof(WMI_OPT_RX_INFO_HDR);

    A_DPRINTF(DBG_WMI2, (DBGFMT "opt frame event %2.2x:%2.2x\n", DBGARG,
        bih->bssid[4], bih->bssid[5]));

    bss = wlan_find_node(&wmip->wmi_scan_table, bih->bssid);
    if (bss != NULL) {
        /*
         * Free up the node.  Not the most efficient process given
         * we are about to allocate a new node but it is simple and should be
         * adequate.
         */
        wlan_node_reclaim(&wmip->wmi_scan_table, bss);
    }

    bss = wlan_node_alloc(&wmip->wmi_scan_table, len);
    if (bss == NULL) {
        return A_NO_MEMORY;
    }

    bss->ni_snr        = bih->snr;
    bss->ni_cie.ie_chan = bih->channel;
    A_ASSERT(bss->ni_buf != NULL);
    memcpy(bss->ni_buf, buf, len);
    wlan_setup_node(&wmip->wmi_scan_table, bss, bih->bssid);

    return 0;
}

    /* This event indicates inactivity timeout of a fatpipe(pstream)
     * at the target
     */
static int
wmi_pstream_timeout_event_rx(struct wmi_t *wmip, u8 *datap, int len)
{
    WMI_PSTREAM_TIMEOUT_EVENT *ev;

    if (len < sizeof(WMI_PSTREAM_TIMEOUT_EVENT)) {
        return A_EINVAL;
    }

    A_DPRINTF(DBG_WMI, (DBGFMT "wmi_pstream_timeout_event_rx\n", DBGARG));

    ev = (WMI_PSTREAM_TIMEOUT_EVENT *)datap;

        /* When the pstream (fat pipe == AC) timesout, it means there were no
         * thinStreams within this pstream & it got implicitly created due to
         * data flow on this AC. We start the inactivity timer only for
         * implicitly created pstream. Just reset the host state.
     */
        /* Set the activeTsids for this AC to 0 */
    LOCK_WMI(wmip);
    wmip->wmi_streamExistsForAC[ev->trafficClass]=0;
    wmip->wmi_fatPipeExists &= ~(1 << ev->trafficClass);
    UNLOCK_WMI(wmip);

        /*Indicate inactivity to driver layer for this fatpipe (pstream)*/
    A_WMI_STREAM_TX_INACTIVE(wmip->wmi_devt, ev->trafficClass);

    return 0;
}

static int
wmi_bitrate_reply_rx(struct wmi_t *wmip, u8 *datap, int len)
{
    WMI_BIT_RATE_REPLY *reply;
    s32 rate;
    u32 sgi,index;
    /* 54149:
     * WMI_BIT_RATE_CMD structure is changed to WMI_BIT_RATE_REPLY.
     * since there is difference in the length and to avoid returning
     * error value.
     */
    if (len < sizeof(WMI_BIT_RATE_REPLY)) {
        return A_EINVAL;
    }
    reply = (WMI_BIT_RATE_REPLY *)datap;
    A_DPRINTF(DBG_WMI,
        (DBGFMT "Enter - rateindex %d\n", DBGARG, reply->rateIndex));

    if (reply->rateIndex == (s8) RATE_AUTO) {
        rate = RATE_AUTO;
    } else {
        // the SGI state is stored as the MSb of the rateIndex
        index = reply->rateIndex & 0x7f;
        sgi = (reply->rateIndex & 0x80)? 1:0;
        rate = wmi_rateTable[index][sgi];
    }

    A_WMI_BITRATE_RX(wmip->wmi_devt, rate);
    return 0;
}

static int
wmi_ratemask_reply_rx(struct wmi_t *wmip, u8 *datap, int len)
{
    WMI_FIX_RATES_REPLY *reply;

    if (len < sizeof(WMI_FIX_RATES_REPLY)) {
        return A_EINVAL;
    }
    reply = (WMI_FIX_RATES_REPLY *)datap;
    A_DPRINTF(DBG_WMI,
        (DBGFMT "Enter - fixed rate mask %x\n", DBGARG, reply->fixRateMask));

    A_WMI_RATEMASK_RX(wmip->wmi_devt, reply->fixRateMask);

    return 0;
}

static int
wmi_channelList_reply_rx(struct wmi_t *wmip, u8 *datap, int len)
{
    WMI_CHANNEL_LIST_REPLY *reply;

    if (len < sizeof(WMI_CHANNEL_LIST_REPLY)) {
        return A_EINVAL;
    }
    reply = (WMI_CHANNEL_LIST_REPLY *)datap;
    A_DPRINTF(DBG_WMI, (DBGFMT "Enter\n", DBGARG));

    A_WMI_CHANNELLIST_RX(wmip->wmi_devt, reply->numChannels,
                          reply->channelList);

    return 0;
}

static int
wmi_txPwr_reply_rx(struct wmi_t *wmip, u8 *datap, int len)
{
    WMI_TX_PWR_REPLY *reply;

    if (len < sizeof(*reply)) {
        return A_EINVAL;
    }
    reply = (WMI_TX_PWR_REPLY *)datap;
    A_DPRINTF(DBG_WMI, (DBGFMT "Enter\n", DBGARG));

    A_WMI_TXPWR_RX(wmip->wmi_devt, reply->dbM);

    return 0;
}
static int
wmi_keepalive_reply_rx(struct wmi_t *wmip, u8 *datap, int len)
{
    WMI_GET_KEEPALIVE_CMD *reply;

    if (len < sizeof(*reply)) {
        return A_EINVAL;
    }
    reply = (WMI_GET_KEEPALIVE_CMD *)datap;
    A_DPRINTF(DBG_WMI, (DBGFMT "Enter\n", DBGARG));

    A_WMI_KEEPALIVE_RX(wmip->wmi_devt, reply->configured);

    return 0;
}


static int
wmi_dset_open_req_rx(struct wmi_t *wmip, u8 *datap, int len)
{
    WMIX_DSETOPENREQ_EVENT *dsetopenreq;

    if (len < sizeof(WMIX_DSETOPENREQ_EVENT)) {
        return A_EINVAL;
    }
    dsetopenreq = (WMIX_DSETOPENREQ_EVENT *)datap;
    A_DPRINTF(DBG_WMI,
        (DBGFMT "Enter - dset_id=0x%x\n", DBGARG, dsetopenreq->dset_id));
    A_WMI_DSET_OPEN_REQ(wmip->wmi_devt,
                        dsetopenreq->dset_id,
                        dsetopenreq->targ_dset_handle,
                        dsetopenreq->targ_reply_fn,
                        dsetopenreq->targ_reply_arg);

    return 0;
}

#ifdef CONFIG_HOST_DSET_SUPPORT
static int
wmi_dset_close_rx(struct wmi_t *wmip, u8 *datap, int len)
{
    WMIX_DSETCLOSE_EVENT *dsetclose;

    if (len < sizeof(WMIX_DSETCLOSE_EVENT)) {
        return A_EINVAL;
    }
    A_DPRINTF(DBG_WMI, (DBGFMT "Enter\n", DBGARG));

    dsetclose = (WMIX_DSETCLOSE_EVENT *)datap;
    A_WMI_DSET_CLOSE(wmip->wmi_devt, dsetclose->access_cookie);

    return 0;
}

static int
wmi_dset_data_req_rx(struct wmi_t *wmip, u8 *datap, int len)
{
    WMIX_DSETDATAREQ_EVENT *dsetdatareq;

    if (len < sizeof(WMIX_DSETDATAREQ_EVENT)) {
        return A_EINVAL;
    }
    A_DPRINTF(DBG_WMI, (DBGFMT "Enter\n", DBGARG));

    dsetdatareq = (WMIX_DSETDATAREQ_EVENT *)datap;
    A_WMI_DSET_DATA_REQ(wmip->wmi_devt,
                         dsetdatareq->access_cookie,
                         dsetdatareq->offset,
                         dsetdatareq->length,
                         dsetdatareq->targ_buf,
                         dsetdatareq->targ_reply_fn,
                         dsetdatareq->targ_reply_arg);

    return 0;
}
#endif /* CONFIG_HOST_DSET_SUPPORT */

static int
wmi_scanComplete_rx(struct wmi_t *wmip, u8 *datap, int len)
{
    WMI_SCAN_COMPLETE_EVENT *ev;

    ev = (WMI_SCAN_COMPLETE_EVENT *)datap;
    if ((int)ev->status == 0) {
        wlan_refresh_inactive_nodes(&wmip->wmi_scan_table);
    }
    A_WMI_SCANCOMPLETE_EVENT(wmip->wmi_devt, (int) ev->status);
    is_probe_ssid = false;

    return 0;
}

/*
 * Target is reporting a programming error.  This is for
 * developer aid only.  Target only checks a few common violations
 * and it is responsibility of host to do all error checking.
 * Behavior of target after wmi error event is undefined.
 * A reset is recommended.
 */
static int
wmi_errorEvent_rx(struct wmi_t *wmip, u8 *datap, int len)
{
    WMI_CMD_ERROR_EVENT *ev;

    ev = (WMI_CMD_ERROR_EVENT *)datap;
    AR_DEBUG_PRINTF(ATH_DEBUG_WMI, ("Programming Error: cmd=%d ", ev->commandId));
    switch (ev->errorCode) {
    case (INVALID_PARAM):
        AR_DEBUG_PRINTF(ATH_DEBUG_WMI, ("Illegal Parameter\n"));
        break;
    case (ILLEGAL_STATE):
        AR_DEBUG_PRINTF(ATH_DEBUG_WMI, ("Illegal State\n"));
        break;
    case (INTERNAL_ERROR):
        AR_DEBUG_PRINTF(ATH_DEBUG_WMI, ("Internal Error\n"));
        break;
    }

    return 0;
}


static int
wmi_statsEvent_rx(struct wmi_t *wmip, u8 *datap, int len)
{
    A_DPRINTF(DBG_WMI, (DBGFMT "Enter\n", DBGARG));

    A_WMI_TARGETSTATS_EVENT(wmip->wmi_devt, datap, len);

    return 0;
}

static int
wmi_rssiThresholdEvent_rx(struct wmi_t *wmip, u8 *datap, int len)
{
    WMI_RSSI_THRESHOLD_EVENT *reply;
    WMI_RSSI_THRESHOLD_VAL newThreshold;
    WMI_RSSI_THRESHOLD_PARAMS_CMD cmd;
    SQ_THRESHOLD_PARAMS *sq_thresh =
           &wmip->wmi_SqThresholdParams[SIGNAL_QUALITY_METRICS_RSSI];
    u8 upper_rssi_threshold, lower_rssi_threshold;
    s16 rssi;

    if (len < sizeof(*reply)) {
        return A_EINVAL;
    }
    reply = (WMI_RSSI_THRESHOLD_EVENT *)datap;
    A_DPRINTF(DBG_WMI, (DBGFMT "Enter\n", DBGARG));
    newThreshold = (WMI_RSSI_THRESHOLD_VAL) reply->range;
    rssi = reply->rssi;

    /*
     * Identify the threshold breached and communicate that to the app. After
     * that install a new set of thresholds based on the signal quality
     * reported by the target
     */
    if (newThreshold) {
        /* Upper threshold breached */
        if (rssi < sq_thresh->upper_threshold[0]) {
            A_DPRINTF(DBG_WMI, (DBGFMT "Spurious upper RSSI threshold event: "
                      " %d\n", DBGARG, rssi));
        } else if ((rssi < sq_thresh->upper_threshold[1]) &&
                   (rssi >= sq_thresh->upper_threshold[0]))
        {
            newThreshold = WMI_RSSI_THRESHOLD1_ABOVE;
        } else if ((rssi < sq_thresh->upper_threshold[2]) &&
                   (rssi >= sq_thresh->upper_threshold[1]))
        {
            newThreshold = WMI_RSSI_THRESHOLD2_ABOVE;
        } else if ((rssi < sq_thresh->upper_threshold[3]) &&
                   (rssi >= sq_thresh->upper_threshold[2]))
        {
            newThreshold = WMI_RSSI_THRESHOLD3_ABOVE;
        } else if ((rssi < sq_thresh->upper_threshold[4]) &&
                   (rssi >= sq_thresh->upper_threshold[3]))
        {
            newThreshold = WMI_RSSI_THRESHOLD4_ABOVE;
        } else if ((rssi < sq_thresh->upper_threshold[5]) &&
                   (rssi >= sq_thresh->upper_threshold[4]))
        {
            newThreshold = WMI_RSSI_THRESHOLD5_ABOVE;
        } else if (rssi >= sq_thresh->upper_threshold[5]) {
            newThreshold = WMI_RSSI_THRESHOLD6_ABOVE;
        }
    } else {
        /* Lower threshold breached */
        if (rssi > sq_thresh->lower_threshold[0]) {
            A_DPRINTF(DBG_WMI, (DBGFMT "Spurious lower RSSI threshold event: "
                      "%d %d\n", DBGARG, rssi, sq_thresh->lower_threshold[0]));
        } else if ((rssi > sq_thresh->lower_threshold[1]) &&
                   (rssi <= sq_thresh->lower_threshold[0]))
        {
            newThreshold = WMI_RSSI_THRESHOLD6_BELOW;
        } else if ((rssi > sq_thresh->lower_threshold[2]) &&
                   (rssi <= sq_thresh->lower_threshold[1]))
        {
            newThreshold = WMI_RSSI_THRESHOLD5_BELOW;
        } else if ((rssi > sq_thresh->lower_threshold[3]) &&
                   (rssi <= sq_thresh->lower_threshold[2]))
        {
            newThreshold = WMI_RSSI_THRESHOLD4_BELOW;
        } else if ((rssi > sq_thresh->lower_threshold[4]) &&
                   (rssi <= sq_thresh->lower_threshold[3]))
        {
            newThreshold = WMI_RSSI_THRESHOLD3_BELOW;
        } else if ((rssi > sq_thresh->lower_threshold[5]) &&
                   (rssi <= sq_thresh->lower_threshold[4]))
        {
            newThreshold = WMI_RSSI_THRESHOLD2_BELOW;
        } else if (rssi <= sq_thresh->lower_threshold[5]) {
            newThreshold = WMI_RSSI_THRESHOLD1_BELOW;
        }
    }
    /* Calculate and install the next set of thresholds */
    lower_rssi_threshold = ar6000_get_lower_threshold(rssi, sq_thresh,
                                      sq_thresh->lower_threshold_valid_count);
    upper_rssi_threshold = ar6000_get_upper_threshold(rssi, sq_thresh,
                                      sq_thresh->upper_threshold_valid_count);
    /* Issue a wmi command to install the thresholds */
    cmd.thresholdAbove1_Val = upper_rssi_threshold;
    cmd.thresholdBelow1_Val = lower_rssi_threshold;
    cmd.weight = sq_thresh->weight;
    cmd.pollTime = sq_thresh->polling_interval;

    rssi_event_value = rssi;

    if (wmi_send_rssi_threshold_params(wmip, &cmd) != 0) {
        A_DPRINTF(DBG_WMI, (DBGFMT "Unable to configure the RSSI thresholds\n",
                  DBGARG));
    }

    A_WMI_RSSI_THRESHOLD_EVENT(wmip->wmi_devt, newThreshold, reply->rssi);

    return 0;
}


static int
wmi_reportErrorEvent_rx(struct wmi_t *wmip, u8 *datap, int len)
{
    WMI_TARGET_ERROR_REPORT_EVENT *reply;

    if (len < sizeof(*reply)) {
        return A_EINVAL;
    }
    reply = (WMI_TARGET_ERROR_REPORT_EVENT *)datap;
    A_DPRINTF(DBG_WMI, (DBGFMT "Enter\n", DBGARG));

    A_WMI_REPORT_ERROR_EVENT(wmip->wmi_devt, (WMI_TARGET_ERROR_VAL) reply->errorVal);

    return 0;
}

static int
wmi_cac_event_rx(struct wmi_t *wmip, u8 *datap, int len)
{
    WMI_CAC_EVENT *reply;
    WMM_TSPEC_IE *tspec_ie;
    u16 activeTsids;

    if (len < sizeof(*reply)) {
        return A_EINVAL;
    }
    reply = (WMI_CAC_EVENT *)datap;

    A_DPRINTF(DBG_WMI, (DBGFMT "Enter\n", DBGARG));

    if ((reply->cac_indication == CAC_INDICATION_ADMISSION_RESP) &&
        (reply->statusCode != TSPEC_STATUS_CODE_ADMISSION_ACCEPTED)) {
        tspec_ie = (WMM_TSPEC_IE *) &(reply->tspecSuggestion);

        wmi_delete_pstream_cmd(wmip, reply->ac,
                (tspec_ie->tsInfo_info >> TSPEC_TSID_S) & TSPEC_TSID_MASK);
    }
    else if (reply->cac_indication == CAC_INDICATION_NO_RESP) {
        u8 i;

        /* following assumes that there is only one outstanding ADDTS request
           when this event is received */
        LOCK_WMI(wmip);
        activeTsids = wmip->wmi_streamExistsForAC[reply->ac];
        UNLOCK_WMI(wmip);

        for (i = 0; i < sizeof(activeTsids) * 8; i++) {
            if ((activeTsids >> i) & 1) {
                break;
            }
        }
        if (i < (sizeof(activeTsids) * 8)) {
            wmi_delete_pstream_cmd(wmip, reply->ac, i);
        }
    }
        /*
         * Ev#72990: Clear active tsids and Add missing handling
         * for delete qos stream from AP
         */
    else if (reply->cac_indication == CAC_INDICATION_DELETE) {
        u8 tsid = 0;

        tspec_ie = (WMM_TSPEC_IE *) &(reply->tspecSuggestion);
        tsid= ((tspec_ie->tsInfo_info >> TSPEC_TSID_S) & TSPEC_TSID_MASK);
        LOCK_WMI(wmip);
        wmip->wmi_streamExistsForAC[reply->ac] &= ~(1<<tsid);
        activeTsids = wmip->wmi_streamExistsForAC[reply->ac];
        UNLOCK_WMI(wmip);


        /* Indicate stream inactivity to driver layer only if all tsids
         * within this AC are deleted.
         */
       if (!activeTsids) {
           A_WMI_STREAM_TX_INACTIVE(wmip->wmi_devt, reply->ac);
           wmip->wmi_fatPipeExists &= ~(1 << reply->ac);
        }
    }

    A_WMI_CAC_EVENT(wmip->wmi_devt, reply->ac,
                reply->cac_indication, reply->statusCode,
                reply->tspecSuggestion);

    return 0;
}

static int
wmi_channel_change_event_rx(struct wmi_t *wmip, u8 *datap, int len)
{
    WMI_CHANNEL_CHANGE_EVENT *reply;

    if (len < sizeof(*reply)) {
        return A_EINVAL;
    }
    reply = (WMI_CHANNEL_CHANGE_EVENT *)datap;
    A_DPRINTF(DBG_WMI, (DBGFMT "Enter\n", DBGARG));

    A_WMI_CHANNEL_CHANGE_EVENT(wmip->wmi_devt, reply->oldChannel,
                               reply->newChannel);

    return 0;
}

static int
wmi_hbChallengeResp_rx(struct wmi_t *wmip, u8 *datap, int len)
{
    WMIX_HB_CHALLENGE_RESP_EVENT *reply;

    if (len < sizeof(*reply)) {
        return A_EINVAL;
    }
    reply = (WMIX_HB_CHALLENGE_RESP_EVENT *)datap;
    A_DPRINTF(DBG_WMI, (DBGFMT "wmi: challenge response event\n", DBGARG));

    A_WMI_HBCHALLENGERESP_EVENT(wmip->wmi_devt, reply->cookie, reply->source);

    return 0;
}

static int
wmi_roam_tbl_event_rx(struct wmi_t *wmip, u8 *datap, int len)
{
    WMI_TARGET_ROAM_TBL *reply;

    if (len < sizeof(*reply)) {
        return A_EINVAL;
    }
    reply = (WMI_TARGET_ROAM_TBL *)datap;
    A_DPRINTF(DBG_WMI, (DBGFMT "Enter\n", DBGARG));

    A_WMI_ROAM_TABLE_EVENT(wmip->wmi_devt, reply);

    return 0;
}

static int
wmi_roam_data_event_rx(struct wmi_t *wmip, u8 *datap, int len)
{
    WMI_TARGET_ROAM_DATA *reply;

    if (len < sizeof(*reply)) {
        return A_EINVAL;
    }
    reply = (WMI_TARGET_ROAM_DATA *)datap;
    A_DPRINTF(DBG_WMI, (DBGFMT "Enter\n", DBGARG));

    A_WMI_ROAM_DATA_EVENT(wmip->wmi_devt, reply);

    return 0;
}

static int
wmi_txRetryErrEvent_rx(struct wmi_t *wmip, u8 *datap, int len)
{
    if (len < sizeof(WMI_TX_RETRY_ERR_EVENT)) {
        return A_EINVAL;
    }
    A_DPRINTF(DBG_WMI, (DBGFMT "Enter\n", DBGARG));

    A_WMI_TX_RETRY_ERR_EVENT(wmip->wmi_devt);

    return 0;
}

static int
wmi_snrThresholdEvent_rx(struct wmi_t *wmip, u8 *datap, int len)
{
    WMI_SNR_THRESHOLD_EVENT *reply;
    SQ_THRESHOLD_PARAMS *sq_thresh =
           &wmip->wmi_SqThresholdParams[SIGNAL_QUALITY_METRICS_SNR];
    WMI_SNR_THRESHOLD_VAL newThreshold;
    WMI_SNR_THRESHOLD_PARAMS_CMD cmd;
    u8 upper_snr_threshold, lower_snr_threshold;
    s16 snr;

    if (len < sizeof(*reply)) {
        return A_EINVAL;
    }
    reply = (WMI_SNR_THRESHOLD_EVENT *)datap;
    A_DPRINTF(DBG_WMI, (DBGFMT "Enter\n", DBGARG));

    newThreshold = (WMI_SNR_THRESHOLD_VAL) reply->range;
    snr = reply->snr;
    /*
     * Identify the threshold breached and communicate that to the app. After
     * that install a new set of thresholds based on the signal quality
     * reported by the target
     */
    if (newThreshold) {
        /* Upper threshold breached */
        if (snr < sq_thresh->upper_threshold[0]) {
            A_DPRINTF(DBG_WMI, (DBGFMT "Spurious upper SNR threshold event: "
                     "%d\n", DBGARG, snr));
        } else if ((snr < sq_thresh->upper_threshold[1]) &&
                   (snr >= sq_thresh->upper_threshold[0]))
        {
            newThreshold = WMI_SNR_THRESHOLD1_ABOVE;
        } else if ((snr < sq_thresh->upper_threshold[2]) &&
                   (snr >= sq_thresh->upper_threshold[1]))
        {
            newThreshold = WMI_SNR_THRESHOLD2_ABOVE;
        } else if ((snr < sq_thresh->upper_threshold[3]) &&
                   (snr >= sq_thresh->upper_threshold[2]))
        {
            newThreshold = WMI_SNR_THRESHOLD3_ABOVE;
        } else if (snr >= sq_thresh->upper_threshold[3]) {
            newThreshold = WMI_SNR_THRESHOLD4_ABOVE;
        }
    } else {
        /* Lower threshold breached */
        if (snr > sq_thresh->lower_threshold[0]) {
            A_DPRINTF(DBG_WMI, (DBGFMT "Spurious lower SNR threshold event: "
                      "%d %d\n", DBGARG, snr, sq_thresh->lower_threshold[0]));
        } else if ((snr > sq_thresh->lower_threshold[1]) &&
                   (snr <= sq_thresh->lower_threshold[0]))
        {
            newThreshold = WMI_SNR_THRESHOLD4_BELOW;
        } else if ((snr > sq_thresh->lower_threshold[2]) &&
                   (snr <= sq_thresh->lower_threshold[1]))
        {
            newThreshold = WMI_SNR_THRESHOLD3_BELOW;
        } else if ((snr > sq_thresh->lower_threshold[3]) &&
                   (snr <= sq_thresh->lower_threshold[2]))
        {
            newThreshold = WMI_SNR_THRESHOLD2_BELOW;
        } else if (snr <= sq_thresh->lower_threshold[3]) {
            newThreshold = WMI_SNR_THRESHOLD1_BELOW;
        }
    }

    /* Calculate and install the next set of thresholds */
    lower_snr_threshold = ar6000_get_lower_threshold(snr, sq_thresh,
                                      sq_thresh->lower_threshold_valid_count);
    upper_snr_threshold = ar6000_get_upper_threshold(snr, sq_thresh,
                                      sq_thresh->upper_threshold_valid_count);

    /* Issue a wmi command to install the thresholds */
    cmd.thresholdAbove1_Val = upper_snr_threshold;
    cmd.thresholdBelow1_Val = lower_snr_threshold;
    cmd.weight = sq_thresh->weight;
    cmd.pollTime = sq_thresh->polling_interval;

    A_DPRINTF(DBG_WMI, (DBGFMT "snr: %d, threshold: %d, lower: %d, upper: %d\n"
              ,DBGARG, snr, newThreshold, lower_snr_threshold,
              upper_snr_threshold));

    snr_event_value = snr;

    if (wmi_send_snr_threshold_params(wmip, &cmd) != 0) {
        A_DPRINTF(DBG_WMI, (DBGFMT "Unable to configure the SNR thresholds\n",
                  DBGARG));
    }
    A_WMI_SNR_THRESHOLD_EVENT_RX(wmip->wmi_devt, newThreshold, reply->snr);

    return 0;
}

static int
wmi_lqThresholdEvent_rx(struct wmi_t *wmip, u8 *datap, int len)
{
    WMI_LQ_THRESHOLD_EVENT *reply;

    if (len < sizeof(*reply)) {
        return A_EINVAL;
    }
    reply = (WMI_LQ_THRESHOLD_EVENT *)datap;
    A_DPRINTF(DBG_WMI, (DBGFMT "Enter\n", DBGARG));

    A_WMI_LQ_THRESHOLD_EVENT_RX(wmip->wmi_devt,
                                (WMI_LQ_THRESHOLD_VAL) reply->range,
                                reply->lq);

    return 0;
}

static int
wmi_aplistEvent_rx(struct wmi_t *wmip, u8 *datap, int len)
{
    u16 ap_info_entry_size;
    WMI_APLIST_EVENT *ev = (WMI_APLIST_EVENT *)datap;
    WMI_AP_INFO_V1 *ap_info_v1;
    u8 i;

    if (len < sizeof(WMI_APLIST_EVENT)) {
        return A_EINVAL;
    }

    if (ev->apListVer == APLIST_VER1) {
        ap_info_entry_size = sizeof(WMI_AP_INFO_V1);
        ap_info_v1 = (WMI_AP_INFO_V1 *)ev->apList;
    } else {
        return A_EINVAL;
    }

    AR_DEBUG_PRINTF(ATH_DEBUG_WMI, ("Number of APs in APLIST Event is %d\n", ev->numAP));
    if (len < (int)(sizeof(WMI_APLIST_EVENT) +
              (ev->numAP - 1) * ap_info_entry_size))
    {
        return A_EINVAL;
    }

    /*
     * AP List Ver1 Contents
     */
    for (i = 0; i < ev->numAP; i++) {
        AR_DEBUG_PRINTF(ATH_DEBUG_WMI, ("AP#%d BSSID %2.2x %2.2x %2.2x %2.2x %2.2x %2.2x "\
                    "Channel %d\n", i,
                   ap_info_v1->bssid[0], ap_info_v1->bssid[1],
                   ap_info_v1->bssid[2], ap_info_v1->bssid[3],
                   ap_info_v1->bssid[4], ap_info_v1->bssid[5],
                   ap_info_v1->channel));
        ap_info_v1++;
    }
    return 0;
}

static int
wmi_dbglog_event_rx(struct wmi_t *wmip, u8 *datap, int len)
{
    u32 dropped;

    dropped = *((u32 *)datap);
    datap += sizeof(dropped);
    len -= sizeof(dropped);
    A_WMI_DBGLOG_EVENT(wmip->wmi_devt, dropped, (s8 *)datap, len);
    return 0;
}

#ifdef CONFIG_HOST_GPIO_SUPPORT
static int
wmi_gpio_intr_rx(struct wmi_t *wmip, u8 *datap, int len)
{
    WMIX_GPIO_INTR_EVENT *gpio_intr = (WMIX_GPIO_INTR_EVENT *)datap;

    A_DPRINTF(DBG_WMI,
        (DBGFMT "Enter - intrmask=0x%x input=0x%x.\n", DBGARG,
        gpio_intr->intr_mask, gpio_intr->input_values));

    A_WMI_GPIO_INTR_RX(gpio_intr->intr_mask, gpio_intr->input_values);

    return 0;
}

static int
wmi_gpio_data_rx(struct wmi_t *wmip, u8 *datap, int len)
{
    WMIX_GPIO_DATA_EVENT *gpio_data = (WMIX_GPIO_DATA_EVENT *)datap;

    A_DPRINTF(DBG_WMI,
        (DBGFMT "Enter - reg=%d value=0x%x\n", DBGARG,
        gpio_data->reg_id, gpio_data->value));

    A_WMI_GPIO_DATA_RX(gpio_data->reg_id, gpio_data->value);

    return 0;
}

static int
wmi_gpio_ack_rx(struct wmi_t *wmip, u8 *datap, int len)
{
    A_DPRINTF(DBG_WMI, (DBGFMT "Enter\n", DBGARG));

    A_WMI_GPIO_ACK_RX();

    return 0;
}
#endif /* CONFIG_HOST_GPIO_SUPPORT */

/*
 * Called to send a wmi command. Command specific data is already built
 * on osbuf and current osbuf->data points to it.
 */
int
wmi_cmd_send(struct wmi_t *wmip, void *osbuf, WMI_COMMAND_ID cmdId,
               WMI_SYNC_FLAG syncflag)
{
    int status;
#define IS_OPT_TX_CMD(cmdId) ((cmdId == WMI_OPT_TX_FRAME_CMDID))
    WMI_CMD_HDR         *cHdr;
    HTC_ENDPOINT_ID     eid  = wmip->wmi_endpoint_id;

    A_ASSERT(osbuf != NULL);

    if (syncflag >= END_WMIFLAG) {
        A_NETBUF_FREE(osbuf);
        return A_EINVAL;
    }

    if ((syncflag == SYNC_BEFORE_WMIFLAG) || (syncflag == SYNC_BOTH_WMIFLAG)) {
        /*
         * We want to make sure all data currently queued is transmitted before
         * the cmd execution.  Establish a new sync point.
         */
        wmi_sync_point(wmip);
    }

    if (A_NETBUF_PUSH(osbuf, sizeof(WMI_CMD_HDR)) != 0) {
        A_NETBUF_FREE(osbuf);
        return A_NO_MEMORY;
    }

    cHdr = (WMI_CMD_HDR *)A_NETBUF_DATA(osbuf);
    cHdr->commandId = (u16) cmdId;
    cHdr->info1 = 0; // added for virtual interface

    /*
     * Only for OPT_TX_CMD, use BE endpoint.
     */
    if (IS_OPT_TX_CMD(cmdId)) {
        if ((status=wmi_data_hdr_add(wmip, osbuf, OPT_MSGTYPE, false, false,0,NULL)) != 0) {
            A_NETBUF_FREE(osbuf);
            return status;
        }
        eid = A_WMI_Ac2EndpointID(wmip->wmi_devt, WMM_AC_BE);
    }
    A_WMI_CONTROL_TX(wmip->wmi_devt, osbuf, eid);

    if ((syncflag == SYNC_AFTER_WMIFLAG) || (syncflag == SYNC_BOTH_WMIFLAG)) {
        /*
         * We want to make sure all new data queued waits for the command to
         * execute. Establish a new sync point.
         */
        wmi_sync_point(wmip);
    }
    return (0);
#undef IS_OPT_TX_CMD
}

int
wmi_cmd_send_xtnd(struct wmi_t *wmip, void *osbuf, WMIX_COMMAND_ID cmdId,
                  WMI_SYNC_FLAG syncflag)
{
    WMIX_CMD_HDR     *cHdr;

    if (A_NETBUF_PUSH(osbuf, sizeof(WMIX_CMD_HDR)) != 0) {
        A_NETBUF_FREE(osbuf);
        return A_NO_MEMORY;
    }

    cHdr = (WMIX_CMD_HDR *)A_NETBUF_DATA(osbuf);
    cHdr->commandId = (u32) cmdId;

    return wmi_cmd_send(wmip, osbuf, WMI_EXTENSION_CMDID, syncflag);
}

int
wmi_connect_cmd(struct wmi_t *wmip, NETWORK_TYPE netType,
                DOT11_AUTH_MODE dot11AuthMode, AUTH_MODE authMode,
                CRYPTO_TYPE pairwiseCrypto, u8 pairwiseCryptoLen,
                CRYPTO_TYPE groupCrypto, u8 groupCryptoLen,
                int ssidLength, u8 *ssid,
                u8 *bssid, u16 channel, u32 ctrl_flags)
{
    void *osbuf;
    WMI_CONNECT_CMD *cc;
    wmip->wmi_traffic_class = 100;

    if ((pairwiseCrypto == NONE_CRYPT) && (groupCrypto != NONE_CRYPT)) {
        return A_EINVAL;
    }
    if ((pairwiseCrypto != NONE_CRYPT) && (groupCrypto == NONE_CRYPT)) {
        return A_EINVAL;
    }

    osbuf = A_NETBUF_ALLOC(sizeof(WMI_CONNECT_CMD));
    if (osbuf == NULL) {
        return A_NO_MEMORY;
    }

    A_NETBUF_PUT(osbuf, sizeof(WMI_CONNECT_CMD));

    cc = (WMI_CONNECT_CMD *)(A_NETBUF_DATA(osbuf));
    A_MEMZERO(cc, sizeof(*cc));

    if (ssidLength)
    {
        memcpy(cc->ssid, ssid, ssidLength);
    }

    cc->ssidLength          = ssidLength;
    cc->networkType         = netType;
    cc->dot11AuthMode       = dot11AuthMode;
    cc->authMode            = authMode;
    cc->pairwiseCryptoType  = pairwiseCrypto;
    cc->pairwiseCryptoLen   = pairwiseCryptoLen;
    cc->groupCryptoType     = groupCrypto;
    cc->groupCryptoLen      = groupCryptoLen;
    cc->channel             = channel;
    cc->ctrl_flags          = ctrl_flags;

    if (bssid != NULL) {
        memcpy(cc->bssid, bssid, ATH_MAC_LEN);
    }

    wmip->wmi_pair_crypto_type  = pairwiseCrypto;
    wmip->wmi_grp_crypto_type   = groupCrypto;

    return (wmi_cmd_send(wmip, osbuf, WMI_CONNECT_CMDID, NO_SYNC_WMIFLAG));
}

int
wmi_reconnect_cmd(struct wmi_t *wmip, u8 *bssid, u16 channel)
{
    void *osbuf;
    WMI_RECONNECT_CMD *cc;
    wmip->wmi_traffic_class = 100;

    osbuf = A_NETBUF_ALLOC(sizeof(WMI_RECONNECT_CMD));
    if (osbuf == NULL) {
        return A_NO_MEMORY;
    }

    A_NETBUF_PUT(osbuf, sizeof(WMI_RECONNECT_CMD));

    cc = (WMI_RECONNECT_CMD *)(A_NETBUF_DATA(osbuf));
    A_MEMZERO(cc, sizeof(*cc));

    cc->channel = channel;

    if (bssid != NULL) {
        memcpy(cc->bssid, bssid, ATH_MAC_LEN);
    }

    return (wmi_cmd_send(wmip, osbuf, WMI_RECONNECT_CMDID, NO_SYNC_WMIFLAG));
}

int
wmi_disconnect_cmd(struct wmi_t *wmip)
{
    int status;
    wmip->wmi_traffic_class = 100;

    /* Bug fix for 24817(elevator bug) - the disconnect command does not
       need to do a SYNC before.*/
    status = wmi_simple_cmd(wmip, WMI_DISCONNECT_CMDID);

    return status;
}

int
wmi_startscan_cmd(struct wmi_t *wmip, WMI_SCAN_TYPE scanType,
                  u32 forceFgScan, u32 isLegacy,
                  u32 homeDwellTime, u32 forceScanInterval,
                  s8 numChan, u16 *channelList)
{
    void *osbuf;
    WMI_START_SCAN_CMD *sc;
    s8 size;

    size = sizeof (*sc);

    if ((scanType != WMI_LONG_SCAN) && (scanType != WMI_SHORT_SCAN)) {
        return A_EINVAL;
    }

    if (numChan) {
        if (numChan > WMI_MAX_CHANNELS) {
            return A_EINVAL;
        }
        size += sizeof(u16) * (numChan - 1);
    }

    osbuf = A_NETBUF_ALLOC(size);
    if (osbuf == NULL) {
        return A_NO_MEMORY;
    }

    A_NETBUF_PUT(osbuf, size);

    sc = (WMI_START_SCAN_CMD *)(A_NETBUF_DATA(osbuf));
    sc->scanType = scanType;
    sc->forceFgScan = forceFgScan;
    sc->isLegacy = isLegacy;
    sc->homeDwellTime = homeDwellTime;
    sc->forceScanInterval = forceScanInterval;
    sc->numChannels = numChan;
    if (numChan) {
        memcpy(sc->channelList, channelList, numChan * sizeof(u16));
    }

    return (wmi_cmd_send(wmip, osbuf, WMI_START_SCAN_CMDID, NO_SYNC_WMIFLAG));
}

int
wmi_scanparams_cmd(struct wmi_t *wmip, u16 fg_start_sec,
                   u16 fg_end_sec, u16 bg_sec,
                   u16 minact_chdw_msec, u16 maxact_chdw_msec,
                   u16 pas_chdw_msec,
                   u8 shScanRatio, u8 scanCtrlFlags,
                   u32 max_dfsch_act_time, u16 maxact_scan_per_ssid)
{
    void *osbuf;
    WMI_SCAN_PARAMS_CMD *sc;

    osbuf = A_NETBUF_ALLOC(sizeof(*sc));
    if (osbuf == NULL) {
        return A_NO_MEMORY;
    }

    A_NETBUF_PUT(osbuf, sizeof(*sc));

    sc = (WMI_SCAN_PARAMS_CMD *)(A_NETBUF_DATA(osbuf));
    A_MEMZERO(sc, sizeof(*sc));
    sc->fg_start_period  = fg_start_sec;
    sc->fg_end_period    = fg_end_sec;
    sc->bg_period        = bg_sec;
    sc->minact_chdwell_time = minact_chdw_msec;
    sc->maxact_chdwell_time = maxact_chdw_msec;
    sc->pas_chdwell_time = pas_chdw_msec;
    sc->shortScanRatio   = shScanRatio;
    sc->scanCtrlFlags    = scanCtrlFlags;
    sc->max_dfsch_act_time = max_dfsch_act_time;
    sc->maxact_scan_per_ssid = maxact_scan_per_ssid;

    return (wmi_cmd_send(wmip, osbuf, WMI_SET_SCAN_PARAMS_CMDID,
                         NO_SYNC_WMIFLAG));
}

int
wmi_bssfilter_cmd(struct wmi_t *wmip, u8 filter, u32 ieMask)
{
    void *osbuf;
    WMI_BSS_FILTER_CMD *cmd;

    if (filter >= LAST_BSS_FILTER) {
        return A_EINVAL;
    }

    osbuf = A_NETBUF_ALLOC(sizeof(*cmd));
    if (osbuf == NULL) {
        return A_NO_MEMORY;
    }

    A_NETBUF_PUT(osbuf, sizeof(*cmd));

    cmd = (WMI_BSS_FILTER_CMD *)(A_NETBUF_DATA(osbuf));
    A_MEMZERO(cmd, sizeof(*cmd));
    cmd->bssFilter = filter;
    cmd->ieMask = ieMask;

    return (wmi_cmd_send(wmip, osbuf, WMI_SET_BSS_FILTER_CMDID,
                         NO_SYNC_WMIFLAG));
}

int
wmi_probedSsid_cmd(struct wmi_t *wmip, u8 index, u8 flag,
                   u8 ssidLength, u8 *ssid)
{
    void *osbuf;
    WMI_PROBED_SSID_CMD *cmd;

    if (index > MAX_PROBED_SSID_INDEX) {
        return A_EINVAL;
    }
    if (ssidLength > sizeof(cmd->ssid)) {
        return A_EINVAL;
    }
    if ((flag & (DISABLE_SSID_FLAG | ANY_SSID_FLAG)) && (ssidLength > 0)) {
        return A_EINVAL;
    }
    if ((flag & SPECIFIC_SSID_FLAG) && !ssidLength) {
        return A_EINVAL;
    }

    if (flag & SPECIFIC_SSID_FLAG) {
        is_probe_ssid = true;
    }

    osbuf = A_NETBUF_ALLOC(sizeof(*cmd));
    if (osbuf == NULL) {
        return A_NO_MEMORY;
    }

    A_NETBUF_PUT(osbuf, sizeof(*cmd));

    cmd = (WMI_PROBED_SSID_CMD *)(A_NETBUF_DATA(osbuf));
    A_MEMZERO(cmd, sizeof(*cmd));
    cmd->entryIndex = index;
    cmd->flag       = flag;
    cmd->ssidLength = ssidLength;
    memcpy(cmd->ssid, ssid, ssidLength);

    return (wmi_cmd_send(wmip, osbuf, WMI_SET_PROBED_SSID_CMDID,
                         NO_SYNC_WMIFLAG));
}

int
wmi_listeninterval_cmd(struct wmi_t *wmip, u16 listenInterval, u16 listenBeacons)
{
    void *osbuf;
    WMI_LISTEN_INT_CMD *cmd;

    osbuf = A_NETBUF_ALLOC(sizeof(*cmd));
    if (osbuf == NULL) {
        return A_NO_MEMORY;
    }

    A_NETBUF_PUT(osbuf, sizeof(*cmd));

    cmd = (WMI_LISTEN_INT_CMD *)(A_NETBUF_DATA(osbuf));
    A_MEMZERO(cmd, sizeof(*cmd));
    cmd->listenInterval = listenInterval;
    cmd->numBeacons = listenBeacons;

    return (wmi_cmd_send(wmip, osbuf, WMI_SET_LISTEN_INT_CMDID,
                         NO_SYNC_WMIFLAG));
}

int
wmi_bmisstime_cmd(struct wmi_t *wmip, u16 bmissTime, u16 bmissBeacons)
{
    void *osbuf;
    WMI_BMISS_TIME_CMD *cmd;

    osbuf = A_NETBUF_ALLOC(sizeof(*cmd));
    if (osbuf == NULL) {
        return A_NO_MEMORY;
    }

    A_NETBUF_PUT(osbuf, sizeof(*cmd));

    cmd = (WMI_BMISS_TIME_CMD *)(A_NETBUF_DATA(osbuf));
    A_MEMZERO(cmd, sizeof(*cmd));
    cmd->bmissTime = bmissTime;
    cmd->numBeacons =  bmissBeacons;

    return (wmi_cmd_send(wmip, osbuf, WMI_SET_BMISS_TIME_CMDID,
                         NO_SYNC_WMIFLAG));
}

int
wmi_associnfo_cmd(struct wmi_t *wmip, u8 ieType,
                     u8 ieLen, u8 *ieInfo)
{
    void *osbuf;
    WMI_SET_ASSOC_INFO_CMD *cmd;
    u16 cmdLen;

    cmdLen = sizeof(*cmd) + ieLen - 1;
    osbuf = A_NETBUF_ALLOC(cmdLen);
    if (osbuf == NULL) {
        return A_NO_MEMORY;
    }

    A_NETBUF_PUT(osbuf, cmdLen);

    cmd = (WMI_SET_ASSOC_INFO_CMD *)(A_NETBUF_DATA(osbuf));
    A_MEMZERO(cmd, cmdLen);
    cmd->ieType = ieType;
    cmd->bufferSize = ieLen;
    memcpy(cmd->assocInfo, ieInfo, ieLen);

    return (wmi_cmd_send(wmip, osbuf, WMI_SET_ASSOC_INFO_CMDID,
                         NO_SYNC_WMIFLAG));
}

int
wmi_powermode_cmd(struct wmi_t *wmip, u8 powerMode)
{
    void *osbuf;
    WMI_POWER_MODE_CMD *cmd;

    osbuf = A_NETBUF_ALLOC(sizeof(*cmd));
    if (osbuf == NULL) {
        return A_NO_MEMORY;
    }

    A_NETBUF_PUT(osbuf, sizeof(*cmd));

    cmd = (WMI_POWER_MODE_CMD *)(A_NETBUF_DATA(osbuf));
    A_MEMZERO(cmd, sizeof(*cmd));
    cmd->powerMode = powerMode;
    wmip->wmi_powerMode = powerMode;

    return (wmi_cmd_send(wmip, osbuf, WMI_SET_POWER_MODE_CMDID,
                         NO_SYNC_WMIFLAG));
}

int
wmi_ibsspmcaps_cmd(struct wmi_t *wmip, u8 pmEnable, u8 ttl,
                   u16 atim_windows, u16 timeout_value)
{
    void *osbuf;
    WMI_IBSS_PM_CAPS_CMD *cmd;

    osbuf = A_NETBUF_ALLOC(sizeof(*cmd));
    if (osbuf == NULL) {
        return A_NO_MEMORY;
    }

    A_NETBUF_PUT(osbuf, sizeof(*cmd));

    cmd = (WMI_IBSS_PM_CAPS_CMD *)(A_NETBUF_DATA(osbuf));
    A_MEMZERO(cmd, sizeof(*cmd));
    cmd->power_saving = pmEnable;
    cmd->ttl = ttl;
    cmd->atim_windows = atim_windows;
    cmd->timeout_value = timeout_value;

    return (wmi_cmd_send(wmip, osbuf, WMI_SET_IBSS_PM_CAPS_CMDID,
                         NO_SYNC_WMIFLAG));
}

int
wmi_apps_cmd(struct wmi_t *wmip, u8 psType, u32 idle_time,
                   u32 ps_period, u8 sleep_period)
{
    void *osbuf;
    WMI_AP_PS_CMD *cmd;

    osbuf = A_NETBUF_ALLOC(sizeof(*cmd));
    if (osbuf == NULL) {
        return A_NO_MEMORY;
    }

    A_NETBUF_PUT(osbuf, sizeof(*cmd));

    cmd = (WMI_AP_PS_CMD *)(A_NETBUF_DATA(osbuf));
    A_MEMZERO(cmd, sizeof(*cmd));
    cmd->psType = psType;
    cmd->idle_time = idle_time;
    cmd->ps_period = ps_period;
    cmd->sleep_period = sleep_period;

    return (wmi_cmd_send(wmip, osbuf, WMI_SET_AP_PS_CMDID,
                         NO_SYNC_WMIFLAG));
}

int
wmi_pmparams_cmd(struct wmi_t *wmip, u16 idlePeriod,
                 u16 psPollNum, u16 dtimPolicy,
                 u16 tx_wakeup_policy, u16 num_tx_to_wakeup,
                 u16 ps_fail_event_policy)
{
    void *osbuf;
    WMI_POWER_PARAMS_CMD *pm;

    osbuf = A_NETBUF_ALLOC(sizeof(*pm));
    if (osbuf == NULL) {
        return A_NO_MEMORY;
    }

    A_NETBUF_PUT(osbuf, sizeof(*pm));

    pm = (WMI_POWER_PARAMS_CMD *)(A_NETBUF_DATA(osbuf));
    A_MEMZERO(pm, sizeof(*pm));
    pm->idle_period   = idlePeriod;
    pm->pspoll_number = psPollNum;
    pm->dtim_policy   = dtimPolicy;
    pm->tx_wakeup_policy = tx_wakeup_policy;
    pm->num_tx_to_wakeup = num_tx_to_wakeup;
    pm->ps_fail_event_policy = ps_fail_event_policy;

    return (wmi_cmd_send(wmip, osbuf, WMI_SET_POWER_PARAMS_CMDID,
                         NO_SYNC_WMIFLAG));
}

int
wmi_disctimeout_cmd(struct wmi_t *wmip, u8 timeout)
{
    void *osbuf;
    WMI_DISC_TIMEOUT_CMD *cmd;

    osbuf = A_NETBUF_ALLOC(sizeof(*cmd));
    if (osbuf == NULL) {
        return A_NO_MEMORY;
    }

    A_NETBUF_PUT(osbuf, sizeof(*cmd));

    cmd = (WMI_DISC_TIMEOUT_CMD *)(A_NETBUF_DATA(osbuf));
    A_MEMZERO(cmd, sizeof(*cmd));
    cmd->disconnectTimeout = timeout;

    return (wmi_cmd_send(wmip, osbuf, WMI_SET_DISC_TIMEOUT_CMDID,
                         NO_SYNC_WMIFLAG));
}

int
wmi_addKey_cmd(struct wmi_t *wmip, u8 keyIndex, CRYPTO_TYPE keyType,
               u8 keyUsage, u8 keyLength, u8 *keyRSC,
               u8 *keyMaterial, u8 key_op_ctrl, u8 *macAddr,
               WMI_SYNC_FLAG sync_flag)
{
    void *osbuf;
    WMI_ADD_CIPHER_KEY_CMD *cmd;

    if ((keyIndex > WMI_MAX_KEY_INDEX) || (keyLength > WMI_MAX_KEY_LEN) ||
        (keyMaterial == NULL))
    {
        return A_EINVAL;
    }

    if ((WEP_CRYPT != keyType) && (NULL == keyRSC)) {
        return A_EINVAL;
    }

    osbuf = A_NETBUF_ALLOC(sizeof(*cmd));
    if (osbuf == NULL) {
        return A_NO_MEMORY;
    }

    A_NETBUF_PUT(osbuf, sizeof(*cmd));

    cmd = (WMI_ADD_CIPHER_KEY_CMD *)(A_NETBUF_DATA(osbuf));
    A_MEMZERO(cmd, sizeof(*cmd));
    cmd->keyIndex = keyIndex;
    cmd->keyType  = keyType;
    cmd->keyUsage = keyUsage;
    cmd->keyLength = keyLength;
    memcpy(cmd->key, keyMaterial, keyLength);
#ifdef WAPI_ENABLE
    if (NULL != keyRSC && key_op_ctrl != KEY_OP_INIT_WAPIPN) {
#else
    if (NULL != keyRSC) {
#endif // WAPI_ENABLE
        memcpy(cmd->keyRSC, keyRSC, sizeof(cmd->keyRSC));
    }
    cmd->key_op_ctrl = key_op_ctrl;

    if(macAddr) {
        memcpy(cmd->key_macaddr,macAddr,IEEE80211_ADDR_LEN);
    }

    return (wmi_cmd_send(wmip, osbuf, WMI_ADD_CIPHER_KEY_CMDID, sync_flag));
}

int
wmi_add_krk_cmd(struct wmi_t *wmip, u8 *krk)
{
    void *osbuf;
    WMI_ADD_KRK_CMD *cmd;

    osbuf = A_NETBUF_ALLOC(sizeof(*cmd));
    if (osbuf == NULL) {
        return A_NO_MEMORY;
    }

    A_NETBUF_PUT(osbuf, sizeof(*cmd));

    cmd = (WMI_ADD_KRK_CMD *)(A_NETBUF_DATA(osbuf));
    A_MEMZERO(cmd, sizeof(*cmd));
    memcpy(cmd->krk, krk, WMI_KRK_LEN);

    return (wmi_cmd_send(wmip, osbuf, WMI_ADD_KRK_CMDID, NO_SYNC_WMIFLAG));
}

int
wmi_delete_krk_cmd(struct wmi_t *wmip)
{
    return wmi_simple_cmd(wmip, WMI_DELETE_KRK_CMDID);
}

int
wmi_deleteKey_cmd(struct wmi_t *wmip, u8 keyIndex)
{
    void *osbuf;
    WMI_DELETE_CIPHER_KEY_CMD *cmd;

    if (keyIndex > WMI_MAX_KEY_INDEX) {
        return A_EINVAL;
    }

    osbuf = A_NETBUF_ALLOC(sizeof(*cmd));
    if (osbuf == NULL) {
        return A_NO_MEMORY;
    }

    A_NETBUF_PUT(osbuf, sizeof(*cmd));

    cmd = (WMI_DELETE_CIPHER_KEY_CMD *)(A_NETBUF_DATA(osbuf));
    A_MEMZERO(cmd, sizeof(*cmd));
    cmd->keyIndex = keyIndex;

    return (wmi_cmd_send(wmip, osbuf, WMI_DELETE_CIPHER_KEY_CMDID,
                         NO_SYNC_WMIFLAG));
}

int
wmi_setPmkid_cmd(struct wmi_t *wmip, u8 *bssid, u8 *pmkId,
                 bool set)
{
    void *osbuf;
    WMI_SET_PMKID_CMD *cmd;

    if (bssid == NULL) {
        return A_EINVAL;
    }

    if ((set == true) && (pmkId == NULL)) {
        return A_EINVAL;
    }

    osbuf = A_NETBUF_ALLOC(sizeof(*cmd));
    if (osbuf == NULL) {
        return A_NO_MEMORY;
    }

    A_NETBUF_PUT(osbuf, sizeof(*cmd));

    cmd = (WMI_SET_PMKID_CMD *)(A_NETBUF_DATA(osbuf));
    memcpy(cmd->bssid, bssid, sizeof(cmd->bssid));
    if (set == true) {
        memcpy(cmd->pmkid, pmkId, sizeof(cmd->pmkid));
        cmd->enable = PMKID_ENABLE;
    } else {
        A_MEMZERO(cmd->pmkid, sizeof(cmd->pmkid));
        cmd->enable = PMKID_DISABLE;
    }

    return (wmi_cmd_send(wmip, osbuf, WMI_SET_PMKID_CMDID, NO_SYNC_WMIFLAG));
}

int
wmi_set_tkip_countermeasures_cmd(struct wmi_t *wmip, bool en)
{
    void *osbuf;
    WMI_SET_TKIP_COUNTERMEASURES_CMD *cmd;

    osbuf = A_NETBUF_ALLOC(sizeof(*cmd));
    if (osbuf == NULL) {
        return A_NO_MEMORY;
    }

    A_NETBUF_PUT(osbuf, sizeof(*cmd));

    cmd = (WMI_SET_TKIP_COUNTERMEASURES_CMD *)(A_NETBUF_DATA(osbuf));
    cmd->cm_en = (en == true)? WMI_TKIP_CM_ENABLE : WMI_TKIP_CM_DISABLE;

    return (wmi_cmd_send(wmip, osbuf, WMI_SET_TKIP_COUNTERMEASURES_CMDID,
            NO_SYNC_WMIFLAG));
}

int
wmi_set_akmp_params_cmd(struct wmi_t *wmip,
                        WMI_SET_AKMP_PARAMS_CMD *akmpParams)
{
    void *osbuf;
    WMI_SET_AKMP_PARAMS_CMD *cmd;

    osbuf = A_NETBUF_ALLOC(sizeof(*cmd));
    if (osbuf == NULL) {
        return A_NO_MEMORY;
    }

    A_NETBUF_PUT(osbuf, sizeof(*cmd));
    cmd = (WMI_SET_AKMP_PARAMS_CMD *)(A_NETBUF_DATA(osbuf));
    cmd->akmpInfo = akmpParams->akmpInfo;

    return (wmi_cmd_send(wmip, osbuf, WMI_SET_AKMP_PARAMS_CMDID,
            NO_SYNC_WMIFLAG));
}

int
wmi_set_pmkid_list_cmd(struct wmi_t *wmip,
                       WMI_SET_PMKID_LIST_CMD *pmkInfo)
{
    void *osbuf;
    WMI_SET_PMKID_LIST_CMD *cmd;
    u16 cmdLen;
    u8 i;

    cmdLen = sizeof(pmkInfo->numPMKID) +
             pmkInfo->numPMKID * sizeof(WMI_PMKID);

    osbuf = A_NETBUF_ALLOC(cmdLen);
    if (osbuf == NULL) {
        return A_NO_MEMORY;
    }

    A_NETBUF_PUT(osbuf, cmdLen);
    cmd = (WMI_SET_PMKID_LIST_CMD *)(A_NETBUF_DATA(osbuf));
    cmd->numPMKID = pmkInfo->numPMKID;

    for (i = 0; i < cmd->numPMKID; i++) {
        memcpy(&cmd->pmkidList[i], &pmkInfo->pmkidList[i],
                 WMI_PMKID_LEN);
    }

    return (wmi_cmd_send(wmip, osbuf, WMI_SET_PMKID_LIST_CMDID,
            NO_SYNC_WMIFLAG));
}

int
wmi_get_pmkid_list_cmd(struct wmi_t *wmip)
{
    return wmi_simple_cmd(wmip, WMI_GET_PMKID_LIST_CMDID);
}

int
wmi_dataSync_send(struct wmi_t *wmip, void *osbuf, HTC_ENDPOINT_ID eid)
{
    WMI_DATA_HDR     *dtHdr;

    A_ASSERT( eid != wmip->wmi_endpoint_id);
    A_ASSERT(osbuf != NULL);

    if (A_NETBUF_PUSH(osbuf, sizeof(WMI_DATA_HDR)) != 0) {
        return A_NO_MEMORY;
    }

    dtHdr = (WMI_DATA_HDR *)A_NETBUF_DATA(osbuf);
    dtHdr->info =
      (SYNC_MSGTYPE & WMI_DATA_HDR_MSG_TYPE_MASK) << WMI_DATA_HDR_MSG_TYPE_SHIFT;

    A_DPRINTF(DBG_WMI, (DBGFMT "Enter - eid %d\n", DBGARG, eid));

    return (A_WMI_CONTROL_TX(wmip->wmi_devt, osbuf, eid));
}

typedef struct _WMI_DATA_SYNC_BUFS {
    u8 trafficClass;
    void               *osbuf;
}WMI_DATA_SYNC_BUFS;

static int
wmi_sync_point(struct wmi_t *wmip)
{
    void *cmd_osbuf;
    WMI_SYNC_CMD *cmd;
    WMI_DATA_SYNC_BUFS dataSyncBufs[WMM_NUM_AC];
    u8 i,numPriStreams=0;
    int status = 0;

    A_DPRINTF(DBG_WMI, (DBGFMT "Enter\n", DBGARG));

    memset(dataSyncBufs,0,sizeof(dataSyncBufs));

    /* lock out while we walk through the priority list and assemble our local array */
    LOCK_WMI(wmip);

    for (i=0; i < WMM_NUM_AC ; i++) {
        if (wmip->wmi_fatPipeExists & (1 << i)) {
            numPriStreams++;
            dataSyncBufs[numPriStreams-1].trafficClass = i;
        }
    }

    UNLOCK_WMI(wmip);

    /* dataSyncBufs is now filled with entries (starting at index 0) containing valid streamIDs */

    do {
        /*
         * We allocate all network buffers needed so we will be able to
         * send all required frames.
         */
        cmd_osbuf = A_NETBUF_ALLOC(sizeof(*cmd));
        if (cmd_osbuf == NULL) {
            status = A_NO_MEMORY;
            break;
    }

        A_NETBUF_PUT(cmd_osbuf, sizeof(*cmd));

        cmd = (WMI_SYNC_CMD *)(A_NETBUF_DATA(cmd_osbuf));
        A_MEMZERO(cmd, sizeof(*cmd));

        /* In the SYNC cmd sent on the control Ep, send a bitmap of the data
         * eps on which the Data Sync will be sent
         */
        cmd->dataSyncMap = wmip->wmi_fatPipeExists;

        for (i=0; i < numPriStreams ; i++) {
            dataSyncBufs[i].osbuf = A_NETBUF_ALLOC(0);
            if (dataSyncBufs[i].osbuf == NULL) {
                status = A_NO_MEMORY;
                break;
            }
        } //end for

        /* if Buffer allocation for any of the dataSync fails, then do not
         * send the Synchronize cmd on the control ep
         */
        if (status) {
            break;
        }

    /*
     * Send sync cmd followed by sync data messages on all endpoints being
     * used
     */
    status = wmi_cmd_send(wmip, cmd_osbuf, WMI_SYNCHRONIZE_CMDID,
                          NO_SYNC_WMIFLAG);

        if (status) {
            break;
    }
            /* cmd buffer sent, we no longer own it */
        cmd_osbuf = NULL;

        for(i=0; i < numPriStreams; i++) {
            A_ASSERT(dataSyncBufs[i].osbuf != NULL);
            status = wmi_dataSync_send(wmip,
                                       dataSyncBufs[i].osbuf,
                                       A_WMI_Ac2EndpointID(wmip->wmi_devt,
                                                            dataSyncBufs[i].
                                                            trafficClass)
                                      );

            if (status) {
                break;
            }
            /* we don't own this buffer anymore, NULL it out of the array so it
             * won't get cleaned up */
            dataSyncBufs[i].osbuf = NULL;
        } //end for

    } while(false);

    /* free up any resources left over (possibly due to an error) */

    if (cmd_osbuf != NULL) {
        A_NETBUF_FREE(cmd_osbuf);
            }

    for (i = 0; i < numPriStreams; i++) {
        if (dataSyncBufs[i].osbuf != NULL) {
            A_NETBUF_FREE(dataSyncBufs[i].osbuf);
        }
    }

    return (status);
}

int
wmi_create_pstream_cmd(struct wmi_t *wmip, WMI_CREATE_PSTREAM_CMD *params)
{
    void *osbuf;
    WMI_CREATE_PSTREAM_CMD *cmd;
    u8 fatPipeExistsForAC=0;
    s32 minimalPHY = 0;
    s32 nominalPHY = 0;

    /* Validate all the parameters. */
    if( !((params->userPriority < 8) &&
         (params->userPriority <= 0x7) &&
         (convert_userPriority_to_trafficClass(params->userPriority) == params->trafficClass)  &&
         (params->trafficDirection == UPLINK_TRAFFIC ||
            params->trafficDirection == DNLINK_TRAFFIC ||
            params->trafficDirection == BIDIR_TRAFFIC) &&
         (params->trafficType == TRAFFIC_TYPE_APERIODIC ||
            params->trafficType == TRAFFIC_TYPE_PERIODIC ) &&
         (params->voicePSCapability == DISABLE_FOR_THIS_AC  ||
            params->voicePSCapability == ENABLE_FOR_THIS_AC ||
            params->voicePSCapability == ENABLE_FOR_ALL_AC) &&
         (params->tsid == WMI_IMPLICIT_PSTREAM || params->tsid <= WMI_MAX_THINSTREAM)) )
    {
        return  A_EINVAL;
    }

    //
    // check nominal PHY rate is >= minimalPHY, so that DUT
    // can allow TSRS IE
    //

    // get the physical rate
    minimalPHY = ((params->minPhyRate / 1000)/1000); // unit of bps

    // check minimal phy < nominal phy rate
    //
    if (params->nominalPHY >= minimalPHY)
    {
        nominalPHY = (params->nominalPHY * 1000)/500; // unit of 500 kbps
        A_DPRINTF(DBG_WMI,
                  (DBGFMT "TSRS IE Enabled::MinPhy %x->NominalPhy ===> %x\n", DBGARG,
                  minimalPHY, nominalPHY));

        params->nominalPHY = nominalPHY;
    }
    else
    {
        params->nominalPHY = 0;
    }

    osbuf = A_NETBUF_ALLOC(sizeof(*cmd));
    if (osbuf == NULL) {
        return A_NO_MEMORY;
    }

    A_NETBUF_PUT(osbuf, sizeof(*cmd));

    A_DPRINTF(DBG_WMI,
        (DBGFMT "Sending create_pstream_cmd: ac=%d    tsid:%d\n", DBGARG,
        params->trafficClass, params->tsid));

    cmd = (WMI_CREATE_PSTREAM_CMD *)(A_NETBUF_DATA(osbuf));
    A_MEMZERO(cmd, sizeof(*cmd));
    memcpy(cmd, params, sizeof(*cmd));

        /* this is an implicitly created Fat pipe */
    if ((u32)params->tsid == (u32)WMI_IMPLICIT_PSTREAM) {
        LOCK_WMI(wmip);
        fatPipeExistsForAC = (wmip->wmi_fatPipeExists & (1 << params->trafficClass));
        wmip->wmi_fatPipeExists |= (1<<params->trafficClass);
        UNLOCK_WMI(wmip);
    } else {
            /* this is an explicitly created thin stream within a fat pipe */
    LOCK_WMI(wmip);
        fatPipeExistsForAC = (wmip->wmi_fatPipeExists & (1 << params->trafficClass));
    wmip->wmi_streamExistsForAC[params->trafficClass] |= (1<<params->tsid);
            /* if a thinstream becomes active, the fat pipe automatically
            * becomes active
            */
        wmip->wmi_fatPipeExists |= (1<<params->trafficClass);
    UNLOCK_WMI(wmip);
    }

        /* Indicate activty change to driver layer only if this is the
         * first TSID to get created in this AC explicitly or an implicit
         * fat pipe is getting created.
         */
    if (!fatPipeExistsForAC) {
        A_WMI_STREAM_TX_ACTIVE(wmip->wmi_devt, params->trafficClass);
    }

    /* mike: should be SYNC_BEFORE_WMIFLAG */
    return (wmi_cmd_send(wmip, osbuf, WMI_CREATE_PSTREAM_CMDID,
                         NO_SYNC_WMIFLAG));
}

int
wmi_delete_pstream_cmd(struct wmi_t *wmip, u8 trafficClass, u8 tsid)
{
    void *osbuf;
    WMI_DELETE_PSTREAM_CMD *cmd;
    int status;
    u16 activeTsids=0;

    /* validate the parameters */
    if (trafficClass > 3) {
        A_DPRINTF(DBG_WMI, (DBGFMT "Invalid trafficClass: %d\n", DBGARG, trafficClass));
        return A_EINVAL;
    }

    osbuf = A_NETBUF_ALLOC(sizeof(*cmd));
    if (osbuf == NULL) {
        return A_NO_MEMORY;
    }

    A_NETBUF_PUT(osbuf, sizeof(*cmd));

    cmd = (WMI_DELETE_PSTREAM_CMD *)(A_NETBUF_DATA(osbuf));
    A_MEMZERO(cmd, sizeof(*cmd));

    cmd->trafficClass = trafficClass;
    cmd->tsid = tsid;

    LOCK_WMI(wmip);
    activeTsids = wmip->wmi_streamExistsForAC[trafficClass];
    UNLOCK_WMI(wmip);

        /* Check if the tsid was created & exists */
    if (!(activeTsids & (1<<tsid))) {

        A_NETBUF_FREE(osbuf);
        A_DPRINTF(DBG_WMI,
        (DBGFMT "TSID %d does'nt exist for trafficClass: %d\n", DBGARG, tsid, trafficClass));
            /* TODO: return a more appropriate err code */
        return A_ERROR;
    }

    A_DPRINTF(DBG_WMI,
        (DBGFMT "Sending delete_pstream_cmd: trafficClass: %d tsid=%d\n", DBGARG, trafficClass, tsid));

    status = (wmi_cmd_send(wmip, osbuf, WMI_DELETE_PSTREAM_CMDID,
                         SYNC_BEFORE_WMIFLAG));

    LOCK_WMI(wmip);
    wmip->wmi_streamExistsForAC[trafficClass] &= ~(1<<tsid);
    activeTsids = wmip->wmi_streamExistsForAC[trafficClass];
    UNLOCK_WMI(wmip);


        /* Indicate stream inactivity to driver layer only if all tsids
         * within this AC are deleted.
         */
    if(!activeTsids) {
        A_WMI_STREAM_TX_INACTIVE(wmip->wmi_devt, trafficClass);
        wmip->wmi_fatPipeExists &= ~(1<<trafficClass);
    }

    return status;
}

int
wmi_set_framerate_cmd(struct wmi_t *wmip, u8 bEnable, u8 type, u8 subType, u16 rateMask)
{
    void *osbuf;
    WMI_FRAME_RATES_CMD *cmd;
    u8 frameType;

    A_DPRINTF(DBG_WMI,
        (DBGFMT " type %02X, subType %02X, rateMask %04x\n", DBGARG, type, subType, rateMask));

    if((type != IEEE80211_FRAME_TYPE_MGT && type != IEEE80211_FRAME_TYPE_CTL) ||
        (subType > 15)){

        return A_EINVAL;
    }

    osbuf = A_NETBUF_ALLOC(sizeof(*cmd));
    if (osbuf == NULL) {
        return A_NO_MEMORY;
    }

    A_NETBUF_PUT(osbuf, sizeof(*cmd));

    cmd = (WMI_FRAME_RATES_CMD *)(A_NETBUF_DATA(osbuf));
    A_MEMZERO(cmd, sizeof(*cmd));

    frameType = (u8)((subType << 4) | type);

    cmd->bEnableMask = bEnable;
    cmd->frameType = frameType;
    cmd->frameRateMask = rateMask;

    return (wmi_cmd_send(wmip, osbuf, WMI_SET_FRAMERATES_CMDID, NO_SYNC_WMIFLAG));
}

/*
 * used to set the bit rate.  rate is in Kbps.  If rate == -1
 * then auto selection is used.
 */
int
wmi_set_bitrate_cmd(struct wmi_t *wmip, s32 dataRate, s32 mgmtRate, s32 ctlRate)
{
    void *osbuf;
    WMI_BIT_RATE_CMD *cmd;
    s8 drix, mrix, crix, ret_val;

    if (dataRate != -1) {
        ret_val = wmi_validate_bitrate(wmip, dataRate, &drix);
        if(ret_val == A_EINVAL){
            return A_EINVAL;
        }
    } else {
        drix = -1;
    }

    if (mgmtRate != -1) {
        ret_val = wmi_validate_bitrate(wmip, mgmtRate, &mrix);
        if(ret_val == A_EINVAL){
            return A_EINVAL;
        }
    } else {
        mrix = -1;
    }
    if (ctlRate != -1) {
        ret_val = wmi_validate_bitrate(wmip, ctlRate, &crix);
        if(ret_val == A_EINVAL){
            return A_EINVAL;
        }
    } else {
        crix = -1;
    }
    osbuf = A_NETBUF_ALLOC(sizeof(*cmd));
    if (osbuf == NULL) {
        return A_NO_MEMORY;
    }

    A_NETBUF_PUT(osbuf, sizeof(*cmd));

    cmd = (WMI_BIT_RATE_CMD *)(A_NETBUF_DATA(osbuf));
    A_MEMZERO(cmd, sizeof(*cmd));

    cmd->rateIndex = drix;
    cmd->mgmtRateIndex = mrix;
    cmd->ctlRateIndex  = crix;


    return (wmi_cmd_send(wmip, osbuf, WMI_SET_BITRATE_CMDID, NO_SYNC_WMIFLAG));
}

int
wmi_get_bitrate_cmd(struct wmi_t *wmip)
{
    return wmi_simple_cmd(wmip, WMI_GET_BITRATE_CMDID);
}

/*
 * Returns true iff the given rate index is legal in the current PHY mode.
 */
bool
wmi_is_bitrate_index_valid(struct wmi_t *wmip, s32 rateIndex)
{
    WMI_PHY_MODE phyMode = (WMI_PHY_MODE) wmip->wmi_phyMode;
    bool isValid = true;
    switch(phyMode) {
        case WMI_11A_MODE:
            if (wmip->wmi_ht_allowed[A_BAND_5GHZ]){
                if ((rateIndex < MODE_A_SUPPORT_RATE_START) || (rateIndex > MODE_GHT20_SUPPORT_RATE_STOP)) {
                    isValid = false;
                }
            } else {
                if ((rateIndex < MODE_A_SUPPORT_RATE_START) || (rateIndex > MODE_A_SUPPORT_RATE_STOP)) {
                    isValid = false;
                }
            }
            break;

        case WMI_11B_MODE:
            if ((rateIndex < MODE_B_SUPPORT_RATE_START) || (rateIndex > MODE_B_SUPPORT_RATE_STOP)) {
                isValid = false;
            }
            break;

        case WMI_11GONLY_MODE:
            if (wmip->wmi_ht_allowed[A_BAND_24GHZ]){
                if ((rateIndex < MODE_GONLY_SUPPORT_RATE_START) || (rateIndex > MODE_GHT20_SUPPORT_RATE_STOP)) {
                    isValid = false;
                }
            } else {
                if ((rateIndex < MODE_GONLY_SUPPORT_RATE_START) || (rateIndex > MODE_GONLY_SUPPORT_RATE_STOP)) {
                    isValid = false;
                }
            }
            break;

        case WMI_11G_MODE:
        case WMI_11AG_MODE:
            if (wmip->wmi_ht_allowed[A_BAND_24GHZ]){
                if ((rateIndex < MODE_G_SUPPORT_RATE_START) || (rateIndex > MODE_GHT20_SUPPORT_RATE_STOP)) {
                    isValid = false;
                }
            } else {
                if ((rateIndex < MODE_G_SUPPORT_RATE_START) || (rateIndex > MODE_G_SUPPORT_RATE_STOP)) {
                    isValid = false;
                }
            }
            break;
        default:
            A_ASSERT(false);
            break;
    }

    return isValid;
}

s8 wmi_validate_bitrate(struct wmi_t *wmip, s32 rate, s8 *rate_idx)
{
    s8 i;

    for (i=0;;i++)
    {
        if (wmi_rateTable[(u32) i][0] == 0) {
            return A_EINVAL;
        }
        if (wmi_rateTable[(u32) i][0] == rate) {
            break;
        }
    }

    if(wmi_is_bitrate_index_valid(wmip, (s32) i) != true) {
        return A_EINVAL;
    }

    *rate_idx = i;
    return 0;
}

int
wmi_set_fixrates_cmd(struct wmi_t *wmip, u32 fixRatesMask)
{
    void *osbuf;
    WMI_FIX_RATES_CMD *cmd;
#if 0
    s32 rateIndex;
/* This check does not work for AR6003 as the HT modes are enabled only when
 * the STA is connected to a HT_BSS and is not based only on channel. It is
 * safe to skip this check however because rate control will only use rates
 * that are permitted by the valid rate mask and the fix rate mask. Meaning
 * the fix rate mask is not sufficient by itself to cause an invalid rate
 * to be used. */
    /* Make sure all rates in the mask are valid in the current PHY mode */
    for(rateIndex = 0; rateIndex < MAX_NUMBER_OF_SUPPORT_RATES; rateIndex++) {
       if((1 << rateIndex) & (u32)fixRatesMask) {
            if(wmi_is_bitrate_index_valid(wmip, rateIndex) != true) {
                A_DPRINTF(DBG_WMI, (DBGFMT "Set Fix Rates command failed: Given rate is illegal in current PHY mode\n", DBGARG));
                return A_EINVAL;
            }
       }
    }
#endif


    osbuf = A_NETBUF_ALLOC(sizeof(*cmd));
    if (osbuf == NULL) {
        return A_NO_MEMORY;
    }

    A_NETBUF_PUT(osbuf, sizeof(*cmd));

    cmd = (WMI_FIX_RATES_CMD *)(A_NETBUF_DATA(osbuf));
    A_MEMZERO(cmd, sizeof(*cmd));

    cmd->fixRateMask = fixRatesMask;

    return (wmi_cmd_send(wmip, osbuf, WMI_SET_FIXRATES_CMDID, NO_SYNC_WMIFLAG));
}

int
wmi_get_ratemask_cmd(struct wmi_t *wmip)
{
    return wmi_simple_cmd(wmip, WMI_GET_FIXRATES_CMDID);
}

int
wmi_get_channelList_cmd(struct wmi_t *wmip)
{
    return wmi_simple_cmd(wmip, WMI_GET_CHANNEL_LIST_CMDID);
}

/*
 * used to generate a wmi sey channel Parameters cmd.
 * mode should always be specified and corresponds to the phy mode of the
 * wlan.
 * numChan should alway sbe specified. If zero indicates that all available
 * channels should be used.
 * channelList is an array of channel frequencies (in Mhz) which the radio
 * should limit its operation to.  It should be NULL if numChan == 0.  Size of
 * array should correspond to numChan entries.
 */
int
wmi_set_channelParams_cmd(struct wmi_t *wmip, u8 scanParam,
                          WMI_PHY_MODE mode, s8 numChan,
                          u16 *channelList)
{
    void *osbuf;
    WMI_CHANNEL_PARAMS_CMD *cmd;
    s8 size;

    size = sizeof (*cmd);

    if (numChan) {
        if (numChan > WMI_MAX_CHANNELS) {
            return A_EINVAL;
        }
        size += sizeof(u16) * (numChan - 1);
    }

    osbuf = A_NETBUF_ALLOC(size);
    if (osbuf == NULL) {
        return A_NO_MEMORY;
    }

    A_NETBUF_PUT(osbuf, size);

    cmd = (WMI_CHANNEL_PARAMS_CMD *)(A_NETBUF_DATA(osbuf));
    A_MEMZERO(cmd, size);

    wmip->wmi_phyMode = mode;
    cmd->scanParam   = scanParam;
    cmd->phyMode     = mode;
    cmd->numChannels = numChan;
    memcpy(cmd->channelList, channelList, numChan * sizeof(u16));

    return (wmi_cmd_send(wmip, osbuf, WMI_SET_CHANNEL_PARAMS_CMDID,
                         NO_SYNC_WMIFLAG));
}

void
wmi_cache_configure_rssithreshold(struct wmi_t *wmip, WMI_RSSI_THRESHOLD_PARAMS_CMD *rssiCmd)
{
    SQ_THRESHOLD_PARAMS *sq_thresh =
           &wmip->wmi_SqThresholdParams[SIGNAL_QUALITY_METRICS_RSSI];
    /*
     * Parse the command and store the threshold values here. The checks
     * for valid values can be put here
     */
    sq_thresh->weight = rssiCmd->weight;
    sq_thresh->polling_interval = rssiCmd->pollTime;

    sq_thresh->upper_threshold[0] = rssiCmd->thresholdAbove1_Val - SIGNAL_QUALITY_NOISE_FLOOR;
    sq_thresh->upper_threshold[1] = rssiCmd->thresholdAbove2_Val - SIGNAL_QUALITY_NOISE_FLOOR;
    sq_thresh->upper_threshold[2] = rssiCmd->thresholdAbove3_Val - SIGNAL_QUALITY_NOISE_FLOOR;
    sq_thresh->upper_threshold[3] = rssiCmd->thresholdAbove4_Val - SIGNAL_QUALITY_NOISE_FLOOR;
    sq_thresh->upper_threshold[4] = rssiCmd->thresholdAbove5_Val - SIGNAL_QUALITY_NOISE_FLOOR;
    sq_thresh->upper_threshold[5] = rssiCmd->thresholdAbove6_Val - SIGNAL_QUALITY_NOISE_FLOOR;
    sq_thresh->upper_threshold_valid_count = 6;

    /* List sorted in descending order */
    sq_thresh->lower_threshold[0] = rssiCmd->thresholdBelow6_Val - SIGNAL_QUALITY_NOISE_FLOOR;
    sq_thresh->lower_threshold[1] = rssiCmd->thresholdBelow5_Val - SIGNAL_QUALITY_NOISE_FLOOR;
    sq_thresh->lower_threshold[2] = rssiCmd->thresholdBelow4_Val - SIGNAL_QUALITY_NOISE_FLOOR;
    sq_thresh->lower_threshold[3] = rssiCmd->thresholdBelow3_Val - SIGNAL_QUALITY_NOISE_FLOOR;
    sq_thresh->lower_threshold[4] = rssiCmd->thresholdBelow2_Val - SIGNAL_QUALITY_NOISE_FLOOR;
    sq_thresh->lower_threshold[5] = rssiCmd->thresholdBelow1_Val - SIGNAL_QUALITY_NOISE_FLOOR;
    sq_thresh->lower_threshold_valid_count = 6;

    if (!rssi_event_value) {
    /*
     * Configuring the thresholds to their extremes allows the host to get an
     * event from the target which is used for the configuring the correct
     * thresholds
     */
    rssiCmd->thresholdAbove1_Val = sq_thresh->upper_threshold[0];
    rssiCmd->thresholdBelow1_Val = sq_thresh->lower_threshold[0];
    } else {
        /*
         * In case the user issues multiple times of rssi_threshold_setting,
         * we should not use the extreames anymore, the target does not expect that.
         */
        rssiCmd->thresholdAbove1_Val = ar6000_get_upper_threshold(rssi_event_value, sq_thresh,
                                              sq_thresh->upper_threshold_valid_count);
        rssiCmd->thresholdBelow1_Val = ar6000_get_lower_threshold(rssi_event_value, sq_thresh,
                                              sq_thresh->lower_threshold_valid_count);
}
}

int
wmi_set_rssi_threshold_params(struct wmi_t *wmip,
                              WMI_RSSI_THRESHOLD_PARAMS_CMD *rssiCmd)
{

     /* Check these values are in ascending order */
    if( rssiCmd->thresholdAbove6_Val <= rssiCmd->thresholdAbove5_Val ||
        rssiCmd->thresholdAbove5_Val <= rssiCmd->thresholdAbove4_Val ||
        rssiCmd->thresholdAbove4_Val <= rssiCmd->thresholdAbove3_Val ||
        rssiCmd->thresholdAbove3_Val <= rssiCmd->thresholdAbove2_Val ||
        rssiCmd->thresholdAbove2_Val <= rssiCmd->thresholdAbove1_Val ||
        rssiCmd->thresholdBelow6_Val <= rssiCmd->thresholdBelow5_Val ||
        rssiCmd->thresholdBelow5_Val <= rssiCmd->thresholdBelow4_Val ||
        rssiCmd->thresholdBelow4_Val <= rssiCmd->thresholdBelow3_Val ||
        rssiCmd->thresholdBelow3_Val <= rssiCmd->thresholdBelow2_Val ||
        rssiCmd->thresholdBelow2_Val <= rssiCmd->thresholdBelow1_Val)
    {
        return A_EINVAL;
    }

    wmi_cache_configure_rssithreshold(wmip, rssiCmd);

    return (wmi_send_rssi_threshold_params(wmip, rssiCmd));
}

int
wmi_set_ip_cmd(struct wmi_t *wmip, WMI_SET_IP_CMD *ipCmd)
{
    void    *osbuf;
    WMI_SET_IP_CMD *cmd;

    /* Multicast address are not valid */
    if((*((u8 *)&ipCmd->ips[0]) >= 0xE0) ||
       (*((u8 *)&ipCmd->ips[1]) >= 0xE0)) {
        return A_EINVAL;
    }

    osbuf = A_NETBUF_ALLOC(sizeof(WMI_SET_IP_CMD));
    if (osbuf == NULL) {
        return A_NO_MEMORY;
    }

    A_NETBUF_PUT(osbuf, sizeof(WMI_SET_IP_CMD));
    cmd = (WMI_SET_IP_CMD *)(A_NETBUF_DATA(osbuf));
    memcpy(cmd, ipCmd, sizeof(WMI_SET_IP_CMD));

    return (wmi_cmd_send(wmip, osbuf, WMI_SET_IP_CMDID,
                            NO_SYNC_WMIFLAG));
}

int
wmi_set_host_sleep_mode_cmd(struct wmi_t *wmip,
                              WMI_SET_HOST_SLEEP_MODE_CMD *hostModeCmd)
{
    void    *osbuf;
    s8 size;
    WMI_SET_HOST_SLEEP_MODE_CMD *cmd;
    u16 activeTsids=0;
    u8 streamExists=0;
    u8 i;

    if( hostModeCmd->awake == hostModeCmd->asleep) {
        return A_EINVAL;
    }

    size = sizeof (*cmd);

    osbuf = A_NETBUF_ALLOC(size);
    if (osbuf == NULL) {
        return A_NO_MEMORY;
    }

    A_NETBUF_PUT(osbuf, size);

    cmd = (WMI_SET_HOST_SLEEP_MODE_CMD *)(A_NETBUF_DATA(osbuf));
    A_MEMZERO(cmd, size);
    memcpy(cmd, hostModeCmd, sizeof(WMI_SET_HOST_SLEEP_MODE_CMD));

    if(hostModeCmd->asleep) {
        /*
         * Relinquish credits from all implicitly created pstreams since when we
         * go to sleep. If user created explicit thinstreams exists with in a
         * fatpipe leave them intact for the user to delete
         */
        LOCK_WMI(wmip);
        streamExists = wmip->wmi_fatPipeExists;
        UNLOCK_WMI(wmip);

        for(i=0;i< WMM_NUM_AC;i++) {
            if (streamExists & (1<<i)) {
                LOCK_WMI(wmip);
                activeTsids = wmip->wmi_streamExistsForAC[i];
                UNLOCK_WMI(wmip);
                /* If there are no user created thin streams delete the fatpipe */
                if(!activeTsids) {
                    streamExists &= ~(1<<i);
                    /*Indicate inactivity to drv layer for this fatpipe(pstream)*/
                    A_WMI_STREAM_TX_INACTIVE(wmip->wmi_devt,i);
                }
            }
        }

        /* Update the fatpipes that exists*/
        LOCK_WMI(wmip);
        wmip->wmi_fatPipeExists = streamExists;
        UNLOCK_WMI(wmip);
    }

    return (wmi_cmd_send(wmip, osbuf, WMI_SET_HOST_SLEEP_MODE_CMDID,
                            NO_SYNC_WMIFLAG));
}

int
wmi_set_wow_mode_cmd(struct wmi_t *wmip,
                              WMI_SET_WOW_MODE_CMD *wowModeCmd)
{
    void    *osbuf;
    s8 size;
    WMI_SET_WOW_MODE_CMD *cmd;

    size = sizeof (*cmd);

    osbuf = A_NETBUF_ALLOC(size);
    if (osbuf == NULL) {
        return A_NO_MEMORY;
    }

    A_NETBUF_PUT(osbuf, size);

    cmd = (WMI_SET_WOW_MODE_CMD *)(A_NETBUF_DATA(osbuf));
    A_MEMZERO(cmd, size);
    memcpy(cmd, wowModeCmd, sizeof(WMI_SET_WOW_MODE_CMD));

    return (wmi_cmd_send(wmip, osbuf, WMI_SET_WOW_MODE_CMDID,
                            NO_SYNC_WMIFLAG));

}

int
wmi_get_wow_list_cmd(struct wmi_t *wmip,
                              WMI_GET_WOW_LIST_CMD *wowListCmd)
{
    void    *osbuf;
    s8 size;
    WMI_GET_WOW_LIST_CMD *cmd;

    size = sizeof (*cmd);

    osbuf = A_NETBUF_ALLOC(size);
    if (osbuf == NULL) {
        return A_NO_MEMORY;
    }

    A_NETBUF_PUT(osbuf, size);

    cmd = (WMI_GET_WOW_LIST_CMD *)(A_NETBUF_DATA(osbuf));
    A_MEMZERO(cmd, size);
    memcpy(cmd, wowListCmd, sizeof(WMI_GET_WOW_LIST_CMD));

    return (wmi_cmd_send(wmip, osbuf, WMI_GET_WOW_LIST_CMDID,
                            NO_SYNC_WMIFLAG));

}

static int
wmi_get_wow_list_event_rx(struct wmi_t *wmip, u8 *datap, int len)
{
    WMI_GET_WOW_LIST_REPLY *reply;

    if (len < sizeof(WMI_GET_WOW_LIST_REPLY)) {
        return A_EINVAL;
    }
    reply = (WMI_GET_WOW_LIST_REPLY *)datap;

    A_WMI_WOW_LIST_EVENT(wmip->wmi_devt, reply->num_filters,
                          reply);

    return 0;
}

int wmi_add_wow_pattern_cmd(struct wmi_t *wmip,
                                 WMI_ADD_WOW_PATTERN_CMD *addWowCmd,
                                 u8 *pattern, u8 *mask,
                                 u8 pattern_size)
{
    void    *osbuf;
    s8 size;
    WMI_ADD_WOW_PATTERN_CMD *cmd;
    u8 *filter_mask = NULL;

    size = sizeof (*cmd);

    size += ((2 * addWowCmd->filter_size)* sizeof(u8));
    osbuf = A_NETBUF_ALLOC(size);
    if (osbuf == NULL) {
        return A_NO_MEMORY;
    }

    A_NETBUF_PUT(osbuf, size);

    cmd = (WMI_ADD_WOW_PATTERN_CMD *)(A_NETBUF_DATA(osbuf));
    cmd->filter_list_id = addWowCmd->filter_list_id;
    cmd->filter_offset = addWowCmd->filter_offset;
    cmd->filter_size = addWowCmd->filter_size;

    memcpy(cmd->filter, pattern, addWowCmd->filter_size);

    filter_mask = (u8 *)(cmd->filter + cmd->filter_size);
    memcpy(filter_mask, mask, addWowCmd->filter_size);


    return (wmi_cmd_send(wmip, osbuf, WMI_ADD_WOW_PATTERN_CMDID,
                            NO_SYNC_WMIFLAG));
}

int
wmi_del_wow_pattern_cmd(struct wmi_t *wmip,
                              WMI_DEL_WOW_PATTERN_CMD *delWowCmd)
{
    void    *osbuf;
    s8 size;
    WMI_DEL_WOW_PATTERN_CMD *cmd;

    size = sizeof (*cmd);

    osbuf = A_NETBUF_ALLOC(size);
    if (osbuf == NULL) {
        return A_NO_MEMORY;
    }

    A_NETBUF_PUT(osbuf, size);

    cmd = (WMI_DEL_WOW_PATTERN_CMD *)(A_NETBUF_DATA(osbuf));
    A_MEMZERO(cmd, size);
    memcpy(cmd, delWowCmd, sizeof(WMI_DEL_WOW_PATTERN_CMD));

    return (wmi_cmd_send(wmip, osbuf, WMI_DEL_WOW_PATTERN_CMDID,
                            NO_SYNC_WMIFLAG));

}

void
wmi_cache_configure_snrthreshold(struct wmi_t *wmip, WMI_SNR_THRESHOLD_PARAMS_CMD *snrCmd)
{
    SQ_THRESHOLD_PARAMS *sq_thresh =
           &wmip->wmi_SqThresholdParams[SIGNAL_QUALITY_METRICS_SNR];
    /*
     * Parse the command and store the threshold values here. The checks
     * for valid values can be put here
     */
    sq_thresh->weight = snrCmd->weight;
    sq_thresh->polling_interval = snrCmd->pollTime;

    sq_thresh->upper_threshold[0] = snrCmd->thresholdAbove1_Val;
    sq_thresh->upper_threshold[1] = snrCmd->thresholdAbove2_Val;
    sq_thresh->upper_threshold[2] = snrCmd->thresholdAbove3_Val;
    sq_thresh->upper_threshold[3] = snrCmd->thresholdAbove4_Val;
    sq_thresh->upper_threshold_valid_count = 4;

    /* List sorted in descending order */
    sq_thresh->lower_threshold[0] = snrCmd->thresholdBelow4_Val;
    sq_thresh->lower_threshold[1] = snrCmd->thresholdBelow3_Val;
    sq_thresh->lower_threshold[2] = snrCmd->thresholdBelow2_Val;
    sq_thresh->lower_threshold[3] = snrCmd->thresholdBelow1_Val;
    sq_thresh->lower_threshold_valid_count = 4;

    if (!snr_event_value) {
    /*
     * Configuring the thresholds to their extremes allows the host to get an
     * event from the target which is used for the configuring the correct
     * thresholds
     */
    snrCmd->thresholdAbove1_Val = (u8)sq_thresh->upper_threshold[0];
    snrCmd->thresholdBelow1_Val = (u8)sq_thresh->lower_threshold[0];
    } else {
        /*
         * In case the user issues multiple times of snr_threshold_setting,
         * we should not use the extreames anymore, the target does not expect that.
         */
        snrCmd->thresholdAbove1_Val = ar6000_get_upper_threshold(snr_event_value, sq_thresh,
                                              sq_thresh->upper_threshold_valid_count);
        snrCmd->thresholdBelow1_Val = ar6000_get_lower_threshold(snr_event_value, sq_thresh,
                                              sq_thresh->lower_threshold_valid_count);
    }

}
int
wmi_set_snr_threshold_params(struct wmi_t *wmip,
                             WMI_SNR_THRESHOLD_PARAMS_CMD *snrCmd)
{
    if( snrCmd->thresholdAbove4_Val <= snrCmd->thresholdAbove3_Val ||
        snrCmd->thresholdAbove3_Val <= snrCmd->thresholdAbove2_Val ||
        snrCmd->thresholdAbove2_Val <= snrCmd->thresholdAbove1_Val ||
        snrCmd->thresholdBelow4_Val <= snrCmd->thresholdBelow3_Val ||
        snrCmd->thresholdBelow3_Val <= snrCmd->thresholdBelow2_Val ||
        snrCmd->thresholdBelow2_Val <= snrCmd->thresholdBelow1_Val)
    {
        return A_EINVAL;
    }
    wmi_cache_configure_snrthreshold(wmip, snrCmd);
    return (wmi_send_snr_threshold_params(wmip, snrCmd));
}

int
wmi_clr_rssi_snr(struct wmi_t *wmip)
{
    void    *osbuf;

    osbuf = A_NETBUF_ALLOC(sizeof(int));
    if (osbuf == NULL) {
        return A_NO_MEMORY;
    }

    return (wmi_cmd_send(wmip, osbuf, WMI_CLR_RSSI_SNR_CMDID,
                            NO_SYNC_WMIFLAG));
}

int
wmi_set_lq_threshold_params(struct wmi_t *wmip,
                             WMI_LQ_THRESHOLD_PARAMS_CMD *lqCmd)
{
    void    *osbuf;
    s8 size;
    WMI_LQ_THRESHOLD_PARAMS_CMD *cmd;
    /* These values are in ascending order */
    if( lqCmd->thresholdAbove4_Val <= lqCmd->thresholdAbove3_Val ||
        lqCmd->thresholdAbove3_Val <= lqCmd->thresholdAbove2_Val ||
        lqCmd->thresholdAbove2_Val <= lqCmd->thresholdAbove1_Val ||
        lqCmd->thresholdBelow4_Val <= lqCmd->thresholdBelow3_Val ||
        lqCmd->thresholdBelow3_Val <= lqCmd->thresholdBelow2_Val ||
        lqCmd->thresholdBelow2_Val <= lqCmd->thresholdBelow1_Val ) {

        return A_EINVAL;
    }

    size = sizeof (*cmd);

    osbuf = A_NETBUF_ALLOC(size);
    if (osbuf == NULL) {
        return A_NO_MEMORY;
    }

    A_NETBUF_PUT(osbuf, size);

    cmd = (WMI_LQ_THRESHOLD_PARAMS_CMD *)(A_NETBUF_DATA(osbuf));
    A_MEMZERO(cmd, size);
    memcpy(cmd, lqCmd, sizeof(WMI_LQ_THRESHOLD_PARAMS_CMD));

    return (wmi_cmd_send(wmip, osbuf, WMI_LQ_THRESHOLD_PARAMS_CMDID,
                            NO_SYNC_WMIFLAG));
}

int
wmi_set_error_report_bitmask(struct wmi_t *wmip, u32 mask)
{
    void    *osbuf;
    s8 size;
    WMI_TARGET_ERROR_REPORT_BITMASK *cmd;

    size = sizeof (*cmd);

    osbuf = A_NETBUF_ALLOC(size);
    if (osbuf == NULL) {
        return A_NO_MEMORY;
    }

    A_NETBUF_PUT(osbuf, size);

    cmd = (WMI_TARGET_ERROR_REPORT_BITMASK *)(A_NETBUF_DATA(osbuf));
    A_MEMZERO(cmd, size);

    cmd->bitmask = mask;

    return (wmi_cmd_send(wmip, osbuf, WMI_TARGET_ERROR_REPORT_BITMASK_CMDID,
                            NO_SYNC_WMIFLAG));
}

int
wmi_get_challenge_resp_cmd(struct wmi_t *wmip, u32 cookie, u32 source)
{
    void *osbuf;
    WMIX_HB_CHALLENGE_RESP_CMD *cmd;

    osbuf = A_NETBUF_ALLOC(sizeof(*cmd));
    if (osbuf == NULL) {
        return A_NO_MEMORY;
    }

    A_NETBUF_PUT(osbuf, sizeof(*cmd));

    cmd = (WMIX_HB_CHALLENGE_RESP_CMD *)(A_NETBUF_DATA(osbuf));
    cmd->cookie = cookie;
    cmd->source = source;

    return (wmi_cmd_send_xtnd(wmip, osbuf, WMIX_HB_CHALLENGE_RESP_CMDID,
                              NO_SYNC_WMIFLAG));
}

int
wmi_config_debug_module_cmd(struct wmi_t *wmip, u16 mmask,
                            u16 tsr, bool rep, u16 size,
                            u32 valid)
{
    void *osbuf;
    WMIX_DBGLOG_CFG_MODULE_CMD *cmd;

    osbuf = A_NETBUF_ALLOC(sizeof(*cmd));
    if (osbuf == NULL) {
        return A_NO_MEMORY;
    }

    A_NETBUF_PUT(osbuf, sizeof(*cmd));

    cmd = (WMIX_DBGLOG_CFG_MODULE_CMD *)(A_NETBUF_DATA(osbuf));
    cmd->config.cfgmmask = mmask;
    cmd->config.cfgtsr = tsr;
    cmd->config.cfgrep = rep;
    cmd->config.cfgsize = size;
    cmd->config.cfgvalid = valid;

    return (wmi_cmd_send_xtnd(wmip, osbuf, WMIX_DBGLOG_CFG_MODULE_CMDID,
                              NO_SYNC_WMIFLAG));
}

int
wmi_get_stats_cmd(struct wmi_t *wmip)
{
    return wmi_simple_cmd(wmip, WMI_GET_STATISTICS_CMDID);
}

int
wmi_addBadAp_cmd(struct wmi_t *wmip, u8 apIndex, u8 *bssid)
{
    void *osbuf;
    WMI_ADD_BAD_AP_CMD *cmd;

    if ((bssid == NULL) || (apIndex > WMI_MAX_BAD_AP_INDEX)) {
        return A_EINVAL;
    }

    osbuf = A_NETBUF_ALLOC(sizeof(*cmd));
    if (osbuf == NULL) {
        return A_NO_MEMORY;
    }

    A_NETBUF_PUT(osbuf, sizeof(*cmd));

    cmd = (WMI_ADD_BAD_AP_CMD *)(A_NETBUF_DATA(osbuf));
    cmd->badApIndex = apIndex;
    memcpy(cmd->bssid, bssid, sizeof(cmd->bssid));

    return (wmi_cmd_send(wmip, osbuf, WMI_ADD_BAD_AP_CMDID, SYNC_BEFORE_WMIFLAG));
}

int
wmi_deleteBadAp_cmd(struct wmi_t *wmip, u8 apIndex)
{
    void *osbuf;
    WMI_DELETE_BAD_AP_CMD *cmd;

    if (apIndex > WMI_MAX_BAD_AP_INDEX) {
        return A_EINVAL;
    }

    osbuf = A_NETBUF_ALLOC(sizeof(*cmd));
    if (osbuf == NULL) {
        return A_NO_MEMORY;
    }

    A_NETBUF_PUT(osbuf, sizeof(*cmd));

    cmd = (WMI_DELETE_BAD_AP_CMD *)(A_NETBUF_DATA(osbuf));
    cmd->badApIndex = apIndex;

    return (wmi_cmd_send(wmip, osbuf, WMI_DELETE_BAD_AP_CMDID,
                         NO_SYNC_WMIFLAG));
}

int
wmi_abort_scan_cmd(struct wmi_t *wmip)
{
    return wmi_simple_cmd(wmip, WMI_ABORT_SCAN_CMDID);
}

int
wmi_set_txPwr_cmd(struct wmi_t *wmip, u8 dbM)
{
    void *osbuf;
    WMI_SET_TX_PWR_CMD *cmd;

    osbuf = A_NETBUF_ALLOC(sizeof(*cmd));
    if (osbuf == NULL) {
        return A_NO_MEMORY;
    }

    A_NETBUF_PUT(osbuf, sizeof(*cmd));

    cmd = (WMI_SET_TX_PWR_CMD *)(A_NETBUF_DATA(osbuf));
    cmd->dbM = dbM;

    return (wmi_cmd_send(wmip, osbuf, WMI_SET_TX_PWR_CMDID, NO_SYNC_WMIFLAG));
}

int
wmi_get_txPwr_cmd(struct wmi_t *wmip)
{
    return wmi_simple_cmd(wmip, WMI_GET_TX_PWR_CMDID);
}

u16 wmi_get_mapped_qos_queue(struct wmi_t *wmip, u8 trafficClass)
{
    u16 activeTsids=0;

    LOCK_WMI(wmip);
    activeTsids = wmip->wmi_streamExistsForAC[trafficClass];
    UNLOCK_WMI(wmip);

    return activeTsids;
}

int
wmi_get_roam_tbl_cmd(struct wmi_t *wmip)
{
    return wmi_simple_cmd(wmip, WMI_GET_ROAM_TBL_CMDID);
}

int
wmi_get_roam_data_cmd(struct wmi_t *wmip, u8 roamDataType)
{
    void *osbuf;
    u32 size = sizeof(u8);
    WMI_TARGET_ROAM_DATA *cmd;

    osbuf = A_NETBUF_ALLOC(size);      /* no payload */
    if (osbuf == NULL) {
        return A_NO_MEMORY;
    }

    A_NETBUF_PUT(osbuf, size);

    cmd = (WMI_TARGET_ROAM_DATA *)(A_NETBUF_DATA(osbuf));
    cmd->roamDataType = roamDataType;

    return (wmi_cmd_send(wmip, osbuf, WMI_GET_ROAM_DATA_CMDID,
                         NO_SYNC_WMIFLAG));
}

int
wmi_set_roam_ctrl_cmd(struct wmi_t *wmip, WMI_SET_ROAM_CTRL_CMD *p,
                      u8 size)
{
    void *osbuf;
    WMI_SET_ROAM_CTRL_CMD *cmd;

    osbuf = A_NETBUF_ALLOC(size);
    if (osbuf == NULL) {
        return A_NO_MEMORY;
    }

    A_NETBUF_PUT(osbuf, size);

    cmd = (WMI_SET_ROAM_CTRL_CMD *)(A_NETBUF_DATA(osbuf));
    A_MEMZERO(cmd, size);

    memcpy(cmd, p, size);

    return (wmi_cmd_send(wmip, osbuf, WMI_SET_ROAM_CTRL_CMDID,
                         NO_SYNC_WMIFLAG));
}

int
wmi_set_powersave_timers_cmd(struct wmi_t *wmip,
                            WMI_POWERSAVE_TIMERS_POLICY_CMD *pCmd,
                            u8 size)
{
    void *osbuf;
    WMI_POWERSAVE_TIMERS_POLICY_CMD *cmd;

    /* These timers can't be zero */
    if(!pCmd->psPollTimeout || !pCmd->triggerTimeout ||
       !(pCmd->apsdTimPolicy == IGNORE_TIM_ALL_QUEUES_APSD ||
         pCmd->apsdTimPolicy == PROCESS_TIM_ALL_QUEUES_APSD) ||
       !(pCmd->simulatedAPSDTimPolicy == IGNORE_TIM_SIMULATED_APSD ||
         pCmd->simulatedAPSDTimPolicy == PROCESS_TIM_SIMULATED_APSD))
        return A_EINVAL;

    osbuf = A_NETBUF_ALLOC(size);
    if (osbuf == NULL) {
        return A_NO_MEMORY;
    }

    A_NETBUF_PUT(osbuf, size);

    cmd = (WMI_POWERSAVE_TIMERS_POLICY_CMD *)(A_NETBUF_DATA(osbuf));
    A_MEMZERO(cmd, size);

    memcpy(cmd, pCmd, size);

    return (wmi_cmd_send(wmip, osbuf, WMI_SET_POWERSAVE_TIMERS_POLICY_CMDID,
                         NO_SYNC_WMIFLAG));
}

#ifdef CONFIG_HOST_GPIO_SUPPORT
/* Send a command to Target to change GPIO output pins. */
int
wmi_gpio_output_set(struct wmi_t *wmip,
                    u32 set_mask,
                    u32 clear_mask,
                    u32 enable_mask,
                    u32 disable_mask)
{
    void *osbuf;
    WMIX_GPIO_OUTPUT_SET_CMD *output_set;
    int size;

    size = sizeof(*output_set);

    A_DPRINTF(DBG_WMI,
        (DBGFMT "Enter - set=0x%x clear=0x%x enb=0x%x dis=0x%x\n", DBGARG,
        set_mask, clear_mask, enable_mask, disable_mask));

    osbuf = A_NETBUF_ALLOC(size);
    if (osbuf == NULL) {
        return A_NO_MEMORY;
    }
    A_NETBUF_PUT(osbuf, size);
    output_set = (WMIX_GPIO_OUTPUT_SET_CMD *)(A_NETBUF_DATA(osbuf));

    output_set->set_mask                   = set_mask;
    output_set->clear_mask                 = clear_mask;
    output_set->enable_mask                = enable_mask;
    output_set->disable_mask               = disable_mask;

    return (wmi_cmd_send_xtnd(wmip, osbuf, WMIX_GPIO_OUTPUT_SET_CMDID,
                             NO_SYNC_WMIFLAG));
}

/* Send a command to the Target requesting state of the GPIO input pins */
int
wmi_gpio_input_get(struct wmi_t *wmip)
{
    A_DPRINTF(DBG_WMI, (DBGFMT "Enter\n", DBGARG));

    return wmi_simple_cmd_xtnd(wmip, WMIX_GPIO_INPUT_GET_CMDID);
}

/* Send a command to the Target that changes the value of a GPIO register. */
int
wmi_gpio_register_set(struct wmi_t *wmip,
                      u32 gpioreg_id,
                      u32 value)
{
    void *osbuf;
    WMIX_GPIO_REGISTER_SET_CMD *register_set;
    int size;

    size = sizeof(*register_set);

    A_DPRINTF(DBG_WMI,
        (DBGFMT "Enter - reg=%d value=0x%x\n", DBGARG, gpioreg_id, value));

    osbuf = A_NETBUF_ALLOC(size);
    if (osbuf == NULL) {
        return A_NO_MEMORY;
    }
    A_NETBUF_PUT(osbuf, size);
    register_set = (WMIX_GPIO_REGISTER_SET_CMD *)(A_NETBUF_DATA(osbuf));

    register_set->gpioreg_id               = gpioreg_id;
    register_set->value                    = value;

    return (wmi_cmd_send_xtnd(wmip, osbuf, WMIX_GPIO_REGISTER_SET_CMDID,
                             NO_SYNC_WMIFLAG));
}

/* Send a command to the Target to fetch the value of a GPIO register. */
int
wmi_gpio_register_get(struct wmi_t *wmip,
                      u32 gpioreg_id)
{
    void *osbuf;
    WMIX_GPIO_REGISTER_GET_CMD *register_get;
    int size;

    size = sizeof(*register_get);

    A_DPRINTF(DBG_WMI, (DBGFMT "Enter - reg=%d\n", DBGARG, gpioreg_id));

    osbuf = A_NETBUF_ALLOC(size);
    if (osbuf == NULL) {
        return A_NO_MEMORY;
    }
    A_NETBUF_PUT(osbuf, size);
    register_get = (WMIX_GPIO_REGISTER_GET_CMD *)(A_NETBUF_DATA(osbuf));

    register_get->gpioreg_id               = gpioreg_id;

    return (wmi_cmd_send_xtnd(wmip, osbuf, WMIX_GPIO_REGISTER_GET_CMDID,
                             NO_SYNC_WMIFLAG));
}

/* Send a command to the Target acknowledging some GPIO interrupts. */
int
wmi_gpio_intr_ack(struct wmi_t *wmip,
                  u32 ack_mask)
{
    void *osbuf;
    WMIX_GPIO_INTR_ACK_CMD *intr_ack;
    int size;

    size = sizeof(*intr_ack);

    A_DPRINTF(DBG_WMI, (DBGFMT "Enter ack_mask=0x%x\n", DBGARG, ack_mask));

    osbuf = A_NETBUF_ALLOC(size);
    if (osbuf == NULL) {
        return A_NO_MEMORY;
    }
    A_NETBUF_PUT(osbuf, size);
    intr_ack = (WMIX_GPIO_INTR_ACK_CMD *)(A_NETBUF_DATA(osbuf));

    intr_ack->ack_mask               = ack_mask;

    return (wmi_cmd_send_xtnd(wmip, osbuf, WMIX_GPIO_INTR_ACK_CMDID,
                             NO_SYNC_WMIFLAG));
}
#endif /* CONFIG_HOST_GPIO_SUPPORT */

int
wmi_set_access_params_cmd(struct wmi_t *wmip, u8 ac,  u16 txop, u8 eCWmin,
                          u8 eCWmax, u8 aifsn)
{
    void *osbuf;
    WMI_SET_ACCESS_PARAMS_CMD *cmd;

    if ((eCWmin > WMI_MAX_CW_ACPARAM) || (eCWmax > WMI_MAX_CW_ACPARAM) ||
        (aifsn > WMI_MAX_AIFSN_ACPARAM) || (ac >= WMM_NUM_AC))
    {
        return A_EINVAL;
    }

    osbuf = A_NETBUF_ALLOC(sizeof(*cmd));
    if (osbuf == NULL) {
        return A_NO_MEMORY;
    }

    A_NETBUF_PUT(osbuf, sizeof(*cmd));

    cmd = (WMI_SET_ACCESS_PARAMS_CMD *)(A_NETBUF_DATA(osbuf));
    cmd->txop   = txop;
    cmd->eCWmin = eCWmin;
    cmd->eCWmax = eCWmax;
    cmd->aifsn  = aifsn;
    cmd->ac = ac;

    return (wmi_cmd_send(wmip, osbuf, WMI_SET_ACCESS_PARAMS_CMDID,
                         NO_SYNC_WMIFLAG));
}

int
wmi_set_retry_limits_cmd(struct wmi_t *wmip, u8 frameType,
                         u8 trafficClass, u8 maxRetries,
                         u8 enableNotify)
{
    void *osbuf;
    WMI_SET_RETRY_LIMITS_CMD *cmd;

    if ((frameType != MGMT_FRAMETYPE) && (frameType != CONTROL_FRAMETYPE) &&
        (frameType != DATA_FRAMETYPE))
    {
        return A_EINVAL;
    }

    if (maxRetries > WMI_MAX_RETRIES) {
        return A_EINVAL;
    }

    if (frameType != DATA_FRAMETYPE) {
        trafficClass = 0;
    }

    osbuf = A_NETBUF_ALLOC(sizeof(*cmd));
    if (osbuf == NULL) {
        return A_NO_MEMORY;
    }

    A_NETBUF_PUT(osbuf, sizeof(*cmd));

    cmd = (WMI_SET_RETRY_LIMITS_CMD *)(A_NETBUF_DATA(osbuf));
    cmd->frameType    = frameType;
    cmd->trafficClass = trafficClass;
    cmd->maxRetries   = maxRetries;
    cmd->enableNotify = enableNotify;

    return (wmi_cmd_send(wmip, osbuf, WMI_SET_RETRY_LIMITS_CMDID,
                         NO_SYNC_WMIFLAG));
}

void
wmi_get_current_bssid(struct wmi_t *wmip, u8 *bssid)
{
    if (bssid != NULL) {
        memcpy(bssid, wmip->wmi_bssid, ATH_MAC_LEN);
    }
}

int
wmi_set_opt_mode_cmd(struct wmi_t *wmip, u8 optMode)
{
    void *osbuf;
    WMI_SET_OPT_MODE_CMD *cmd;

    osbuf = A_NETBUF_ALLOC(sizeof(*cmd));
    if (osbuf == NULL) {
        return A_NO_MEMORY;
    }

    A_NETBUF_PUT(osbuf, sizeof(*cmd));

    cmd = (WMI_SET_OPT_MODE_CMD *)(A_NETBUF_DATA(osbuf));
    A_MEMZERO(cmd, sizeof(*cmd));
    cmd->optMode = optMode;

    return (wmi_cmd_send(wmip, osbuf, WMI_SET_OPT_MODE_CMDID,
                         SYNC_BOTH_WMIFLAG));
}

int
wmi_opt_tx_frame_cmd(struct wmi_t *wmip,
                      u8 frmType,
                      u8 *dstMacAddr,
                      u8 *bssid,
                      u16 optIEDataLen,
                      u8 *optIEData)
{
    void *osbuf;
    WMI_OPT_TX_FRAME_CMD *cmd;
    osbuf = A_NETBUF_ALLOC(optIEDataLen + sizeof(*cmd));
    if (osbuf == NULL) {
        return A_NO_MEMORY;
    }

    A_NETBUF_PUT(osbuf, (optIEDataLen + sizeof(*cmd)));

    cmd = (WMI_OPT_TX_FRAME_CMD *)(A_NETBUF_DATA(osbuf));
    A_MEMZERO(cmd, (optIEDataLen + sizeof(*cmd)-1));

    cmd->frmType    = frmType;
    cmd->optIEDataLen   = optIEDataLen;
    //cmd->optIEData     = (u8 *)((int)cmd + sizeof(*cmd));
    memcpy(cmd->bssid, bssid, sizeof(cmd->bssid));
    memcpy(cmd->dstAddr, dstMacAddr, sizeof(cmd->dstAddr));
    memcpy(&cmd->optIEData[0], optIEData, optIEDataLen);

    return (wmi_cmd_send(wmip, osbuf, WMI_OPT_TX_FRAME_CMDID,
                         NO_SYNC_WMIFLAG));
}

int
wmi_set_adhoc_bconIntvl_cmd(struct wmi_t *wmip, u16 intvl)
{
    void *osbuf;
    WMI_BEACON_INT_CMD *cmd;

    osbuf = A_NETBUF_ALLOC(sizeof(*cmd));
    if (osbuf == NULL) {
        return A_NO_MEMORY;
    }

    A_NETBUF_PUT(osbuf, sizeof(*cmd));

    cmd = (WMI_BEACON_INT_CMD *)(A_NETBUF_DATA(osbuf));
    A_MEMZERO(cmd, sizeof(*cmd));
    cmd->beaconInterval = intvl;

    return (wmi_cmd_send(wmip, osbuf, WMI_SET_BEACON_INT_CMDID,
            NO_SYNC_WMIFLAG));
}


int
wmi_set_voice_pkt_size_cmd(struct wmi_t *wmip, u16 voicePktSize)
{
    void *osbuf;
    WMI_SET_VOICE_PKT_SIZE_CMD *cmd;

    osbuf = A_NETBUF_ALLOC(sizeof(*cmd));
    if (osbuf == NULL) {
        return A_NO_MEMORY;
    }

    A_NETBUF_PUT(osbuf, sizeof(*cmd));

    cmd = (WMI_SET_VOICE_PKT_SIZE_CMD *)(A_NETBUF_DATA(osbuf));
    A_MEMZERO(cmd, sizeof(*cmd));
    cmd->voicePktSize = voicePktSize;

    return (wmi_cmd_send(wmip, osbuf, WMI_SET_VOICE_PKT_SIZE_CMDID,
            NO_SYNC_WMIFLAG));
}


int
wmi_set_max_sp_len_cmd(struct wmi_t *wmip, u8 maxSPLen)
{
    void *osbuf;
    WMI_SET_MAX_SP_LEN_CMD *cmd;

    /* maxSPLen is a two-bit value. If user trys to set anything
     * other than this, then its invalid
     */
    if(maxSPLen & ~0x03)
        return  A_EINVAL;

    osbuf = A_NETBUF_ALLOC(sizeof(*cmd));
    if (osbuf == NULL) {
        return A_NO_MEMORY;
    }

    A_NETBUF_PUT(osbuf, sizeof(*cmd));

    cmd = (WMI_SET_MAX_SP_LEN_CMD *)(A_NETBUF_DATA(osbuf));
    A_MEMZERO(cmd, sizeof(*cmd));
    cmd->maxSPLen = maxSPLen;

    return (wmi_cmd_send(wmip, osbuf, WMI_SET_MAX_SP_LEN_CMDID,
            NO_SYNC_WMIFLAG));
}

u8 wmi_determine_userPriority(
    u8 *pkt,
    u32 layer2Pri)
{
    u8 ipPri;
    iphdr *ipHdr = (iphdr *)pkt;

    /* Determine IPTOS priority */
    /*
     * IP Tos format :
     *      (Refer Pg 57 WMM-test-plan-v1.2)
     * IP-TOS - 8bits
     *          : DSCP(6-bits) ECN(2-bits)
     *          : DSCP - P2 P1 P0 X X X
     *              where (P2 P1 P0) form 802.1D
     */
    ipPri = ipHdr->ip_tos >> 5;
    ipPri &= 0x7;

    if ((layer2Pri & 0x7) > ipPri)
        return ((u8)layer2Pri & 0x7);
    else
        return ipPri;
}

u8 convert_userPriority_to_trafficClass(u8 userPriority)
{
    return  (up_to_ac[userPriority & 0x7]);
}

u8 wmi_get_power_mode_cmd(struct wmi_t *wmip)
{
    return wmip->wmi_powerMode;
}

int
wmi_verify_tspec_params(WMI_CREATE_PSTREAM_CMD *pCmd, int tspecCompliance)
{
    int ret = 0;

#define TSPEC_SUSPENSION_INTERVAL_ATHEROS_DEF (~0)
#define TSPEC_SERVICE_START_TIME_ATHEROS_DEF  0
#define TSPEC_MAX_BURST_SIZE_ATHEROS_DEF      0
#define TSPEC_DELAY_BOUND_ATHEROS_DEF         0
#define TSPEC_MEDIUM_TIME_ATHEROS_DEF         0
#define TSPEC_SBA_ATHEROS_DEF                 0x2000  /* factor is 1 */

    /* Verify TSPEC params for ATHEROS compliance */
    if(tspecCompliance == ATHEROS_COMPLIANCE) {
        if ((pCmd->suspensionInt != TSPEC_SUSPENSION_INTERVAL_ATHEROS_DEF) ||
            (pCmd->serviceStartTime != TSPEC_SERVICE_START_TIME_ATHEROS_DEF) ||
            (pCmd->minDataRate != pCmd->meanDataRate) ||
            (pCmd->minDataRate != pCmd->peakDataRate) ||
            (pCmd->maxBurstSize != TSPEC_MAX_BURST_SIZE_ATHEROS_DEF) ||
            (pCmd->delayBound != TSPEC_DELAY_BOUND_ATHEROS_DEF) ||
            (pCmd->sba != TSPEC_SBA_ATHEROS_DEF) ||
            (pCmd->mediumTime != TSPEC_MEDIUM_TIME_ATHEROS_DEF)) {

            A_DPRINTF(DBG_WMI, (DBGFMT "Invalid TSPEC params\n", DBGARG));
            //A_PRINTF("%s: Invalid TSPEC params\n", __func__);
            ret = A_EINVAL;
        }
    }

    return ret;
}

#ifdef CONFIG_HOST_TCMD_SUPPORT
static int
wmi_tcmd_test_report_rx(struct wmi_t *wmip, u8 *datap, int len)
{

   A_DPRINTF(DBG_WMI, (DBGFMT "Enter\n", DBGARG));

   A_WMI_TCMD_RX_REPORT_EVENT(wmip->wmi_devt, datap, len);

   return 0;
}

#endif /* CONFIG_HOST_TCMD_SUPPORT*/

int
wmi_set_authmode_cmd(struct wmi_t *wmip, u8 mode)
{
    void *osbuf;
    WMI_SET_AUTH_MODE_CMD *cmd;

    osbuf = A_NETBUF_ALLOC(sizeof(*cmd));
    if (osbuf == NULL) {
        return A_NO_MEMORY;
    }

    A_NETBUF_PUT(osbuf, sizeof(*cmd));

    cmd = (WMI_SET_AUTH_MODE_CMD *)(A_NETBUF_DATA(osbuf));
    A_MEMZERO(cmd, sizeof(*cmd));
    cmd->mode = mode;

    return (wmi_cmd_send(wmip, osbuf, WMI_SET_AUTH_MODE_CMDID,
            NO_SYNC_WMIFLAG));
}

int
wmi_set_reassocmode_cmd(struct wmi_t *wmip, u8 mode)
{
    void *osbuf;
    WMI_SET_REASSOC_MODE_CMD *cmd;

    osbuf = A_NETBUF_ALLOC(sizeof(*cmd));
    if (osbuf == NULL) {
        return A_NO_MEMORY;
    }

    A_NETBUF_PUT(osbuf, sizeof(*cmd));

    cmd = (WMI_SET_REASSOC_MODE_CMD *)(A_NETBUF_DATA(osbuf));
    A_MEMZERO(cmd, sizeof(*cmd));
    cmd->mode = mode;

    return (wmi_cmd_send(wmip, osbuf, WMI_SET_REASSOC_MODE_CMDID,
            NO_SYNC_WMIFLAG));
}

int
wmi_set_lpreamble_cmd(struct wmi_t *wmip, u8 status, u8 preamblePolicy)
{
    void *osbuf;
    WMI_SET_LPREAMBLE_CMD *cmd;

    osbuf = A_NETBUF_ALLOC(sizeof(*cmd));
    if (osbuf == NULL) {
        return A_NO_MEMORY;
    }

    A_NETBUF_PUT(osbuf, sizeof(*cmd));

    cmd = (WMI_SET_LPREAMBLE_CMD *)(A_NETBUF_DATA(osbuf));
    A_MEMZERO(cmd, sizeof(*cmd));
    cmd->status = status;
    cmd->preamblePolicy = preamblePolicy;

    return (wmi_cmd_send(wmip, osbuf, WMI_SET_LPREAMBLE_CMDID,
            NO_SYNC_WMIFLAG));
}

int
wmi_set_rts_cmd(struct wmi_t *wmip, u16 threshold)
{
    void *osbuf;
    WMI_SET_RTS_CMD *cmd;

    osbuf = A_NETBUF_ALLOC(sizeof(*cmd));
    if (osbuf == NULL) {
        return A_NO_MEMORY;
    }

    A_NETBUF_PUT(osbuf, sizeof(*cmd));

    cmd = (WMI_SET_RTS_CMD*)(A_NETBUF_DATA(osbuf));
    A_MEMZERO(cmd, sizeof(*cmd));
    cmd->threshold = threshold;

    return (wmi_cmd_send(wmip, osbuf, WMI_SET_RTS_CMDID,
            NO_SYNC_WMIFLAG));
}

int
wmi_set_wmm_cmd(struct wmi_t *wmip, WMI_WMM_STATUS status)
{
    void *osbuf;
    WMI_SET_WMM_CMD *cmd;

    osbuf = A_NETBUF_ALLOC(sizeof(*cmd));
    if (osbuf == NULL) {
        return A_NO_MEMORY;
    }

    A_NETBUF_PUT(osbuf, sizeof(*cmd));

    cmd = (WMI_SET_WMM_CMD*)(A_NETBUF_DATA(osbuf));
    A_MEMZERO(cmd, sizeof(*cmd));
    cmd->status = status;

    return (wmi_cmd_send(wmip, osbuf, WMI_SET_WMM_CMDID,
            NO_SYNC_WMIFLAG));

}

int
wmi_set_qos_supp_cmd(struct wmi_t *wmip, u8 status)
{
    void *osbuf;
    WMI_SET_QOS_SUPP_CMD *cmd;

    osbuf = A_NETBUF_ALLOC(sizeof(*cmd));
    if (osbuf == NULL) {
        return A_NO_MEMORY;
    }

    A_NETBUF_PUT(osbuf, sizeof(*cmd));

    cmd = (WMI_SET_QOS_SUPP_CMD*)(A_NETBUF_DATA(osbuf));
    A_MEMZERO(cmd, sizeof(*cmd));
    cmd->status = status;
    return (wmi_cmd_send(wmip, osbuf, WMI_SET_QOS_SUPP_CMDID,
            NO_SYNC_WMIFLAG));
}


int
wmi_set_wmm_txop(struct wmi_t *wmip, WMI_TXOP_CFG cfg)
{
    void *osbuf;
    WMI_SET_WMM_TXOP_CMD *cmd;

    if( !((cfg == WMI_TXOP_DISABLED) || (cfg == WMI_TXOP_ENABLED)) )
        return A_EINVAL;

    osbuf = A_NETBUF_ALLOC(sizeof(*cmd));
    if (osbuf == NULL) {
        return A_NO_MEMORY;
    }

    A_NETBUF_PUT(osbuf, sizeof(*cmd));

    cmd = (WMI_SET_WMM_TXOP_CMD *)(A_NETBUF_DATA(osbuf));
    A_MEMZERO(cmd, sizeof(*cmd));
    cmd->txopEnable = cfg;

    return (wmi_cmd_send(wmip, osbuf, WMI_SET_WMM_TXOP_CMDID,
            NO_SYNC_WMIFLAG));

}

int
wmi_set_country(struct wmi_t *wmip, u8 *countryCode)
{
    void *osbuf;
    WMI_AP_SET_COUNTRY_CMD *cmd;

    osbuf = A_NETBUF_ALLOC(sizeof(*cmd));
    if (osbuf == NULL) {
        return A_NO_MEMORY;
    }

    A_NETBUF_PUT(osbuf, sizeof(*cmd));

    cmd = (WMI_AP_SET_COUNTRY_CMD *)(A_NETBUF_DATA(osbuf));
    A_MEMZERO(cmd, sizeof(*cmd));
    memcpy(cmd->countryCode,countryCode,3);

    return (wmi_cmd_send(wmip, osbuf, WMI_AP_SET_COUNTRY_CMDID,
            NO_SYNC_WMIFLAG));
}

#ifdef CONFIG_HOST_TCMD_SUPPORT
/* WMI  layer doesn't need to know the data type of the test cmd.
   This would be beneficial for customers like Qualcomm, who might
   have different test command requirements from different manufacturers
 */
int
wmi_test_cmd(struct wmi_t *wmip, u8 *buf, u32 len)
{
    void *osbuf;
    char *data;

    A_DPRINTF(DBG_WMI, (DBGFMT "Enter\n", DBGARG));

    osbuf= A_NETBUF_ALLOC(len);
    if(osbuf == NULL)
    {
        return A_NO_MEMORY;
    }
    A_NETBUF_PUT(osbuf, len);
    data = A_NETBUF_DATA(osbuf);
    memcpy(data, buf, len);

    return(wmi_cmd_send(wmip, osbuf, WMI_TEST_CMDID,
         NO_SYNC_WMIFLAG));
}

#endif

int
wmi_set_bt_status_cmd(struct wmi_t *wmip, u8 streamType, u8 status)
{
    void *osbuf;
    WMI_SET_BT_STATUS_CMD *cmd;

    AR_DEBUG_PRINTF(ATH_DEBUG_WARN, ("Enter - streamType=%d, status=%d\n", streamType, status));

    osbuf = A_NETBUF_ALLOC(sizeof(*cmd));
    if (osbuf == NULL) {
        return A_NO_MEMORY;
    }

    A_NETBUF_PUT(osbuf, sizeof(*cmd));

    cmd = (WMI_SET_BT_STATUS_CMD *)(A_NETBUF_DATA(osbuf));
    A_MEMZERO(cmd, sizeof(*cmd));
    cmd->streamType = streamType;
    cmd->status = status;

    return (wmi_cmd_send(wmip, osbuf, WMI_SET_BT_STATUS_CMDID,
            NO_SYNC_WMIFLAG));
}

int
wmi_set_bt_params_cmd(struct wmi_t *wmip, WMI_SET_BT_PARAMS_CMD* cmd)
{
    void *osbuf;
    WMI_SET_BT_PARAMS_CMD* alloc_cmd;

    AR_DEBUG_PRINTF(ATH_DEBUG_WARN, ("cmd params is %d\n", cmd->paramType));

    if (cmd->paramType == BT_PARAM_SCO) {
      AR_DEBUG_PRINTF(ATH_DEBUG_WARN, ("sco params %d %d %d %d %d %d %d %d %d %d %d %d\n", cmd->info.scoParams.numScoCyclesForceTrigger,
        cmd->info.scoParams.dataResponseTimeout,
        cmd->info.scoParams.stompScoRules,
        cmd->info.scoParams.scoOptFlags,
        cmd->info.scoParams.stompDutyCyleVal,
        cmd->info.scoParams.stompDutyCyleMaxVal,
        cmd->info.scoParams.psPollLatencyFraction,
        cmd->info.scoParams.noSCOSlots,
        cmd->info.scoParams.noIdleSlots,
        cmd->info.scoParams.scoOptOffRssi,
        cmd->info.scoParams.scoOptOnRssi,
        cmd->info.scoParams.scoOptRtsCount));
    }
    else if (cmd->paramType == BT_PARAM_A2DP) {
      AR_DEBUG_PRINTF(ATH_DEBUG_WARN, ("A2DP params %d %d %d %d %d %d %d %d\n", cmd->info.a2dpParams.a2dpWlanUsageLimit,
        cmd->info.a2dpParams.a2dpBurstCntMin,
        cmd->info.a2dpParams.a2dpDataRespTimeout,
        cmd->info.a2dpParams.a2dpOptFlags,
        cmd->info.a2dpParams.isCoLocatedBtRoleMaster,
        cmd->info.a2dpParams.a2dpOptOffRssi,
        cmd->info.a2dpParams.a2dpOptOnRssi,
        cmd->info.a2dpParams.a2dpOptRtsCount));
    }
    else if (cmd->paramType == BT_PARAM_ANTENNA_CONFIG) {
      AR_DEBUG_PRINTF(ATH_DEBUG_WARN, ("Ant config %d\n", cmd->info.antType));
    }
    else if (cmd->paramType == BT_PARAM_COLOCATED_BT_DEVICE) {
      AR_DEBUG_PRINTF(ATH_DEBUG_WARN, ("co-located BT %d\n", cmd->info.coLocatedBtDev));
    }
    else if (cmd->paramType == BT_PARAM_ACLCOEX) {
      AR_DEBUG_PRINTF(ATH_DEBUG_WARN, ("ACL params %d %d %d\n", cmd->info.aclCoexParams.aclWlanMediumUsageTime,
        cmd->info.aclCoexParams.aclBtMediumUsageTime,
        cmd->info.aclCoexParams.aclDataRespTimeout));
    }
    else if (cmd->paramType == BT_PARAM_11A_SEPARATE_ANT) {
      A_DPRINTF(DBG_WMI, (DBGFMT "11A ant\n", DBGARG));
    }

    osbuf = A_NETBUF_ALLOC(sizeof(*cmd));
    if (osbuf == NULL) {
        return A_NO_MEMORY;
    }

    A_NETBUF_PUT(osbuf, sizeof(*cmd));

    alloc_cmd = (WMI_SET_BT_PARAMS_CMD *)(A_NETBUF_DATA(osbuf));
    A_MEMZERO(alloc_cmd, sizeof(*cmd));
    memcpy(alloc_cmd, cmd, sizeof(*cmd));

    return (wmi_cmd_send(wmip, osbuf, WMI_SET_BT_PARAMS_CMDID,
            NO_SYNC_WMIFLAG));
}

int
wmi_set_btcoex_fe_ant_cmd(struct wmi_t *wmip, WMI_SET_BTCOEX_FE_ANT_CMD * cmd)
{
	void *osbuf;
    WMI_SET_BTCOEX_FE_ANT_CMD *alloc_cmd;

    osbuf = A_NETBUF_ALLOC(sizeof(*cmd));
    if (osbuf == NULL) {
        return A_NO_MEMORY;
    }
    A_NETBUF_PUT(osbuf, sizeof(*cmd));
    alloc_cmd = (WMI_SET_BTCOEX_FE_ANT_CMD *)(A_NETBUF_DATA(osbuf));
    A_MEMZERO(alloc_cmd, sizeof(*cmd));
	memcpy(alloc_cmd,cmd,sizeof(WMI_SET_BTCOEX_FE_ANT_CMD));
    return (wmi_cmd_send(wmip, osbuf, WMI_SET_BTCOEX_FE_ANT_CMDID,
                         NO_SYNC_WMIFLAG));

}


int
wmi_set_btcoex_colocated_bt_dev_cmd(struct wmi_t *wmip,
						WMI_SET_BTCOEX_COLOCATED_BT_DEV_CMD * cmd)
{
	void *osbuf;
    WMI_SET_BTCOEX_COLOCATED_BT_DEV_CMD *alloc_cmd;

    osbuf = A_NETBUF_ALLOC(sizeof(*cmd));
    if (osbuf == NULL) {
        return A_NO_MEMORY;
    }
    A_NETBUF_PUT(osbuf, sizeof(*cmd));
    alloc_cmd = (WMI_SET_BTCOEX_COLOCATED_BT_DEV_CMD *)(A_NETBUF_DATA(osbuf));
    A_MEMZERO(alloc_cmd, sizeof(*cmd));
    memcpy(alloc_cmd,cmd,sizeof(WMI_SET_BTCOEX_COLOCATED_BT_DEV_CMD));
    A_PRINTF("colocated bt = %d\n", alloc_cmd->btcoexCoLocatedBTdev);
    return (wmi_cmd_send(wmip, osbuf, WMI_SET_BTCOEX_COLOCATED_BT_DEV_CMDID,
                         NO_SYNC_WMIFLAG));

}

int
wmi_set_btcoex_btinquiry_page_config_cmd(struct wmi_t *wmip,
						WMI_SET_BTCOEX_BTINQUIRY_PAGE_CONFIG_CMD* cmd)
{
	void *osbuf;
    WMI_SET_BTCOEX_BTINQUIRY_PAGE_CONFIG_CMD *alloc_cmd;

    osbuf = A_NETBUF_ALLOC(sizeof(*cmd));
    if (osbuf == NULL) {
        return A_NO_MEMORY;
    }
    A_NETBUF_PUT(osbuf, sizeof(*cmd));
    alloc_cmd = (WMI_SET_BTCOEX_BTINQUIRY_PAGE_CONFIG_CMD *)(A_NETBUF_DATA(osbuf));
    A_MEMZERO(alloc_cmd, sizeof(*cmd));
	memcpy(alloc_cmd,cmd,sizeof(WMI_SET_BTCOEX_BTINQUIRY_PAGE_CONFIG_CMD));
    return (wmi_cmd_send(wmip, osbuf, WMI_SET_BTCOEX_BTINQUIRY_PAGE_CONFIG_CMDID,
                         NO_SYNC_WMIFLAG));

}

int
wmi_set_btcoex_sco_config_cmd(struct wmi_t *wmip,
						WMI_SET_BTCOEX_SCO_CONFIG_CMD * cmd)
{
	void *osbuf;
    WMI_SET_BTCOEX_SCO_CONFIG_CMD *alloc_cmd;

    osbuf = A_NETBUF_ALLOC(sizeof(*cmd));
    if (osbuf == NULL) {
        return A_NO_MEMORY;
    }
    A_NETBUF_PUT(osbuf, sizeof(*cmd));
    alloc_cmd = (WMI_SET_BTCOEX_SCO_CONFIG_CMD *)(A_NETBUF_DATA(osbuf));
    A_MEMZERO(alloc_cmd, sizeof(*cmd));
	memcpy(alloc_cmd,cmd,sizeof(WMI_SET_BTCOEX_SCO_CONFIG_CMD));
    return (wmi_cmd_send(wmip, osbuf, WMI_SET_BTCOEX_SCO_CONFIG_CMDID ,
                         NO_SYNC_WMIFLAG));

}

int
wmi_set_btcoex_a2dp_config_cmd(struct wmi_t *wmip,
						WMI_SET_BTCOEX_A2DP_CONFIG_CMD * cmd)
{
	void *osbuf;
    WMI_SET_BTCOEX_A2DP_CONFIG_CMD *alloc_cmd;

    osbuf = A_NETBUF_ALLOC(sizeof(*cmd));
    if (osbuf == NULL) {
        return A_NO_MEMORY;
    }
    A_NETBUF_PUT(osbuf, sizeof(*cmd));
    alloc_cmd = (WMI_SET_BTCOEX_A2DP_CONFIG_CMD *)(A_NETBUF_DATA(osbuf));
    A_MEMZERO(alloc_cmd, sizeof(*cmd));
	memcpy(alloc_cmd,cmd,sizeof(WMI_SET_BTCOEX_A2DP_CONFIG_CMD));
    return (wmi_cmd_send(wmip, osbuf, WMI_SET_BTCOEX_A2DP_CONFIG_CMDID ,
                         NO_SYNC_WMIFLAG));

}

int
wmi_set_btcoex_aclcoex_config_cmd(struct wmi_t *wmip,
						WMI_SET_BTCOEX_ACLCOEX_CONFIG_CMD * cmd)
{
	void *osbuf;
    WMI_SET_BTCOEX_ACLCOEX_CONFIG_CMD *alloc_cmd;

    osbuf = A_NETBUF_ALLOC(sizeof(*cmd));
    if (osbuf == NULL) {
        return A_NO_MEMORY;
    }
    A_NETBUF_PUT(osbuf, sizeof(*cmd));
    alloc_cmd = (WMI_SET_BTCOEX_ACLCOEX_CONFIG_CMD *)(A_NETBUF_DATA(osbuf));
    A_MEMZERO(alloc_cmd, sizeof(*cmd));
	memcpy(alloc_cmd,cmd,sizeof(WMI_SET_BTCOEX_ACLCOEX_CONFIG_CMD));
    return (wmi_cmd_send(wmip, osbuf, WMI_SET_BTCOEX_ACLCOEX_CONFIG_CMDID ,
                         NO_SYNC_WMIFLAG));

}

int
wmi_set_btcoex_debug_cmd(struct wmi_t *wmip, WMI_SET_BTCOEX_DEBUG_CMD * cmd)
{
	void *osbuf;
	WMI_SET_BTCOEX_DEBUG_CMD *alloc_cmd;

	osbuf = A_NETBUF_ALLOC(sizeof(*cmd));
	if (osbuf == NULL) {
			return A_NO_MEMORY;
	}
	A_NETBUF_PUT(osbuf, sizeof(*cmd));
	alloc_cmd = (WMI_SET_BTCOEX_DEBUG_CMD *)(A_NETBUF_DATA(osbuf));
	A_MEMZERO(alloc_cmd, sizeof(*cmd));
	memcpy(alloc_cmd,cmd,sizeof(WMI_SET_BTCOEX_DEBUG_CMD));
	return (wmi_cmd_send(wmip, osbuf, WMI_SET_BTCOEX_DEBUG_CMDID ,
							 NO_SYNC_WMIFLAG));

}

int
wmi_set_btcoex_bt_operating_status_cmd(struct wmi_t * wmip,
					WMI_SET_BTCOEX_BT_OPERATING_STATUS_CMD * cmd)
{
	void *osbuf;
	WMI_SET_BTCOEX_BT_OPERATING_STATUS_CMD *alloc_cmd;

	osbuf = A_NETBUF_ALLOC(sizeof(*cmd));
	if (osbuf == NULL) {
			return A_NO_MEMORY;
	}
	A_NETBUF_PUT(osbuf, sizeof(*cmd));
	alloc_cmd = (WMI_SET_BTCOEX_BT_OPERATING_STATUS_CMD *)(A_NETBUF_DATA(osbuf));
	A_MEMZERO(alloc_cmd, sizeof(*cmd));
	memcpy(alloc_cmd,cmd,sizeof(WMI_SET_BTCOEX_BT_OPERATING_STATUS_CMD));
	return (wmi_cmd_send(wmip, osbuf, WMI_SET_BTCOEX_BT_OPERATING_STATUS_CMDID ,
								 NO_SYNC_WMIFLAG));

}

int
wmi_get_btcoex_config_cmd(struct wmi_t * wmip, WMI_GET_BTCOEX_CONFIG_CMD * cmd)
{
	void *osbuf;
	WMI_GET_BTCOEX_CONFIG_CMD *alloc_cmd;

	osbuf = A_NETBUF_ALLOC(sizeof(*cmd));
	if (osbuf == NULL) {
			return A_NO_MEMORY;
	}
	A_NETBUF_PUT(osbuf, sizeof(*cmd));
	alloc_cmd = (WMI_GET_BTCOEX_CONFIG_CMD *)(A_NETBUF_DATA(osbuf));
	A_MEMZERO(alloc_cmd, sizeof(*cmd));
	memcpy(alloc_cmd,cmd,sizeof(WMI_GET_BTCOEX_CONFIG_CMD));
	return (wmi_cmd_send(wmip, osbuf, WMI_GET_BTCOEX_CONFIG_CMDID ,
							 NO_SYNC_WMIFLAG));

}

int
wmi_get_btcoex_stats_cmd(struct wmi_t *wmip)
{

    return wmi_simple_cmd(wmip, WMI_GET_BTCOEX_STATS_CMDID);

}

int
wmi_get_keepalive_configured(struct wmi_t *wmip)
{
    void *osbuf;
    WMI_GET_KEEPALIVE_CMD *cmd;
    osbuf = A_NETBUF_ALLOC(sizeof(*cmd));
    if (osbuf == NULL) {
        return A_NO_MEMORY;
    }
    A_NETBUF_PUT(osbuf, sizeof(*cmd));
    cmd = (WMI_GET_KEEPALIVE_CMD *)(A_NETBUF_DATA(osbuf));
    A_MEMZERO(cmd, sizeof(*cmd));
    return (wmi_cmd_send(wmip, osbuf, WMI_GET_KEEPALIVE_CMDID,
                         NO_SYNC_WMIFLAG));
}

u8 wmi_get_keepalive_cmd(struct wmi_t *wmip)
{
    return wmip->wmi_keepaliveInterval;
}

int
wmi_set_keepalive_cmd(struct wmi_t *wmip, u8 keepaliveInterval)
{
    void *osbuf;
    WMI_SET_KEEPALIVE_CMD *cmd;

    osbuf = A_NETBUF_ALLOC(sizeof(*cmd));
    if (osbuf == NULL) {
        return A_NO_MEMORY;
    }

    A_NETBUF_PUT(osbuf, sizeof(*cmd));

    cmd = (WMI_SET_KEEPALIVE_CMD *)(A_NETBUF_DATA(osbuf));
    A_MEMZERO(cmd, sizeof(*cmd));
    cmd->keepaliveInterval = keepaliveInterval;
    wmip->wmi_keepaliveInterval = keepaliveInterval;

    return (wmi_cmd_send(wmip, osbuf, WMI_SET_KEEPALIVE_CMDID,
                         NO_SYNC_WMIFLAG));
}

int
wmi_set_params_cmd(struct wmi_t *wmip, u32 opcode, u32 length, char *buffer)
{
    void *osbuf;
    WMI_SET_PARAMS_CMD *cmd;

    osbuf = A_NETBUF_ALLOC(sizeof(*cmd) + length);
    if (osbuf == NULL) {
        return A_NO_MEMORY;
    }

    A_NETBUF_PUT(osbuf, sizeof(*cmd) + length);

    cmd = (WMI_SET_PARAMS_CMD *)(A_NETBUF_DATA(osbuf));
    A_MEMZERO(cmd, sizeof(*cmd));
    cmd->opcode = opcode;
    cmd->length = length;
    memcpy(cmd->buffer, buffer, length);

    return (wmi_cmd_send(wmip, osbuf, WMI_SET_PARAMS_CMDID,
                         NO_SYNC_WMIFLAG));
}


int
wmi_set_mcast_filter_cmd(struct wmi_t *wmip, u8 dot1, u8 dot2, u8 dot3, u8 dot4)
{
    void *osbuf;
    WMI_SET_MCAST_FILTER_CMD *cmd;

    osbuf = A_NETBUF_ALLOC(sizeof(*cmd));
    if (osbuf == NULL) {
        return A_NO_MEMORY;
    }

    A_NETBUF_PUT(osbuf, sizeof(*cmd));

    cmd = (WMI_SET_MCAST_FILTER_CMD *)(A_NETBUF_DATA(osbuf));
    cmd->multicast_mac[0] = 0x01;
    cmd->multicast_mac[1] = 0x00;
    cmd->multicast_mac[2] = 0x5e;
    cmd->multicast_mac[3] = dot2&0x7F;
    cmd->multicast_mac[4] = dot3;
    cmd->multicast_mac[5] = dot4;

    return (wmi_cmd_send(wmip, osbuf, WMI_SET_MCAST_FILTER_CMDID,
                         NO_SYNC_WMIFLAG));
}


int
wmi_del_mcast_filter_cmd(struct wmi_t *wmip, u8 dot1, u8 dot2, u8 dot3, u8 dot4)
{
    void *osbuf;
    WMI_SET_MCAST_FILTER_CMD *cmd;

    osbuf = A_NETBUF_ALLOC(sizeof(*cmd));
    if (osbuf == NULL) {
        return A_NO_MEMORY;
    }

    A_NETBUF_PUT(osbuf, sizeof(*cmd));

    cmd = (WMI_SET_MCAST_FILTER_CMD *)(A_NETBUF_DATA(osbuf));
    cmd->multicast_mac[0] = 0x01;
    cmd->multicast_mac[1] = 0x00;
    cmd->multicast_mac[2] = 0x5e;
    cmd->multicast_mac[3] = dot2&0x7F;
    cmd->multicast_mac[4] = dot3;
    cmd->multicast_mac[5] = dot4;

    return (wmi_cmd_send(wmip, osbuf, WMI_DEL_MCAST_FILTER_CMDID,
                         NO_SYNC_WMIFLAG));
}

int
wmi_mcast_filter_cmd(struct wmi_t *wmip, u8 enable)
{
    void *osbuf;
    WMI_MCAST_FILTER_CMD *cmd;

    osbuf = A_NETBUF_ALLOC(sizeof(*cmd));
    if (osbuf == NULL) {
        return A_NO_MEMORY;
    }

    A_NETBUF_PUT(osbuf, sizeof(*cmd));

    cmd = (WMI_MCAST_FILTER_CMD *)(A_NETBUF_DATA(osbuf));
    cmd->enable = enable;

    return (wmi_cmd_send(wmip, osbuf, WMI_MCAST_FILTER_CMDID,
                         NO_SYNC_WMIFLAG));
}

int
wmi_set_appie_cmd(struct wmi_t *wmip, u8 mgmtFrmType, u8 ieLen,
                  u8 *ieInfo)
{
    void *osbuf;
    WMI_SET_APPIE_CMD *cmd;
    u16 cmdLen;

    cmdLen = sizeof(*cmd) + ieLen - 1;
    osbuf = A_NETBUF_ALLOC(cmdLen);
    if (osbuf == NULL) {
        return A_NO_MEMORY;
    }

    A_NETBUF_PUT(osbuf, cmdLen);

    cmd = (WMI_SET_APPIE_CMD *)(A_NETBUF_DATA(osbuf));
    A_MEMZERO(cmd, cmdLen);

    cmd->mgmtFrmType = mgmtFrmType;
    cmd->ieLen = ieLen;
    memcpy(cmd->ieInfo, ieInfo, ieLen);

    return (wmi_cmd_send(wmip, osbuf, WMI_SET_APPIE_CMDID, NO_SYNC_WMIFLAG));
}

int
wmi_set_halparam_cmd(struct wmi_t *wmip, u8 *cmd, u16 dataLen)
{
    void *osbuf;
    u8 *data;

    osbuf = A_NETBUF_ALLOC(dataLen);
    if (osbuf == NULL) {
        return A_NO_MEMORY;
    }

    A_NETBUF_PUT(osbuf, dataLen);

    data = A_NETBUF_DATA(osbuf);

    memcpy(data, cmd, dataLen);

    return (wmi_cmd_send(wmip, osbuf, WMI_SET_WHALPARAM_CMDID, NO_SYNC_WMIFLAG));
}

s32 wmi_get_rate(s8 rateindex)
{
    if (rateindex == RATE_AUTO) {
        return 0;
    } else {
        return(wmi_rateTable[(u32) rateindex][0]);
    }
}

void
wmi_node_return (struct wmi_t *wmip, bss_t *bss)
{
    if (NULL != bss)
    {
        wlan_node_return (&wmip->wmi_scan_table, bss);
    }
}

void
wmi_set_nodeage(struct wmi_t *wmip, u32 nodeAge)
{
    wlan_set_nodeage(&wmip->wmi_scan_table,nodeAge);
}

bss_t *
wmi_find_Ssidnode (struct wmi_t *wmip, u8 *pSsid,
                   u32 ssidLength, bool bIsWPA2, bool bMatchSSID)
{
    bss_t *node = NULL;
    node = wlan_find_Ssidnode (&wmip->wmi_scan_table, pSsid,
                               ssidLength, bIsWPA2, bMatchSSID);
    return node;
}


#ifdef THREAD_X
void
wmi_refresh_scan_table (struct wmi_t *wmip)
{
	wlan_refresh_inactive_nodes (&wmip->wmi_scan_table);
}
#endif

void
wmi_free_allnodes(struct wmi_t *wmip)
{
    wlan_free_allnodes(&wmip->wmi_scan_table);
}

bss_t *
wmi_find_node(struct wmi_t *wmip, const u8 *macaddr)
{
    bss_t *ni=NULL;
    ni=wlan_find_node(&wmip->wmi_scan_table,macaddr);
    return ni;
}

void
wmi_free_node(struct wmi_t *wmip, const u8 *macaddr)
{
    bss_t *ni=NULL;

    ni=wlan_find_node(&wmip->wmi_scan_table,macaddr);
    if (ni != NULL) {
        wlan_node_reclaim(&wmip->wmi_scan_table, ni);
    }

    return;
}

int
wmi_dset_open_reply(struct wmi_t *wmip,
                    u32 status,
                    u32 access_cookie,
                    u32 dset_size,
                    u32 dset_version,
                    u32 targ_handle,
                    u32 targ_reply_fn,
                    u32 targ_reply_arg)
{
    void *osbuf;
    WMIX_DSETOPEN_REPLY_CMD *open_reply;

    A_DPRINTF(DBG_WMI, (DBGFMT "Enter - wmip=0x%lx\n", DBGARG, (unsigned long)wmip));

    osbuf = A_NETBUF_ALLOC(sizeof(*open_reply));
    if (osbuf == NULL) {
        return A_NO_MEMORY;
    }

    A_NETBUF_PUT(osbuf, sizeof(*open_reply));
    open_reply = (WMIX_DSETOPEN_REPLY_CMD *)(A_NETBUF_DATA(osbuf));

    open_reply->status                   = status;
    open_reply->targ_dset_handle         = targ_handle;
    open_reply->targ_reply_fn            = targ_reply_fn;
    open_reply->targ_reply_arg           = targ_reply_arg;
    open_reply->access_cookie            = access_cookie;
    open_reply->size                     = dset_size;
    open_reply->version                  = dset_version;

    return (wmi_cmd_send_xtnd(wmip, osbuf, WMIX_DSETOPEN_REPLY_CMDID,
                             NO_SYNC_WMIFLAG));
}

static int
wmi_get_pmkid_list_event_rx(struct wmi_t *wmip, u8 *datap, u32 len)
{
    WMI_PMKID_LIST_REPLY *reply;
    u32 expected_len;

    if (len < sizeof(WMI_PMKID_LIST_REPLY)) {
        return A_EINVAL;
    }
    reply = (WMI_PMKID_LIST_REPLY *)datap;
    expected_len = sizeof(reply->numPMKID) + reply->numPMKID * WMI_PMKID_LEN;

    if (len < expected_len) {
        return A_EINVAL;
    }

    A_WMI_PMKID_LIST_EVENT(wmip->wmi_devt, reply->numPMKID,
                           reply->pmkidList, reply->bssidList[0]);

    return 0;
}


static int
wmi_set_params_event_rx(struct wmi_t *wmip, u8 *datap, u32 len)
{
    WMI_SET_PARAMS_REPLY *reply;

    if (len < sizeof(WMI_SET_PARAMS_REPLY)) {
        return A_EINVAL;
    }
    reply = (WMI_SET_PARAMS_REPLY *)datap;

    if (0 == reply->status)
    {

    }
    else
    {

    }

    return 0;
}



static int
wmi_acm_reject_event_rx(struct wmi_t *wmip, u8 *datap, u32 len)
{
    WMI_ACM_REJECT_EVENT *ev;

    ev = (WMI_ACM_REJECT_EVENT *)datap;
    wmip->wmi_traffic_class = ev->trafficClass;
    printk("ACM REJECT %d\n",wmip->wmi_traffic_class);
    return 0;
}


#ifdef CONFIG_HOST_DSET_SUPPORT
int
wmi_dset_data_reply(struct wmi_t *wmip,
                    u32 status,
                    u8 *user_buf,
                    u32 length,
                    u32 targ_buf,
                    u32 targ_reply_fn,
                    u32 targ_reply_arg)
{
    void *osbuf;
    WMIX_DSETDATA_REPLY_CMD *data_reply;
    u32 size;

    size = sizeof(*data_reply) + length;

    if (size <= length) {
        return A_ERROR;
    }

    A_DPRINTF(DBG_WMI,
        (DBGFMT "Enter - length=%d status=%d\n", DBGARG, length, status));

    osbuf = A_NETBUF_ALLOC(size);
    if (osbuf == NULL) {
        return A_NO_MEMORY;
    }
    A_NETBUF_PUT(osbuf, size);
    data_reply = (WMIX_DSETDATA_REPLY_CMD *)(A_NETBUF_DATA(osbuf));

    data_reply->status                     = status;
    data_reply->targ_buf                   = targ_buf;
    data_reply->targ_reply_fn              = targ_reply_fn;
    data_reply->targ_reply_arg             = targ_reply_arg;
    data_reply->length                     = length;

    if (status == 0) {
        if (a_copy_from_user(data_reply->buf, user_buf, length)) {
            A_NETBUF_FREE(osbuf);
            return A_ERROR;
        }
    }

    return (wmi_cmd_send_xtnd(wmip, osbuf, WMIX_DSETDATA_REPLY_CMDID,
                             NO_SYNC_WMIFLAG));
}
#endif /* CONFIG_HOST_DSET_SUPPORT */

int
wmi_set_wsc_status_cmd(struct wmi_t *wmip, u32 status)
{
    void *osbuf;
    char *cmd;

    wps_enable = status;

    osbuf = a_netbuf_alloc(sizeof(1));
    if (osbuf == NULL) {
        return A_NO_MEMORY;
    }

    a_netbuf_put(osbuf, sizeof(1));

    cmd = (char *)(a_netbuf_to_data(osbuf));

    A_MEMZERO(cmd, sizeof(*cmd));
    cmd[0] = (status?1:0);
    return (wmi_cmd_send(wmip, osbuf, WMI_SET_WSC_STATUS_CMDID,
                         NO_SYNC_WMIFLAG));
}

#if defined(CONFIG_TARGET_PROFILE_SUPPORT)
int
wmi_prof_cfg_cmd(struct wmi_t *wmip,
                 u32 period,
                 u32 nbins)
{
    void *osbuf;
    WMIX_PROF_CFG_CMD *cmd;

    osbuf = A_NETBUF_ALLOC(sizeof(*cmd));
    if (osbuf == NULL) {
        return A_NO_MEMORY;
    }

    A_NETBUF_PUT(osbuf, sizeof(*cmd));

    cmd = (WMIX_PROF_CFG_CMD *)(A_NETBUF_DATA(osbuf));
    A_MEMZERO(cmd, sizeof(*cmd));
    cmd->period = period;
    cmd->nbins  = nbins;

    return (wmi_cmd_send_xtnd(wmip, osbuf, WMIX_PROF_CFG_CMDID, NO_SYNC_WMIFLAG));
}

int
wmi_prof_addr_set_cmd(struct wmi_t *wmip, u32 addr)
{
    void *osbuf;
    WMIX_PROF_ADDR_SET_CMD *cmd;

    osbuf = A_NETBUF_ALLOC(sizeof(*cmd));
    if (osbuf == NULL) {
        return A_NO_MEMORY;
    }

    A_NETBUF_PUT(osbuf, sizeof(*cmd));

    cmd = (WMIX_PROF_ADDR_SET_CMD *)(A_NETBUF_DATA(osbuf));
    A_MEMZERO(cmd, sizeof(*cmd));
    cmd->addr = addr;

    return (wmi_cmd_send_xtnd(wmip, osbuf, WMIX_PROF_ADDR_SET_CMDID, NO_SYNC_WMIFLAG));
}

int
wmi_prof_start_cmd(struct wmi_t *wmip)
{
    return wmi_simple_cmd_xtnd(wmip, WMIX_PROF_START_CMDID);
}

int
wmi_prof_stop_cmd(struct wmi_t *wmip)
{
    return wmi_simple_cmd_xtnd(wmip, WMIX_PROF_STOP_CMDID);
}

int
wmi_prof_count_get_cmd(struct wmi_t *wmip)
{
    return wmi_simple_cmd_xtnd(wmip, WMIX_PROF_COUNT_GET_CMDID);
}

/* Called to handle WMIX_PROF_CONT_EVENTID */
static int
wmi_prof_count_rx(struct wmi_t *wmip, u8 *datap, int len)
{
    WMIX_PROF_COUNT_EVENT *prof_data = (WMIX_PROF_COUNT_EVENT *)datap;

    A_DPRINTF(DBG_WMI,
        (DBGFMT "Enter - addr=0x%x count=%d\n", DBGARG,
        prof_data->addr, prof_data->count));

    A_WMI_PROF_COUNT_RX(prof_data->addr, prof_data->count);

    return 0;
}
#endif /* CONFIG_TARGET_PROFILE_SUPPORT */

#ifdef OS_ROAM_MANAGEMENT

#define ETHERNET_MAC_ADDRESS_LENGTH    6

void
wmi_scan_indication (struct wmi_t *wmip)
{
    struct ieee80211_node_table *nt;
    u32 gen;
    u32 size;
    u32 bsssize;
    bss_t *bss;
    u32 numbss;
    PNDIS_802_11_BSSID_SCAN_INFO psi;
    PBYTE  pie;
    NDIS_802_11_FIXED_IEs *pFixed;
    NDIS_802_11_VARIABLE_IEs *pVar;
    u32 RateSize;

    struct ar6kScanIndication
    {
        NDIS_802_11_STATUS_INDICATION     ind;
        NDIS_802_11_BSSID_SCAN_INFO_LIST  slist;
    } *pAr6kScanIndEvent;

    nt = &wmip->wmi_scan_table;

    ++nt->nt_si_gen;


    gen = nt->nt_si_gen;

    size = offsetof(struct ar6kScanIndication, slist) +
           offsetof(NDIS_802_11_BSSID_SCAN_INFO_LIST, BssidScanInfo);

    numbss = 0;

    IEEE80211_NODE_LOCK(nt);

    //calc size
    for (bss = nt->nt_node_first; bss; bss = bss->ni_list_next) {
        if (bss->ni_si_gen != gen) {
            bsssize = offsetof(NDIS_802_11_BSSID_SCAN_INFO, Bssid) + offsetof(NDIS_WLAN_BSSID_EX, IEs);
            bsssize = bsssize + sizeof(NDIS_802_11_FIXED_IEs);

#ifdef SUPPORT_WPA2
            if (bss->ni_cie.ie_rsn) {
                bsssize = bsssize + bss->ni_cie.ie_rsn[1] + 2;
            }
#endif
            if (bss->ni_cie.ie_wpa) {
                bsssize = bsssize + bss->ni_cie.ie_wpa[1] + 2;
            }

            // bsssize must be a multiple of 4 to maintain alignment.
            bsssize = (bsssize + 3) & ~3;

            size += bsssize;

            numbss++;
        }
    }

    if (0 == numbss)
    {
//        RETAILMSG(1, (L"AR6K: scan indication: 0 bss\n"));
        ar6000_scan_indication (wmip->wmi_devt, NULL, 0);
        IEEE80211_NODE_UNLOCK (nt);
        return;
    }

    pAr6kScanIndEvent = A_MALLOC(size);

    if (NULL == pAr6kScanIndEvent)
    {
        IEEE80211_NODE_UNLOCK(nt);
        return;
    }

    A_MEMZERO(pAr6kScanIndEvent, size);

    //copy data
    pAr6kScanIndEvent->ind.StatusType = Ndis802_11StatusType_BssidScanInfoList;
    pAr6kScanIndEvent->slist.Version = 1;
    pAr6kScanIndEvent->slist.NumItems = numbss;

    psi = &pAr6kScanIndEvent->slist.BssidScanInfo[0];

    for (bss = nt->nt_node_first; bss; bss = bss->ni_list_next) {
        if (bss->ni_si_gen != gen) {

            bss->ni_si_gen = gen;

            //Set scan time
            psi->ScanTime = bss->ni_tstamp - WLAN_NODE_INACT_TIMEOUT_MSEC;

            // Copy data to bssid_ex
            bsssize = offsetof(NDIS_WLAN_BSSID_EX, IEs);
            bsssize = bsssize + sizeof(NDIS_802_11_FIXED_IEs);

#ifdef SUPPORT_WPA2
            if (bss->ni_cie.ie_rsn) {
                bsssize = bsssize + bss->ni_cie.ie_rsn[1] + 2;
            }
#endif
            if (bss->ni_cie.ie_wpa) {
                bsssize = bsssize + bss->ni_cie.ie_wpa[1] + 2;
            }

            // bsssize must be a multiple of 4 to maintain alignment.
            bsssize = (bsssize + 3) & ~3;

            psi->Bssid.Length = bsssize;

            memcpy (psi->Bssid.MacAddress, bss->ni_macaddr, ETHERNET_MAC_ADDRESS_LENGTH);


//if (((bss->ni_macaddr[3] == 0xCE) && (bss->ni_macaddr[4] == 0xF0) && (bss->ni_macaddr[5] == 0xE7)) ||
//  ((bss->ni_macaddr[3] == 0x03) && (bss->ni_macaddr[4] == 0xE2) && (bss->ni_macaddr[5] == 0x70)))
//            RETAILMSG (1, (L"%x\n",bss->ni_macaddr[5]));

            psi->Bssid.Ssid.SsidLength = 0;
            pie = bss->ni_cie.ie_ssid;

            if (pie) {
                // Format of SSID IE is:
                //  Type   (1 octet)
                //  Length (1 octet)
                //  SSID (Length octets)
                //
                //  Validation of the IE should have occurred within WMI.
                //
                if (pie[1] <= 32) {
                    psi->Bssid.Ssid.SsidLength = pie[1];
                    memcpy(psi->Bssid.Ssid.Ssid, &pie[2], psi->Bssid.Ssid.SsidLength);
                }
            }
            psi->Bssid.Privacy = (bss->ni_cie.ie_capInfo & 0x10) ? 1 : 0;

            //Post the RSSI value relative to the Standard Noise floor value.
            psi->Bssid.Rssi = bss->ni_rssi;

            if (bss->ni_cie.ie_chan >= 2412 && bss->ni_cie.ie_chan <= 2484) {

                if (bss->ni_cie.ie_rates && bss->ni_cie.ie_xrates) {
                    psi->Bssid.NetworkTypeInUse = Ndis802_11OFDM24;
                }
                else {
                    psi->Bssid.NetworkTypeInUse = Ndis802_11DS;
                }
            }
            else {
                psi->Bssid.NetworkTypeInUse = Ndis802_11OFDM5;
            }

            psi->Bssid.Configuration.Length = sizeof(psi->Bssid.Configuration);
            psi->Bssid.Configuration.BeaconPeriod = bss->ni_cie.ie_beaconInt; // Units are Kmicroseconds (1024 us)
            psi->Bssid.Configuration.ATIMWindow =  0;
            psi->Bssid.Configuration.DSConfig =  bss->ni_cie.ie_chan * 1000;
            psi->Bssid.InfrastructureMode = ((bss->ni_cie.ie_capInfo & 0x03) == 0x01 ) ? Ndis802_11Infrastructure : Ndis802_11IBSS;

            RateSize = 0;
            pie = bss->ni_cie.ie_rates;
            if (pie) {
                RateSize = (pie[1] < NDIS_802_11_LENGTH_RATES_EX) ? pie[1] : NDIS_802_11_LENGTH_RATES_EX;
                memcpy(psi->Bssid.SupportedRates, &pie[2], RateSize);
            }
            pie = bss->ni_cie.ie_xrates;
            if (pie && RateSize < NDIS_802_11_LENGTH_RATES_EX) {
                memcpy(psi->Bssid.SupportedRates + RateSize, &pie[2],
                       (pie[1] < (NDIS_802_11_LENGTH_RATES_EX - RateSize)) ? pie[1] : (NDIS_802_11_LENGTH_RATES_EX - RateSize));
            }

            // Copy the fixed IEs
            psi->Bssid.IELength = sizeof(NDIS_802_11_FIXED_IEs);

            pFixed = (NDIS_802_11_FIXED_IEs *)psi->Bssid.IEs;
            memcpy(pFixed->Timestamp, bss->ni_cie.ie_tstamp, sizeof(pFixed->Timestamp));
            pFixed->BeaconInterval = bss->ni_cie.ie_beaconInt;
            pFixed->Capabilities = bss->ni_cie.ie_capInfo;

            // Copy selected variable IEs

            pVar = (NDIS_802_11_VARIABLE_IEs *)((PBYTE)pFixed + sizeof(NDIS_802_11_FIXED_IEs));

#ifdef SUPPORT_WPA2
            // Copy the WPAv2 IE
            if (bss->ni_cie.ie_rsn) {
                pie = bss->ni_cie.ie_rsn;
                psi->Bssid.IELength += pie[1] + 2;
                memcpy(pVar, pie, pie[1] + 2);
                pVar = (NDIS_802_11_VARIABLE_IEs *)((PBYTE)pVar + pie[1] + 2);
            }
#endif
            // Copy the WPAv1 IE
            if (bss->ni_cie.ie_wpa) {
                pie = bss->ni_cie.ie_wpa;
                psi->Bssid.IELength += pie[1] + 2;
                memcpy(pVar, pie, pie[1] + 2);
                pVar = (NDIS_802_11_VARIABLE_IEs *)((PBYTE)pVar + pie[1] + 2);
            }

            // Advance buffer pointer
            psi = (PNDIS_802_11_BSSID_SCAN_INFO)((BYTE*)psi + bsssize + FIELD_OFFSET(NDIS_802_11_BSSID_SCAN_INFO, Bssid));
        }
    }

    IEEE80211_NODE_UNLOCK(nt);

//    wmi_free_allnodes(wmip);

//    RETAILMSG(1, (L"AR6K: scan indication: %u bss\n", numbss));

    ar6000_scan_indication (wmip->wmi_devt, pAr6kScanIndEvent, size);

    A_FREE(pAr6kScanIndEvent);
}
#endif

u8 ar6000_get_upper_threshold(s16 rssi, SQ_THRESHOLD_PARAMS *sq_thresh,
                           u32 size)
{
    u32 index;
    u8 threshold = (u8)sq_thresh->upper_threshold[size - 1];

    /* The list is already in sorted order. Get the next lower value */
    for (index = 0; index < size; index ++) {
        if (rssi < sq_thresh->upper_threshold[index]) {
            threshold = (u8)sq_thresh->upper_threshold[index];
            break;
        }
    }

    return threshold;
}

u8 ar6000_get_lower_threshold(s16 rssi, SQ_THRESHOLD_PARAMS *sq_thresh,
                           u32 size)
{
    u32 index;
    u8 threshold = (u8)sq_thresh->lower_threshold[size - 1];

    /* The list is already in sorted order. Get the next lower value */
    for (index = 0; index < size; index ++) {
        if (rssi > sq_thresh->lower_threshold[index]) {
            threshold = (u8)sq_thresh->lower_threshold[index];
            break;
        }
    }

    return threshold;
}
static int
wmi_send_rssi_threshold_params(struct wmi_t *wmip,
                              WMI_RSSI_THRESHOLD_PARAMS_CMD *rssiCmd)
{
    void    *osbuf;
    s8 size;
    WMI_RSSI_THRESHOLD_PARAMS_CMD *cmd;

    size = sizeof (*cmd);

    osbuf = A_NETBUF_ALLOC(size);
    if (osbuf == NULL) {
        return A_NO_MEMORY;
    }

    A_NETBUF_PUT(osbuf, size);

    cmd = (WMI_RSSI_THRESHOLD_PARAMS_CMD *)(A_NETBUF_DATA(osbuf));
    A_MEMZERO(cmd, size);
    memcpy(cmd, rssiCmd, sizeof(WMI_RSSI_THRESHOLD_PARAMS_CMD));

    return (wmi_cmd_send(wmip, osbuf, WMI_RSSI_THRESHOLD_PARAMS_CMDID,
                            NO_SYNC_WMIFLAG));
}
static int
wmi_send_snr_threshold_params(struct wmi_t *wmip,
                             WMI_SNR_THRESHOLD_PARAMS_CMD *snrCmd)
{
    void    *osbuf;
    s8 size;
    WMI_SNR_THRESHOLD_PARAMS_CMD *cmd;

    size = sizeof (*cmd);

    osbuf = A_NETBUF_ALLOC(size);
    if (osbuf == NULL) {
        return A_NO_MEMORY;
    }

    A_NETBUF_PUT(osbuf, size);
    cmd = (WMI_SNR_THRESHOLD_PARAMS_CMD *)(A_NETBUF_DATA(osbuf));
    A_MEMZERO(cmd, size);
    memcpy(cmd, snrCmd, sizeof(WMI_SNR_THRESHOLD_PARAMS_CMD));

    return (wmi_cmd_send(wmip, osbuf, WMI_SNR_THRESHOLD_PARAMS_CMDID,
                            NO_SYNC_WMIFLAG));
}

int
wmi_set_target_event_report_cmd(struct wmi_t *wmip, WMI_SET_TARGET_EVENT_REPORT_CMD* cmd)
{
    void *osbuf;
    WMI_SET_TARGET_EVENT_REPORT_CMD* alloc_cmd;

    osbuf = A_NETBUF_ALLOC(sizeof(*cmd));
    if (osbuf == NULL) {
        return A_NO_MEMORY;
    }

    A_NETBUF_PUT(osbuf, sizeof(*cmd));

    alloc_cmd = (WMI_SET_TARGET_EVENT_REPORT_CMD *)(A_NETBUF_DATA(osbuf));
    A_MEMZERO(alloc_cmd, sizeof(*cmd));
    memcpy(alloc_cmd, cmd, sizeof(*cmd));

    return (wmi_cmd_send(wmip, osbuf, WMI_SET_TARGET_EVENT_REPORT_CMDID,
            NO_SYNC_WMIFLAG));
}

bss_t *wmi_rm_current_bss (struct wmi_t *wmip, u8 *id)
{
    wmi_get_current_bssid (wmip, id);
    return wlan_node_remove (&wmip->wmi_scan_table, id);
}

int wmi_add_current_bss (struct wmi_t *wmip, u8 *id, bss_t *bss)
{
    wlan_setup_node (&wmip->wmi_scan_table, bss, id);
    return 0;
}

#ifdef ATH_AR6K_11N_SUPPORT
static int
wmi_addba_req_event_rx(struct wmi_t *wmip, u8 *datap, int len)
{
    WMI_ADDBA_REQ_EVENT *cmd = (WMI_ADDBA_REQ_EVENT *)datap;

    A_WMI_AGGR_RECV_ADDBA_REQ_EVT(wmip->wmi_devt, cmd);

    return 0;
}


static int
wmi_addba_resp_event_rx(struct wmi_t *wmip, u8 *datap, int len)
{
    WMI_ADDBA_RESP_EVENT *cmd = (WMI_ADDBA_RESP_EVENT *)datap;

    A_WMI_AGGR_RECV_ADDBA_RESP_EVT(wmip->wmi_devt, cmd);

    return 0;
}

static int
wmi_delba_req_event_rx(struct wmi_t *wmip, u8 *datap, int len)
{
    WMI_DELBA_EVENT *cmd = (WMI_DELBA_EVENT *)datap;

    A_WMI_AGGR_RECV_DELBA_REQ_EVT(wmip->wmi_devt, cmd);

    return 0;
}

int
wmi_btcoex_config_event_rx(struct wmi_t *wmip, u8 *datap, int len)
{
	A_DPRINTF(DBG_WMI, (DBGFMT "Enter\n", DBGARG));

    A_WMI_BTCOEX_CONFIG_EVENT(wmip->wmi_devt, datap, len);

     return 0;
}


int
wmi_btcoex_stats_event_rx(struct wmi_t * wmip,u8 *datap,int len)
{
	A_DPRINTF(DBG_WMI, (DBGFMT "Enter\n", DBGARG));

    A_WMI_BTCOEX_STATS_EVENT(wmip->wmi_devt, datap, len);

     return 0;

}
#endif

static int
wmi_hci_event_rx(struct wmi_t *wmip, u8 *datap, int len)
{
    WMI_HCI_EVENT *cmd = (WMI_HCI_EVENT *)datap;
    A_WMI_HCI_EVENT_EVT(wmip->wmi_devt, cmd);

    return 0;
}

////////////////////////////////////////////////////////////////////////////////
////                                                                        ////
////                AP mode functions                                       ////
////                                                                        ////
////////////////////////////////////////////////////////////////////////////////
/*
 * IOCTL: AR6000_XIOCTL_AP_COMMIT_CONFIG
 *
 * When AR6K in AP mode, This command will be called after
 * changing ssid, channel etc. It will pass the profile to
 * target with a flag which will indicate which parameter changed,
 * also if this flag is 0, there was no change in parametes, so
 * commit cmd will not be sent to target. Without calling this IOCTL
 * the changes will not take effect.
 */
int
wmi_ap_profile_commit(struct wmi_t *wmip, WMI_CONNECT_CMD *p)
{
    void *osbuf;
    WMI_CONNECT_CMD *cm;

    osbuf = A_NETBUF_ALLOC(sizeof(*cm));
    if (osbuf == NULL) {
        return A_NO_MEMORY;
    }

    A_NETBUF_PUT(osbuf, sizeof(*cm));
    cm = (WMI_CONNECT_CMD *)(A_NETBUF_DATA(osbuf));
    A_MEMZERO(cm, sizeof(*cm));

    memcpy(cm,p,sizeof(*cm));

    return (wmi_cmd_send(wmip, osbuf, WMI_AP_CONFIG_COMMIT_CMDID, NO_SYNC_WMIFLAG));
}

/*
 * IOCTL: AR6000_XIOCTL_AP_HIDDEN_SSID
 *
 * This command will be used to enable/disable hidden ssid functioanlity of
 * beacon. If it is enabled, ssid will be NULL in beacon.
 */
int
wmi_ap_set_hidden_ssid(struct wmi_t *wmip, u8 hidden_ssid)
{
    void *osbuf;
    WMI_AP_HIDDEN_SSID_CMD *hs;

    osbuf = A_NETBUF_ALLOC(sizeof(WMI_AP_HIDDEN_SSID_CMD));
    if (osbuf == NULL) {
        return A_NO_MEMORY;
    }

    A_NETBUF_PUT(osbuf, sizeof(WMI_AP_HIDDEN_SSID_CMD));
    hs = (WMI_AP_HIDDEN_SSID_CMD *)(A_NETBUF_DATA(osbuf));
    A_MEMZERO(hs, sizeof(*hs));

    hs->hidden_ssid          = hidden_ssid;

    A_DPRINTF(DBG_WMI, (DBGFMT "AR6000_XIOCTL_AP_HIDDEN_SSID %d\n", DBGARG , hidden_ssid));
    return (wmi_cmd_send(wmip, osbuf, WMI_AP_HIDDEN_SSID_CMDID, NO_SYNC_WMIFLAG));
}

/*
 * IOCTL: AR6000_XIOCTL_AP_SET_MAX_NUM_STA
 *
 * This command is used to limit max num of STA that can connect
 * with this AP. This value should not exceed AP_MAX_NUM_STA (this
 * is max num of STA supported by AP). Value was already validated
 * in ioctl.c
 */
int
wmi_ap_set_num_sta(struct wmi_t *wmip, u8 num_sta)
{
    void *osbuf;
    WMI_AP_SET_NUM_STA_CMD *ns;

    osbuf = A_NETBUF_ALLOC(sizeof(WMI_AP_SET_NUM_STA_CMD));
    if (osbuf == NULL) {
        return A_NO_MEMORY;
    }

    A_NETBUF_PUT(osbuf, sizeof(WMI_AP_SET_NUM_STA_CMD));
    ns = (WMI_AP_SET_NUM_STA_CMD *)(A_NETBUF_DATA(osbuf));
    A_MEMZERO(ns, sizeof(*ns));

    ns->num_sta          = num_sta;

    A_DPRINTF(DBG_WMI, (DBGFMT "AR6000_XIOCTL_AP_SET_MAX_NUM_STA %d\n", DBGARG , num_sta));
    return (wmi_cmd_send(wmip, osbuf, WMI_AP_SET_NUM_STA_CMDID, NO_SYNC_WMIFLAG));
}

/*
 * IOCTL: AR6000_XIOCTL_AP_SET_ACL_MAC
 *
 * This command is used to send list of mac of STAs which will
 * be allowed to connect with this AP. When this list is empty
 * firware will allow all STAs till the count reaches AP_MAX_NUM_STA.
 */
int
wmi_ap_acl_mac_list(struct wmi_t *wmip, WMI_AP_ACL_MAC_CMD *acl)
{
    void *osbuf;
    WMI_AP_ACL_MAC_CMD *a;

    osbuf = A_NETBUF_ALLOC(sizeof(WMI_AP_ACL_MAC_CMD));
    if (osbuf == NULL) {
        return A_NO_MEMORY;
    }

    A_NETBUF_PUT(osbuf, sizeof(WMI_AP_ACL_MAC_CMD));
    a = (WMI_AP_ACL_MAC_CMD *)(A_NETBUF_DATA(osbuf));
    A_MEMZERO(a, sizeof(*a));
    memcpy(a,acl,sizeof(*acl));

    return (wmi_cmd_send(wmip, osbuf, WMI_AP_ACL_MAC_LIST_CMDID, NO_SYNC_WMIFLAG));
}

/*
 * IOCTL: AR6000_XIOCTL_AP_SET_MLME
 *
 * This command is used to send list of mac of STAs which will
 * be allowed to connect with this AP. When this list is empty
 * firware will allow all STAs till the count reaches AP_MAX_NUM_STA.
 */
int
wmi_ap_set_mlme(struct wmi_t *wmip, u8 cmd, u8 *mac, u16 reason)
{
    void *osbuf;
    WMI_AP_SET_MLME_CMD *mlme;

    osbuf = A_NETBUF_ALLOC(sizeof(WMI_AP_SET_MLME_CMD));
    if (osbuf == NULL) {
        return A_NO_MEMORY;
    }

    A_NETBUF_PUT(osbuf, sizeof(WMI_AP_SET_MLME_CMD));
    mlme = (WMI_AP_SET_MLME_CMD *)(A_NETBUF_DATA(osbuf));
    A_MEMZERO(mlme, sizeof(*mlme));

    mlme->cmd = cmd;
    memcpy(mlme->mac, mac, ATH_MAC_LEN);
    mlme->reason = reason;

    return (wmi_cmd_send(wmip, osbuf, WMI_AP_SET_MLME_CMDID, NO_SYNC_WMIFLAG));
}

static int
wmi_pspoll_event_rx(struct wmi_t *wmip, u8 *datap, int len)
{
    WMI_PSPOLL_EVENT *ev;

    if (len < sizeof(WMI_PSPOLL_EVENT)) {
        return A_EINVAL;
    }
    ev = (WMI_PSPOLL_EVENT *)datap;

    A_WMI_PSPOLL_EVENT(wmip->wmi_devt, ev->aid);
    return 0;
}

static int
wmi_dtimexpiry_event_rx(struct wmi_t *wmip, u8 *datap,int len)
{
    A_WMI_DTIMEXPIRY_EVENT(wmip->wmi_devt);
    return 0;
}

#ifdef WAPI_ENABLE
static int
wmi_wapi_rekey_event_rx(struct wmi_t *wmip, u8 *datap,int len)
{
    u8 *ev;

    if (len < 7) {
        return A_EINVAL;
    }
    ev = (u8 *)datap;

    A_WMI_WAPI_REKEY_EVENT(wmip->wmi_devt, *ev, &ev[1]);
    return 0;
}
#endif

int
wmi_set_pvb_cmd(struct wmi_t *wmip, u16 aid, bool flag)
{
    WMI_AP_SET_PVB_CMD *cmd;
    void *osbuf = NULL;

    osbuf = A_NETBUF_ALLOC(sizeof(WMI_AP_SET_PVB_CMD));
    if (osbuf == NULL) {
        return A_NO_MEMORY;
    }

    A_NETBUF_PUT(osbuf, sizeof(WMI_AP_SET_PVB_CMD));
    cmd = (WMI_AP_SET_PVB_CMD *)(A_NETBUF_DATA(osbuf));
    A_MEMZERO(cmd, sizeof(*cmd));

    cmd->aid = aid;
    cmd->flag = flag;

    return (wmi_cmd_send(wmip, osbuf, WMI_AP_SET_PVB_CMDID, NO_SYNC_WMIFLAG));
}

int
wmi_ap_conn_inact_time(struct wmi_t *wmip, u32 period)
{
    WMI_AP_CONN_INACT_CMD *cmd;
    void *osbuf = NULL;

    osbuf = A_NETBUF_ALLOC(sizeof(WMI_AP_CONN_INACT_CMD));
    if (osbuf == NULL) {
        return A_NO_MEMORY;
    }

    A_NETBUF_PUT(osbuf, sizeof(WMI_AP_CONN_INACT_CMD));
    cmd = (WMI_AP_CONN_INACT_CMD *)(A_NETBUF_DATA(osbuf));
    A_MEMZERO(cmd, sizeof(*cmd));

    cmd->period = period;

    return (wmi_cmd_send(wmip, osbuf, WMI_AP_CONN_INACT_CMDID, NO_SYNC_WMIFLAG));
}

int
wmi_ap_bgscan_time(struct wmi_t *wmip, u32 period, u32 dwell)
{
    WMI_AP_PROT_SCAN_TIME_CMD *cmd;
    void *osbuf = NULL;

    osbuf = A_NETBUF_ALLOC(sizeof(WMI_AP_PROT_SCAN_TIME_CMD));
    if (osbuf == NULL) {
        return A_NO_MEMORY;
    }

    A_NETBUF_PUT(osbuf, sizeof(WMI_AP_PROT_SCAN_TIME_CMD));
    cmd = (WMI_AP_PROT_SCAN_TIME_CMD *)(A_NETBUF_DATA(osbuf));
    A_MEMZERO(cmd, sizeof(*cmd));

    cmd->period_min = period;
    cmd->dwell_ms   = dwell;

    return (wmi_cmd_send(wmip, osbuf, WMI_AP_PROT_SCAN_TIME_CMDID, NO_SYNC_WMIFLAG));
}

int
wmi_ap_set_dtim(struct wmi_t *wmip, u8 dtim)
{
    WMI_AP_SET_DTIM_CMD *cmd;
    void *osbuf = NULL;

    osbuf = A_NETBUF_ALLOC(sizeof(WMI_AP_SET_DTIM_CMD));
    if (osbuf == NULL) {
        return A_NO_MEMORY;
    }

    A_NETBUF_PUT(osbuf, sizeof(WMI_AP_SET_DTIM_CMD));
    cmd = (WMI_AP_SET_DTIM_CMD *)(A_NETBUF_DATA(osbuf));
    A_MEMZERO(cmd, sizeof(*cmd));

    cmd->dtim = dtim;

    return (wmi_cmd_send(wmip, osbuf, WMI_AP_SET_DTIM_CMDID, NO_SYNC_WMIFLAG));
}

/*
 * IOCTL: AR6000_XIOCTL_AP_SET_ACL_POLICY
 *
 * This command is used to set ACL policay. While changing policy, if you
 * want to retain the existing MAC addresses in the ACL list, policy should be
 * OR with AP_ACL_RETAIN_LIST_MASK, else the existing list will be cleared.
 * If there is no chage in policy, the list will be intact.
 */
int
wmi_ap_set_acl_policy(struct wmi_t *wmip, u8 policy)
{
    void *osbuf;
    WMI_AP_ACL_POLICY_CMD *po;

    osbuf = A_NETBUF_ALLOC(sizeof(WMI_AP_ACL_POLICY_CMD));
    if (osbuf == NULL) {
        return A_NO_MEMORY;
}

    A_NETBUF_PUT(osbuf, sizeof(WMI_AP_ACL_POLICY_CMD));
    po = (WMI_AP_ACL_POLICY_CMD *)(A_NETBUF_DATA(osbuf));
    A_MEMZERO(po, sizeof(*po));

    po->policy = policy;

    return (wmi_cmd_send(wmip, osbuf, WMI_AP_ACL_POLICY_CMDID, NO_SYNC_WMIFLAG));
}

int
wmi_ap_set_rateset(struct wmi_t *wmip, u8 rateset)
{
    void *osbuf;
    WMI_AP_SET_11BG_RATESET_CMD *rs;

    osbuf = A_NETBUF_ALLOC(sizeof(WMI_AP_SET_11BG_RATESET_CMD));
    if (osbuf == NULL) {
        return A_NO_MEMORY;
    }

    A_NETBUF_PUT(osbuf, sizeof(WMI_AP_SET_11BG_RATESET_CMD));
    rs = (WMI_AP_SET_11BG_RATESET_CMD *)(A_NETBUF_DATA(osbuf));
    A_MEMZERO(rs, sizeof(*rs));

    rs->rateset = rateset;

    return (wmi_cmd_send(wmip, osbuf, WMI_AP_SET_11BG_RATESET_CMDID, NO_SYNC_WMIFLAG));
}

#ifdef ATH_AR6K_11N_SUPPORT
int
wmi_set_ht_cap_cmd(struct wmi_t *wmip, WMI_SET_HT_CAP_CMD *cmd)
{
    void *osbuf;
    WMI_SET_HT_CAP_CMD *htCap;
    u8 band;

    osbuf = A_NETBUF_ALLOC(sizeof(*htCap));
    if (osbuf == NULL) {
        return A_NO_MEMORY;
    }

    A_NETBUF_PUT(osbuf, sizeof(*htCap));

    band = (cmd->band)? A_BAND_5GHZ : A_BAND_24GHZ;
    wmip->wmi_ht_allowed[band] = (cmd->enable)? 1:0;

    htCap = (WMI_SET_HT_CAP_CMD *)(A_NETBUF_DATA(osbuf));
    A_MEMZERO(htCap, sizeof(*htCap));
    memcpy(htCap, cmd, sizeof(*htCap));

    return (wmi_cmd_send(wmip, osbuf, WMI_SET_HT_CAP_CMDID,
                         NO_SYNC_WMIFLAG));
}

int
wmi_set_ht_op_cmd(struct wmi_t *wmip, u8 sta_chan_width)
{
    void *osbuf;
    WMI_SET_HT_OP_CMD *htInfo;

    osbuf = A_NETBUF_ALLOC(sizeof(*htInfo));
    if (osbuf == NULL) {
        return A_NO_MEMORY;
    }

    A_NETBUF_PUT(osbuf, sizeof(*htInfo));

    htInfo = (WMI_SET_HT_OP_CMD *)(A_NETBUF_DATA(osbuf));
    A_MEMZERO(htInfo, sizeof(*htInfo));
    htInfo->sta_chan_width = sta_chan_width;

    return (wmi_cmd_send(wmip, osbuf, WMI_SET_HT_OP_CMDID,
                         NO_SYNC_WMIFLAG));
}
#endif

int
wmi_set_tx_select_rates_cmd(struct wmi_t *wmip, u32 *pMaskArray)
{
    void *osbuf;
    WMI_SET_TX_SELECT_RATES_CMD *pData;

    osbuf = A_NETBUF_ALLOC(sizeof(*pData));
    if (osbuf == NULL) {
        return A_NO_MEMORY;
    }

    A_NETBUF_PUT(osbuf, sizeof(*pData));

    pData = (WMI_SET_TX_SELECT_RATES_CMD *)(A_NETBUF_DATA(osbuf));
    memcpy(pData, pMaskArray, sizeof(*pData));

    return (wmi_cmd_send(wmip, osbuf, WMI_SET_TX_SELECT_RATES_CMDID,
                         NO_SYNC_WMIFLAG));
}


int
wmi_send_hci_cmd(struct wmi_t *wmip, u8 *buf, u16 sz)
{
    void *osbuf;
    WMI_HCI_CMD *cmd;

    osbuf = A_NETBUF_ALLOC(sizeof(*cmd) + sz);
    if (osbuf == NULL) {
        return A_NO_MEMORY;
    }

    A_NETBUF_PUT(osbuf, sizeof(*cmd) + sz);
    cmd = (WMI_HCI_CMD *)(A_NETBUF_DATA(osbuf));

    cmd->cmd_buf_sz = sz;
    memcpy(cmd->buf, buf, sz);
    return (wmi_cmd_send(wmip, osbuf, WMI_HCI_CMD_CMDID, NO_SYNC_WMIFLAG));
}

#ifdef ATH_AR6K_11N_SUPPORT
int
wmi_allow_aggr_cmd(struct wmi_t *wmip, u16 tx_tidmask, u16 rx_tidmask)
{
    void *osbuf;
    WMI_ALLOW_AGGR_CMD *cmd;

    osbuf = A_NETBUF_ALLOC(sizeof(*cmd));
    if (osbuf == NULL) {
        return A_NO_MEMORY;
    }

    A_NETBUF_PUT(osbuf, sizeof(*cmd));

    cmd = (WMI_ALLOW_AGGR_CMD *)(A_NETBUF_DATA(osbuf));
    cmd->tx_allow_aggr = tx_tidmask;
    cmd->rx_allow_aggr = rx_tidmask;

    return (wmi_cmd_send(wmip, osbuf, WMI_ALLOW_AGGR_CMDID, NO_SYNC_WMIFLAG));
}

int
wmi_setup_aggr_cmd(struct wmi_t *wmip, u8 tid)
{
    void *osbuf;
    WMI_ADDBA_REQ_CMD *cmd;

    osbuf = A_NETBUF_ALLOC(sizeof(*cmd));
    if (osbuf == NULL) {
        return A_NO_MEMORY;
    }

    A_NETBUF_PUT(osbuf, sizeof(*cmd));

    cmd = (WMI_ADDBA_REQ_CMD *)(A_NETBUF_DATA(osbuf));
    cmd->tid = tid;

    return (wmi_cmd_send(wmip, osbuf, WMI_ADDBA_REQ_CMDID, NO_SYNC_WMIFLAG));
}

int
wmi_delete_aggr_cmd(struct wmi_t *wmip, u8 tid, bool uplink)
{
    void *osbuf;
    WMI_DELBA_REQ_CMD *cmd;

    osbuf = A_NETBUF_ALLOC(sizeof(*cmd));
    if (osbuf == NULL) {
        return A_NO_MEMORY;
    }

    A_NETBUF_PUT(osbuf, sizeof(*cmd));

    cmd = (WMI_DELBA_REQ_CMD *)(A_NETBUF_DATA(osbuf));
    cmd->tid = tid;
    cmd->is_sender_initiator = uplink;  /* uplink =1 - uplink direction, 0=downlink direction */

    /* Delete the local aggr state, on host */
    return (wmi_cmd_send(wmip, osbuf, WMI_DELBA_REQ_CMDID, NO_SYNC_WMIFLAG));
}
#endif

int
wmi_set_rx_frame_format_cmd(struct wmi_t *wmip, u8 rxMetaVersion,
                            bool rxDot11Hdr, bool defragOnHost)
{
    void *osbuf;
    WMI_RX_FRAME_FORMAT_CMD *cmd;

    osbuf = A_NETBUF_ALLOC(sizeof(*cmd));
    if (osbuf == NULL) {
        return A_NO_MEMORY;
    }

    A_NETBUF_PUT(osbuf, sizeof(*cmd));

    cmd = (WMI_RX_FRAME_FORMAT_CMD *)(A_NETBUF_DATA(osbuf));
    cmd->dot11Hdr = (rxDot11Hdr==true)? 1:0;
    cmd->defragOnHost = (defragOnHost==true)? 1:0;
    cmd->metaVersion = rxMetaVersion;  /*  */

    /* Delete the local aggr state, on host */
    return (wmi_cmd_send(wmip, osbuf, WMI_RX_FRAME_FORMAT_CMDID, NO_SYNC_WMIFLAG));
}


int
wmi_set_thin_mode_cmd(struct wmi_t *wmip, bool bThinMode)
{
    void *osbuf;
    WMI_SET_THIN_MODE_CMD *cmd;

    osbuf = A_NETBUF_ALLOC(sizeof(*cmd));
    if (osbuf == NULL) {
        return A_NO_MEMORY;
    }

    A_NETBUF_PUT(osbuf, sizeof(*cmd));

    cmd = (WMI_SET_THIN_MODE_CMD *)(A_NETBUF_DATA(osbuf));
    cmd->enable = (bThinMode==true)? 1:0;

    /* Delete the local aggr state, on host */
    return (wmi_cmd_send(wmip, osbuf, WMI_SET_THIN_MODE_CMDID, NO_SYNC_WMIFLAG));
}


int
wmi_set_wlan_conn_precedence_cmd(struct wmi_t *wmip, BT_WLAN_CONN_PRECEDENCE precedence)
{
    void *osbuf;
    WMI_SET_BT_WLAN_CONN_PRECEDENCE *cmd;

    osbuf = A_NETBUF_ALLOC(sizeof(*cmd));
    if (osbuf == NULL) {
        return A_NO_MEMORY;
    }

    A_NETBUF_PUT(osbuf, sizeof(*cmd));

    cmd = (WMI_SET_BT_WLAN_CONN_PRECEDENCE *)(A_NETBUF_DATA(osbuf));
    A_MEMZERO(cmd, sizeof(*cmd));
    cmd->precedence = precedence;

    return (wmi_cmd_send(wmip, osbuf, WMI_SET_BT_WLAN_CONN_PRECEDENCE_CMDID,
                         NO_SYNC_WMIFLAG));
}

int
wmi_set_pmk_cmd(struct wmi_t *wmip, u8 *pmk)
{
    void *osbuf;
    WMI_SET_PMK_CMD *p;

    osbuf = A_NETBUF_ALLOC(sizeof(WMI_SET_PMK_CMD));
    if (osbuf == NULL) {
        return A_NO_MEMORY;
    }

    A_NETBUF_PUT(osbuf, sizeof(WMI_SET_PMK_CMD));

    p = (WMI_SET_PMK_CMD *)(A_NETBUF_DATA(osbuf));
    A_MEMZERO(p, sizeof(*p));

    memcpy(p->pmk, pmk, WMI_PMK_LEN);

    return (wmi_cmd_send(wmip, osbuf, WMI_SET_PMK_CMDID, NO_SYNC_WMIFLAG));
}

int
wmi_set_excess_tx_retry_thres_cmd(struct wmi_t *wmip, WMI_SET_EXCESS_TX_RETRY_THRES_CMD *cmd)
{
    void *osbuf;
    WMI_SET_EXCESS_TX_RETRY_THRES_CMD *p;

    osbuf = A_NETBUF_ALLOC(sizeof(WMI_SET_EXCESS_TX_RETRY_THRES_CMD));
    if (osbuf == NULL) {
        return A_NO_MEMORY;
    }

    A_NETBUF_PUT(osbuf, sizeof(WMI_SET_EXCESS_TX_RETRY_THRES_CMD));

    p = (WMI_SET_EXCESS_TX_RETRY_THRES_CMD *)(A_NETBUF_DATA(osbuf));
    memset(p, 0, sizeof(*p));

    p->threshold = cmd->threshold;

    return (wmi_cmd_send(wmip, osbuf, WMI_SET_EXCESS_TX_RETRY_THRES_CMDID, NO_SYNC_WMIFLAG));
}

int
wmi_SGI_cmd(struct wmi_t *wmip, u32 sgiMask, u8 sgiPERThreshold)
{
    void *osbuf;
    WMI_SET_TX_SGI_PARAM_CMD *cmd;

    osbuf = A_NETBUF_ALLOC(sizeof(*cmd));
    if (osbuf == NULL) {
        return A_NO_MEMORY ;
    }

    A_NETBUF_PUT(osbuf, sizeof(*cmd));

    cmd = (WMI_SET_TX_SGI_PARAM_CMD *)(A_NETBUF_DATA(osbuf));
    A_MEMZERO(cmd, sizeof(*cmd));
    cmd->sgiMask = sgiMask;
    cmd->sgiPERThreshold = sgiPERThreshold;
    return (wmi_cmd_send(wmip, osbuf, WMI_SET_TX_SGI_PARAM_CMDID,
                         NO_SYNC_WMIFLAG));
}

bss_t *
wmi_find_matching_Ssidnode (struct wmi_t *wmip, u8 *pSsid,
                   u32 ssidLength,
                   u32 dot11AuthMode, u32 authMode,
                   u32 pairwiseCryptoType, u32 grpwiseCryptoTyp)
{
    bss_t *node = NULL;
    node = wlan_find_matching_Ssidnode (&wmip->wmi_scan_table, pSsid,
                               ssidLength, dot11AuthMode, authMode, pairwiseCryptoType, grpwiseCryptoTyp);

    return node;
}

u16 wmi_ieee2freq (int chan)
{
    u16 freq = 0;
    freq = wlan_ieee2freq (chan);
    return freq;

}

u32 wmi_freq2ieee (u16 freq)
{
    u16 chan = 0;
    chan = wlan_freq2ieee (freq);
    return chan;
}
