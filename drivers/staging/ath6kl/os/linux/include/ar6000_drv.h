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

#include <linux/init.h>
#include <linux/sched.h>
#include <linux/spinlock.h>
#include <linux/if_ether.h>
#include <linux/etherdevice.h>
#include <net/iw_handler.h>
#include <linux/if_arp.h>
#include <linux/ip.h>
#include <linux/wireless.h>
#include <net/cfg80211.h>
#include <linux/module.h>
#include <asm/io.h>

#include <a_config.h>
#include <athdefs.h>
#include "a_osapi.h"
#include "htc_api.h"
#include "wmi.h"
#include "a_drv.h"
#include "bmi.h"
#include <ieee80211.h>
#include <ieee80211_ioctl.h>
#include <wlan_api.h>
#include <wmi_api.h>
#include "pkt_log.h"
#include "aggr_recv_api.h"
#include <host_version.h>
#include <linux/rtnetlink.h>
#include <linux/moduleparam.h>
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
#include "hw/apb_map.h"
#include "hw/rtc_reg.h"
#include "hw/mbox_reg.h"
#include "gpio_reg.h"

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


#define USER_SAVEDKEYS_STAT_INIT     0
#define USER_SAVEDKEYS_STAT_RUN      1

// TODO this needs to move into the AR_SOFTC struct
struct USER_SAVEDKEYS {
    struct ieee80211req_key   ucast_ik;
    struct ieee80211req_key   bcast_ik;
    CRYPTO_TYPE               keyType;
    bool                    keyOk;
};

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


int ar6000_ReadRegDiag(struct hif_device *hifDevice, u32 *address, u32 *data);
int ar6000_WriteRegDiag(struct hif_device *hifDevice, u32 *address, u32 *data);

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
#define MAX_DEF_COOKIE_NUM                180
#define MAX_HI_COOKIE_NUM                 18 /* 10% of MAX_COOKIE_NUM */
#define MAX_COOKIE_NUM                    (MAX_DEF_COOKIE_NUM + MAX_HI_COOKIE_NUM)

/* MAX_DEFAULT_SEND_QUEUE_DEPTH is used to set the default queue depth for the
 * WMM send queues.  If a queue exceeds this depth htc will query back to the
 * OS specific layer by calling EpSendFull().  This gives the OS layer the
 * opportunity to drop the packet if desired.  Therefore changing
 * MAX_DEFAULT_SEND_QUEUE_DEPTH does not affect resource utilization but
 * does impact the threshold used to identify if a packet should be
 * dropped. */
#define MAX_DEFAULT_SEND_QUEUE_DEPTH      (MAX_DEF_COOKIE_NUM / WMM_NUM_AC)

#define AR6000_HB_CHALLENGE_RESP_FREQ_DEFAULT        1
#define AR6000_HB_CHALLENGE_RESP_MISS_THRES_DEFAULT  1
#define A_DISCONNECT_TIMER_INTERVAL       10 * 1000
#define A_DEFAULT_LISTEN_INTERVAL         100
#define A_MAX_WOW_LISTEN_INTERVAL         1000

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
    WLAN_WOW_STATE_SUSPENDED,
    WLAN_WOW_STATE_SUSPENDING
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

#ifdef SETUPBTDEV_ENABLED
#define SETUPBTDEV_DEFAULT         1
#else
#define SETUPBTDEV_DEFAULT         0
#endif /* SETUPBTDEV_ENABLED */

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


#if defined(CONFIG_ATH6KL_ENABLE_COEXISTENCE)

#ifdef CONFIG_AR600x_BT_QCOM
#define ATH6KL_BT_DEV 1
#elif defined(CONFIG_AR600x_BT_CSR)
#define ATH6KL_BT_DEV 2
#else
#define ATH6KL_BT_DEV 3
#endif

#ifdef CONFIG_AR600x_DUAL_ANTENNA
#define ATH6KL_BT_ANTENNA 2
#else
#define ATH6KL_BT_ANTENNA 1
#endif

#endif /* CONFIG_ATH6KL_ENABLE_COEXISTENCE */

