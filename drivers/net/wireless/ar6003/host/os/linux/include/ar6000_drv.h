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

#ifndef _AR6000_H_
#define _AR6000_H_

#include <linux/version.h>


#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,17)
#include <linux/config.h>
#elif LINUX_VERSION_CODE < KERNEL_VERSION(2,6,33)
#include <linux/autoconf.h>
#else
#include <generated/autoconf.h>
#endif
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/spinlock.h>
#include <linux/skbuff.h>
#include <linux/if_ether.h>
#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <net/iw_handler.h>
#include <linux/if_arp.h>
#include <linux/ip.h>
#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,26)
#include <asm/semaphore.h>
#else
#include <linux/semaphore.h>
#endif
#include <linux/wireless.h>
#ifdef ATH6K_CONFIG_CFG80211
#include <net/cfg80211.h>
#endif /* ATH6K_CONFIG_CFG80211 */
#include <linux/module.h>
#include <asm/io.h>

#include <a_config.h>
#include <athdefs.h>
#include "a_types.h"
#include "a_osapi.h"
#include "htc_api.h"
#include "wmi.h"
#include "a_drv.h"
#include "bmi.h"
#include <ieee80211.h>
#include <ieee80211_ioctl.h>
#include <wlan_api.h>
#include <wmi_api.h>
#include "gpio_api.h"
#include "gpio.h"
#include "pkt_log.h"
#include "aggr_recv_api.h"
#include <host_version.h>
#include <linux/rtnetlink.h>
#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,0)
#include <asm/uaccess.h>
#else
#include <linux/init.h>
#include <linux/moduleparam.h>
#endif
#include "ar6000_api.h"
#ifdef CONFIG_HOST_TCMD_SUPPORT
#include <testcmd.h>
#endif
#include <linux/firmware.h>

#include "targaddrs.h"
#include "dbglog_api.h"
#include "ar6000_diag.h"
#include "common_drv.h"
#include "roaming.h"
#include "hci_transport_api.h"
#define ATH_MODULE_NAME driver
#include "a_debug.h"

#define  ATH_DEBUG_DBG_LOG       ATH_DEBUG_MAKE_MODULE_MASK(0)
#define  ATH_DEBUG_WLAN_CONNECT  ATH_DEBUG_MAKE_MODULE_MASK(1)
#define  ATH_DEBUG_WLAN_SCAN     ATH_DEBUG_MAKE_MODULE_MASK(2)
#define  ATH_DEBUG_WLAN_TX       ATH_DEBUG_MAKE_MODULE_MASK(3)
#define  ATH_DEBUG_WLAN_RX       ATH_DEBUG_MAKE_MODULE_MASK(4)
#define  ATH_DEBUG_HTC_RAW       ATH_DEBUG_MAKE_MODULE_MASK(5)
#define  ATH_DEBUG_HCI_BRIDGE    ATH_DEBUG_MAKE_MODULE_MASK(6)
#define  ATH_DEBUG_HCI_RECV      ATH_DEBUG_MAKE_MODULE_MASK(7)
#define  ATH_DEBUG_HCI_SEND      ATH_DEBUG_MAKE_MODULE_MASK(8)
#define  ATH_DEBUG_HCI_DUMP      ATH_DEBUG_MAKE_MODULE_MASK(9)

#ifndef  __dev_put
#define  __dev_put(dev) dev_put(dev)
#endif



#ifdef ATH6K_CONFIG_CFG80211
#define NUM_SUBQUEUE		     1
#endif

#ifdef USER_KEYS

#define USER_SAVEDKEYS_STAT_INIT     0
#define USER_SAVEDKEYS_STAT_RUN      1

// TODO this needs to move into the AR_SOFTC struct
struct USER_SAVEDKEYS {
    struct ieee80211req_key   ucast_ik;
    struct ieee80211req_key   bcast_ik;
    CRYPTO_TYPE               keyType;
    A_BOOL                    keyOk;
};
#endif

#define DBG_INFO        0x00000001
#define DBG_ERROR       0x00000002
#define DBG_WARNING     0x00000004
#define DBG_SDIO        0x00000008
#define DBG_HIF         0x00000010
#define DBG_HTC         0x00000020
#define DBG_WMI         0x00000040
#define DBG_WMI2        0x00000080
#define DBG_DRIVER      0x00000100

#define DBG_DEFAULTS    (DBG_ERROR|DBG_WARNING)


A_STATUS ar6000_ReadRegDiag(HIF_DEVICE *hifDevice, A_UINT32 *address, A_UINT32 *data);
A_STATUS ar6000_WriteRegDiag(HIF_DEVICE *hifDevice, A_UINT32 *address, A_UINT32 *data);

