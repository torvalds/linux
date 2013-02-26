/*
 * Copyright (c) 1996, 2003 VIA Networking Technologies, Inc.
 * All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 *
 * File: device.h
 *
 * Purpose: MAC Data structure
 *
 * Author: Tevin Chen
 *
 * Date: Mar 17, 1997
 *
 */

#ifndef __DEVICE_H__
#define __DEVICE_H__

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/init.h>
#include <linux/delay.h>
#include <linux/device.h>
#include <linux/firmware.h>
#include <linux/etherdevice.h>
#include <linux/suspend.h>
#include <linux/if_arp.h>
#include <linux/wireless.h>
#include <net/iw_handler.h>
#include <net/cfg80211.h>
#include <linux/timer.h>
#include <linux/usb.h>


#ifdef SIOCETHTOOL
#define DEVICE_ETHTOOL_IOCTL_SUPPORT
#include <linux/ethtool.h>
#else
#undef DEVICE_ETHTOOL_IOCTL_SUPPORT
#endif

/* please copy below macro to driver_event.c for API */
#define RT_INSMOD_EVENT_FLAG                             0x0101
#define RT_UPDEV_EVENT_FLAG                               0x0102
#define RT_DISCONNECTED_EVENT_FLAG               0x0103
#define RT_WPACONNECTED_EVENT_FLAG             0x0104
#define RT_DOWNDEV_EVENT_FLAG                        0x0105
#define RT_RMMOD_EVENT_FLAG                              0x0106

/*
 * device specific
 */

#include "device_cfg.h"
#include "80211hdr.h"
#include "tether.h"
#include "wmgr.h"
#include "wcmd.h"
#include "mib.h"
#include "srom.h"
#include "rc4.h"
#include "desc.h"
#include "key.h"
#include "card.h"

/*---------------------  Export Definitions -------------------------*/
#define VNT_USB_VENDOR_ID                     0x160a
#define VNT_USB_PRODUCT_ID                    0x3184

#define MAC_MAX_CONTEXT_REG     (256+128)

#define MAX_MULTICAST_ADDRESS_NUM       32
#define MULTICAST_ADDRESS_LIST_SIZE     (MAX_MULTICAST_ADDRESS_NUM * ETH_ALEN)

#define DUPLICATE_RX_CACHE_LENGTH       5

#define NUM_KEY_ENTRY                   11

#define TX_WEP_NONE                     0
#define TX_WEP_OTF                      1
#define TX_WEP_SW                       2
#define TX_WEP_SWOTP                    3
#define TX_WEP_OTPSW                    4
#define TX_WEP_SW232                    5

#define KEYSEL_WEP40                    0
#define KEYSEL_WEP104                   1
#define KEYSEL_TKIP                     2
#define KEYSEL_CCMP                     3

#define AUTO_FB_NONE            0
#define AUTO_FB_0               1
#define AUTO_FB_1               2

#define FB_RATE0                0
#define FB_RATE1                1

/* Antenna Mode */
#define ANT_A                   0
#define ANT_B                   1
#define ANT_DIVERSITY           2
#define ANT_RXD_TXA             3
#define ANT_RXD_TXB             4
#define ANT_UNKNOWN             0xFF
#define ANT_TXA                 0
#define ANT_TXB                 1
#define ANT_RXA                 2
#define ANT_RXB                 3


#define MAXCHECKHANGCNT         4

/* Packet type */
#define TX_PKT_UNI              0x00
#define TX_PKT_MULTI            0x01
#define TX_PKT_BROAD            0x02

#define BB_VGA_LEVEL            4
#define BB_VGA_CHANGE_THRESHOLD 3

#ifndef RUN_AT
#define RUN_AT(x)                       (jiffies+(x))
#endif

/* DMA related */
#define RESERV_AC0DMA                   4

#define PRIVATE_Message                 0

/*---------------------  Export Types  ------------------------------*/

#define DBG_PRT(l, p, args...) { if (l <= msglevel) printk(p, ##args); }
#define PRINT_K(p, args...) { if (PRIVATE_Message) printk(p, ##args); }