#ifdef AR600x_BT_AR3001
#define AR3KHCIBAUD_DEFAULT        3000000
#define HCIUARTSCALE_DEFAULT       1
#define HCIUARTSTEP_DEFAULT        8937
#else
#define AR3KHCIBAUD_DEFAULT        0
#define HCIUARTSCALE_DEFAULT       0
#define HCIUARTSTEP_DEFAULT        0
#endif /* AR600x_BT_AR3001 */

#define WLAN_INIT_MODE_DEFAULT     WLAN_INIT_MODE_DRV

#define AR6K_PATCH_DOWNLOAD_ADDRESS(_param, _ver) do { \
    if ((_ver) == AR6003_REV1_VERSION) { \
        (_param) = AR6003_REV1_PATCH_DOWNLOAD_ADDRESS; \
    } else if ((_ver) == AR6003_REV2_VERSION) { \
        (_param) = AR6003_REV2_PATCH_DOWNLOAD_ADDRESS; \
    } else { \
       AR_DEBUG_PRINTF(ATH_DEBUG_ERR, ("Unknown Version: %d\n", _ver)); \
       A_ASSERT(0); \
    } \
} while (0)

#define AR6K_DATA_DOWNLOAD_ADDRESS(_param, _ver) do { \
    if ((_ver) == AR6003_REV1_VERSION) { \
        (_param) = AR6003_REV1_DATA_DOWNLOAD_ADDRESS; \
    } else if ((_ver) == AR6003_REV2_VERSION) { \
        (_param) = AR6003_REV2_DATA_DOWNLOAD_ADDRESS; \
    } else { \
       AR_DEBUG_PRINTF(ATH_DEBUG_ERR, ("Unknown Version: %d\n", _ver)); \
       A_ASSERT(0); \
    } \
} while (0)

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

/* AR6003 1.0 definitions */
#define AR6003_REV1_VERSION                 0x300002ba
#define AR6003_REV1_DATA_DOWNLOAD_ADDRESS   AR6003_REV1_OTP_DATA_ADDRESS
#define AR6003_REV1_PATCH_DOWNLOAD_ADDRESS  0x57ea6c
#define AR6003_REV1_OTP_FILE                "ath6k/AR6003/hw1.0/otp.bin.z77"
#define AR6003_REV1_FIRMWARE_FILE           "ath6k/AR6003/hw1.0/athwlan.bin.z77"
#define AR6003_REV1_TCMD_FIRMWARE_FILE      "ath6k/AR6003/hw1.0/athtcmd_ram.bin"
#define AR6003_REV1_ART_FIRMWARE_FILE       "ath6k/AR6003/hw1.0/device.bin"
#define AR6003_REV1_PATCH_FILE              "ath6k/AR6003/hw1.0/data.patch.bin"
#define AR6003_REV1_EPPING_FIRMWARE_FILE    "ath6k/AR6003/hw1.0/endpointping.bin"
#ifdef CONFIG_AR600x_SD31_XXX
#define AR6003_REV1_BOARD_DATA_FILE         "ath6k/AR6003/hw1.0/bdata.SD31.bin"
#elif defined(CONFIG_AR600x_SD32_XXX)
#define AR6003_REV1_BOARD_DATA_FILE         "ath6k/AR6003/hw1.0/bdata.SD32.bin"
#elif defined(CONFIG_AR600x_WB31_XXX)
#define AR6003_REV1_BOARD_DATA_FILE         "ath6k/AR6003/hw1.0/bdata.WB31.bin"
#else
#define AR6003_REV1_BOARD_DATA_FILE         "ath6k/AR6003/hw1.0/bdata.CUSTOM.bin"
#endif /* Board Data File */

