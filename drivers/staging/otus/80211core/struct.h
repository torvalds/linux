/*
 * Copyright (c) 2007-2008 Atheros Communications Inc.
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#ifndef _STRUCT_H
#define _STRUCT_H

#include "../oal_marc.h"

#define ZM_SW_LOOP_BACK                     0 /* 1=>enable, 0=>disable */
#define ZM_PCI_LOOP_BACK                    0 /* 1=>enable, 0=>disable */
#define ZM_PROTOCOL_RESPONSE_SIMULATION     0

#define ZM_RX_FRAME_SIZE               1600

extern const u8_t zg11bRateTbl[4];
extern const u8_t zg11gRateTbl[8];

#define ZM_DRIVER_CORE_MAJOR_VERSION        1
#define ZM_DRIVER_CORE_MINOR_VERSION        1
#define ZM_DRIVER_CORE_BRANCH_MAJOR_VERSION 3
#define ZM_DRIVER_CORE_BRANCH_MINOR_VERSION 39

#ifndef ZM_VTXQ_SIZE
#define ZM_VTXQ_SIZE                        1024 //2^N
#endif

#define ZM_VTXQ_SIZE_MASK                   (ZM_VTXQ_SIZE-1)
#define ZM_VMMQ_SIZE                        8 //2^N
#define ZM_VMMQ_SIZE_MASK                   (ZM_VMMQ_SIZE-1)

#include "cagg.h"

#define ZM_AGG_POOL_SIZE                    20
#define ZM_RATE_TABLE_SIZE                  32

#define ZM_MAX_BUF_DISCRETE_NUMBER          5









/**********************************************************************************/
/* IBSS macros                                                     */
/**********************************************************************************/
#define ZM_IBSS_PEER_ALIVE_COUNTER     4

/**********************************************************************************/
/* BIT mapping related macros                                                     */
/**********************************************************************************/

#define ZM_BIT_0       0x1
#define ZM_BIT_1       0x2
#define ZM_BIT_2       0x4
#define ZM_BIT_3       0x8
#define ZM_BIT_4       0x10
#define ZM_BIT_5       0x20
#define ZM_BIT_6       0x40
#define ZM_BIT_7       0x80
#define ZM_BIT_8       0x100
#define ZM_BIT_9       0x200
#define ZM_BIT_10      0x400
#define ZM_BIT_11      0x800
#define ZM_BIT_12      0x1000
#define ZM_BIT_13      0x2000
#define ZM_BIT_14      0x4000
#define ZM_BIT_15      0x8000
#define ZM_BIT_16      0x10000
#define ZM_BIT_17      0x20000
#define ZM_BIT_18      0x40000
#define ZM_BIT_19      0x80000
#define ZM_BIT_20      0x100000
#define ZM_BIT_21      0x200000
#define ZM_BIT_22      0x400000
#define ZM_BIT_23      0x800000
#define ZM_BIT_24      0x1000000
#define ZM_BIT_25      0x2000000
#define ZM_BIT_26      0x4000000
#define ZM_BIT_27      0x8000000
#define ZM_BIT_28      0x10000000
#define ZM_BIT_29      0x20000000   //WPA support
#define ZM_BIT_30      0x40000000
#define ZM_BIT_31      0x80000000


/**********************************************************************************/
/* MAC address related macros                                                     */
/**********************************************************************************/
#define ZM_MAC_BYTE_TO_WORD(macb, macw)   macw[0] = macb[0] + (macb[1] << 8); \
                                          macw[1] = macb[2] + (macb[3] << 8); \
                                          macw[2] = macb[4] + (macb[5] << 8);

#define ZM_MAC_WORD_TO_BYTE(macw, macb)   macb[0] = (u8_t) (macw[0] & 0xff); \
                                          macb[1] = (u8_t) (macw[0] >> 8);   \
                                          macb[2] = (u8_t) (macw[1] & 0xff); \
                                          macb[3] = (u8_t) (macw[1] >> 8);   \
                                          macb[4] = (u8_t) (macw[2] & 0xff); \
                                          macb[5] = (u8_t) (macw[2] >> 8);

#define ZM_MAC_0(macw)   ((u8_t)(macw[0] & 0xff))
#define ZM_MAC_1(macw)   ((u8_t)(macw[0] >> 8))
#define ZM_MAC_2(macw)   ((u8_t)(macw[1] & 0xff))
#define ZM_MAC_3(macw)   ((u8_t)(macw[1] >> 8))
#define ZM_MAC_4(macw)   ((u8_t)(macw[2] & 0xff))
#define ZM_MAC_5(macw)   ((u8_t)(macw[2] >> 8))

#define ZM_IS_MULTICAST_OR_BROADCAST(mac) (mac[0] & 0x01)
#define ZM_IS_MULTICAST(mac) ((mac[0] & 0x01) && (((u8_t)mac[0]) != 0xFF))

#define ZM_MAC_EQUAL(mac1, mac2)   ((mac1[0]==mac2[0])&&(mac1[1]==mac2[1])&&(mac1[2]==mac2[2]))
#define ZM_MAC_NOT_EQUAL(mac1, mac2)   ((mac1[0]!=mac2[0])||(mac1[1]!=mac2[1])||(mac1[2]!=mac2[2]))
/**********************************************************************************/
/* MAC address related mac'ros (end)                                               */
/**********************************************************************************/
#define ZM_BYTE_TO_WORD(A, B)   ((A<<8)+B)
#define ZM_ROL32( A, n ) \
        ( ((A) << (n)) | ( ((A)>>(32-(n)))  & ( (1UL << (n)) - 1 ) ) )
#define ZM_ROR32( A, n ) ZM_ROL32( (A), 32-(n) )
#define ZM_LO8(v16)  ((u8_t)((v16) & 0xFF))
#define ZM_HI8(v16)  ((u8_t)(((v16)>>8)&0xFF))

#ifdef ZM_ENABLE_BUFFER_TRACE
extern void zfwBufTrace(zdev_t* dev, zbuf_t *buf, u8_t *functionName);
#define ZM_BUFFER_TRACE(dev, buf)       zfwBufTrace(dev, buf, __func__);
#else
#define ZM_BUFFER_TRACE(dev, buf)
#endif

/* notification events to heart beat function */
#define ZM_BSSID_LIST_SCAN         0x01

/* CAM mode */
#define ZM_CAM_AP                       0x1
#define ZM_CAM_STA                      0x2
#define ZM_CAM_HOST                     0x4

/* finite state machine for adapter */
#define ZM_STA_STATE_DISCONNECT           1
#define ZM_STA_STATE_CONNECTING           2
#define ZM_STA_STATE_CONNECTED            3