typedef enum __device_msg_level {
	MSG_LEVEL_ERR = 0,            /* Errors causing abnormal operation */
	MSG_LEVEL_NOTICE = 1,         /* Errors needing user notification */
	MSG_LEVEL_INFO = 2,           /* Normal message. */
	MSG_LEVEL_VERBOSE = 3,        /* Will report all trival errors. */
	MSG_LEVEL_DEBUG = 4           /* Only for debug purpose. */
} DEVICE_MSG_LEVEL, *PDEVICE_MSG_LEVEL;

typedef enum __device_init_type {
	DEVICE_INIT_COLD = 0,       /* cold init */
	DEVICE_INIT_RESET,          /* reset init or Dx to D0 power remain */
	DEVICE_INIT_DXPL            /* Dx to D0 power lost init */
} DEVICE_INIT_TYPE, *PDEVICE_INIT_TYPE;

/* USB */

/*
 * Enum of context types for SendPacket
 */
typedef enum _CONTEXT_TYPE {
    CONTEXT_DATA_PACKET = 1,
    CONTEXT_MGMT_PACKET
} CONTEXT_TYPE;

/* RCB (Receive Control Block) */
typedef struct _RCB
{
	void *Next;
	signed long Ref;
	void *pDevice;
	struct urb *pUrb;
	struct vnt_rx_mgmt sMngPacket;
	struct sk_buff *skb;
	int bBoolInUse;

} RCB, *PRCB;

/* used to track bulk out irps */
typedef struct _USB_SEND_CONTEXT {
    void *pDevice;
    struct sk_buff *pPacket;
    struct urb      *pUrb;
    unsigned int            uBufLen;
    CONTEXT_TYPE    Type;
    SEthernetHeader sEthHeader;
    void *Next;
    bool            bBoolInUse;
    unsigned char           Data[MAX_TOTAL_SIZE_WITH_ALL_HEADERS];
} USB_SEND_CONTEXT, *PUSB_SEND_CONTEXT;

/* structure got from configuration file as user-desired default settings */
typedef struct _DEFAULT_CONFIG {
	signed int    ZoneType;
	signed int    eConfigMode;
	signed int    eAuthenMode;        /* open/wep/wpa */
	signed int    bShareKeyAlgorithm; /* open-open/{open,wep}-sharekey */
	signed int    keyidx;             /* wepkey index */
	signed int    eEncryptionStatus;
} DEFAULT_CONFIG, *PDEFAULT_CONFIG;

/*
 * Structure to keep track of USB interrupt packets
 */
typedef struct {
    unsigned int            uDataLen;
    u8 *           pDataBuf;
  /* struct urb *pUrb; */
    bool            bInUse;
} INT_BUFFER, *PINT_BUFFER;

/* 0:11A 1:11B 2:11G */
typedef enum _VIA_BB_TYPE
{
    BB_TYPE_11A = 0,
    BB_TYPE_11B,
    BB_TYPE_11G
} VIA_BB_TYPE, *PVIA_BB_TYPE;

/* 0:11a, 1:11b, 2:11gb (only CCK in BasicRate), 3:11ga(OFDM in BasicRate) */
typedef enum _VIA_PKT_TYPE
{
    PK_TYPE_11A = 0,
    PK_TYPE_11B,
    PK_TYPE_11GB,
    PK_TYPE_11GA
} VIA_PKT_TYPE, *PVIA_PKT_TYPE;

/*++ NDIS related */

typedef enum __DEVICE_NDIS_STATUS {
    STATUS_SUCCESS = 0,
    STATUS_FAILURE,
    STATUS_RESOURCES,
    STATUS_PENDING,
} DEVICE_NDIS_STATUS, *PDEVICE_NDIS_STATUS;

#define MAX_BSSIDINFO_4_PMKID   16
#define MAX_PMKIDLIST           5
/* flags for PMKID Candidate list structure */
#define NDIS_802_11_PMKID_CANDIDATE_PREAUTH_ENABLED	0x01

/* PMKID Structures */
typedef unsigned char   NDIS_802_11_PMKID_VALUE[16];