/* AR6003 2.0 definitions */
#define AR6003_REV2_VERSION                 0x30000384 
#define AR6003_REV2_DATA_DOWNLOAD_ADDRESS   AR6003_REV2_OTP_DATA_ADDRESS
#define AR6003_REV2_PATCH_DOWNLOAD_ADDRESS  0x57e910
#define AR6003_REV2_OTP_FILE                "ath6k/AR6003/hw2.0/otp.bin.z77"
#define AR6003_REV2_FIRMWARE_FILE           "ath6k/AR6003/hw2.0/athwlan.bin.z77"
#define AR6003_REV2_TCMD_FIRMWARE_FILE      "ath6k/AR6003/hw2.0/athtcmd_ram.bin"
#define AR6003_REV2_ART_FIRMWARE_FILE       "ath6k/AR6003/hw2.0/device.bin"
#define AR6003_REV2_PATCH_FILE              "ath6k/AR6003/hw2.0/data.patch.bin"
#define AR6003_REV2_EPPING_FIRMWARE_FILE    "ath6k/AR6003/hw2.0/endpointping.bin"
#ifdef CONFIG_AR600x_SD31_XXX
#define AR6003_REV2_BOARD_DATA_FILE         "ath6k/AR6003/hw2.0/bdata.SD31.bin"
#elif defined(CONFIG_AR600x_SD32_XXX)
#define AR6003_REV2_BOARD_DATA_FILE         "ath6k/AR6003/hw2.0/bdata.SD32.bin"
#elif defined(CONFIG_AR600x_WB31_XXX)
#define AR6003_REV2_BOARD_DATA_FILE         "ath6k/AR6003/hw2.0/bdata.WB31.bin"
#else
#define AR6003_REV2_BOARD_DATA_FILE         "ath6k/AR6003/hw2.0/bdata.CUSTOM.bin"
#endif /* Board Data File */

/* AR6003 3.0 definitions */
#define AR6003_REV3_VERSION                 0x30000582
#define AR6003_REV3_OTP_FILE                "ath6k/AR6003/hw2.1.1/otp.bin"
#define AR6003_REV3_FIRMWARE_FILE           "ath6k/AR6003/hw2.1.1/athwlan.bin"
#define AR6003_REV3_TCMD_FIRMWARE_FILE    "ath6k/AR6003/hw2.1.1/athtcmd_ram.bin"
#define AR6003_REV3_ART_FIRMWARE_FILE       "ath6k/AR6003/hw2.1.1/device.bin"
#define AR6003_REV3_PATCH_FILE            "ath6k/AR6003/hw2.1.1/data.patch.bin"
#define AR6003_REV3_EPPING_FIRMWARE_FILE "ath6k/AR6003/hw2.1.1/endpointping.bin"
#ifdef CONFIG_AR600x_SD31_XXX
#define AR6003_REV3_BOARD_DATA_FILE       "ath6k/AR6003/hw2.1.1/bdata.SD31.bin"
#elif defined(CONFIG_AR600x_SD32_XXX)
#define AR6003_REV3_BOARD_DATA_FILE        "ath6k/AR6003/hw2.1.1/bdata.SD32.bin"
#elif defined(CONFIG_AR600x_WB31_XXX)
#define AR6003_REV3_BOARD_DATA_FILE        "ath6k/AR6003/hw2.1.1/bdata.WB31.bin"
#else
#define AR6003_REV3_BOARD_DATA_FILE      "ath6k/AR6003/hw2.1.1/bdata.CUSTOM.bin"
#endif /* Board Data File */


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
    unsigned char data[HTC_RAW_BUFFER_SIZE];
    struct htc_packet    HTCPacket;
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
    u8 arKeyIndex;
    u8 arKeyLen;
    u8 arKey[64];
} ;

struct ar_key {
    u8 key[WLAN_MAX_KEY_LEN];
    u8 key_len;
    u8 seq[IW_ENCODE_SEQ_MAX_SIZE];
    u8 seq_len;
    u32 cipher;
};

enum {
    SME_DISCONNECTED,
    SME_CONNECTING,
    SME_CONNECTED
};

struct ar_node_mapping {
    u8 macAddress[6];
    u8 epId;
    u8 txPending;
};

struct ar_cookie {
    unsigned long          arc_bp[2];    /* Must be first field */
    struct htc_packet             HtcPkt;       /* HTC packet wrapper */
    struct ar_cookie *arc_list_next;
};

struct ar_hb_chlng_resp {
    A_TIMER                 timer;
    u32 frequency;
    u32 seqNum;
    bool                  outstanding;
    u8 missCnt;
    u8 missThres;
};

/* Per STA data, used in AP mode */
/*TODO: All this should move to OS independent dir */

#define STA_PWR_MGMT_MASK 0x1
#define STA_PWR_MGMT_SHIFT 0x0
#define STA_PWR_MGMT_AWAKE 0x0
#define STA_PWR_MGMT_SLEEP 0x1