#ifdef __cplusplus
extern "C" {
#endif

#define MAX_AR6000                        1
#define AR6000_MAX_RX_BUFFERS             16
#define AR6000_BUFFER_SIZE                1664
#define AR6000_MAX_AMSDU_RX_BUFFERS       4
#define AR6000_AMSDU_REFILL_THRESHOLD     3
#define AR6000_AMSDU_BUFFER_SIZE          (WMI_MAX_AMSDU_RX_DATA_FRAME_LENGTH + 128)
#define AR6000_MAX_RX_MESSAGE_SIZE        (max(WMI_MAX_NORMAL_RX_DATA_FRAME_LENGTH,WMI_MAX_AMSDU_RX_DATA_FRAME_LENGTH))

#define AR6000_TX_TIMEOUT                 10
#define AR6000_ETH_ADDR_LEN               6
#define AR6000_MAX_ENDPOINTS              4
#define MAX_NODE_NUM                      15
/* MAX_HI_COOKIE_NUM are reserved for high priority traffic */
#define MAX_DEF_COOKIE_NUM                150
#define MAX_HI_COOKIE_NUM                 15 /* 10% of MAX_COOKIE_NUM */
#define MAX_COOKIE_NUM                    (MAX_DEF_COOKIE_NUM + MAX_HI_COOKIE_NUM)

/* MAX_DEFAULT_SEND_QUEUE_DEPTH is used to set the default queue depth for the
 * WMM send queues.  If a queue exceeds this depth htc will query back to the
 * OS specific layer by calling EpSendFull().  This gives the OS layer the
 * opportunity to drop the packet if desired.  Therefore changing
 * MAX_DEFAULT_SEND_QUEUE_DEPTH does not affect resource utilization but
 * does impact the threshold used to identify if a packet should be
 * dropped. */
// #define MAX_DEFAULT_SEND_QUEUE_DEPTH      (MAX_DEF_COOKIE_NUM / WMM_NUM_AC)

// Host Queue depth has been increased during performace chariot endpoint runs. Host may not pump
// as fast as host application expected, due to that panic/packet loss / chariot error happens
// adjusting queue depth size resolve this issue
#define MAX_DEFAULT_SEND_QUEUE_DEPTH     64

#define AR6000_HB_CHALLENGE_RESP_FREQ_DEFAULT        1
#define AR6000_HB_CHALLENGE_RESP_MISS_THRES_DEFAULT  1
#define A_DISCONNECT_TIMER_INTERVAL       10 * 1000
#define A_DEFAULT_LISTEN_INTERVAL         100
#define A_DEFAULT_BMISS_TIME              1500
#define A_MAX_WOW_LISTEN_INTERVAL         300
#define A_MAX_WOW_BMISS_TIME              4500

enum {
    DRV_HB_CHALLENGE = 0,
    APP_HB_CHALLENGE
};

enum {
    WLAN_INIT_MODE_NONE = 0,
    WLAN_INIT_MODE_USR,
    WLAN_INIT_MODE_UDEV,
    WLAN_INIT_MODE_DRV
};

/* Suspend - configuration */
enum {
    WLAN_SUSPEND_CUT_PWR = 0,
    WLAN_SUSPEND_DEEP_SLEEP,
    WLAN_SUSPEND_WOW,
    WLAN_SUSPEND_CUT_PWR_IF_BT_OFF
};

/* WiFi OFF - configuration */
enum {
    WLAN_OFF_CUT_PWR = 0,
    WLAN_OFF_DEEP_SLEEP,
};

/* WLAN low power state */
enum {
    WLAN_POWER_STATE_ON = 0,
    WLAN_POWER_STATE_CUT_PWR = 1,
    WLAN_POWER_STATE_DEEP_SLEEP,
    WLAN_POWER_STATE_WOW
};

/* WLAN WoW State */
enum {
    WLAN_WOW_STATE_NONE = 0,
    WLAN_WOW_STATE_SUSPENDING,
    WLAN_WOW_STATE_SUSPENDED
};


typedef enum _AR6K_BIN_FILE {
    AR6K_OTP_FILE,
    AR6K_FIRMWARE_FILE,
    AR6K_PATCH_FILE,
    AR6K_BOARD_DATA_FILE,
} AR6K_BIN_FILE;

#ifdef SETUPHCI_ENABLED
#define SETUPHCI_DEFAULT           1
#else
#define SETUPHCI_DEFAULT           0
#endif /* SETUPHCI_ENABLED */

#ifdef SETUPHCIPAL_ENABLED
#define SETUPHCIPAL_DEFAULT           1
#else
#define SETUPHCIPAL_DEFAULT           0
#endif /* SETUPHCIPAL_ENABLED */

#ifdef SETUPBTDEV_ENABLED
#define SETUPBTDEV_DEFAULT         1
#else
#define SETUPBTDEV_DEFAULT         0
#endif /* SETUPBTDEV_ENABLED */

#ifdef BMIENABLE_SET
#define BMIENABLE_DEFAULT          1
#else
#define BMIENABLE_DEFAULT          0
#endif /* BMIENABLE_SET */

#ifdef ENABLEUARTPRINT_SET
#define ENABLEUARTPRINT_DEFAULT    1
#else
#define ENABLEUARTPRINT_DEFAULT    0
#endif /* ENABLEARTPRINT_SET */

#ifdef ATH6KL_CONFIG_HIF_VIRTUAL_SCATTER
#define NOHIFSCATTERSUPPORT_DEFAULT    1
#else /* ATH6KL_CONFIG_HIF_VIRTUAL_SCATTER */
#define NOHIFSCATTERSUPPORT_DEFAULT    0
#endif /* ATH6KL_CONFIG_HIF_VIRTUAL_SCATTER */

#ifdef AR600x_BT_AR3001
#define AR3KHCIBAUD_DEFAULT        3000000
#define HCIUARTSCALE_DEFAULT       1
#define HCIUARTSTEP_DEFAULT        8937
#else
#define AR3KHCIBAUD_DEFAULT        0
#define HCIUARTSCALE_DEFAULT       0
#define HCIUARTSTEP_DEFAULT        0
#endif /* AR600x_BT_AR3001 */

#ifdef INIT_MODE_DRV_ENABLED
#define WLAN_INIT_MODE_DEFAULT     WLAN_INIT_MODE_DRV
#else
#define WLAN_INIT_MODE_DEFAULT     WLAN_INIT_MODE_USR
#endif /* INIT_MODE_DRV_ENABLED */

#define AR6K_DATASET_PATCH_ADDRESS(_param, _ver) do { \
    if ((_ver) == AR6003_REV2_VERSION) { \
        (_param) = AR6003_REV2_DATASET_PATCH_ADDRESS; \
    } else if ((_ver) == AR6003_REV3_VERSION) { \
        (_param) = AR6003_REV3_DATASET_PATCH_ADDRESS; \
    } else { \
       AR_DEBUG_PRINTF(ATH_DEBUG_ERR, ("Unknown Version: %d\n", _ver)); \
       A_ASSERT(0); \
    } \
} while (0)