typedef enum _NDIS_802_11_WEP_STATUS
{
    Ndis802_11WEPEnabled,
    Ndis802_11Encryption1Enabled = Ndis802_11WEPEnabled,
    Ndis802_11WEPDisabled,
    Ndis802_11EncryptionDisabled = Ndis802_11WEPDisabled,
    Ndis802_11WEPKeyAbsent,
    Ndis802_11Encryption1KeyAbsent = Ndis802_11WEPKeyAbsent,
    Ndis802_11WEPNotSupported,
    Ndis802_11EncryptionNotSupported = Ndis802_11WEPNotSupported,
    Ndis802_11Encryption2Enabled,
    Ndis802_11Encryption2KeyAbsent,
    Ndis802_11Encryption3Enabled,
    Ndis802_11Encryption3KeyAbsent
} NDIS_802_11_WEP_STATUS, *PNDIS_802_11_WEP_STATUS,
  NDIS_802_11_ENCRYPTION_STATUS, *PNDIS_802_11_ENCRYPTION_STATUS;


typedef enum _NDIS_802_11_STATUS_TYPE
{
	Ndis802_11StatusType_Authentication,
	Ndis802_11StatusType_MediaStreamMode,
	Ndis802_11StatusType_PMKID_CandidateList,
	Ndis802_11StatusTypeMax, /* not a real type, defined as upper bound */
} NDIS_802_11_STATUS_TYPE, *PNDIS_802_11_STATUS_TYPE;

/* added new types for PMKID Candidate lists */
typedef struct _PMKID_CANDIDATE {
    NDIS_802_11_MAC_ADDRESS BSSID;
    unsigned long Flags;
} PMKID_CANDIDATE, *PPMKID_CANDIDATE;


typedef struct _BSSID_INFO
{
    NDIS_802_11_MAC_ADDRESS BSSID;
    NDIS_802_11_PMKID_VALUE PMKID;
} BSSID_INFO, *PBSSID_INFO;

typedef struct tagSPMKID {
    unsigned long Length;
    unsigned long BSSIDInfoCount;
    BSSID_INFO BSSIDInfo[MAX_BSSIDINFO_4_PMKID];
} SPMKID, *PSPMKID;

typedef struct tagSPMKIDCandidateEvent {
    NDIS_802_11_STATUS_TYPE     StatusType;
	unsigned long Version;       /* Version of the structure */
	unsigned long NumCandidates; /* No. of pmkid candidates */
    PMKID_CANDIDATE CandidateList[MAX_PMKIDLIST];
} SPMKIDCandidateEvent, *PSPMKIDCandidateEvent;

/*++ 802.11h related */
#define MAX_QUIET_COUNT     8

typedef struct tagSQuietControl {
    bool        bEnable;
    u32       dwStartTime;
    u8        byPeriod;
    u16        wDuration;
} SQuietControl, *PSQuietControl;

/* The receive duplicate detection cache entry */
typedef struct tagSCacheEntry{
    u16        wFmSequence;
    u8        abyAddr2[ETH_ALEN];
    u16        wFrameCtl;
} SCacheEntry, *PSCacheEntry;

typedef struct tagSCache{
/* The receive cache is updated circularly.  The next entry to be written is
 * indexed by the "InPtr".
 */
	unsigned int uInPtr; /* Place to use next */
    SCacheEntry     asCacheEntry[DUPLICATE_RX_CACHE_LENGTH];
} SCache, *PSCache;

#define CB_MAX_RX_FRAG                 64
/*
 * DeFragment Control Block, used for collecting fragments prior to reassembly
 */
typedef struct tagSDeFragControlBlock
{
    u16            wSequence;
    u16            wFragNum;
    u8            abyAddr2[ETH_ALEN];
	unsigned int            uLifetime;
    struct sk_buff* skb;
    u8 *           pbyRxBuffer;
    unsigned int            cbFrameLength;
    bool            bInUse;
} SDeFragControlBlock, *PSDeFragControlBlock;

