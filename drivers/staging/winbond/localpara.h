#ifndef __WINBOND_LOCALPARA_H
#define __WINBOND_LOCALPARA_H

/*
 * =============================================================
 * LocalPara.h -
 * =============================================================
 */

#include "mac_structures.h"

/* Define the local ability */

#define LOCAL_DEFAULT_BEACON_PERIOD	100	/* ms */
#define LOCAL_DEFAULT_ATIM_WINDOW	0
#define LOCAL_DEFAULT_ERP_CAPABILITY	0x0431	/*
						 * 0x0001: ESS
						 * 0x0010: Privacy
						 * 0x0020: short preamble
						 * 0x0400: short slot time
						 */
#define LOCAL_DEFAULT_LISTEN_INTERVAL	5

#define LOCAL_DEFAULT_24_CHANNEL_NUM	13	/* channel 1..13 */
#define LOCAL_DEFAULT_5_CHANNEL_NUM	8	/* channel 36..64 */

#define LOCAL_USA_24_CHANNEL_NUM	11
#define LOCAL_USA_5_CHANNEL_NUM		12
#define LOCAL_EUROPE_24_CHANNEL_NUM	13
#define LOCAL_EUROPE_5_CHANNEL_NUM	19
#define LOCAL_JAPAN_24_CHANNEL_NUM	14
#define LOCAL_JAPAN_5_CHANNEL_NUM	11
#define LOCAL_UNKNOWN_24_CHANNEL_NUM	14
#define LOCAL_UNKNOWN_5_CHANNEL_NUM	34	/* not include 165 */

#define psLOCAL				(&(adapter->sLocalPara))

#define MODE_802_11_BG			0
#define MODE_802_11_A			1
#define MODE_802_11_ABG			2
#define MODE_802_11_BG_IBSS		3
#define MODE_802_11_B			4
#define MODE_AUTO			255

#define BAND_TYPE_DSSS			0
#define BAND_TYPE_OFDM_24		1
#define BAND_TYPE_OFDM_5		2

/* refer Bitmap2RateValue table */

/* the bitmap value of all the H/W supported rates: */
/* 1, 2, 5.5, 11, 6, 9, 12, 18, 24, 36, 48, 54 */
#define LOCAL_ALL_SUPPORTED_RATES_BITMAP	0x130c1a66
/* the bitmap value of all the H/W supported rates except to non-OFDM rates: */
/* 6, 9, 12, 18, 24, 36, 48, 54 */
#define LOCAL_OFDM_SUPPORTED_RATES_BITMAP	0x130c1240
#define LOCAL_11B_SUPPORTED_RATE_BITMAP		0x826
#define LOCAL_11B_BASIC_RATE_BITMAP		0x826
#define LOCAL_11B_OPERATION_RATE_BITMAP		0x826
#define LOCAL_11G_BASIC_RATE_BITMAP		0x826	   /* 1, 2, 5.5, 11 */
#define LOCAL_11G_OPERATION_RATE_BITMAP		0x130c1240 /* 6, 9, 12, 18, 24, 36, 48, 54 */
#define LOCAL_11A_BASIC_RATE_BITMAP		0x01001040 /* 6, 12, 24 */
#define LOCAL_11A_OPERATION_RATE_BITMAP		0x120c0200 /* 9, 18, 36, 48, 54 */


#define PWR_ACTIVE				0
#define PWR_SAVE				1
#define PWR_TX_IDLE_CYCLE			6

/* bPreambleMode and bSlotTimeMode */
#define AUTO_MODE				0
#define LONG_MODE				1

/* Region definition */
#define REGION_AUTO				0xff
#define REGION_UNKNOWN				0
#define REGION_EUROPE				1	/* ETSI */
#define REGION_JAPAN				2	/* MKK */
#define REGION_USA				3	/* FCC */
#define	REGION_FRANCE				4	/* FRANCE */
#define REGION_SPAIN				5	/* SPAIN */
#define REGION_ISRAEL				6	/* ISRAEL */