#define AR6K_APP_LOAD_ADDRESS(_param, _ver) do { \
    if ((_ver) == AR6003_REV2_VERSION) { \
        (_param) = AR6003_REV2_APP_LOAD_ADDRESS; \
    } else if ((_ver) == AR6003_REV3_VERSION) { \
        (_param) = AR6003_REV3_APP_LOAD_ADDRESS; \
    } else { \
       AR_DEBUG_PRINTF(ATH_DEBUG_ERR, ("Unknown Version: %d\n", _ver)); \
       A_ASSERT(0); \
    } \
} while (0)

#define AR6K_APP_START_OVERRIDE_ADDRESS(_param, _ver) do { \
    if ((_ver) == AR6003_REV2_VERSION) { \
        (_param) = AR6003_REV2_APP_START_OVERRIDE; \
    } else if ((_ver) == AR6003_REV3_VERSION) { \
        (_param) = AR6003_REV3_APP_START_OVERRIDE; \
    } else { \
       AR_DEBUG_PRINTF(ATH_DEBUG_ERR, ("Unknown Version: %d\n", _ver)); \
       A_ASSERT(0); \
    } \
} while (0)

#define AR6003_SUBVER_DEFAULT               1
#define AR6003_SUBVER_ROUTER                2
#define AR6003_SUBVER_MOBILE                3
#define AR6003_SUBVER_TABLET                4

/* AR6003 2.0 definitions */
#define AR6003_REV2_VERSION                 0x30000384
#define AR6003_REV2_OTP_FILE                "otp.bin.z77"
#define AR6003_REV2_FIRMWARE_FILE           "athwlan.bin.z77"
#define AR6003_REV2_TCMD_FIRMWARE_FILE      "athtcmd_ram.bin"
#define AR6003_REV2_TESTSCRIPT_FILE			"testflow.bin"
#define AR6003_REV2_UTF_FIRMWARE_FILE	    "utf.bin"
#define AR6003_REV2_ART_FIRMWARE_FILE       "device.bin"
#define AR6003_REV2_PATCH_FILE              "data.patch.hw2_0.bin"
#define AR6003_REV2_EPPING_FIRMWARE_FILE    "endpointping.bin"
#ifdef AR600x_SD31_XXX
#define AR6003_REV2_BOARD_DATA_FILE         "bdata.SD31.bin"
#elif defined(AR600x_SD32_XXX)
#define AR6003_REV2_BOARD_DATA_FILE         "bdata.SD32.bin"
#elif defined(AR600x_WB31_XXX)
#define AR6003_REV2_BOARD_DATA_FILE         "bdata.WB31.bin"
#else
#define AR6003_REV2_BOARD_DATA_FILE         "bdata.CUSTOM.bin"
#endif /* Board Data File */

/* AR6003 3.0 definitions */
#define AR6003_REV3_VERSION                 0x30000582
#define AR6003_REV3_OTP_FILE                "ath6k/AR6003/hw2.1.1/otp.bin"
#define AR6003_REV3_DEFAULT_FIRMWARE_FILE   "ath6k/AR6003/hw2.1.1/athwlan.bin"
#define AR6003_REV3_ROUTER_FIRMWARE_FILE    "ath6k/AR6003/hw2.1.1/athwlan_router.bin"
#define AR6003_REV3_MOBILE_FIRMWARE_FILE    "ath6k/AR6003/hw2.1.1/athwlan_mobile.bin"
#define AR6003_REV3_TABLET_FIRMWARE_FILE    "ath6k/AR6003/hw2.1.1/athwlan_tablet.bin"
#define AR6003_REV3_TCMD_FIRMWARE_FILE      "ath6k/AR6003/hw2.1.1/athtcmd_ram.bin"
#define AR6003_REV3_TESTSCRIPT_FILE			"ath6k/AR6003/hw2.1.1/testflow.bin"
#define AR6003_REV3_UTF_FIRMWARE_FILE	    "ath6k/AR6003/hw2.1.1/utf.bin"
#define AR6003_REV3_ART_FIRMWARE_FILE       "ath6k/AR6003/hw2.1.1/device.bin"
#define AR6003_REV3_PATCH_FILE              "ath6k/AR6003/hw2.1.1/data.patch.hw3_0.bin"
#define AR6003_REV3_EPPING_FIRMWARE_FILE    "ath6k/AR6003/hw2.1.1/endpointping.bin"
#ifdef AR600x_SD31_XXX
#define AR6003_REV3_BOARD_DATA_FILE         "ath6k/AR6003/hw2.1.1/bdata.SD31.bin"
#elif defined(AR600x_SD32_XXX)
#define AR6003_REV3_BOARD_DATA_FILE         "ath6k/AR6003/hw2.1.1/bdata.SD32.bin"
#elif defined(AR600x_WB31_XXX)
#define AR6003_REV3_BOARD_DATA_FILE         "ath6k/AR6003/hw2.1.1/bdata.WB31.bin"
#else
#define AR6003_REV3_BOARD_DATA_FILE         "ath6k/AR6003/hw2.1.1/bdata.CUSTOM.bin"
#endif /* Board Data File */