/* flags for options */
#define     DEVICE_FLAGS_UNPLUG          0x00000001UL
#define     DEVICE_FLAGS_PREAMBLE_TYPE   0x00000002UL
#define     DEVICE_FLAGS_OP_MODE         0x00000004UL
#define     DEVICE_FLAGS_PS_MODE         0x00000008UL
#define		DEVICE_FLAGS_80211h_MODE	 0x00000010UL

/* flags for driver status */
#define     DEVICE_FLAGS_OPENED          0x00010000UL
#define     DEVICE_FLAGS_WOL_ENABLED     0x00080000UL
/* flags for capabilities */
#define     DEVICE_FLAGS_TX_ALIGN        0x01000000UL
#define     DEVICE_FLAGS_HAVE_CAM        0x02000000UL
#define     DEVICE_FLAGS_FLOW_CTRL       0x04000000UL

/* flags for MII status */
#define     DEVICE_LINK_FAIL             0x00000001UL
#define     DEVICE_SPEED_10              0x00000002UL
#define     DEVICE_SPEED_100             0x00000004UL
#define     DEVICE_SPEED_1000            0x00000008UL
#define     DEVICE_DUPLEX_FULL           0x00000010UL
#define     DEVICE_AUTONEG_ENABLE        0x00000020UL
#define     DEVICE_FORCED_BY_EEPROM      0x00000040UL
/* for device_set_media_duplex */
#define     DEVICE_LINK_CHANGE           0x00000001UL


typedef struct __device_opt {
	int nRxDescs0;  /* number of RX descriptors 0 */
	int nTxDescs0;  /* number of TX descriptors 0, 1, 2, 3 */
	int rts_thresh; /* RTS threshold */
    int         frag_thresh;
    int         OpMode;
    int         data_rate;
    int         channel_num;
    int         short_retry;
    int         long_retry;
    int         bbp_type;
    u32         flags;
} OPTIONS, *POPTIONS;


struct vnt_private {
	/* netdev */
	struct usb_device *usb;
	struct net_device *dev;
	struct net_device_stats stats;

	OPTIONS sOpts;

	struct tasklet_struct CmdWorkItem;
	struct tasklet_struct EventWorkItem;
	struct tasklet_struct ReadWorkItem;
	struct tasklet_struct RxMngWorkItem;

	u32 rx_buf_sz;
	int multicast_limit;
	u8 byRxMode;

	spinlock_t lock;

	u32 rx_bytes;

	u8 byRevId;

	u32 flags;
	unsigned long Flags;

	SCache sDupRxCache;

	SDeFragControlBlock sRxDFCB[CB_MAX_RX_FRAG];
	u32 cbDFCB;
	u32 cbFreeDFCB;
	u32 uCurrentDFCBIdx;


	/* USB */
	struct urb *pControlURB;
	struct urb *pInterruptURB;
	struct usb_ctrlrequest sUsbCtlRequest;
	u32 int_interval;

	/* Variables to track resources for the BULK In Pipe */
	PRCB pRCBMem;
	PRCB apRCB[CB_MAX_RX_DESC];
	u32 cbRD;
	PRCB FirstRecvFreeList;
	PRCB LastRecvFreeList;
	u32 NumRecvFreeList;
	PRCB FirstRecvMngList;
	PRCB LastRecvMngList;
	u32 NumRecvMngList;
	int bIsRxWorkItemQueued;
	int bIsRxMngWorkItemQueued;
	unsigned long ulRcvRefCount; /* packets that have not returned back */

	/* Variables to track resources for the BULK Out Pipe */
	PUSB_SEND_CONTEXT apTD[CB_MAX_TX_DESC];
	u32 cbTD;

	/* Variables to track resources for the Interrupt In Pipe */
	INT_BUFFER intBuf;
	int fKillEventPollingThread;
	int bEventAvailable;

	/* default config from file by user setting */
	DEFAULT_CONFIG config_file;


	/* Statistic for USB */
	unsigned long ulBulkInPosted;
	unsigned long ulBulkInError;
	unsigned long ulBulkInContCRCError;
	unsigned long ulBulkInBytesRead;

	unsigned long ulBulkOutPosted;
	unsigned long ulBulkOutError;
	unsigned long ulBulkOutContCRCError;
	unsigned long ulBulkOutBytesWrite;