#define MAX_BSS_DESCRIPT_ELEMENT		32
#define MAX_PMKID_CandidateList			16

/*
 * High byte : Event number,  low byte : reason
 * Event definition
 * -- SME/MLME event
 */
#define EVENT_RCV_DEAUTH			0x0100
#define EVENT_JOIN_FAIL				0x0200
#define EVENT_AUTH_FAIL				0x0300
#define EVENT_ASSOC_FAIL			0x0400
#define EVENT_LOST_SIGNAL			0x0500
#define EVENT_BSS_DESCRIPT_LACK			0x0600
#define EVENT_COUNTERMEASURE			0x0700
#define EVENT_JOIN_FILTER			0x0800
/* -- TX/RX event */
#define EVENT_RX_BUFF_UNAVAILABLE		0x4100

#define EVENT_CONNECT				0x8100
#define EVENT_DISCONNECT			0x8200
#define EVENT_SCAN_REQ				0x8300

/* Reason of Event */
#define EVENT_REASON_FILTER_BASIC_RATE		0x0001
#define EVENT_REASON_FILTER_PRIVACY		0x0002
#define EVENT_REASON_FILTER_AUTH_MODE		0x0003
#define EVENT_REASON_TIMEOUT			0x00ff

/* Due to[E id][Length][OUI][Data] may be 257 bytes */
#define MAX_IE_APPEND_SIZE			(256 + 4)

struct chan_info {
	u8	band;
	u8	ChanNo;
};

struct radio_off {
	u8	boHwRadioOff;
	u8	boSwRadioOff;
};

struct wb_local_para {
	/* read from EPROM, manufacture set for each NetCard */
	u8	PermanentAddress[MAC_ADDR_LENGTH + 2];
	/* the driver will use this one actually. */
	u8	ThisMacAddress[MAC_ADDR_LENGTH + 2];
	u32	MTUsize;	/* Ind to Uplayer, Max transmission unit size */
	u8	region_INF;	/* region setting from INF */
	u8	region;		/* real region setting of the device */
	u8	Reserved_1[2];

	/* power-save variables */
	u8	iPowerSaveMode; /* 0 indicates on, 1 indicates off */
	u8	ATIMmode;
	u8	ExcludeUnencrypted;
	/* Unit ime count for the decision to enter PS mode */
	u16	CheckCountForPS;
	u8	boHasTxActivity;/* tx activity has occurred */
	u8	boMacPsValid;	/* Power save mode obtained from H/W is valid or not */

	/* Rate */
	u8	TxRateMode; /*
			     * Initial, input from Registry,
			     * may be updated by GUI
			     * Tx Rate Mode: auto(DTO on), max, 1M, 2M, ..
			     */
	u8	CurrentTxRate;		/* The current Tx rate */
	u8	CurrentTxRateForMng;	/*
					 * The current Tx rate for management
					 * frames. It will be decided before
					 * connection succeeds.
					 */
	u8	CurrentTxFallbackRate;

	/* for Rate handler */
	u8	BRateSet[32];		/* basic rate set */
	u8	SRateSet[32];		/* support rate set */

	u8	NumOfBRate;
	u8	NumOfSRate;
	u8	NumOfDsssRateInSRate;	/* number of DSSS rates in supported rate set */
	u8	reserved1;

	u32	dwBasicRateBitmap;	/* bit map of basic rates */

	u32	dwSupportRateBitmap;	/* bit map of all support rates including basic and operational rates */


	/* For SME/MLME handler */

	u16	wOldSTAindex;		/* valid when boHandover=TRUE, store old connected STA index */
	u16	wConnectedSTAindex;	/* Index of peerly connected AP or IBSS in the descriptionset. */
	u16	Association_ID;		/* The Association ID in the (Re)Association Response frame. */
	u16	ListenInterval;		/* The listen interval when SME invoking MLME_ (Re)Associate_Request(). */