/* Event definitions for  finite state machine */
#define ZM_EVENT_TIMEOUT_SCAN             0x0000
#define ZM_EVENT_TIMEOUT_BG_SCAN          0x0001
#define ZN_EVENT_TIMEOUT_RECONNECT        0x0002
#define ZM_EVENT_TIMEOUT_INIT_SCAN        0x0003
#define ZM_EVENT_TIMEOUT_AUTH             0x0004
#define ZM_EVENT_TIMEOUT_ASSO             0x0005
#define ZM_EVENT_TIMEOUT_AUTO_SCAN        0x0006
#define ZM_EVENT_TIMEOUT_MIC_FAIL         0x0007
#define ZM_EVENT_TIMEOUT_CHECK_AP         0x0008
#define ZM_EVENT_CONNECT                  0x0009
#define ZM_EVENT_INIT_SCAN                0x000a
#define ZM_EVENT_SCAN                     0x000b
#define ZM_EVENT_BG_SCAN                  0x000c
#define ZM_EVENT_DISCONNECT               0x000d
#define ZM_EVENT_WPA_MIC_FAIL             0x000e
#define ZM_EVENT_AP_ALIVE                 0x000f
#define ZM_EVENT_CHANGE_TO_AP             0x0010
#define ZM_EVENT_CHANGE_TO_STA            0x0011
#define ZM_EVENT_IDLE                     0x0012
#define ZM_EVENT_AUTH                     0x0013
#define ZM_EVENT_ASSO_RSP                 0x0014
#define ZM_EVENT_WPA_PK_OK                0x0015
#define ZM_EVENT_WPA_GK_OK                0x0016
#define ZM_EVENT_RCV_BEACON               0x0017
#define ZM_EVENT_RCV_PROBE_RSP            0x0018
#define ZM_EVENT_SEND_DATA                0x0019
#define ZM_EVENT_AUTO_SCAN                0x001a
#define ZM_EVENT_MIC_FAIL1                0x001d
#define ZM_EVENT_MIC_FAIL2                0x001e
#define ZM_EVENT_IBSS_MONITOR             0x001f
#define ZM_EVENT_IN_SCAN                  0x0020
#define ZM_EVENT_CM_TIMER                 0x0021
#define ZM_EVENT_CM_DISCONNECT            0x0022
#define ZM_EVENT_CM_BLOCK_TIMER           0x0023
#define ZM_EVENT_TIMEOUT_ADDBA            0x0024
#define ZM_EVENT_TIMEOUT_PERFORMANCE      0x0025
#define ZM_EVENT_SKIP_COUNTERMEASURE	  0x0026
#define ZM_EVENT_NONE                     0xffff

/* Actions after call finite state machine */
#define ZM_ACTION_NONE                    0x0000
#define ZM_ACTION_QUEUE_DATA              0x0001
#define ZM_ACTION_DROP_DATA               0x0002

/* Timers for finite state machine */
#define ZM_TICK_ZERO                      0
#define ZM_TICK_INIT_SCAN_END             8
#define ZM_TICK_NEXT_BG_SCAN              50
#define ZM_TICK_BG_SCAN_END               8
#define ZM_TICK_AUTH_TIMEOUT              4
#define ZM_TICK_ASSO_TIMEOUT              4
#define ZM_TICK_AUTO_SCAN                 300
#define ZM_TICK_MIC_FAIL_TIMEOUT          6000
#define ZM_TICK_CHECK_AP1                 150
#define ZM_TICK_CHECK_AP2                 350
#define ZM_TICK_CHECK_AP3                 250
#define ZM_TICK_IBSS_MONITOR              160
#define ZM_TICK_IN_SCAN                   4
#define ZM_TICK_CM_TIMEOUT                6000
#define ZM_TICK_CM_DISCONNECT             200
#define ZM_TICK_CM_BLOCK_TIMEOUT          6000

/* Fix bug#33338 Counter Measure Issur */
#ifdef NDIS_CM_FOR_XP
#define ZM_TICK_CM_TIMEOUT_OFFSET        2160
#define ZM_TICK_CM_DISCONNECT_OFFSET     72
#define ZM_TICK_CM_BLOCK_TIMEOUT_OFFSET  2160
#else
#define ZM_TICK_CM_TIMEOUT_OFFSET        0
#define ZM_TICK_CM_DISCONNECT_OFFSET     0
#define ZM_TICK_CM_BLOCK_TIMEOUT_OFFSET  0
#endif

#define ZM_TIME_ACTIVE_SCAN               30 //ms
#define ZM_TIME_PASSIVE_SCAN              110 //ms

/* finite state machine for BSS connect */
#define ZM_STA_CONN_STATE_NONE            0
#define ZM_STA_CONN_STATE_AUTH_OPEN       1
#define ZM_STA_CONN_STATE_AUTH_SHARE_1    2
#define ZM_STA_CONN_STATE_AUTH_SHARE_2    3
#define ZM_STA_CONN_STATE_ASSOCIATE       4
#define ZM_STA_CONN_STATE_SSID_NOT_FOUND  5
#define ZM_STA_CONN_STATE_AUTH_COMPLETED  6

/* finite state machine for WPA handshaking */
#define ZM_STA_WPA_STATE_INIT             0
#define ZM_STA_WPA_STATE_PK_OK            1
#define ZM_STA_WPA_STATE_GK_OK            2

/* various timers */
#define ZM_INTERVAL_CONNECT_TIMEOUT          20   /* 200 milisecond */

/* IBSS definitions */
#define ZM_IBSS_PARTNER_LOST                 0
#define ZM_IBSS_PARTNER_ALIVE                1
#define ZM_IBSS_PARTNER_CHECK                2

#define ZM_BCMC_ARRAY_SIZE                  16 /* Must be 2^N */
#define ZM_UNI_ARRAY_SIZE                   16 /* Must be 2^N */

#define ZM_MAX_DEFRAG_ENTRIES               4  /* 2^N */
#define ZM_DEFRAG_AGING_TIME_SEC            5  /* 5 seconds */

#define ZM_MAX_WPAIE_SIZE                   128
/* WEP related definitions */
#define ZM_USER_KEY_DEFAULT                 64
#define ZM_USER_KEY_PK                      0                /* Pairwise Key */
#define ZM_USER_KEY_GK                      1                /* Group Key */
/* AP WLAN Type */
#define ZM_WLAN_TYPE_PURE_B                 2
#define ZM_WLAN_TYPE_PURE_G                 1
#define ZM_WLAN_TYPE_MIXED                  0

/* HAL State */
#define ZM_HAL_STATE_INIT                   0
#define ZM_HAL_STATE_RUNNING                1

/* AP Capability */
#define ZM_All11N_AP                        0x01
#define ZM_XR_AP                            0x02
#define ZM_SuperG_AP                        0x04