	unsigned long ulIntInPosted;
	unsigned long ulIntInError;
	unsigned long ulIntInContCRCError;
	unsigned long ulIntInBytesRead;


	/* Version control */
	u16 wFirmwareVersion;
	u8 byLocalID;
	u8 byRFType;
	u8 byBBRxConf;


	u8 byZoneType;
	int bZoneRegExist;

	u8 byOriginalZonetype;

	int bLinkPass; /* link status: OK or fail */
	u8 abyCurrentNetAddr[ETH_ALEN];
	u8 abyPermanentNetAddr[ETH_ALEN];

	int bExistSWNetAddr;

	/* Adapter statistics */
	SStatCounter scStatistic;
	/* 802.11 counter */
	SDot11Counters s802_11Counter;

	/* Maintain statistical debug info. */
	unsigned long packetsReceived;
	unsigned long packetsReceivedDropped;
	unsigned long packetsReceivedOverflow;
	unsigned long packetsSent;
	unsigned long packetsSentDropped;
	unsigned long SendContextsInUse;
	unsigned long RcvBuffersInUse;

	/* 802.11 management */
	struct vnt_manager vnt_mgmt;

	u64 qwCurrTSF;
	u32 cbBulkInMax;
	int bPSRxBeacon;

	/* 802.11 MAC specific */
	u32 uCurrRSSI;
	u8 byCurrSQ;

	/* Antenna Diversity */
	int bTxRxAntInv;
	u32 dwRxAntennaSel;
	u32 dwTxAntennaSel;
	u8 byAntennaCount;
	u8 byRxAntennaMode;
	u8 byTxAntennaMode;
	u8 byRadioCtl;
	u8 bHWRadioOff;

	/* SQ3 functions for antenna diversity */
	struct timer_list TimerSQ3Tmax1;
	struct timer_list TimerSQ3Tmax2;
	struct timer_list TimerSQ3Tmax3;

	int bDiversityRegCtlON;
	int bDiversityEnable;
	unsigned long ulDiversityNValue;
	unsigned long ulDiversityMValue;
	u8 byTMax;
	u8 byTMax2;
	u8 byTMax3;
	unsigned long ulSQ3TH;

	unsigned long uDiversityCnt;
	u8 byAntennaState;
	unsigned long ulRatio_State0;
	unsigned long ulRatio_State1;
	unsigned long ulSQ3_State0;
	unsigned long ulSQ3_State1;

	unsigned long aulSQ3Val[MAX_RATE];
	unsigned long aulPktNum[MAX_RATE];

	/* IFS & Cw */
	u32 uSIFS;  /* Current SIFS */
	u32 uDIFS;  /* Current DIFS */
	u32 uEIFS;  /* Current EIFS */
	u32 uSlot;  /* Current SlotTime */
	u32 uCwMin; /* Current CwMin */
	u32 uCwMax; /* CwMax is fixed on 1023 */

	/* PHY parameter */
	u8  bySIFS;
	u8  byDIFS;
	u8  byEIFS;
	u8  bySlot;
	u8  byCWMaxMin;

	/* Rate */
	VIA_BB_TYPE byBBType; /* 0: 11A, 1:11B, 2:11G */
	VIA_PKT_TYPE byPacketType; /* 0:11a 1:11b 2:11gb 3:11ga */
	u16 wBasicRate;
	u8 byACKRate;
	u8 byTopOFDMBasicRate;
	u8 byTopCCKBasicRate;


	u32 dwAotoRateTxOkCnt;
	u32 dwAotoRateTxFailCnt;
	u32 dwErrorRateThreshold[13];
	u32 dwTPTable[MAX_RATE];
	u8 abyEEPROM[EEP_MAX_CONTEXT_SIZE];  /*u32 alignment */

	u8 byMinChannel;
	u8 byMaxChannel;
	u32 uConnectionRate;

	u8 byPreambleType;
	u8 byShortPreamble;
	/* CARD_PHY_TYPE */
	u8 eConfigPHYMode;

