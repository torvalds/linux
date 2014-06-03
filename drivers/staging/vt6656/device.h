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
#include <linux/crc32.h>

#ifdef SIOCETHTOOL
#define DEVICE_ETHTOOL_IOCTL_SUPPORT
#include <linux/ethtool.h>
#else
#undef DEVICE_ETHTOOL_IOCTL_SUPPORT
#endif

#define MAX_RATE			12

/*
 * device specific
 */

#include "80211hdr.h"
#include "tether.h"
#include "wmgr.h"
#include "wcmd.h"
#include "rc4.h"
#include "desc.h"
#include "key.h"
#include "card.h"

#define VNT_USB_VENDOR_ID                     0x160a
#define VNT_USB_PRODUCT_ID                    0x3184

#define DEVICE_NAME			"vt6656"
#define DEVICE_FULL_DRV_NAM		"VIA Networking Wireless LAN USB Driver"

#define DEVICE_VERSION			"1.19_12"

#define CONFIG_PATH			"/etc/vntconfiguration.dat"

#define MAX_UINTS			8
#define OPTION_DEFAULT			{ [0 ... MAX_UINTS-1] = -1}

#define DUPLICATE_RX_CACHE_LENGTH       5

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

#define BB_VGA_LEVEL            4
#define BB_VGA_CHANGE_THRESHOLD 3

#define EEP_MAX_CONTEXT_SIZE    256

/* Contents in the EEPROM */
#define EEP_OFS_PAR		0x0
#define EEP_OFS_ANTENNA		0x17
#define EEP_OFS_RADIOCTL	0x18
#define EEP_OFS_RFTYPE		0x1b
#define EEP_OFS_MINCHANNEL	0x1c
#define EEP_OFS_MAXCHANNEL	0x1d
#define EEP_OFS_SIGNATURE	0x1e
#define EEP_OFS_ZONETYPE	0x1f
#define EEP_OFS_RFTABLE		0x20
#define EEP_OFS_PWR_CCK		0x20
#define EEP_OFS_SETPT_CCK	0x21
#define EEP_OFS_PWR_OFDMG	0x23

#define EEP_OFS_CALIB_TX_IQ	0x24
#define EEP_OFS_CALIB_TX_DC	0x25
#define EEP_OFS_CALIB_RX_IQ	0x26

#define EEP_OFS_MAJOR_VER	0x2e
#define EEP_OFS_MINOR_VER	0x2f

#define EEP_OFS_CCK_PWR_TBL	0x30
#define EEP_OFS_OFDM_PWR_TBL	0x40
#define EEP_OFS_OFDMA_PWR_TBL	0x50

/* Bits in EEP_OFS_ANTENNA */
#define EEP_ANTENNA_MAIN	0x1
#define EEP_ANTENNA_AUX		0x2
#define EEP_ANTINV		0x4

/* Bits in EEP_OFS_RADIOCTL */
#define EEP_RADIOCTL_ENABLE	0x80

/* control commands */
#define MESSAGE_TYPE_READ		0x1
#define MESSAGE_TYPE_WRITE		0x0
#define MESSAGE_TYPE_LOCK_OR		0x2
#define MESSAGE_TYPE_LOCK_AND		0x3
#define MESSAGE_TYPE_WRITE_MASK		0x4
#define MESSAGE_TYPE_CARDINIT		0x5
#define MESSAGE_TYPE_INIT_RSP		0x6
#define MESSAGE_TYPE_MACSHUTDOWN	0x7
#define MESSAGE_TYPE_SETKEY		0x8
#define MESSAGE_TYPE_CLRKEYENTRY	0x9
#define MESSAGE_TYPE_WRITE_MISCFF	0xa
#define MESSAGE_TYPE_SET_ANTMD		0xb
#define MESSAGE_TYPE_SELECT_CHANNLE	0xc
#define MESSAGE_TYPE_SET_TSFTBTT	0xd
#define MESSAGE_TYPE_SET_SSTIFS		0xe
#define MESSAGE_TYPE_CHANGE_BBTYPE	0xf
#define MESSAGE_TYPE_DISABLE_PS		0x10
#define MESSAGE_TYPE_WRITE_IFRF		0x11