/* MPDU Density */
#define ZM_MPDU_DENSITY_NONE                0
#define ZM_MPDU_DENSITY_1_8US               1
#define ZM_MPDU_DENSITY_1_4US               2
#define ZM_MPDU_DENSITY_1_2US               3
#define ZM_MPDU_DENSITY_1US                 4
#define ZM_MPDU_DENSITY_2US                 5
#define ZM_MPDU_DENSITY_4US                 6
#define ZM_MPDU_DENSITY_8US                 7

/* Software Encryption */
#define ZM_SW_TKIP_ENCRY_EN                0x01
#define ZM_SW_TKIP_DECRY_EN                0x02
#define ZM_SW_WEP_ENCRY_EN                 0x04
#define ZM_SW_WEP_DECRY_EN                 0x08

/* Default Support Rate */
#define ZM_DEFAULT_SUPPORT_RATE_ZERO       0x0
#define ZM_DEFAULT_SUPPORT_RATE_DISCONNECT 0x1
#define ZM_DEFAULT_SUPPORT_RATE_IBSS_B     0x2
#define ZM_DEFAULT_SUPPORT_RATE_IBSS_AG    0x3

/* security related definitions */
struct zsTkipSeed
{
    u8_t   tk[32];     /* key */
    u8_t   ta[6];
    u16_t  ttak[5];
    u16_t  ppk[6];
    u16_t  iv16,iv16tmp;
    u32_t  iv32,iv32tmp;
};

struct zsMicVar
{
    u32_t  k0, k1;        // Key
    u32_t  left, right;   // Current state
    u32_t  m;             // Message accumulator (single word)
    u16_t  nBytes;        // # bytes in M
};

struct zsDefragEntry
{
    u8_t    fragCount;
    u8_t    addr[6];
    u16_t   seqNum;
    zbuf_t* fragment[8];
    u32_t   tick;
};

struct zsDefragList
{
    struct zsDefragEntry   defragEntry[ZM_MAX_DEFRAG_ENTRIES];
    u8_t                   replaceNum;
};

#define ZM_MAX_OPPOSITE_COUNT      16
#define ZM_MAX_TX_SAMPLES          15
#define ZM_TX_RATE_DOWN_CRITERIA   80
#define ZM_TX_RATE_UP_CRITERIA    200


#define ZM_MAX_PROBE_HIDDEN_SSID_SIZE 2
struct zsSsidList
{
    u8_t            ssid[32];
    u8_t            ssidLen;
};

struct zsWrapperSetting
{
    u8_t            bDesiredBssid;
    u8_t            desiredBssid[6];
    u16_t           bssid[3];
    u8_t            ssid[32];
    u8_t            ssidLen;
    u8_t            authMode;
    u8_t            wepStatus;
    u8_t            encryMode;
    u8_t            wlanMode;
    u16_t           frequency;
    u16_t           beaconInterval;
    u8_t            dtim;
    u8_t            preambleType;
    u16_t           atimWindow;

    struct zsSsidList probingSsidList[ZM_MAX_PROBE_HIDDEN_SSID_SIZE];

    u8_t            dropUnencryptedPkts;
    u8_t            ibssJoinOnly;
    u32_t           adhocMode;
    u8_t            countryIsoName[4];
    u16_t           autoSetFrequency;

    /* AP */
    u8_t            bRateBasic;
    u8_t            gRateBasic;
    u32_t           nRateBasic;
    u8_t            bgMode;

    /* Common */
    u8_t            staWmeEnabled;
    u8_t            staWmeQosInfo;
    u8_t            apWmeEnabled;


    /* rate information: added in the future */
};

struct zsWrapperFeatureCtrl
{
    u8_t           bIbssGMode;
};

#define  ZM_MAX_PS_STA            16
#define  ZM_PS_QUEUE_SIZE         32

struct zsStaPSEntity
{
    u8_t           bUsed;
    u8_t           macAddr[6];
    u8_t           bDataQueued;
};

struct zsStaPSList
{
    u8_t           count;
    struct zsStaPSEntity    entity[ZM_MAX_PS_STA];
};

#define ZM_MAX_TIMER_COUNT   32

/* double linked list */
struct zsTimerEntry
{
    u16_t   event;
    u32_t   timer;
    struct zsTimerEntry *pre;
    struct zsTimerEntry *next;
};

struct zsTimerList
{
    u8_t   freeCount;
    struct zsTimerEntry list[ZM_MAX_TIMER_COUNT];
    struct zsTimerEntry *head;
    struct zsTimerEntry *tail;
};

/* Multicast list */
#define ZM_MAX_MULTICAST_LIST_SIZE     64

struct zsMulticastAddr
{
    u8_t addr[6];
};

struct zsMulticastList
{
    u8_t   size;
    struct zsMulticastAddr macAddr[ZM_MAX_MULTICAST_LIST_SIZE];
};

enum ieee80211_cwm_mode {
    CWM_MODE20,
    CWM_MODE2040,
    CWM_MODE40,
    CWM_MODEMAX

};

enum ieee80211_cwm_extprotspacing {
    CWM_EXTPROTSPACING20,
    CWM_EXTPROTSPACING25,
    CWM_EXTPROTSPACINGMAX
};

enum ieee80211_cwm_width {
    CWM_WIDTH20,
    CWM_WIDTH40
};

enum ieee80211_cwm_extprotmode {
    CWM_EXTPROTNONE,  /* no protection */
    CWM_EXTPROTCTSONLY,   /* CTS to self */
    CWM_EXTPROTRTSCTS,    /* RTS-CTS */
    CWM_EXTPROTMAX
};

struct ieee80211_cwm {

    /* Configuration */
    enum ieee80211_cwm_mode         cw_mode;            /* CWM mode */
    u8_t                            cw_extoffset;       /* CWM Extension Channel Offset */
    enum ieee80211_cwm_extprotmode  cw_extprotmode;     /* CWM Extension Channel Protection Mode */
    enum ieee80211_cwm_extprotspacing cw_extprotspacing;/* CWM Extension Channel Protection Spacing */
    u32_t                           cw_enable;          /* CWM State Machine Enabled */
    u32_t                           cw_extbusythreshold;/* CWM Extension Channel Busy Threshold */

    /* State */
    enum ieee80211_cwm_width        cw_width;           /* CWM channel width */
};


/* AP : STA database structure */
struct zsStaTable
{
    u32_t time;     /* tick time */
    //u32_t phyCtrl;   /* Tx PHY CTRL */
    u16_t addr[3];  /* STA MAC address */
    u16_t state;    /* aut/asoc */
    //u16_t retry;    /* Retry count */
    struct zsRcCell rcCell;