/* AP-STA Concurrency */
#define GET_CONN_AP_PRIV(_ar,_priv) do { \
    int i; \
    AR_SOFTC_DEV_T *tDev =NULL; \
    for(i=0;i<_ar->arConfNumDev;i++) { \
        tDev = _ar->arDev[i]; \
        if((tDev->arNetworkType == AP_NETWORK) && \
           (tDev->arConnected)) { \
       _priv = tDev; \
           break; \
        } \
    } \
}while(0);

/* Power states */
enum {
    WLAN_PWR_CTRL_UP = 0,
    WLAN_PWR_CTRL_CUT_PWR,
    WLAN_PWR_CTRL_DEEP_SLEEP,
    WLAN_PWR_CTRL_WOW,
    WLAN_PWR_CTRL_DEEP_SLEEP_DISABLED
};

/* HTC RAW streams */
typedef enum _HTC_RAW_STREAM_ID {
    HTC_RAW_STREAM_NOT_MAPPED = -1,
    HTC_RAW_STREAM_0 = 0,
    HTC_RAW_STREAM_1 = 1,
    HTC_RAW_STREAM_2 = 2,
    HTC_RAW_STREAM_3 = 3,
    HTC_RAW_STREAM_NUM_MAX
} HTC_RAW_STREAM_ID;

#define RAW_HTC_READ_BUFFERS_NUM    4
#define RAW_HTC_WRITE_BUFFERS_NUM   4

#define HTC_RAW_BUFFER_SIZE  1664

typedef struct {
    int currPtr;
    int length;
    A_UINT8       _Pad1[A_CACHE_LINE_PAD];	
    unsigned char data[HTC_RAW_BUFFER_SIZE];
    A_UINT8       _Pad2[A_CACHE_LINE_PAD];	
    HTC_PACKET    HTCPacket;
} raw_htc_buffer;

#ifdef CONFIG_HOST_TCMD_SUPPORT
/*
 *  add TCMD_MODE besides wmi and bypasswmi
 *  in TCMD_MODE, only few TCMD releated wmi commands
 *  counld be hanlder
 */
enum {
    AR6000_WMI_MODE = 0,
    AR6000_BYPASS_MODE,
    AR6000_TCMD_MODE,
    AR6000_WLAN_MODE
};
#endif /* CONFIG_HOST_TCMD_SUPPORT */

struct ar_wep_key {
    A_UINT8                 arKeyIndex;
    A_UINT8                 arKeyLen;
    A_UINT8                 arKey[64];
} ;

#ifdef ATH6K_CONFIG_CFG80211
struct ar_key {
    A_UINT8     key[WLAN_MAX_KEY_LEN];
    A_UINT8     key_len;
    A_UINT8     seq[IW_ENCODE_SEQ_MAX_SIZE];
    A_UINT8     seq_len;
    A_UINT32    cipher;
};
#endif /* ATH6K_CONFIG_CFG80211 */


struct ar_node_mapping {
    A_UINT8                 macAddress[6];
    A_UINT8                 epId;
    A_UINT8                 txPending;
};

struct ar_cookie {
    unsigned long          arc_bp[2];    /* Must be first field */
    HTC_PACKET             HtcPkt;       /* HTC packet wrapper */
    struct ar_cookie *arc_list_next;
};

struct ar_hb_chlng_resp {
    A_TIMER                 timer;
    A_UINT32                frequency;
    A_UINT32                seqNum;
    A_BOOL                  outstanding;
    A_UINT8                 missCnt;
    A_UINT8                 missThres;
};

/* Per STA data, used in AP mode */
/*TODO: All this should move to OS independent dir */

#define STA_PWR_MGMT_MASK   0x1
#define STA_PWR_MGMT_SHIFT  0x0
#define STA_PWR_MGMT_AWAKE  0x0
#define STA_PWR_MGMT_SLEEP  0x1

#define STA_SET_PWR_SLEEP(sta) (sta->flags |= (STA_PWR_MGMT_MASK << STA_PWR_MGMT_SHIFT))
#define STA_CLR_PWR_SLEEP(sta) (sta->flags &= ~(STA_PWR_MGMT_MASK << STA_PWR_MGMT_SHIFT))
#define STA_IS_PWR_SLEEP(sta) ((sta->flags >> STA_PWR_MGMT_SHIFT) & STA_PWR_MGMT_MASK)

#define STA_PS_POLLED_MASK  0x1
#define STA_PS_POLLED_SHIFT 0x1

#define STA_SET_PS_POLLED(sta) (sta->flags |= (STA_PS_POLLED_MASK << STA_PS_POLLED_SHIFT))
#define STA_CLR_PS_POLLED(sta) (sta->flags &= ~(STA_PS_POLLED_MASK << STA_PS_POLLED_SHIFT))
#define STA_IS_PS_POLLED(sta) (sta->flags & (STA_PS_POLLED_MASK << STA_PS_POLLED_SHIFT))