/* command read/write(index) */
#define MESSAGE_REQUEST_MEM		0x1
#define MESSAGE_REQUEST_BBREG		0x2
#define MESSAGE_REQUEST_MACREG		0x3
#define MESSAGE_REQUEST_EEPROM		0x4
#define MESSAGE_REQUEST_TSF		0x5
#define MESSAGE_REQUEST_TBTT		0x6
#define MESSAGE_REQUEST_BBAGC		0x7
#define MESSAGE_REQUEST_VERSION		0x8
#define MESSAGE_REQUEST_RF_INIT		0x9
#define MESSAGE_REQUEST_RF_INIT2	0xa
#define MESSAGE_REQUEST_RF_CH0		0xb
#define MESSAGE_REQUEST_RF_CH1		0xc
#define MESSAGE_REQUEST_RF_CH2		0xd

/* USB registers */
#define USB_REG4			0x604

#ifndef RUN_AT
#define RUN_AT(x)                       (jiffies+(x))
#endif

#define PRIVATE_Message                 0

#define DBG_PRT(l, p, args...) { if (l <= msglevel) printk(p, ##args); }
#define PRINT_K(p, args...) { if (PRIVATE_Message) printk(p, ##args); }

typedef enum __device_msg_level {
	MSG_LEVEL_ERR = 0,            /* Errors causing abnormal operation */
	MSG_LEVEL_NOTICE = 1,         /* Errors needing user notification */
	MSG_LEVEL_INFO = 2,           /* Normal message. */
	MSG_LEVEL_VERBOSE = 3,        /* Will report all trival errors. */
	MSG_LEVEL_DEBUG = 4           /* Only for debug purpose. */
} DEVICE_MSG_LEVEL, *PDEVICE_MSG_LEVEL;

#define DEVICE_INIT_COLD	0x0 /* cold init */
#define DEVICE_INIT_RESET	0x1 /* reset init or Dx to D0 power remain */
#define DEVICE_INIT_DXPL	0x2 /* Dx to D0 power lost init */

/* Device init */
struct vnt_cmd_card_init {
	u8 init_class;
	u8 exist_sw_net_addr;
	u8 sw_net_addr[6];
	u8 short_retry_limit;
	u8 long_retry_limit;
};

struct vnt_rsp_card_init {
	u8 status;
	u8 net_addr[6];
	u8 rf_type;
	u8 min_channel;
	u8 max_channel;
};

/* USB */

/*
 * Enum of context types for SendPacket
 */
enum {
	CONTEXT_DATA_PACKET = 1,
	CONTEXT_MGMT_PACKET
};

/* RCB (Receive Control Block) */
struct vnt_rcb {
	void *Next;
	signed long Ref;
	void *pDevice;
	struct urb *pUrb;
	struct vnt_rx_mgmt sMngPacket;
	struct sk_buff *skb;
	int bBoolInUse;
};

/* used to track bulk out irps */
struct vnt_usb_send_context {
	void *priv;
	struct sk_buff *skb;
	struct urb *urb;
	unsigned int buf_len;
	u8 type;
	bool in_use;
	unsigned char data[MAX_TOTAL_SIZE_WITH_ALL_HEADERS];
};

/* tx packet info for rxtx */
struct vnt_tx_pkt_info {
	u16 fifo_ctl;
	u8 dest_addr[ETH_ALEN];
};

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
struct vnt_interrupt_buffer {
	u8 *data_buf;
	bool in_use;
};

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