    u8_t valid;     /* Valid flag : 1=>valid */
    u8_t psMode;    /* STA power saving mode */
    u8_t staType;   /* 0=>11b, 1=>11g, 2=>11n */
    u8_t qosType;   /* 0=>Legacy, 1=>WME */
    u8_t qosInfo;   /* WME QoS info */
    u8_t vap;       /* Virtual AP ID */
    u8_t encryMode; /* Encryption type for this STA */
    u8_t keyIdx;
    struct zsMicVar     txMicKey;
    struct zsMicVar     rxMicKey;
    u16_t iv16;
    u32_t iv32;
#ifdef ZM_ENABLE_CENC
    /* CENC */
    u8_t cencKeyIdx;
    u32_t txiv[4];
    u32_t rxiv[4];
#endif //ZM_ENABLE_CENC
};

struct zdStructWds
{
    u8_t    wdsBitmap;                      /* Set bit-N to 1 to enable WDS */
    u8_t    encryMode[ZM_MAX_WDS_SUPPORT];  /* WDS encryption mode */
    u16_t   macAddr[ZM_MAX_WDS_SUPPORT][3]; /* WDS neighbor MAC address */
};

    // htcapinfo 16bits
#define HTCAP_AdvCodingCap          0x0001
#define HTCAP_SupChannelWidthSet    0x0002
#define HTCAP_DynamicSMPS           0x0004
#define HTCAP_SMEnabled             0x000C
#define HTCAP_GreenField            0x0010
#define HTCAP_ShortGIfor20MHz       0x0020
#define HTCAP_ShortGIfor40MHz       0x0040
#define HTCAP_TxSTBC                0x0080
#define HTCAP_RxOneStream           0x0100
#define HTCAP_RxTwoStream           0x0200
#define HTCAP_RxThreeStream         0x0300
#define HTCAP_DelayedBlockACK       0x0400
#define HTCAP_MaxAMSDULength        0x0800
#define HTCAP_DSSSandCCKin40MHz     0x1000
#define HTCAP_PSMPSup               0x2000
#define HTCAP_STBCControlFrameSup   0x4000
#define HTCAP_LSIGTXOPProtectionSUP 0x8000
    // Ampdu HT Parameter Info 8bits
#define HTCAP_MaxRxAMPDU0           0x00
#define HTCAP_MaxRxAMPDU1           0x01
#define HTCAP_MaxRxAMPDU2           0x02
#define HTCAP_MaxRxAMPDU3           0x03
    // PCO 8bits
#define HTCAP_PCO                   0x01
#define HTCAP_TransmissionTime1     0x02
#define HTCAP_TransmissionTime2     0x04
#define HTCAP_TransmissionTime3     0x06
    // MCS FeedBack 8bits
#define HTCAP_PlusHTCSupport        0x04
#define HTCAP_RDResponder           0x08
    // TX Beamforming 0 8bits
#define HTCAP_TxBFCapable           0x01
#define HTCAP_RxStaggeredSoundCap   0x02
#define HTCAP_TxStaggeredSoundCap   0x04
#define HTCAP_RxZLFCapable          0x08
#define HTCAP_TxZLFCapable          0x10
#define HTCAP_ImplicitTxBFCapable   0x20
    // Tx Beamforming 1 8bits
#define HTCAP_ExplicitCSITxBFCap    0x01
#define HTCAP_ExpUncompSteerMatrCap 0x02
    // Antenna Selection Capabilities 8bits
#define HTCAP_AntennaSelectionCap       0x01
#define HTCAP_ExplicitCSITxASCap        0x02
#define HTCAP_AntennaIndFeeTxASCap      0x04
#define HTCAP_ExplicitCSIFeedbackCap    0x08
#define HTCAP_AntennaIndFeedbackCap     0x10
#define HTCAP_RxASCap                   0x20
#define HTCAP_TxSoundPPDUsCap           0x40



struct zsHTCapability
{
    u8_t ElementID;
    u8_t Length;
    // HT Capability Info
    u16_t HtCapInfo;
    u8_t AMPDUParam;
    u8_t MCSSet[16];    //16 bytes
    // Extended HT Capability Info
    u8_t PCO;
    u8_t MCSFeedBack;

    u8_t TxBFCap[4];
    u8_t AselCap;
};

union zuHTCapability
{
    struct zsHTCapability Data;
    u8_t Byte[28];
};

    //channelinfo 8bits
#define ExtHtCap_ExtChannelOffsetAbove  0x01
#define ExtHtCap_ExtChannelOffsetBelow  0x03
#define ExtHtCap_RecomTxWidthSet        0x04
#define ExtHtCap_RIFSMode               0x08
#define ExtHtCap_ControlAccessOnly      0x10
    //operatinginfo 16bits
#define ExtHtCap_NonGFDevicePresent     0x0004
    //beaconinfo 16bits
#define ExtHtCap_DualBeacon             0x0040
#define ExtHtCap_DualSTBCProtection     0x0080
#define ExtHtCap_SecondaryBeacon        0x0100
#define ExtHtCap_LSIGTXOPProtectFullSup 0x0200
#define ExtHtCap_PCOActive              0x0400
#define ExtHtCap_PCOPhase               0x0800


struct zsExtHTCapability
{
    u8_t    ElementID;
    u8_t    Length;
    u8_t    ControlChannel;
    u8_t    ChannelInfo;
    u16_t   OperatingInfo;
    u16_t   BeaconInfo;
    // Supported MCS Set
    u8_t    MCSSet[16];
};

union zuExtHTCapability
{
    struct zsExtHTCapability Data;
    u8_t Byte[24];
};

struct InformationElementSta {
    struct zsHTCapability       HtCap;
    struct zsExtHTCapability    HtInfo;
};

struct InformationElementAp {
    struct zsHTCapability       HtCap;
};

#define ZM_MAX_FREQ_REQ_QUEUE  32
typedef void (*zfpFreqChangeCompleteCb)(zdev_t* dev);

struct zsWlanDevFreqControl
{
    u16_t                     freqReqQueue[ZM_MAX_FREQ_REQ_QUEUE];
    u8_t                     freqReqBw40[ZM_MAX_FREQ_REQ_QUEUE];
    u8_t                     freqReqExtOffset[ZM_MAX_FREQ_REQ_QUEUE];
    zfpFreqChangeCompleteCb   freqChangeCompCb[ZM_MAX_FREQ_REQ_QUEUE];
    u8_t                      freqReqQueueHead;
    u8_t                      freqReqQueueTail;
};

struct zsWlanDevAp
{
    u16_t   protectedObss;    /* protected overlap BSS */
    u16_t   staAgingTimeSec;  /* in second, STA will be deathed if it does not */
                              /* active for this long time                     */
    u16_t   staProbingTimeSec;/* in second, STA will be probed if it does not  */
                              /* active for this long time                     */
    u8_t    authSharing;      /* authentication on going*/
    u8_t    bStaAssociated;   /* 11b STA associated */
    u8_t    gStaAssociated;   /* 11g STA associated */
    u8_t    nStaAssociated;   /* 11n STA associated */
    u16_t   protectionMode;   /* AP protection mode flag */
    u16_t   staPowerSaving;   /* Set associated power saving STA count */