	/* For RF Power table */
	u8 byCCKPwr;
	u8 byOFDMPwrG;
	u8 byOFDMPwrA;
	u8 byCurPwr;
	u8 abyCCKPwrTbl[14];
	u8 abyOFDMPwrTbl[14];
	u8 abyOFDMAPwrTbl[42];

	u16 wCurrentRate;
	u16 wRTSThreshold;
	u16 wFragmentationThreshold;
	u8 byShortRetryLimit;
	u8 byLongRetryLimit;
	CARD_OP_MODE eOPMode;
	int bBSSIDFilter;
	u16 wMaxTransmitMSDULifetime;
	u8 abyBSSID[ETH_ALEN];
	u8 abyDesireBSSID[ETH_ALEN];

	u16 wCTSDuration;       /* update while speed change */
	u16 wACKDuration;
	u16 wRTSTransmitLen;
	u8 byRTSServiceField;
	u8 byRTSSignalField;

	u32 dwMaxReceiveLifetime;  /* dot11MaxReceiveLifetime */

	int bCCK;
	int bEncryptionEnable;
	int bLongHeader;
	int bSoftwareGenCrcErr;
	int bShortSlotTime;
	int bProtectMode;
	int bNonERPPresent;
	int bBarkerPreambleMd;

	u8 byERPFlag;
	u16 wUseProtectCntDown;

	int bRadioControlOff;
	int bRadioOff;

	/* Power save */
	int bEnablePSMode;
	u16 wListenInterval;
	int bPWBitOn;
	WMAC_POWER_MODE ePSMode;
	unsigned long ulPSModeWaitTx;
	int bPSModeTxBurst;

	/* Beacon releated */
	u16 wSeqCounter;
	int bBeaconBufReady;
	int bBeaconSent;
	int bFixRate;
	u8 byCurrentCh;
	u32 uScanTime;

	CMD_STATE eCommandState;

	CMD_CODE eCommand;
	int bBeaconTx;
	u8 byScanBBType;

	int bStopBeacon;
	int bStopDataPkt;
	int bStopTx0Pkt;
	u32 uAutoReConnectTime;
	u32 uIsroamingTime;

	/* 802.11 counter */

	CMD_ITEM eCmdQueue[CMD_Q_SIZE];
	u32 uCmdDequeueIdx;
	u32 uCmdEnqueueIdx;
	u32 cbFreeCmdQueue;
	int bCmdRunning;
	int bCmdClear;
	int bNeedRadioOFF;

	int bEnableRoaming;
	int bIsRoaming;
	int bFastRoaming;
	u8 bSameBSSMaxNum;
	u8 bSameBSSCurNum;
	int bRoaming;
	int b11hEable;
	unsigned long ulTxPower;

	/* Encryption */
	NDIS_802_11_WEP_STATUS eEncryptionStatus;
	int  bTransmitKey;
	NDIS_802_11_WEP_STATUS eOldEncryptionStatus;
	SKeyManagement sKey;
	u32 dwIVCounter;


	RC4Ext SBox;
	u8 abyPRNG[WLAN_WEPMAX_KEYLEN+3];
	u8 byKeyIndex;

	int bAES;

	u32 uKeyLength;
	u8 abyKey[WLAN_WEP232_KEYLEN];

	/* for AP mode */
	u32 uAssocCount;
	int bMoreData;

	/* QoS */
	int bGrpAckPolicy;


	u8 byAutoFBCtrl;

	int bTxMICFail;
	int bRxMICFail;


	/* For Update BaseBand VGA Gain Offset */
	int bUpdateBBVGA;
	u32 uBBVGADiffCount;
	u8 byBBVGANew;
	u8 byBBVGACurrent;
	u8 abyBBVGA[BB_VGA_LEVEL];
	signed long ldBmThreshold[BB_VGA_LEVEL];

	u8 byBBPreEDRSSI;
	u8 byBBPreEDIndex;


	int bRadioCmd;
	u32 dwDiagRefCount;

	/* For FOE Tuning */
	u8  byFOETuning;

	/* For Auto Power Tunning */
	u8  byAutoPwrTunning;