/* The receive duplicate detection cache entry */
typedef struct tagSCacheEntry{
	__le16 wFmSequence;
	u8 abyAddr2[ETH_ALEN];
	__le16 wFrameCtl;
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

/* flags for driver status */
#define     DEVICE_FLAGS_OPENED          0x00010000UL

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

	struct work_struct read_work_item;
	struct work_struct rx_mng_work_item;

	u32 rx_buf_sz;
	int multicast_limit;
	u8 byRxMode;

	spinlock_t lock;
	struct mutex usb_lock;

	u32 rx_bytes;

	u32 flags;
	unsigned long Flags;

	SCache sDupRxCache;

	SDeFragControlBlock sRxDFCB[CB_MAX_RX_FRAG];
	u32 cbDFCB;
	u32 cbFreeDFCB;
	u32 uCurrentDFCBIdx;

	/* USB */
	struct urb *pInterruptURB;
	u32 int_interval;

	/* Variables to track resources for the BULK In Pipe */
	struct vnt_rcb *pRCBMem;
	struct vnt_rcb *apRCB[CB_MAX_RX_DESC];
	u32 cbRD;
	struct vnt_rcb *FirstRecvFreeList;
	struct vnt_rcb *LastRecvFreeList;
	u32 NumRecvFreeList;
	struct vnt_rcb *FirstRecvMngList;
	struct vnt_rcb *LastRecvMngList;
	u32 NumRecvMngList;
	int bIsRxWorkItemQueued;
	int bIsRxMngWorkItemQueued;
	unsigned long ulRcvRefCount; /* packets that have not returned back */

	/* Variables to track resources for the BULK Out Pipe */
	struct vnt_usb_send_context *apTD[CB_MAX_TX_DESC];
	u32 cbTD;
	struct vnt_tx_pkt_info pkt_info[16];

	/* Variables to track resources for the Interrupt In Pipe */
	struct vnt_interrupt_buffer int_buf;

	/* default config from file by user setting */
	DEFAULT_CONFIG config_file;

	/* Version control */
	u16 wFirmwareVersion;
	u8 byLocalID;
	u8 byRFType;
	u8 byBBRxConf;

	u8 byZoneType;
	int bZoneRegExist;

	u8 byOriginalZonetype;

	int bLinkPass; /* link status: OK or fail */
	struct vnt_cmd_card_init init_command;
	struct vnt_rsp_card_init init_response;
	u8 abyCurrentNetAddr[ETH_ALEN];
	u8 abyPermanentNetAddr[ETH_ALEN];

	int bExistSWNetAddr;

	/* Maintain statistical debug info. */
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
	u8 byBBType; /* 0: 11A, 1:11B, 2:11G */
	u8 byPacketType; /* 0:11a 1:11b 2:11gb 3:11ga */
	u16 wBasicRate;
	u8 byTopOFDMBasicRate;
	u8 byTopCCKBasicRate;

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
	u16 tx_rate_fb0;
	u16 tx_rate_fb1;

	u16 wRTSThreshold;
	u16 wFragmentationThreshold;
	u8 byShortRetryLimit;
	u8 byLongRetryLimit;

	enum nl80211_iftype op_mode;

	int bBSSIDFilter;
	u16 wMaxTransmitMSDULifetime;
	u8 abyBSSID[ETH_ALEN];
	u8 abyDesireBSSID[ETH_ALEN];

	u32 dwMaxReceiveLifetime;  /* dot11MaxReceiveLifetime */

	int bEncryptionEnable;
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

	/* Encryption */
	NDIS_802_11_WEP_STATUS eEncryptionStatus;
	int  bTransmitKey;
	NDIS_802_11_WEP_STATUS eOldEncryptionStatus;
	SKeyManagement sKey;
	u32 dwIVCounter;

	RC4Ext SBox;
	u8 abyPRNG[WLAN_WEPMAX_KEYLEN+3];
	u8 byKeyIndex;

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
	u32 uBBVGADiffCount;
	u8 byBBVGANew;
	u8 byBBVGACurrent;
	u8 abyBBVGA[BB_VGA_LEVEL];
	signed long ldBmThreshold[BB_VGA_LEVEL];

	u8 byBBPreEDRSSI;
	u8 byBBPreEDIndex;

	int bRadioCmd;

	/* command timer */
	struct delayed_work run_command_work;
	/* One second callback */
	struct delayed_work second_callback_work;

	u8 tx_data_time_out;
	bool tx_trigger;
	int fWPA_Authened; /*is WPA/WPA-PSK or WPA2/WPA2-PSK authen?? */
	u8 byReAssocCount;
	u8 byLinkWaitCount;

	struct ethhdr sTxEthHeader;
	struct ethhdr sRxEthHeader;
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
    struct vnt_rcb *RCB = Head;                         \
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

#define fMP_DISCONNECTED                    0x00000002
#define fMP_POST_READS                      0x00000100
#define fMP_POST_WRITES                     0x00000200

#define MP_SET_FLAG(_M, _F)             ((_M)->Flags |= (_F))
#define MP_CLEAR_FLAG(_M, _F)            ((_M)->Flags &= ~(_F))
#define MP_TEST_FLAGS(_M, _F)            (((_M)->Flags & (_F)) == (_F))

#define MP_IS_READY(_M)        (((_M)->Flags & fMP_DISCONNECTED) == 0)

int device_alloc_frag_buf(struct vnt_private *, PSDeFragControlBlock pDeF);
void vnt_configure_filter(struct vnt_private *);

#endif