    zbuf_t*  uniArray[ZM_UNI_ARRAY_SIZE]; /* array to store unicast frames */
    u16_t   uniHead;
    u16_t   uniTail;

    /* HT Capability Info */
    union zuHTCapability HTCap; //CWYang(+)

    /* Extended HT Capability Info */
    union zuExtHTCapability ExtHTCap; //CWYang(+)

    /* STA table */
    struct zsStaTable staTable[ZM_MAX_STA_SUPPORT];

    /* WDS */
    struct zdStructWds wds;
    /* WPA */
    u8_t wpaIe[ZM_MAX_AP_SUPPORT][ZM_MAX_WPAIE_SIZE];
    u8_t wpaLen[ZM_MAX_AP_SUPPORT];
    u8_t stawpaIe[ZM_MAX_AP_SUPPORT][ZM_MAX_WPAIE_SIZE];
    u8_t stawpaLen[ZM_MAX_AP_SUPPORT];
    u8_t wpaSupport[ZM_MAX_AP_SUPPORT];

    //struct zsTkipSeed   bcSeed;
    u8_t bcKeyIndex[ZM_MAX_AP_SUPPORT];
    u8_t bcHalKeyIdx[ZM_MAX_AP_SUPPORT];
    struct zsMicVar     bcMicKey[ZM_MAX_AP_SUPPORT];
    u16_t iv16[ZM_MAX_AP_SUPPORT];
    u32_t iv32[ZM_MAX_AP_SUPPORT];

#ifdef ZM_ENABLE_CENC
    /* CENC */
    u32_t txiv[ZM_MAX_AP_SUPPORT][4];
#endif //ZM_ENABLE_CENC

    /* Virtual AP */
    u8_t    beaconCounter;
    u8_t    vapNumber;
    u8_t    apBitmap;                         /* Set bit-N to 1 to enable VAP */
    u8_t    hideSsid[ZM_MAX_AP_SUPPORT];
    u8_t    authAlgo[ZM_MAX_AP_SUPPORT];
    u8_t    ssid[ZM_MAX_AP_SUPPORT][32];      /* SSID */
    u8_t    ssidLen[ZM_MAX_AP_SUPPORT];       /* SSID length */
    u8_t    encryMode[ZM_MAX_AP_SUPPORT];
    u8_t    wepStatus[ZM_MAX_AP_SUPPORT];
    u16_t   capab[ZM_MAX_AP_SUPPORT];         /* Capability */
    u8_t    timBcmcBit[ZM_MAX_AP_SUPPORT];    /* BMCM bit of TIM */
    u8_t    wlanType[ZM_MAX_AP_SUPPORT];

    /* Array to store BC or MC frames */
    zbuf_t*  bcmcArray[ZM_MAX_AP_SUPPORT][ZM_BCMC_ARRAY_SIZE];
    u16_t   bcmcHead[ZM_MAX_AP_SUPPORT];
    u16_t   bcmcTail[ZM_MAX_AP_SUPPORT];

    u8_t                    qosMode;                          /* 1=>WME */
    u8_t                    uapsdEnabled;
    struct zsQueue*         uapsdQ;

    u8_t                    challengeText[128];

    struct InformationElementAp ie[ZM_MAX_STA_SUPPORT];


};

#define ZM_MAX_BLOCKING_AP_LIST_SIZE    4 /* 2^N */
struct zsBlockingAp
{
    u8_t addr[6];
    u8_t weight;
};

#define ZM_SCAN_MGR_SCAN_NONE           0
#define ZM_SCAN_MGR_SCAN_INTERNAL       1
#define ZM_SCAN_MGR_SCAN_EXTERNAL       2

struct zsWlanDevStaScanMgr
{
    u8_t                    scanReqs[2];
    u8_t                    currScanType;
    u8_t                    scanStartDelay;
};

#define ZM_PS_MSG_STATE_ACTIVE          0
#define ZM_PS_MSG_STATE_SLEEP           1
#define ZM_PS_MSG_STATE_T1              2
#define ZM_PS_MSG_STATE_T2              3
#define ZM_PS_MSG_STATE_S1              4

#define ZM_PS_MAX_SLEEP_PERIODS         3       // The number of beacon periods

struct zsWlanDevStaPSMgr
{
    u8_t                    state;
    u8_t                    isSleepAllowed;
    u8_t                    maxSleepPeriods;
    u8_t                    ticks;
    u32_t                   lastTxUnicastFrm;
    u32_t                   lastTxMulticastFrm;
    u32_t                   lastTxBroadcastFrm;
    u8_t                    tempWakeUp; /*enable when wake up but still in ps mode */
    u16_t                   sleepAllowedtick;
};

struct zsWlanDevSta
{
    u32_t                   beaconTxCnt;  /* Transmitted beacon counter (in IBSS) */
    u8_t                    txBeaconInd;  /* In IBSS mode, true means that we just transmit a beacon during
                                             last beacon period.
                                           */
    u16_t                   beaconCnt;    /* receive beacon count, will be perodically reset */
    u16_t                   bssid[3];     /* BSSID of connected AP */
    u8_t                    ssid[32];     /* SSID */
    u8_t                    ssidLen;      /* SSID length */
    u8_t                    mTxRate;      /* Tx rate for multicast */
    u8_t                    uTxRate;      /* Tx rate for unicast */
    u8_t                    mmTxRate;     /* Tx rate for management frame */
    u8_t                    bChannelScan;
    u8_t                    bScheduleScan;

    u8_t                    InternalScanReq;
    u16_t                   activescanTickPerChannel;
    u16_t                   passiveScanTickPerChannel;
    u16_t                   scanFrequency;
    u32_t                   connPowerInHalfDbm;

    u16_t                   currentFrequency;
    u16_t                   currentBw40;
    u16_t                   currentExtOffset;

    u8_t                    bPassiveScan;

    struct zsBlockingAp     blockingApList[ZM_MAX_BLOCKING_AP_LIST_SIZE];

    //struct zsBssInfo        bssInfoPool[ZM_MAX_BSS];
    struct zsBssInfo*       bssInfoArray[ZM_MAX_BSS];
    struct zsBssList        bssList;
    u8_t                    bssInfoArrayHead;
    u8_t                    bssInfoArrayTail;
    u8_t                    bssInfoFreeCount;