	/* BaseBand Loopback Use */
	u8 byBBCR4d;
	u8 byBBCRc9;
	u8 byBBCR88;
	u8 byBBCR09;

	/* command timer */
	struct timer_list sTimerCommand;

	struct timer_list sTimerTxData;
	unsigned long nTxDataTimeCout;
	int fTxDataInSleep;
	int IsTxDataTrigger;

	int fWPA_Authened; /*is WPA/WPA-PSK or WPA2/WPA2-PSK authen?? */
	u8 byReAssocCount;
	u8 byLinkWaitCount;

	SEthernetHeader sTxEthHeader;
	SEthernetHeader sRxEthHeader;
	u8 abyBroadcastAddr[ETH_ALEN];
	u8 abySNAP_RFC1042[ETH_ALEN];
	u8 abySNAP_Bridgetunnel[ETH_ALEN];

	/* Pre-Authentication & PMK cache */
	SPMKID gsPMKID;
	SPMKIDCandidateEvent gsPMKIDCandidate;


	/* for 802.11h */
	int b11hEnable;

	int bChannelSwitch;
	u8 byNewChannel;
	u8 byChannelSwitchCount;

	/* WPA supplicant daemon */
	int bWPADEVUp;
	int bwextstep0;
	int bwextstep1;
	int bwextstep2;
	int bwextstep3;
	int bWPASuppWextEnabled;

	/* user space daemon: hostapd, is used for HOSTAP */
	int bEnableHostapd;
	int bEnable8021x;
	int bEnableHostWEP;
	struct net_device *apdev;
	int (*tx_80211)(struct sk_buff *skb, struct net_device *dev);

	u32 uChannel;

	struct iw_statistics wstats; /* wireless stats */

	int bCommit;

};




#define EnqueueRCB(_Head, _Tail, _RCB)                  \
{                                                       \
    if (!_Head) {                                       \
        _Head = _RCB;                                   \
    }                                                   \
    else {                                              \
        _Tail->Next = _RCB;                             \
    }                                                   \
    _RCB->Next = NULL;                                  \
    _Tail = _RCB;                                       \
}

#define DequeueRCB(Head, Tail)                          \
{                                                       \
    PRCB   RCB = Head;                                  \
    if (!RCB->Next) {                                   \
        Tail = NULL;                                    \
    }                                                   \
    Head = RCB->Next;                                   \
}


#define ADD_ONE_WITH_WRAP_AROUND(uVar, uModulo) {   \
    if ((uVar) >= ((uModulo) - 1))                  \
        (uVar) = 0;                                 \
    else                                            \
        (uVar)++;                                   \
}


#define fMP_RESET_IN_PROGRESS               0x00000001
#define fMP_DISCONNECTED                    0x00000002
#define fMP_HALT_IN_PROGRESS                0x00000004
#define fMP_SURPRISE_REMOVED                0x00000008
#define fMP_RECV_LOOKASIDE                  0x00000010
#define fMP_INIT_IN_PROGRESS                0x00000020
#define fMP_SEND_SIDE_RESOURCE_ALLOCATED    0x00000040
#define fMP_RECV_SIDE_RESOURCE_ALLOCATED    0x00000080
#define fMP_POST_READS                      0x00000100
#define fMP_POST_WRITES                     0x00000200
#define fMP_CONTROL_READS                   0x00000400
#define fMP_CONTROL_WRITES                  0x00000800

#define MP_SET_FLAG(_M, _F)             ((_M)->Flags |= (_F))
#define MP_CLEAR_FLAG(_M, _F)            ((_M)->Flags &= ~(_F))
#define MP_TEST_FLAGS(_M, _F)            (((_M)->Flags & (_F)) == (_F))

#define MP_IS_READY(_M)        (((_M)->Flags & \
                                 (fMP_DISCONNECTED | fMP_RESET_IN_PROGRESS | fMP_HALT_IN_PROGRESS | fMP_INIT_IN_PROGRESS | fMP_SURPRISE_REMOVED)) == 0)

/*---------------------  Export Functions  --------------------------*/

int device_alloc_frag_buf(struct vnt_private *, PSDeFragControlBlock pDeF);

#endif