#define STA_SET_PWR_SLEEP(sta) (sta->flags |= (STA_PWR_MGMT_MASK << STA_PWR_MGMT_SHIFT))
#define STA_CLR_PWR_SLEEP(sta) (sta->flags &= ~(STA_PWR_MGMT_MASK << STA_PWR_MGMT_SHIFT))
#define STA_IS_PWR_SLEEP(sta) ((sta->flags >> STA_PWR_MGMT_SHIFT) & STA_PWR_MGMT_MASK)

#define STA_PS_POLLED_MASK 0x1
#define STA_PS_POLLED_SHIFT 0x1
#define STA_SET_PS_POLLED(sta) (sta->flags |= (STA_PS_POLLED_MASK << STA_PS_POLLED_SHIFT))
#define STA_CLR_PS_POLLED(sta) (sta->flags &= ~(STA_PS_POLLED_MASK << STA_PS_POLLED_SHIFT))
#define STA_IS_PS_POLLED(sta) (sta->flags & (STA_PS_POLLED_MASK << STA_PS_POLLED_SHIFT))

typedef struct {
    u16 flags;
    u8 mac[ATH_MAC_LEN];
    u8 aid;
    u8 keymgmt;
    u8 ucipher;
    u8 auth;
    u8 wpa_ie[IEEE80211_MAX_IE];
    A_NETBUF_QUEUE_T        psq;    /* power save q */
    A_MUTEX_T               psqLock;
} sta_t;

typedef struct ar6_raw_htc {
    HTC_ENDPOINT_ID         arRaw2EpMapping[HTC_RAW_STREAM_NUM_MAX];
    HTC_RAW_STREAM_ID       arEp2RawMapping[ENDPOINT_MAX];
    struct semaphore        raw_htc_read_sem[HTC_RAW_STREAM_NUM_MAX];
    struct semaphore        raw_htc_write_sem[HTC_RAW_STREAM_NUM_MAX];
    wait_queue_head_t       raw_htc_read_queue[HTC_RAW_STREAM_NUM_MAX];
    wait_queue_head_t       raw_htc_write_queue[HTC_RAW_STREAM_NUM_MAX];
    raw_htc_buffer          raw_htc_read_buffer[HTC_RAW_STREAM_NUM_MAX][RAW_HTC_READ_BUFFERS_NUM];
    raw_htc_buffer          raw_htc_write_buffer[HTC_RAW_STREAM_NUM_MAX][RAW_HTC_WRITE_BUFFERS_NUM];
    bool                  write_buffer_available[HTC_RAW_STREAM_NUM_MAX];
    bool                  read_buffer_available[HTC_RAW_STREAM_NUM_MAX];
} AR_RAW_HTC_T;