    u8_t                    authMode;
    u8_t                    currentAuthMode;
    u8_t                    wepStatus;
    u8_t                    encryMode;
    u8_t                    keyId;
#ifdef ZM_ENABLE_IBSS_WPA2PSK
    u8_t                    ibssWpa2Psk;
#endif
#ifdef ZM_ENABLE_CENC
    u8_t                    cencKeyId; //CENC
#endif //ZM_ENABLE_CENC
    u8_t                    dropUnencryptedPkts;
    u8_t                    ibssJoinOnly;
    u8_t                    adapterState;
    u8_t                    oldAdapterState;
    u8_t                    connectState;
    u8_t                    connectRetry;
    u8_t                    wpaState;
    u8_t                    wpaIe[ZM_MAX_IE_SIZE + 2];
    u8_t                    rsnIe[ZM_MAX_IE_SIZE + 2];
    u8_t                    challengeText[255+2];
    u8_t                    capability[2];
    //u8_t                    connectingHiddenAP;
    //u8_t                    scanWithSSID;
    u16_t                   aid;
    u32_t                   mgtFrameCount;
    u8_t                    bProtectionMode;
	u32_t					NonNAPcount;
	u8_t					RTSInAGGMode;
    u32_t                   connectTimer;
    u16_t                   atimWindow;
    u8_t                    desiredBssid[6];
    u8_t                    bDesiredBssid;
    struct zsTkipSeed       txSeed;
    struct zsTkipSeed       rxSeed[4];
    struct zsMicVar         txMicKey;
    struct zsMicVar         rxMicKey[4];
    u16_t                   iv16;
    u32_t                   iv32;
    struct zsOppositeInfo   oppositeInfo[ZM_MAX_OPPOSITE_COUNT];
    u8_t                    oppositeCount;
    u8_t                    bssNotFoundCount;     /* sitesurvey for search desired ISBB threshold */
    u16_t                   rxBeaconCount;
    u8_t                    beaconMissState;
    u32_t                   rxBeaconTotal;
    u8_t                    bIsSharedKey;
    u8_t                    connectTimeoutCount;

    u8_t                    recvAtim;

    /* ScanMgr Control block */
    struct zsWlanDevStaScanMgr scanMgr;
    struct zsWlanDevStaPSMgr   psMgr;

    // The callback would be called if receiving an unencrypted packets but
    // the station is in encrypted mode. The wrapper could decide whether
    // to drop the packet by its OS setting.
    zfpStaRxSecurityCheckCb pStaRxSecurityCheckCb;

    /* WME */
    u8_t                    apWmeCapability; //bit-0 => a WME AP
                                             //bit-7 => a UAPSD AP
    u8_t                    wmeParameterSetCount;

    u8_t                    wmeEnabled;
    #define ZM_STA_WME_ENABLE_BIT       0x1
    #define ZM_STA_UAPSD_ENABLE_BIT     0x2
    u8_t                    wmeQosInfo;

    u8_t                    wmeConnected;
    u8_t                    qosInfo;
    struct zsQueue*         uapsdQ;

    /* countermeasures */
    u8_t                    cmMicFailureCount;
    u8_t                    cmDisallowSsidLength;
    u8_t                    cmDisallowSsid[32];

    /* power-saving mode */
    u8_t                    powerSaveMode;
    zbuf_t*                 staPSDataQueue[ZM_PS_QUEUE_SIZE];
    u8_t                    staPSDataCount;

    /* IBSS power-saving mode */
    /* record the STA which has entered the PS mode */
    struct zsStaPSList      staPSList;
    /* queue the data of the PS STAs */
    zbuf_t*                  ibssPSDataQueue[ZM_PS_QUEUE_SIZE];
    u8_t                    ibssPSDataCount;
    u8_t                    ibssPrevPSDataCount;
    u8_t                    bIbssPSEnable;
    /* BIT_15: ON/OFF, BIT_0~14: Atim Timer */
    u16_t                   ibssAtimTimer;

    /* WPA2 */
    struct zsPmkidInfo      pmkidInfo;

    /* Multicast list related objects */
    struct zsMulticastList  multicastList;

    /* XP packet filter feature : */
    /* 1=>enable: All multicast address packets, not just the ones enumerated in the multicast address list. */
    /* 0=>disable */
    u8_t                    bAllMulticast;

    /* reassociation flag */
    u8_t                    connectByReasso;
    u8_t                    failCntOfReasso;

	/* for HT configure control setting */
    u8_t                    preambleTypeHT;  /* HT: 0 Mixed mode    1 Green field */
	u8_t                    htCtrlBandwidth;
	u8_t                    htCtrlSTBC;
	u8_t                    htCtrlSG;
    u8_t                    defaultTA;

    u8_t                    connection_11b;

    u8_t                    EnableHT;
    u8_t                    SG40;
    u8_t                    HT2040;
    /* for WPA setting */
    u8_t                    wpaSupport;
    u8_t                    wpaLen;

    /* IBSS related objects */
    u8_t                    ibssDelayedInd;
    struct zsPartnerNotifyEvent ibssDelayedIndEvent;
    u8_t                    ibssPartnerStatus;

    u8_t                    bAutoReconnect;

    u8_t                    flagFreqChanging;
    u8_t                    flagKeyChanging;
    struct zsBssInfo        ibssBssDesc;
    u8_t                    ibssBssIsCreator;
    u16_t                   ibssReceiveBeaconCount;
    u8_t                    ibssSiteSurveyStatus;

    u8_t                    disableProbingWithSsid;
#ifdef ZM_ENABLE_CENC
    /* CENC */
    u8_t                    cencIe[ZM_MAX_IE_SIZE + 2];
#endif //ZM_ENABLE_CENC
    u32_t txiv[4];  //Tx PN Sequence
    u32_t rxiv[4];  //Rx PN Sequence
    u32_t rxivGK[4];//Broadcast Rx PN Sequence
    u8_t  wepKey[4][32];    // For Software WEP
	u8_t  SWEncryMode[4];

    /* 802.11d */
    u8_t b802_11D;

    /* 802.11h */
    u8_t TPCEnable;
    u8_t DFSEnable;
    u8_t DFSDisableTx;

    /* Owl AP */
    u8_t athOwlAp;

    /* Enable BA response in driver */
    u8_t enableDrvBA;

    /* HT Capability Info */
    union zuHTCapability HTCap; //CWYang(+)

    /* Extended HT Capability Info */
    union zuExtHTCapability ExtHTCap; //CWYang(+)

    struct InformationElementSta   ie;

#define ZM_CACHED_FRAMEBODY_SIZE   200
    u8_t                    asocReqFrameBody[ZM_CACHED_FRAMEBODY_SIZE];
    u16_t                   asocReqFrameBodySize;
    u8_t                    asocRspFrameBody[ZM_CACHED_FRAMEBODY_SIZE];
    u16_t                   asocRspFrameBodySize;
    u8_t                    beaconFrameBody[ZM_CACHED_FRAMEBODY_SIZE];
    u16_t                   beaconFrameBodySize;

    u8_t                    ac0PriorityHigherThanAc2;
    u8_t                    SWEncryptEnable;

    u8_t                    leapEnabled;

    u32_t                    TotalNumberOfReceivePackets;
    u32_t                    TotalNumberOfReceiveBytes;
    u32_t                    avgSizeOfReceivePackets;