#define STA_APSD_TRIGGER_MASK  0x1
#define STA_APSD_TRIGGER_SHIFT 0x2
#define STA_APSD_EOSP_MASK     0x1
#define STA_APSD_EOSP_SHIFT    0x3

#define STA_SET_APSD_TRIGGER(sta) (sta->flags |= (STA_APSD_TRIGGER_MASK << STA_APSD_TRIGGER_SHIFT))
#define STA_CLR_APSD_TRIGGER(sta) (sta->flags &= ~(STA_APSD_TRIGGER_MASK << STA_APSD_TRIGGER_SHIFT))
#define STA_IS_APSD_TRIGGER(sta)  (sta->flags & (STA_APSD_TRIGGER_MASK << STA_APSD_TRIGGER_SHIFT))

#define STA_SET_APSD_EOSP(sta) (sta->flags |= (STA_APSD_EOSP_MASK << STA_APSD_EOSP_SHIFT))
#define STA_CLR_APSD_EOSP(sta) (sta->flags &= ~(STA_APSD_EOSP_MASK << STA_APSD_EOSP_SHIFT))
#define STA_IS_APSD_EOSP(sta)  (sta->flags & (STA_APSD_EOSP_MASK << STA_APSD_EOSP_SHIFT))

#ifdef ADAPTIVE_POWER_THROUGHPUT_CONTROL
#define APTC_TRAFFIC_SAMPLING_INTERVAL     100  /* msec */
#define APTC_UPPER_THROUGHPUT_THRESHOLD    3000 /* Kbps */
#define APTC_LOWER_THROUGHPUT_THRESHOLD    2000 /* Kbps */


typedef struct aptc_traffic_record {
    A_BOOL timerScheduled;
    struct timeval samplingTS;
    unsigned long bytesReceived;
    unsigned long bytesTransmitted;
} APTC_TRAFFIC_RECORD;

#endif /* ADAPTIVE_POWER_THROUGHPUT_CONTROL */

typedef struct user_rssi_compensation_t {
    A_UINT16         customerID;
    union {
    A_UINT16         a_enable;
    A_UINT16         bg_enable;
    A_UINT16         enable;
    };
    A_INT16          bg_param_a;
    A_INT16          bg_param_b;
    A_INT16          a_param_a;
    A_INT16          a_param_b;
    A_UINT32         reserved;
} USER_RSSI_CPENSATION;



typedef struct {
    A_UINT16                flags;
    A_UINT8                 mac[ATH_MAC_LEN];
    A_UINT8                 aid;
    A_UINT8                 keymgmt;
    A_UINT8                 ucipher;
    A_UINT8                 auth;
    A_UINT8                 wmode;
    A_UINT8                 wpa_ie[IEEE80211_MAX_IE];
    A_UINT8                 apsd_info;
    A_NETBUF_QUEUE_T        psq;        /* power save q */
    A_NETBUF_QUEUE_T        apsdq;      /* APSD delivery enabled q */
    A_MUTEX_T               psqLock;
    A_UINT8                 ba_state[8];
    void                    *conn_aggr;
    void                    *arPriv;
} conn_t;

typedef struct ar6_raw_htc {
    HTC_ENDPOINT_ID         arRaw2EpMapping[HTC_RAW_STREAM_NUM_MAX];
    HTC_RAW_STREAM_ID       arEp2RawMapping[ENDPOINT_MAX];
    struct semaphore        raw_htc_read_sem[HTC_RAW_STREAM_NUM_MAX];
    struct semaphore        raw_htc_write_sem[HTC_RAW_STREAM_NUM_MAX];
    wait_queue_head_t       raw_htc_read_queue[HTC_RAW_STREAM_NUM_MAX];
    wait_queue_head_t       raw_htc_write_queue[HTC_RAW_STREAM_NUM_MAX];
    raw_htc_buffer          raw_htc_read_buffer[HTC_RAW_STREAM_NUM_MAX][RAW_HTC_READ_BUFFERS_NUM];
    raw_htc_buffer          raw_htc_write_buffer[HTC_RAW_STREAM_NUM_MAX][RAW_HTC_WRITE_BUFFERS_NUM];
    A_BOOL                  write_buffer_available[HTC_RAW_STREAM_NUM_MAX];
    A_BOOL                  read_buffer_available[HTC_RAW_STREAM_NUM_MAX];
} AR_RAW_HTC_T;

#ifdef CONFIG_HOST_TCMD_SUPPORT
typedef struct {
    A_UINT16                len;
    A_UINT8                 ver;
    A_UINT8                 reserved;
    A_UINT8                 buf[TC_CMDS_SIZE_MAX];
} AR_TCMD_RESP;
#endif /* CONFIG_HOST_TCMD_SUPPORT */