struct ar6_softc {
    struct net_device       *arNetDev;    /* net_device pointer */
    void                    *arWmi;
    int                     arTxPending[ENDPOINT_MAX];
    int                     arTotalTxDataPending;
    u8 arNumDataEndPts;
    bool                  arWmiEnabled;
    bool                  arWmiReady;
    bool                  arConnected;
    HTC_HANDLE              arHtcTarget;
    void                    *arHifDevice;
    spinlock_t              arLock;
    struct semaphore        arSem;
    int                     arSsidLen;
    u_char                  arSsid[32];
    u8 arNextMode;
    u8 arNetworkType;
    u8 arDot11AuthMode;
    u8 arAuthMode;
    u8 arPairwiseCrypto;
    u8 arPairwiseCryptoLen;
    u8 arGroupCrypto;
    u8 arGroupCryptoLen;
    u8 arDefTxKeyIndex;
    struct ar_wep_key       arWepKeyList[WMI_MAX_KEY_INDEX + 1];
    u8 arBssid[6];
    u8 arReqBssid[6];
    u16 arChannelHint;
    u16 arBssChannel;
    u16 arListenIntervalB;
    u16 arListenIntervalT;
    struct ar6000_version   arVersion;
    u32 arTargetType;
    s8 arRssi;
    u8 arTxPwr;
    bool                  arTxPwrSet;
    s32 arBitRate;
    struct net_device_stats arNetStats;
    struct iw_statistics    arIwStats;
    s8 arNumChannels;
    u16 arChannelList[32];
    u32 arRegCode;
    bool                  statsUpdatePending;
    TARGET_STATS            arTargetStats;
    s8 arMaxRetries;
    u8 arPhyCapability;
#ifdef CONFIG_HOST_TCMD_SUPPORT
    u32 arTargetMode;
    void *tcmd_rx_report;
    int tcmd_rx_report_len;
#endif
    AR6000_WLAN_STATE       arWlanState;
    struct ar_node_mapping  arNodeMap[MAX_NODE_NUM];
    u8 arIbssPsEnable;
    u8 arNodeNum;
    u8 arNexEpId;
    struct ar_cookie        *arCookieList;
    u32 arCookieCount;
    u32 arRateMask;
    u8 arSkipScan;
    u16 arBeaconInterval;
    bool                  arConnectPending;
    bool                  arWmmEnabled;
    struct ar_hb_chlng_resp arHBChallengeResp;
    u8 arKeepaliveConfigured;
    u32 arMgmtFilter;
    HTC_ENDPOINT_ID         arAc2EpMapping[WMM_NUM_AC];
    bool                  arAcStreamActive[WMM_NUM_AC];
    u8 arAcStreamPriMap[WMM_NUM_AC];
    u8 arHiAcStreamActivePri;
    u8 arEp2AcMapping[ENDPOINT_MAX];
    HTC_ENDPOINT_ID         arControlEp;
#ifdef HTC_RAW_INTERFACE
    AR_RAW_HTC_T            *arRawHtc;
#endif
    bool                  arNetQueueStopped;
    bool                  arRawIfInit;
    int                     arDeviceIndex;
    struct common_credit_state_info arCreditStateInfo;
    bool                  arWMIControlEpFull;
    bool                  dbgLogFetchInProgress;
    u8                 log_buffer[DBGLOG_HOST_LOG_BUFFER_SIZE];
    u32 log_cnt;
    u32 dbglog_init_done;
    u32 arConnectCtrlFlags;
    s32 user_savedkeys_stat;
    u32 user_key_ctrl;
    struct USER_SAVEDKEYS   user_saved_keys;
    USER_RSSI_THOLD rssi_map[12];
    u8 arUserBssFilter;
    u16 ap_profile_flag;    /* AP mode */
    WMI_AP_ACL              g_acl;              /* AP mode */
    sta_t                   sta_list[AP_MAX_NUM_STA]; /* AP mode */
    u8 sta_list_index;     /* AP mode */
    struct ieee80211req_key ap_mode_bkey;           /* AP mode */
    A_NETBUF_QUEUE_T        mcastpsq;    /* power save q for Mcast frames */
    A_MUTEX_T               mcastpsqLock;
    bool                  DTIMExpired; /* flag to indicate DTIM expired */
    u8 intra_bss;   /* enable/disable intra bss data forward */
    void                    *aggr_cntxt;
#ifndef EXPORT_HCI_BRIDGE_INTERFACE
    void                    *hcidev_info;
#endif
    WMI_AP_MODE_STAT        arAPStats;
    u8 ap_hidden_ssid;
    u8 ap_country_code[3];
    u8 ap_wmode;
    u8 ap_dtim_period;
    u16 ap_beacon_interval;
    u16 arRTS;
    u16 arACS; /* AP mode - Auto Channel Selection */
    struct htc_packet_queue        amsdu_rx_buffer_queue;
    bool                  bIsDestroyProgress; /* flag to indicate ar6k destroy is in progress */
    A_TIMER                 disconnect_timer;
    u8 rxMetaVersion;
#ifdef WAPI_ENABLE
    u8 arWapiEnable;
#endif
	WMI_BTCOEX_CONFIG_EVENT arBtcoexConfig;
	WMI_BTCOEX_STATS_EVENT  arBtcoexStats;
    s32 (*exitCallback)(void *config);  /* generic callback at AR6K exit */
    struct hif_device_os_device_info   osDevInfo;
    struct wireless_dev *wdev;
    struct cfg80211_scan_request    *scan_request;
    struct ar_key   keys[WMI_MAX_KEY_INDEX + 1];
    u32 smeState;
    u16 arWlanPowerState;
    bool                  arWlanOff;
#ifdef CONFIG_PM
    u16 arWowState;
    bool                  arBTOff;
    bool                  arBTSharing;
    u16 arSuspendConfig;
    u16 arWlanOffConfig;
    u16 arWow2Config;
#endif
    u8 scan_triggered;
    WMI_SCAN_PARAMS_CMD     scParams;
#define AR_MCAST_FILTER_MAC_ADDR_SIZE  4
    u8 mcast_filters[MAC_MAX_FILTERS_PER_LIST][AR_MCAST_FILTER_MAC_ADDR_SIZE];
    u8 bdaddr[6];
    bool                  scanSpecificSsid;
#ifdef CONFIG_AP_VIRTUAL_ADAPTER_SUPPORT
    void                    *arApDev;
#endif
    u8 arAutoAuthStage;