    u32_t                    ReceivedPacketRateCounter;
    u32_t                    ReceivedPktRatePerSecond;

    /* #2 Record the sequence number to determine whether the unicast frame is separated by RIFS or not */
#define    ZM_RIFS_STATE_DETECTING    0
#define    ZM_RIFS_STATE_DETECTED     1
#define    ZM_RIFS_TIMER_TIMEOUT      4480          // <Driver time>4480ms <Real time>7s
    u8_t                    rifsState;
    u8_t                    rifsLikeFrameCnt;
    u16_t                   rifsLikeFrameSequence[3];
    u32_t                   rifsTimer;
    u32_t                   rifsCount;

    /* RX filter desired by upper layers.  Note this contains some bits which must be filtered
       by sw since the hw supports only a subset of possible filter actions.= */
    u32_t  osRxFilter;

    u8_t   bSafeMode;

    u32_t  ibssAdditionalIESize;
    u8_t   ibssAdditionalIE[256];
}; //struct zsWlanDevSta

#define ZM_CMD_QUEUE_SIZE                   256  //Roger Check, test 64 when ready

#define ZM_OID_READ                         1
#define ZM_OID_WRITE                        2
#define ZM_OID_INTERNAL_WRITE               3
#define ZM_CMD_SET_FREQUENCY                4
#define ZM_CMD_SET_KEY                      5
#define ZM_CWM_READ                         6
#define ZM_MAC_READ                         7
#define ZM_ANI_READ                         8
#define ZM_EEPROM_READ                      9
#define ZM_EEPROM_WRITE                     0x0A
#define ZM_OID_CHAN							0x30
#define ZM_OID_SYNTH						0x32
#define ZM_OID_TALLY						0x81
#define ZM_OID_TALLY_APD					0x82

#define ZM_OID_DKTX_STATUS                  0x92
#define ZM_OID_FLASH_CHKSUM                 0xD0
#define ZM_OID_FLASH_READ                   0xD1
#define ZM_OID_FLASH_PROGRAM                0xD2
#define ZM_OID_FW_DL_INIT                   0xD3

/* Driver to Firmware OID */
#define ZM_CMD_ECHO         0x80
#define ZM_CMD_TALLY        0x81
#define ZM_CMD_TALLY_APD    0x82
#define ZM_CMD_CONFIG       0x83
#define ZM_CMD_RREG         0x00
#define ZM_CMD_WREG         0x01
#define ZM_CMD_RMEM         0x02
#define ZM_CMD_WMEM         0x03
#define ZM_CMD_BITAND       0x04
#define ZM_CMD_BITOR        0x05
#define ZM_CMD_EKEY         0x28
#define ZM_CMD_DKEY         0x29
#define ZM_CMD_FREQUENCY    0x30
#define ZM_CMD_RF_INIT      0x31
#define ZM_CMD_SYNTH        0x32
#define ZM_CMD_FREQ_STRAT   0x33
#define ZM_CMD_RESET        0x90
#define ZM_CMD_DKRESET      0x91
#define ZM_CMD_DKTX_STATUS  0x92
#define ZM_CMD_FDC          0xA0
#define ZM_CMD_WREEPROM     0xB0
#define ZM_CMD_WFLASH       0xB0
#define ZM_CMD_FLASH_ERASE  0xB1
#define ZM_CMD_FLASH_PROG   0xB2
#define ZM_CMD_FLASH_CHKSUM 0xB3
#define ZM_CMD_FLASH_READ   0xB4
#define ZM_CMD_FW_DL_INIT   0xB5
#define ZM_CMD_MEM_WREEPROM 0xBB


/* duplicate filter table column */
#define ZM_FILTER_TABLE_COL                 2 /* 2^n */
/* duplicate filter table Row */
#define ZM_FILTER_TABLE_ROW                 8 /* 2^n */

/* duplicate filter table structure */
struct zsRxFilter
{
    u16_t addr[3];
    u16_t seq;
    u8_t up;
};

struct zsWlanDev
{
    /* AP global variables */
    struct zsWlanDevAp ap;
    /* STA global variables */
    struct zsWlanDevSta sta;
    /* save wrapper setting */
    struct zsWrapperSetting ws;
    /* features determined by wrapper (vendor) */
    struct zsWrapperFeatureCtrl wfc;
    /* Traffic Monitor tally */
    struct zsTrafTally trafTally;
    /* Communication tally */
    struct zsCommTally commTally;
    /* Duplicate frame filter table */
    struct zsRxFilter rxFilterTbl[ZM_FILTER_TABLE_COL][ZM_FILTER_TABLE_ROW];
    /* Regulatory table */
    struct zsRegulationTable  regulationTable;

    /* */
    struct zsWlanDevFreqControl freqCtrl;

    enum devState state;

    u8_t  halState;
    u8_t  wlanMode;         /* AP/INFRASTRUCTURE/IBSS/PSEUDO */
    u16_t macAddr[3];       /* MAC address */
    u16_t beaconInterval;   /* beacon Interval */
    u8_t dtim;              /* DTIM period */
    u8_t            CurrentDtimCount;
    u8_t  preambleType;
    u8_t  preambleTypeInUsed;
    u8_t  maxTxPower2;	    /* 2.4 GHz Max Tx power (Unit: 0.5 dBm) */
    u8_t  maxTxPower5;	    /* 5 GHz Max Tx power (Unit: 0.5 dBm) */
    u8_t  connectMode;
    u32_t supportMode;

    u8_t bRate;             /* 11b Support Rate bit map */
    u8_t bRateBasic;        /* 11b Basic Rate bit map */
    u8_t gRate;             /* 11g Support Rate bit map */
    u8_t gRateBasic;        /* 11g Basic Rate bit map */
    /* channel index point to the item in regulation table */
    u8_t channelIndex;

    /* channel management */
    u8_t    BandWidth40;
    u8_t    ExtOffset;      //1 above, 3 below, 0 not present
    u16_t   frequency;      /* operation frequency */

    u8_t erpElement;        /* ERP information element data */

    u8_t disableSelfCts;    /* set to 1 to disable Self-CTS */
    u8_t bgMode;

	/* private test flag */
    u32_t enableProtectionMode;   /* force enable/disable self cts */
	u32_t checksumTest;     /* OTUS checksum test 1=>zero checksum 0=>normal */
	u32_t rxPacketDump;     /* rx packet dump */

    u8_t enableAggregation; /* force enable/disable A-MSPU */
    u8_t enableWDS;         /* force enable/disable WDS testing */
	u8_t enableTxPathMode;  /* OTUS special testing mode 1=>diable, 0=>enable: ZM_SYSTEM_TEST_MODE */
    u8_t enableHALDbgInfo;  /*  */

    u32_t forceTxTPC;       /* force tx packet send TPC */

    u16_t seq[4];
    u16_t mmseq;