typedef struct ar6_softc {
    spinlock_t              arLock;
    struct semaphore        arSem;
    A_BOOL                  arWmiReady;
    int                     arTxPending[ENDPOINT_MAX];
    int                     arTotalTxDataPending;
    A_UINT8                 arNumDataEndPts;
    HTC_HANDLE              arHtcTarget;
    void                    *arHifDevice;
    struct ar6000_version   arVersion;
    A_UINT32                arTargetType;
    AR6000_WLAN_STATE       arWlanState;
    struct ar_cookie        *arCookieList;
    A_UINT32                arCookieCount;
    struct ar_hb_chlng_resp arHBChallengeResp;
    HTC_ENDPOINT_ID         arAc2EpMapping[WMM_NUM_AC];
    A_BOOL                  arAcStreamActive[WMM_NUM_AC];
    A_UINT8                 arAcStreamPriMap[WMM_NUM_AC];
    A_UINT8                 arHiAcStreamActivePri;
    A_UINT8                 arEp2AcMapping[ENDPOINT_MAX];
    HTC_ENDPOINT_ID         arControlEp;
#ifdef HTC_RAW_INTERFACE
    AR_RAW_HTC_T            *arRawHtc;
#endif
    A_BOOL                  arRawIfInit;
    COMMON_CREDIT_STATE_INFO arCreditStateInfo;
    A_BOOL                  arWMIControlEpFull;
    A_BOOL                  dbgLogFetchInProgress;
    A_UCHAR                 log_buffer[DBGLOG_HOST_LOG_BUFFER_SIZE];
    A_UINT32                log_cnt;
    A_UINT32                dbglog_init_done;
    HTC_PACKET_QUEUE        amsdu_rx_buffer_queue;
    A_BOOL                  bIsDestroyProgress; /* flag to indicate ar6k destroy is in progress */
    A_UINT8                 rxMetaVersion;
    A_INT32                 (*exitCallback)(void *config);  /* generic callback at AR6K exit */
    HIF_DEVICE_OS_DEVICE_INFO   osDevInfo;
    A_UINT16                arWlanPowerState;
    A_BOOL                  arPlatPowerOff;
    USER_RSSI_CPENSATION    rssi_compensation_param;
#ifdef CONFIG_HOST_TCMD_SUPPORT
    A_UINT8                 tcmdRxReport;
    A_UINT32                tcmdRxTotalPkt;
    A_INT32                 tcmdRxRssi;
    A_UINT32                tcmdPm;
    A_UINT32                arTargetMode;
    A_UINT32                tcmdRxcrcErrPkt;
    A_UINT32                tcmdRxsecErrPkt;
    A_UINT16                tcmdRateCnt[TCMD_MAX_RATES];
    A_UINT16                tcmdRateCntShortGuard[TCMD_MAX_RATES];
    AR_TCMD_RESP            tcmdResp;
#endif
    A_BOOL                  arWlanOff;
#if CONFIG_PM
    A_UINT16                arWowState;
    A_BOOL                  arBTOff;
    A_BOOL                  arBTSharing;
    A_UINT16                arSuspendCutPwrConfig;
    A_UINT16                arSuspendConfig;
    A_UINT16                arWlanOffConfig;
    A_UINT16                arWow2Config;
#endif
#ifndef EXPORT_HCI_BRIDGE_INTERFACE
    void                    *hcidev_info;
#endif
    conn_t                  connTbl[NUM_CONN]; /* AP mode */
    WMI_PER_STA_STAT        arAPStats[NUM_CONN]; /* AP mode */
    A_UINT8                 inter_bss;   /* enable/disable inter bss data forward */
    A_UINT8                 arAcsDisableHiChannel;
    /* AP-STA Concurrency */
    A_UINT8                 arConfNumDev;
    A_UINT8                 arHoldConnection;
    A_TIMER                 ap_reconnect_timer;
    A_UINT8                 gNumSta;
    /* AP-STA Concurrency */
    struct ar6_softc_dev    *arDev[NUM_DEV];
    A_BOOL                  arResumeDone;
    /* Bluetooth Address to be read from OTP */
    A_UINT8                 bdaddr[6];
} AR_SOFTC_T;

typedef struct ar6_softc_ap {
    WMI_AP_ACL              g_acl;              /* AP mode */
    A_UINT8                 sta_list_index;     /* AP mode */
    struct ieee80211req_key ap_mode_bkey;           /* AP mode */
    A_NETBUF_QUEUE_T        mcastpsq;    /* power save q for Mcast frames */
    A_MUTEX_T               mcastpsqLock;
    A_BOOL                  DTIMExpired; /* flag to indicate DTIM expired */
    A_UINT8                 intra_bss;   /* enable/disable intra bss data forward */
    A_UINT8                 ap_hidden_ssid;
    A_UINT8                 ap_country_code[3];
    A_UINT8                 ap_dtim_period;
    A_UINT16                ap_beacon_interval;
    A_UINT16                arRTS;
    void                    *pDfs;  /* Pointer to DFS state structure */
    A_BOOL                  deKeySet;
}AR_SOFTC_AP_T;