	u8 *fw_otp;
	size_t fw_otp_len;
	u8 *fw;
	size_t fw_len;
	u8 *fw_patch;
	size_t fw_patch_len;
	u8 *fw_data;
	size_t fw_data_len;
};

#ifdef CONFIG_AP_VIRTUAL_ADAPTER_SUPPORT
struct ar_virtual_interface {
    struct net_device       *arNetDev;    /* net_device pointer */
    struct ar6_softc              *arDev;       /* ar device pointer */
    struct net_device       *arStaNetDev; /* net_device pointer */
};
#endif /* CONFIG_AP_VIRTUAL_ADAPTER_SUPPORT */

static inline void *ar6k_priv(struct net_device *dev)
{
    return (wdev_priv(dev->ieee80211_ptr));
}

#define SET_HCI_BUS_TYPE(pHciDev, __bus, __type) do { \
    (pHciDev)->bus = (__bus); \
    (pHciDev)->dev_type = (__type); \
} while(0)

#define GET_INODE_FROM_FILEP(filp) \
    (filp)->f_path.dentry->d_inode

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
    char *current_ev;
    char *end_buf;
    u32 bytes_needed;
    struct iw_request_info *info;
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

void ar6000_init_profile_info(struct ar6_softc *ar);
void ar6000_install_static_wep_keys(struct ar6_softc *ar);
int ar6000_init(struct net_device *dev);
int ar6000_dbglog_get_debug_logs(struct ar6_softc *ar);
void ar6000_TxDataCleanup(struct ar6_softc *ar);
int ar6000_acl_data_tx(struct sk_buff *skb, struct net_device *dev);
void ar6000_restart_endpoint(struct net_device *dev);
void ar6000_stop_endpoint(struct net_device *dev, bool keepprofile, bool getdbglogs);

#ifdef HTC_RAW_INTERFACE

#ifndef __user
#define __user
#endif

int ar6000_htc_raw_open(struct ar6_softc *ar);
int ar6000_htc_raw_close(struct ar6_softc *ar);
ssize_t ar6000_htc_raw_read(struct ar6_softc *ar,
                            HTC_RAW_STREAM_ID StreamID,
                            char __user *buffer, size_t count);
ssize_t ar6000_htc_raw_write(struct ar6_softc *ar,
                             HTC_RAW_STREAM_ID StreamID,
                             char __user *buffer, size_t count);

#endif /* HTC_RAW_INTERFACE */

/* AP mode */
/*TODO: These routines should be moved to a file that is common across OS */
sta_t *
ieee80211_find_conn(struct ar6_softc *ar, u8 *node_addr);

sta_t *
ieee80211_find_conn_for_aid(struct ar6_softc *ar, u8 aid);

u8 remove_sta(struct ar6_softc *ar, u8 *mac, u16 reason);

/* HCI support */

#ifndef EXPORT_HCI_BRIDGE_INTERFACE
int ar6000_setup_hci(struct ar6_softc *ar);
void     ar6000_cleanup_hci(struct ar6_softc *ar);
void     ar6000_set_default_ar3kconfig(struct ar6_softc *ar, void *ar3kconfig);

/* HCI bridge testing */
int hci_test_send(struct ar6_softc *ar, struct sk_buff *skb);
#endif

ATH_DEBUG_DECLARE_EXTERN(htc);
ATH_DEBUG_DECLARE_EXTERN(wmi);
ATH_DEBUG_DECLARE_EXTERN(bmi);
ATH_DEBUG_DECLARE_EXTERN(hif);
ATH_DEBUG_DECLARE_EXTERN(wlan);
ATH_DEBUG_DECLARE_EXTERN(misc);

extern u8 bcast_mac[];
extern u8 null_mac[];

#ifdef __cplusplus
}
#endif

#endif /* _AR6000_H_ */