    /* driver core time tick */
    u32_t tick;
    u16_t tickIbssSendBeacon;
    u16_t tickIbssReceiveBeacon;

    /* RTS threshold */
    u16_t rtsThreshold;

    /* fragmentation threshold,  256 <= value <= 2346, 0=disabled */
    u16_t fragThreshold;

    /* Tx Rate */
    u16_t txMCS;
    u16_t txMT;
    u32_t CurrentTxRateKbps; //CWYang(+)
    /* Rx Rate */
    u32_t CurrentRxRateKbps; //Janet(+)
    u8_t CurrentRxRateUpdated;
    u8_t modulationType;
    u8_t rxInfo;
    u16_t rateField;

    /* timer related objects */
    struct zsTimerList  timerList;
    u8_t                bTimerReady;

    /* for defragmentation */
    struct zsDefragList defragTable;

    /* Data struct for Interface Dependent Layer */
    //struct zsIdlStruct idlStruct;

    /* Signal Strength/Quality Related Parameters */
    u8_t SignalStrength; //CWYang(+)
    u8_t SignalQuality;  //CWYang(+)



    /* QoS */
    zbuf_t* vtxq[4][ZM_VTXQ_SIZE];
    u16_t vtxqHead[4];
    u16_t vtxqTail[4];
    u16_t qosDropIpFrag[4];

    /* Management Tx queue */
    zbuf_t* vmmq[ZM_VMMQ_SIZE];
    u16_t vmmqHead;
    u16_t vmmqTail;

    u8_t                vtxqPushing;

    /*
     * add by honda
     * 1. Aggregate queues
     * 2. STA's associated information and queue number
     * 3. rx aggregation re-ordering queue
     */
    struct aggQueue    *aggQPool[ZM_AGG_POOL_SIZE];
    u8_t                aggInitiated;
    u8_t                addbaComplete;
    u8_t                addbaCount;
    u8_t                aggState;
    u8_t                destLock;
    struct aggSta       aggSta[ZM_MAX_STA_SUPPORT];
    struct agg_tid_rx  *tid_rx[ZM_AGG_POOL_SIZE];
    struct aggTally     agg_tal;
    struct destQ        destQ;
    struct baw_enabler *baw_enabler;
    struct ieee80211_cwm    cwm;
    u16_t               reorder;
    u16_t               seq_debug;
    /* rate control */
    u32_t txMPDU[ZM_RATE_TABLE_SIZE];
    u32_t txFail[ZM_RATE_TABLE_SIZE];
    u32_t PER[ZM_RATE_TABLE_SIZE];
    u16_t probeCount;
    u16_t probeSuccessCount;
    u16_t probeInterval;
    u16_t success_probing;
    /*
     * end of add by honda
     */

    /* airopeek sniffer mode for upper sw */
    u32_t               swSniffer;   /* window: airoPeek */
    u32_t               XLinkMode;

    /* MDK mode */
    /* init by 0=>normal driver 1=>MDK driver */
    u32_t               modeMDKEnable;

    u32_t               heartBeatNotification;

    /* pointer for HAL Plus private memory */
    void* hpPrivate;

    /* for WPA setting */
    //u8_t                    wpaSupport[ZM_MAX_AP_SUPPORT];
    //u8_t                    wpaLen[ZM_MAX_AP_SUPPORT];
    //u8_t                    wpaIe[ZM_MAX_AP_SUPPORT][ZM_MAX_IE_SIZE];

    struct zsLedStruct      ledStruct;

    /* ani flag */
    u8_t aniEnable;
    u16_t txq_threshold;

	//Skip Mic Error Check
	u8_t	TKIP_Group_KeyChanging;

	u8_t    dynamicSIFSEnable;

	u8_t    queueFlushed;

    u16_t (*zfcbAuthNotify)(zdev_t* dev, u16_t* macAddr);
    u16_t (*zfcbAsocNotify)(zdev_t* dev, u16_t* macAddr, u8_t* body, u16_t bodySize, u16_t port);
    u16_t (*zfcbDisAsocNotify)(zdev_t* dev, u8_t* macAddr, u16_t port);
    u16_t (*zfcbApConnectNotify)(zdev_t* dev, u8_t* macAddr, u16_t port);
    void (*zfcbConnectNotify)(zdev_t* dev, u16_t status, u16_t* bssid);
    void (*zfcbScanNotify)(zdev_t* dev, struct zsScanResult* result);
    void (*zfcbMicFailureNotify)(zdev_t* dev, u16_t* addr, u16_t status);
    void (*zfcbApMicFailureNotify)(zdev_t* dev, u8_t* addr, zbuf_t* buf);
    void (*zfcbIbssPartnerNotify)(zdev_t* dev, u16_t status, struct zsPartnerNotifyEvent *event);
    void (*zfcbMacAddressNotify)(zdev_t* dev, u8_t* addr);
    void (*zfcbSendCompleteIndication)(zdev_t* dev, zbuf_t* buf);
    void (*zfcbRecvEth)(zdev_t* dev, zbuf_t* buf, u16_t port);
    void (*zfcbRecv80211)(zdev_t* dev, zbuf_t* buf, struct zsAdditionInfo* addInfo);
    void (*zfcbRestoreBufData)(zdev_t* dev, zbuf_t* buf);
#ifdef ZM_ENABLE_CENC
    u16_t (*zfcbCencAsocNotify)(zdev_t* dev, u16_t* macAddr, u8_t* body,
            u16_t bodySize, u16_t port);
#endif //ZM_ENABLE_CENC
    u8_t (*zfcbClassifyTxPacket)(zdev_t* dev, zbuf_t* buf);
    void (*zfcbHwWatchDogNotify)(zdev_t* dev);
};


struct zsWlanKey
{
    u8_t key;
};


/* These macros are defined here for backward compatibility */
/* Please leave them alone */
/* For Tx packet allocated in upper layer layer */
#define zmw_tx_buf_readb(dev, buf, offset) zmw_buf_readb(dev, buf, offset)
#define zmw_tx_buf_readh(dev, buf, offset) zmw_buf_readh(dev, buf, offset)
#define zmw_tx_buf_writeb(dev, buf, offset, value) zmw_buf_writeb(dev, buf, offset, value)
#define zmw_tx_buf_writeh(dev, buf, offset, value) zmw_buf_writeh(dev, buf, offset, value)

/* For Rx packet allocated in driver */
#define zmw_rx_buf_readb(dev, buf, offset) zmw_buf_readb(dev, buf, offset)
#define zmw_rx_buf_readh(dev, buf, offset) zmw_buf_readh(dev, buf, offset)
#define zmw_rx_buf_writeb(dev, buf, offset, value) zmw_buf_writeb(dev, buf, offset, value)
#define zmw_rx_buf_writeh(dev, buf, offset, value) zmw_buf_writeh(dev, buf, offset, value)

#endif /* #ifndef _STRUCT_H */