typedef struct ar6_softc_sta {
    A_BOOL                  arConnectPending;
    A_UINT8                 arReqBssid[ATH_MAC_LEN];
    A_UINT16                arListenIntervalB;
    A_UINT16                arListenIntervalT;
    A_UINT16                arBmissTimeB;
    A_UINT16                arBmissTimeT;
    A_INT8                  arRssi;
    A_UINT8                 arSkipScan;
    A_UINT16                arBeaconInterval;
    A_UINT8                 arKeepaliveConfigured;
    A_UINT8                 arIbssPsEnable;
    A_UINT32                arMgmtFilter;
    struct ar_node_mapping  arNodeMap[MAX_NODE_NUM];
    A_UINT8                 arNodeNum;
    A_UINT8                 arNexEpId;
    A_UINT32                arConnectCtrlFlags;
#ifdef USER_KEYS
    A_INT32                 user_savedkeys_stat;
    A_UINT32                user_key_ctrl;
    struct USER_SAVEDKEYS   user_saved_keys;
#endif
    USER_RSSI_THOLD         rssi_map[12];
    A_TIMER                 disconnect_timer;
    A_UINT8                 arUserBssFilter;
    A_INT8                  arNumChannels;
    A_UINT16                arChannelList[WMI_MAX_CHANNELS];
    A_UINT8                 scan_triggered;
    WMI_SCAN_PARAMS_CMD     scParams;
    A_UINT8                 scanSpecificSsid;
    A_BOOL                  wpaOffloadEnabled;
    A_BOOL                  disconnect_timer_inited;
}AR_SOFTC_STA_T;

typedef struct ar6_softc_dev {
    struct net_device       *arNetDev;    /* net_device pointer */
    void                    *arWmi;
    A_BOOL                  arWmiEnabled;
    wait_queue_head_t       arEvent;
    spinlock_t              arPrivLock;
    A_INT8                  arMaxRetries;
    A_BOOL                  statsUpdatePending;
    A_UINT8                 arPhyCapability;
    A_UINT16                arChannelHint;
    A_UINT16                arBssChannel;
    A_BOOL                  arConnected;
    int                     arSsidLen;
    u_char                  arSsid[WMI_MAX_SSID_LEN];
    A_UINT8                 arNextMode;
    A_UINT8                 arNetworkType;
    A_UINT8                 arNetworkSubType;
    A_UINT8                 arDot11AuthMode;
    A_UINT8                 arAuthMode;
    A_UINT8                 arPairwiseCrypto;
    A_UINT8                 arPairwiseCryptoLen;
    A_UINT8                 arGroupCrypto;
    A_UINT8                 arGroupCryptoLen;
    A_UINT8                 arDefTxKeyIndex;
    struct ar_wep_key       arWepKeyList[WMI_MAX_KEY_INDEX + 1];
    A_UINT8                 arBssid[ATH_MAC_LEN];
    A_UINT8                 arTxPwr;
    A_BOOL                  arTxPwrSet;
    A_INT32                 arBitRate;
    struct net_device_stats arNetStats;
    struct iw_statistics    arIwStats;
    A_UINT32                arRateMask[WMI_MAX_RATE_MASK];
    A_BOOL                  arNetQueueStopped;
    A_UINT8                 arDeviceIndex;
    A_BOOL                  arWmmEnabled;
    A_UINT32                arRegCode;
    A_UINT16                ap_profile_flag;    /* AP mode */
    WMI_BTCOEX_CONFIG_EVENT arBtcoexConfig;
    WMI_BTCOEX_STATS_EVENT  arBtcoexStats;
    WMI_GET_WAC_INFO        wacInfo;
#define AR_MCAST_FILTER_MAC_ADDR_SIZE  6
    A_UINT8                 mcast_filters[MAC_MAX_FILTERS_PER_LIST][AR_MCAST_FILTER_MAC_ADDR_SIZE];
    A_UINT8                 bdaddr[ATH_MAC_LEN];
#ifdef ADAPTIVE_POWER_THROUGHPUT_CONTROL
    APTC_TRAFFIC_RECORD     aptcTR;
#endif
#ifdef ATH6K_CONFIG_CFG80211
    struct wireless_dev             *wdev;
    struct cfg80211_scan_request    *scan_request;
    struct ar_key                   keys[WMI_MAX_KEY_INDEX + 1];
#endif /* ATH6K_CONFIG_CFG80211 */
    A_TIMER                 ap_acs_timer;
    TARGET_STATS            arTargetStats;
    void                    *conn_aggr;
    void                    *p2p_ctx;
    AR_SOFTC_AP_T           arAp;
    AR_SOFTC_STA_T          arSta;
    AR_SOFTC_T              *arSoftc;
    A_UINT8                 arHoldConnection;
    A_BOOL                  arDoConnectOnResume;
    A_UINT8                 num_sta;
    void                    *hcipal_info;
    A_BOOL                  isBt30amp;
    A_UINT8                 phymode;
}AR_SOFTC_DEV_T;

#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,0)
/* Looks like we need this for 2.4 kernels */
static inline void *ar6k_priv(struct net_device *dev)
{
    return(dev->priv);
}
#else
#ifdef ATH6K_CONFIG_CFG80211
static inline void *ar6k_priv(struct net_device *dev)
{
    return (wdev_priv(dev->ieee80211_ptr));
}
#else
#define ar6k_priv   netdev_priv
#endif /* ATH6K_CONFIG_CFG80211 */
#endif /* LINUX_VERSION_CODE < KERNEL_VERSION(2,6,0) */

#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,34)
#define SET_HCI_BUS_TYPE(pHciDev, __bus, __type) do { \
    (pHciDev)->type = (__bus); \
} while(0)
#else
#define SET_HCI_BUS_TYPE(pHciDev, __bus, __type) do { \
    (pHciDev)->bus = (__bus); \
    (pHciDev)->dev_type = (__type); \
} while(0)
#endif

#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,20)
#define GET_INODE_FROM_FILEP(filp) \
    (filp)->f_path.dentry->d_inode
#else
#define GET_INODE_FROM_FILEP(filp) \
    (filp)->f_dentry->d_inode
#endif