	struct	radio_off RadioOffStatus;
	u8	Reserved0[2];
	u8	boMsRadioOff;		/* Ndis demands to be true when set Disassoc. OID and be false when set SSID OID. */
	u8	bAntennaNo;		/* which antenna */
	u8	bConnectFlag;		/* the connect status flag for roaming task */

	u8	RoamStatus;
	u8	reserved7[3];

	struct	chan_info CurrentChan;	/* Current channel no. and channel band. It may be changed by scanning. */
	u8	boHandover;		/* Roaming, Hnadover to other AP. */
	u8	boCCAbusy;

	u16	CWMax;			/* It may not be the real value that H/W used */
	u8	CWMin;			/* 255: set according to 802.11 spec. */
	u8	reserved2;

	/* 11G: */
	u8	bMacOperationMode;	/* operation in 802.11b or 802.11g */
	u8	bSlotTimeMode;		/* AUTO, s32 */
	u8	bPreambleMode;		/* AUTO, s32 */
	u8	boNonERPpresent;

	u8	boProtectMechanism;	/* H/W will take the necessary action based on this variable */
	u8	boShortPreamble;	/* Same here */
	u8	boShortSlotTime;	/* Same here */
	u8	reserved_3;

	u32	RSN_IE_Bitmap;
	u32	RSN_OUI_Type;

	/* For the BSSID */
	u8	HwBssid[MAC_ADDR_LENGTH + 2];
	u32	HwBssidValid;

	/* For scan list */
	u8	BssListCount;		/* Total count of valid descriptor indexes */
	u8	boReceiveUncorrectInfo;	/* important settings in beacon/probe resp. have been changed */
	u8	NoOfJoinerInIbss;
	u8	reserved_4;

	/* Store the valid descriptor indexes obtained from scannings */
	u8	BssListIndex[(MAX_BSS_DESCRIPT_ELEMENT + 3) & ~0x03];
	/*
	 * Save the BssDescriptor index in this IBSS.
	 * The index 0 is local descriptor (psLOCAL->wConnectedSTAindex).
	 * If CONNECTED : NoOfJoinerInIbss >= 2
	 * else		: NoOfJoinerInIbss <= 1
	 */
	u8	JoinerInIbss[(MAX_BSS_DESCRIPT_ELEMENT + 3) & ~0x03];

	/* General Statistics, count at Rx_handler or Tx_callback interrupt handler */
	u64	GS_XMIT_OK;		/* Good Frames Transmitted */
	u64	GS_RCV_OK;		/* Good Frames Received */
	u32	GS_RCV_ERROR;		/* Frames received with crc error */
	u32	GS_XMIT_ERROR;		/* Bad Frames Transmitted */
	u32	GS_RCV_NO_BUFFER;	/* Receive Buffer underrun */
	u32	GS_XMIT_ONE_COLLISION;	/* one collision */
	u32	GS_XMIT_MORE_COLLISIONS;/* more collisions */

	/*
	 * ================================================================
	 * Statistics (no matter whether it had done successfully) -wkchen
	 * ================================================================
	 */
	u32	_NumRxMSDU;
	u32	_NumTxMSDU;
	u32	_dot11WEPExcludedCount;
	u32	_dot11WEPUndecryptableCount;
	u32	_dot11FrameDuplicateCount;

	struct	chan_info IbssChanSetting;	/* 2B. Start IBSS Channel setting by registry or WWU. */
	u8	reserved_5[2];		/* It may not be used after considering RF type, region and modulation type. */

	u8	reserved_6[2];		/* two variables are for wep key error detection */
	u32	bWepKeyError;
	u32	bToSelfPacketReceived;
	u32	WepKeyDetectTimerCount;

	u16	SignalLostTh;
	u16	SignalRoamTh;

	u8		IE_Append_data[MAX_IE_APPEND_SIZE];
	u16		IE_Append_size;
	u16		reserved_7;
};

#endif