#define arAc2EndpointID(ar,ac)          (ar)->arAc2EpMapping[(ac)]
#define arSetAc2EndpointIDMap(ar,ac,ep)  \
{  (ar)->arAc2EpMapping[(ac)] = (ep); \
   (ar)->arEp2AcMapping[(ep)] = (ac); }
#define arEndpoint2Ac(ar,ep)           (ar)->arEp2AcMapping[(ep)]

#define arRawIfEnabled(ar) (ar)->arRawIfInit
#define arRawStream2EndpointID(ar,raw)          (ar)->arRawHtc->arRaw2EpMapping[(raw)]
#define arSetRawStream2EndpointIDMap(ar,raw,ep)  \
{  (ar)->arRawHtc->arRaw2EpMapping[(raw)] = (ep); \
   (ar)->arRawHtc->arEp2RawMapping[(ep)] = (raw); }
#define arEndpoint2RawStreamID(ar,ep)           (ar)->arRawHtc->arEp2RawMapping[(ep)]

struct ar_giwscan_param {
    char    *current_ev;
    char    *end_buf;
    A_UINT32 bytes_needed;
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,27)
    struct iw_request_info *info;
#endif
};

#define AR6000_STAT_INC(ar, stat)       (ar->arNetStats.stat++)

#define AR6000_SPIN_LOCK(lock, param)   do {                            \
    if (irqs_disabled()) {                                              \
        AR_DEBUG_PRINTF(ATH_DEBUG_TRC,("IRQs disabled:AR6000_LOCK\n"));                 \
    }                                                                   \
    spin_lock_bh(lock);                                                 \
} while (0)

#define AR6000_SPIN_UNLOCK(lock, param) do {                            \
    if (irqs_disabled()) {                                              \
        AR_DEBUG_PRINTF(ATH_DEBUG_TRC,("IRQs disabled: AR6000_UNLOCK\n"));              \
    }                                                                   \
    spin_unlock_bh(lock);                                               \
} while (0)

int ar6000_ioctl(struct net_device *dev, struct ifreq *rq, int cmd);
int ar6000_ioctl_dispatcher(struct net_device *dev, struct ifreq *rq, int cmd);
void ar6000_gpio_init(void);
void ar6000_init_profile_info(AR_SOFTC_DEV_T *arPriv);
void ar6000_install_static_wep_keys(AR_SOFTC_DEV_T *arPriv);
int ar6000_init(struct net_device *dev);
int ar6000_dbglog_get_debug_logs(AR_SOFTC_T *ar);
void ar6000_TxDataCleanup(AR_SOFTC_T *ar);
int ar6000_acl_data_tx(struct sk_buff *skb, AR_SOFTC_DEV_T *arPriv);
void ar6000_restart_endpoint(AR_SOFTC_T *ar);
void ar6000_stop_endpoint(AR_SOFTC_T *ar, A_BOOL keepprofile, A_BOOL getdbglogs);

#ifdef HTC_RAW_INTERFACE

#ifndef __user
#define __user
#endif

int ar6000_htc_raw_open(AR_SOFTC_T *ar);
int ar6000_htc_raw_close(AR_SOFTC_T *ar);
ssize_t ar6000_htc_raw_read(AR_SOFTC_T *ar,
                            HTC_RAW_STREAM_ID StreamID,
                            char __user *buffer, size_t count);
ssize_t ar6000_htc_raw_write(AR_SOFTC_T *ar,
                             HTC_RAW_STREAM_ID StreamID,
                             char __user *buffer, size_t count);

#endif /* HTC_RAW_INTERFACE */

/* AP mode */
/*TODO: These routines should be moved to a file that is common across OS */
conn_t *
ieee80211_find_conn(AR_SOFTC_DEV_T *arPriv, A_UINT8 *node_addr);

conn_t *
ieee80211_find_conn_for_aid(AR_SOFTC_DEV_T *arPriv, A_UINT8 aid);

A_UINT8
remove_sta(AR_SOFTC_DEV_T *arPriv, A_UINT8 *mac, A_UINT16 reason);

void
ar6000_ap_cleanup(AR_SOFTC_DEV_T *arPriv);

int
ar6000_ap_set_num_sta(AR_SOFTC_T *ar, AR_SOFTC_DEV_T *arPriv, A_UINT8 num_sta);

/* HCI support */

#ifndef EXPORT_HCI_BRIDGE_INTERFACE
A_STATUS ar6000_setup_hci(AR_SOFTC_T *ar);
void     ar6000_cleanup_hci(AR_SOFTC_T *ar);
void     ar6000_set_default_ar3kconfig(AR_SOFTC_T *ar, void *ar3kconfig);

/* HCI bridge testing */
A_STATUS hci_test_send(AR_SOFTC_T *ar, struct sk_buff *skb);
#endif
void ar6000_init_mode_info(AR_SOFTC_DEV_T *arPriv);
ATH_DEBUG_DECLARE_EXTERN(htc);
ATH_DEBUG_DECLARE_EXTERN(wmi);
ATH_DEBUG_DECLARE_EXTERN(bmi);
ATH_DEBUG_DECLARE_EXTERN(hif);
ATH_DEBUG_DECLARE_EXTERN(wlan);
ATH_DEBUG_DECLARE_EXTERN(misc);

extern A_UINT8 bcast_mac[];
extern A_UINT8 null_mac[];

#ifdef __cplusplus
}
#endif

#endif /* _AR6000_H_ */
