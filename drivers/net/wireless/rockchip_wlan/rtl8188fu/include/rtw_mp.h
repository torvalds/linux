/******************************************************************************
 *
 * Copyright(c) 2007 - 2011 Realtek Corporation. All rights reserved.
 *                                        
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of version 2 of the GNU General Public License as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110, USA
 *
 *
 ******************************************************************************/
#ifndef _RTW_MP_H_
#define _RTW_MP_H_

#define RTWPRIV_VER_INFO	1

#define MAX_MP_XMITBUF_SZ 	2048
#define NR_MP_XMITFRAME		8

struct mp_xmit_frame
{
	_list	list;

	struct pkt_attrib attrib;

	_pkt *pkt;

	int frame_tag;

	_adapter *padapter;

#ifdef CONFIG_USB_HCI

	//insert urb, irp, and irpcnt info below...
	//max frag_cnt = 8

	u8 *mem_addr;
	u32 sz[8];

#if defined(PLATFORM_OS_XP) || defined(PLATFORM_LINUX)
	PURB pxmit_urb[8];
#endif

#ifdef PLATFORM_OS_XP
	PIRP pxmit_irp[8];
#endif

	u8 bpending[8];
	sint ac_tag[8];
	sint last[8];
	uint irpcnt;
	uint fragcnt;
#endif /* CONFIG_USB_HCI */

	uint mem[(MAX_MP_XMITBUF_SZ >> 2)];
};

struct mp_wiparam
{
	u32 bcompleted;
	u32 act_type;
	u32 io_offset;
	u32 io_value;
};

typedef void(*wi_act_func)(void* padapter);

#ifdef PLATFORM_WINDOWS
struct mp_wi_cntx
{
	u8 bmpdrv_unload;

	// Work Item
	NDIS_WORK_ITEM mp_wi;
	NDIS_EVENT mp_wi_evt;
	_lock mp_wi_lock;
	u8 bmp_wi_progress;
	wi_act_func curractfunc;
	// Variable needed in each implementation of CurrActFunc.
	struct mp_wiparam param;
};
#endif

struct mp_tx
{
	u8 stop;
	u32 count, sended;
	u8 payload;
	struct pkt_attrib attrib;
	//struct tx_desc desc;
	//u8 resvdtx[7];
	u8 desc[TXDESC_SIZE];
	u8 *pallocated_buf;
	u8 *buf;
	u32 buf_size, write_size;
	_thread_hdl_ PktTxThread;
};

#define MP_MAX_LINES		1000
#define MP_MAX_LINES_BYTES	256
#define u1Byte u8
#define s1Byte s8
#define u4Byte u32
#define s4Byte s32
#define u1Byte		u8
#define pu1Byte 		u8* 

#define u2Byte		u16
#define pu2Byte 		u16*		

#define u4Byte		u32
#define pu4Byte 		u32*	

#define u8Byte		u64
#define pu8Byte 		u64*

#define s1Byte		s8
#define ps1Byte 		s8* 

#define s2Byte		s16
#define ps2Byte 		s16*	

#define s4Byte		s32
#define ps4Byte 		s32*	

#define s8Byte		s64
#define ps8Byte 		s64*

#define UCHAR u8
#define USHORT u16
#define UINT u32
#define ULONG u32
#define PULONG u32*

typedef struct _RT_PMAC_PKT_INFO {
	UCHAR			MCS;
	UCHAR			Nss;
	UCHAR			Nsts;
	UINT			N_sym;
	UCHAR			SIGA2B3;
} RT_PMAC_PKT_INFO, *PRT_PMAC_PKT_INFO;

typedef struct _RT_PMAC_TX_INFO {
	u8			bEnPMacTx:1;		/* 0: Disable PMac 1: Enable PMac */
	u8			Mode:3;				/* 0: Packet TX 3:Continuous TX */
	u8			Ntx:4;				/* 0-7 */
	u8			TX_RATE;			/* MPT_RATE_E */
	u8			TX_RATE_HEX;
	u8			TX_SC;
	u8			bSGI:1;
	u8			bSPreamble:1;
	u8			bSTBC:1;
	u8			bLDPC:1;
	u8			NDP_sound:1;
	u8			BandWidth:3;		/* 0: 20 1:40 2:80Mhz */
	u8			m_STBC;			/* bSTBC + 1 */
	USHORT			PacketPeriod;
	UINT		PacketCount;
	UINT		PacketLength;
	u8			PacketPattern;
	USHORT			SFD;
	u8			SignalField;
	u8			ServiceField;
	USHORT			LENGTH;
	u8			CRC16[2];
	u8			LSIG[3];
	u8			HT_SIG[6];
	u8			VHT_SIG_A[6];
	u8			VHT_SIG_B[4];
	u8			VHT_SIG_B_CRC;
	u8			VHT_Delimiter[4];
	u8			MacAddress[6];
} RT_PMAC_TX_INFO, *PRT_PMAC_TX_INFO;


typedef VOID (*MPT_WORK_ITEM_HANDLER)(IN PVOID Adapter);
typedef struct _MPT_CONTEXT
{
	// Indicate if we have started Mass Production Test.
	BOOLEAN			bMassProdTest;

	// Indicate if the driver is unloading or unloaded.
	BOOLEAN			bMptDrvUnload;

	_sema			MPh2c_Sema;
	_timer			MPh2c_timeout_timer;
// Event used to sync H2c for BT control

	BOOLEAN		MptH2cRspEvent;
	BOOLEAN		MptBtC2hEvent;
	BOOLEAN		bMPh2c_timeout;
	
	/* 8190 PCI does not support NDIS_WORK_ITEM. */
	// Work Item for Mass Production Test.
	//NDIS_WORK_ITEM	MptWorkItem;
//	RT_WORK_ITEM		MptWorkItem;
	// Event used to sync the case unloading driver and MptWorkItem is still in progress.
//	NDIS_EVENT		MptWorkItemEvent;
	// To protect the following variables.
//	NDIS_SPIN_LOCK		MptWorkItemSpinLock;
	// Indicate a MptWorkItem is scheduled and not yet finished.
	BOOLEAN			bMptWorkItemInProgress;
	// An instance which implements function and context of MptWorkItem.
	MPT_WORK_ITEM_HANDLER	CurrMptAct;

	// 1=Start, 0=Stop from UI.
	ULONG			MptTestStart;
	// _TEST_MODE, defined in MPT_Req2.h
	ULONG			MptTestItem;
	// Variable needed in each implementation of CurrMptAct.
	ULONG			MptActType; 	// Type of action performed in CurrMptAct.
	// The Offset of IO operation is depend of MptActType.
	ULONG			MptIoOffset;
	// The Value of IO operation is depend of MptActType.
	ULONG			MptIoValue;
	// The RfPath of IO operation is depend of MptActType.
	ULONG			MptRfPath;

	WIRELESS_MODE		MptWirelessModeToSw;	// Wireless mode to switch.
	u8			MptChannelToSw; 	// Channel to switch.
	u8			MptInitGainToSet; 	// Initial gain to set.
	//ULONG			bMptAntennaA; 		// TRUE if we want to use antenna A.
	ULONG			MptBandWidth;		// bandwidth to switch.
	ULONG			MptRateIndex;		// rate index.
	// Register value kept for Single Carrier Tx test.
	u8			btMpCckTxPower;
	// Register value kept for Single Carrier Tx test.
	u8			btMpOfdmTxPower;
	// For MP Tx Power index
	u8			TxPwrLevel[4];	/* rf-A, rf-B*/
	u32			RegTxPwrLimit;
	// Content of RCR Regsiter for Mass Production Test.
	ULONG			MptRCR;
	// TRUE if we only receive packets with specific pattern.
	BOOLEAN			bMptFilterPattern;
 	// Rx OK count, statistics used in Mass Production Test.
 	ULONG			MptRxOkCnt;
 	// Rx CRC32 error count, statistics used in Mass Production Test.
 	ULONG			MptRxCrcErrCnt;

	BOOLEAN			bCckContTx;	// TRUE if we are in CCK Continuous Tx test.
 	BOOLEAN			bOfdmContTx;	// TRUE if we are in OFDM Continuous Tx test.
	BOOLEAN			bStartContTx; 	// TRUE if we have start Continuous Tx test.
	// TRUE if we are in Single Carrier Tx test.
	BOOLEAN			bSingleCarrier;
	// TRUE if we are in Carrier Suppression Tx Test.
	BOOLEAN			bCarrierSuppression;
	//TRUE if we are in Single Tone Tx test.
	BOOLEAN			bSingleTone;

	// ACK counter asked by K.Y..
	BOOLEAN			bMptEnableAckCounter;
	ULONG			MptAckCounter;

	// SD3 Willis For 8192S to save 1T/2T RF table for ACUT	Only fro ACUT delete later ~~~!
	//s1Byte		BufOfLines[2][MAX_LINES_HWCONFIG_TXT][MAX_BYTES_LINE_HWCONFIG_TXT];
	//s1Byte			BufOfLines[2][MP_MAX_LINES][MP_MAX_LINES_BYTES];
	//s4Byte			RfReadLine[2];

	u8		APK_bound[2];	//for APK	path A/path B
	BOOLEAN		bMptIndexEven;

	u8		backup0xc50;
	u8		backup0xc58;
	u8		backup0xc30;
	u8 		backup0x52_RF_A;
	u8 		backup0x52_RF_B;
	
	u4Byte			backup0x58_RF_A;	
	u4Byte			backup0x58_RF_B;
	
	u1Byte			h2cReqNum;
	u1Byte			c2hBuf[32];

    u1Byte          btInBuf[100];
	ULONG			mptOutLen;
    u1Byte          mptOutBuf[100];
	RT_PMAC_TX_INFO	PMacTxInfo;
	RT_PMAC_PKT_INFO	PMacPktInfo;
	u8 HWTxmode;

	BOOLEAN			bldpc;
	BOOLEAN			bstbc;
}MPT_CONTEXT, *PMPT_CONTEXT;
//#endif

/* E-Fuse */
#ifdef CONFIG_RTL8188E
#define EFUSE_MAP_SIZE		512
#endif
#if defined(CONFIG_RTL8812A) || defined(CONFIG_RTL8821A) || defined(CONFIG_RTL8814A)
#define EFUSE_MAP_SIZE		512
#endif
#ifdef CONFIG_RTL8192E
#define EFUSE_MAP_SIZE		512
#endif
#ifdef CONFIG_RTL8723B
#define EFUSE_MAP_SIZE		512
#endif
#ifdef CONFIG_RTL8814A
#define EFUSE_MAP_SIZE		512
#endif
#ifdef CONFIG_RTL8703B
#define EFUSE_MAP_SIZE		512
#endif
#ifdef CONFIG_RTL8188F
#define EFUSE_MAP_SIZE		512
#endif

#if defined(CONFIG_RTL8814A)
#define EFUSE_MAX_SIZE		1024
#elif defined(CONFIG_RTL8188E) || defined(CONFIG_RTL8188F) || defined(CONFIG_RTL8703B)
#define EFUSE_MAX_SIZE		256
#else
#define EFUSE_MAX_SIZE		512
#endif
/* end of E-Fuse */

//#define RTPRIV_IOCTL_MP 					( SIOCIWFIRSTPRIV + 0x17)
enum {	  
	WRITE_REG = 1,
	READ_REG,
	WRITE_RF,
	READ_RF,
	MP_START,
	MP_STOP,
	MP_RATE,
	MP_CHANNEL,
	MP_BANDWIDTH,
	MP_TXPOWER,
	MP_ANT_TX,
	MP_ANT_RX,
	MP_CTX,
	MP_QUERY,
	MP_ARX,
	MP_PSD,
	MP_PWRTRK,
	MP_THER,
	MP_IOCTL,
	EFUSE_GET,
	EFUSE_SET,
	MP_RESET_STATS,
	MP_DUMP,
	MP_PHYPARA,
	MP_SetRFPathSwh,
	MP_QueryDrvStats,
	MP_SetBT,
	CTA_TEST,
	MP_DISABLE_BT_COEXIST,
	MP_PwrCtlDM,
	MP_GETVER,
	MP_MON,
	EFUSE_MASK,
	EFUSE_FILE,
	MP_TX,
	MP_RX,
	MP_HW_TX_MODE,
#ifdef CONFIG_WOWLAN
	MP_WOW_ENABLE,
	MP_WOW_SET_PATTERN,
#endif
#ifdef CONFIG_AP_WOWLAN
	MP_AP_WOW_ENABLE,
#endif
	MP_CUSTOMER_STR,
	MP_NULL,
	MP_GET_TXPOWER_INX,

	MP_SD_IREAD,
	MP_SD_IWRITE,
};

struct mp_priv
{
	_adapter *papdater;

	//Testing Flag
	u32 mode;//0 for normal type packet, 1 for loopback packet (16bytes TXCMD)

	u32 prev_fw_state;

	//OID cmd handler
	struct mp_wiparam workparam;
//	u8 act_in_progress;

	//Tx Section
	u8 TID;
	u32 tx_pktcount;
	u32 pktInterval;
	u32 pktLength;
	struct mp_tx tx;

	//Rx Section
	u32 rx_bssidpktcount;
	u32 rx_pktcount;
	u32 rx_pktcount_filter_out;
	u32 rx_crcerrpktcount;
	u32 rx_pktloss;
	BOOLEAN  rx_bindicatePkt;
	struct recv_stat rxstat;

	//RF/BB relative
	u8 channel;
	u8 bandwidth;
	u8 prime_channel_offset;
	u8 txpoweridx;
	u8 rateidx;
	u32 preamble;
//	u8 modem;
	u32 CrystalCap;
//	u32 curr_crystalcap;

	u16 antenna_tx;
	u16 antenna_rx;
//	u8 curr_rfpath;
	
	u8 check_mp_pkt;

	u8 bSetTxPower;
//	uint ForcedDataRate;
	u8 mp_dm;
	u8 mac_filter[ETH_ALEN];
	u8 bmac_filter;
	
	struct wlan_network mp_network;
	NDIS_802_11_MAC_ADDRESS network_macaddr;

#ifdef PLATFORM_WINDOWS
	u32 rx_testcnt;
	u32 rx_testcnt1;
	u32 rx_testcnt2;
	u32 tx_testcnt;
	u32 tx_testcnt1;

	struct mp_wi_cntx wi_cntx;

	u8 h2c_result;
	u8 h2c_seqnum;
	u16 h2c_cmdcode;
	u8 h2c_resp_parambuf[512];
	_lock h2c_lock;
	_lock wkitm_lock;
	u32 h2c_cmdcnt;
	NDIS_EVENT h2c_cmd_evt;
	NDIS_EVENT c2h_set;
	NDIS_EVENT h2c_clr;
	NDIS_EVENT cpwm_int;

	NDIS_EVENT scsir_full_evt;
	NDIS_EVENT scsiw_empty_evt;
#endif

	u8 *pallocated_mp_xmitframe_buf;
	u8 *pmp_xmtframe_buf;
	_queue free_mp_xmitqueue;
	u32 free_mp_xmitframe_cnt;
	BOOLEAN bSetRxBssid;
	BOOLEAN bTxBufCkFail;
	BOOLEAN bRTWSmbCfg;
	BOOLEAN bloopback;
	MPT_CONTEXT MptCtx;

	u8		*TXradomBuffer;
};

typedef struct _IOCMD_STRUCT_ {
	u8	cmdclass;
	u16	value;
	u8	index;
}IOCMD_STRUCT;

struct rf_reg_param {
	u32 path;
	u32 offset;
	u32 value;
};

struct bb_reg_param {
	u32 offset;
	u32 value;
};

typedef struct _MP_FIRMWARE {
	FIRMWARE_SOURCE eFWSource;
#ifdef CONFIG_EMBEDDED_FWIMG
	u8* 		szFwBuffer;
#else
	u8			szFwBuffer[0x8000];
#endif
	u32 		ulFwLength;
} RT_MP_FIRMWARE, *PRT_MP_FIRMWARE;




//=======================================================================

#define LOWER 	_TRUE
#define RAISE	_FALSE

/* Hardware Registers */
#if 0
#if 0
#define IOCMD_CTRL_REG			0x102502C0
#define IOCMD_DATA_REG			0x102502C4
#else
#define IOCMD_CTRL_REG			0x10250370
#define IOCMD_DATA_REG			0x10250374
#endif

#define IOCMD_GET_THERMAL_METER		0xFD000028

#define IOCMD_CLASS_BB_RF		0xF0
#define IOCMD_BB_READ_IDX		0x00
#define IOCMD_BB_WRITE_IDX		0x01
#define IOCMD_RF_READ_IDX		0x02
#define IOCMD_RF_WRIT_IDX		0x03
#endif
#define BB_REG_BASE_ADDR		0x800

/* MP variables */
#if 0
#define _2MAC_MODE_	0
#define _LOOPBOOK_MODE_	1
#endif
typedef enum _MP_MODE_ {
	MP_OFF,
	MP_ON,
	MP_ERR,
	MP_CONTINUOUS_TX,
	MP_SINGLE_CARRIER_TX,
	MP_CARRIER_SUPPRISSION_TX,
	MP_SINGLE_TONE_TX,
	MP_PACKET_TX,
	MP_PACKET_RX
} MP_MODE;

typedef enum _TEST_MODE {
	TEST_NONE                 ,
	PACKETS_TX                ,
	PACKETS_RX                ,
	CONTINUOUS_TX             ,
	OFDM_Single_Tone_TX       ,
	CCK_Carrier_Suppression_TX
} TEST_MODE;


typedef enum _MPT_BANDWIDTH {
	MPT_BW_20MHZ = 0,
	MPT_BW_40MHZ_DUPLICATE = 1,
	MPT_BW_40MHZ_ABOVE = 2,
	MPT_BW_40MHZ_BELOW = 3,
	MPT_BW_40MHZ = 4,
	MPT_BW_80MHZ = 5,
	MPT_BW_80MHZ_20_ABOVE = 6,
	MPT_BW_80MHZ_20_BELOW = 7,
	MPT_BW_80MHZ_20_BOTTOM = 8,
	MPT_BW_80MHZ_20_TOP = 9,
	MPT_BW_80MHZ_40_ABOVE = 10,
	MPT_BW_80MHZ_40_BELOW = 11,
} MPT_BANDWIDTHE, *PMPT_BANDWIDTH;

#define MAX_RF_PATH_NUMS	RF_PATH_MAX


extern u8 mpdatarate[NumRates];

/* MP set force data rate base on the definition. */
typedef enum _MPT_RATE_INDEX
{
	/* CCK rate. */
	MPT_RATE_1M = 1 ,	/* 0 */
	MPT_RATE_2M,
	MPT_RATE_55M,
	MPT_RATE_11M,	/* 3 */

	/* OFDM rate. */
	MPT_RATE_6M,	/* 4 */
	MPT_RATE_9M,
	MPT_RATE_12M,
	MPT_RATE_18M,
	MPT_RATE_24M,
	MPT_RATE_36M,
	MPT_RATE_48M,
	MPT_RATE_54M,	/* 11 */

	/* HT rate. */
	MPT_RATE_MCS0,	/* 12 */
	MPT_RATE_MCS1,
	MPT_RATE_MCS2,
	MPT_RATE_MCS3,
	MPT_RATE_MCS4,
	MPT_RATE_MCS5,
	MPT_RATE_MCS6,
	MPT_RATE_MCS7,	/* 19 */
	MPT_RATE_MCS8,
	MPT_RATE_MCS9,
	MPT_RATE_MCS10,
	MPT_RATE_MCS11,
	MPT_RATE_MCS12,
	MPT_RATE_MCS13,
	MPT_RATE_MCS14,
	MPT_RATE_MCS15,	/* 27 */
	MPT_RATE_MCS16,
	MPT_RATE_MCS17, // #29
	MPT_RATE_MCS18,
	MPT_RATE_MCS19,
	MPT_RATE_MCS20,
	MPT_RATE_MCS21,
	MPT_RATE_MCS22, // #34
	MPT_RATE_MCS23,
	MPT_RATE_MCS24,
	MPT_RATE_MCS25,
	MPT_RATE_MCS26,
	MPT_RATE_MCS27, // #39
	MPT_RATE_MCS28, // #40
	MPT_RATE_MCS29, // #41
	MPT_RATE_MCS30, // #42
	MPT_RATE_MCS31, // #43
	/* VHT rate. Total: 20*/
	MPT_RATE_VHT1SS_MCS0 = 100,/*  #44*/
	MPT_RATE_VHT1SS_MCS1, // #
	MPT_RATE_VHT1SS_MCS2,
	MPT_RATE_VHT1SS_MCS3,
	MPT_RATE_VHT1SS_MCS4,
	MPT_RATE_VHT1SS_MCS5,
	MPT_RATE_VHT1SS_MCS6, // #
	MPT_RATE_VHT1SS_MCS7,
	MPT_RATE_VHT1SS_MCS8,
	MPT_RATE_VHT1SS_MCS9, //#53
	MPT_RATE_VHT2SS_MCS0, //#54
	MPT_RATE_VHT2SS_MCS1, 
	MPT_RATE_VHT2SS_MCS2,
	MPT_RATE_VHT2SS_MCS3,
	MPT_RATE_VHT2SS_MCS4,
	MPT_RATE_VHT2SS_MCS5,
	MPT_RATE_VHT2SS_MCS6,
	MPT_RATE_VHT2SS_MCS7,
	MPT_RATE_VHT2SS_MCS8,
	MPT_RATE_VHT2SS_MCS9, //#63
	MPT_RATE_VHT3SS_MCS0,
	MPT_RATE_VHT3SS_MCS1, 
	MPT_RATE_VHT3SS_MCS2,
	MPT_RATE_VHT3SS_MCS3,
	MPT_RATE_VHT3SS_MCS4,
	MPT_RATE_VHT3SS_MCS5,
	MPT_RATE_VHT3SS_MCS6, // #126
	MPT_RATE_VHT3SS_MCS7,
	MPT_RATE_VHT3SS_MCS8,
	MPT_RATE_VHT3SS_MCS9,
	MPT_RATE_VHT4SS_MCS0,
	MPT_RATE_VHT4SS_MCS1, // #131
	MPT_RATE_VHT4SS_MCS2,
	MPT_RATE_VHT4SS_MCS3,
	MPT_RATE_VHT4SS_MCS4,
	MPT_RATE_VHT4SS_MCS5,
	MPT_RATE_VHT4SS_MCS6, // #136
	MPT_RATE_VHT4SS_MCS7,
	MPT_RATE_VHT4SS_MCS8,
	MPT_RATE_VHT4SS_MCS9,
	MPT_RATE_LAST
}MPT_RATE_E, *PMPT_RATE_E;

#define MAX_TX_PWR_INDEX_N_MODE 64	// 0x3F

#define MPT_IS_CCK_RATE(_value)		(MPT_RATE_1M <= _value && _value <= MPT_RATE_11M)
#define MPT_IS_OFDM_RATE(_value)	(MPT_RATE_6M <= _value && _value <= MPT_RATE_54M)
#define MPT_IS_HT_RATE(_value)		(MPT_RATE_MCS0 <= _value && _value <= MPT_RATE_MCS31)
#define MPT_IS_HT_1S_RATE(_value)	(MPT_RATE_MCS0 <= _value && _value <= MPT_RATE_MCS7)
#define MPT_IS_HT_2S_RATE(_value)	(MPT_RATE_MCS8 <= _value && _value <= MPT_RATE_MCS15)
#define MPT_IS_HT_3S_RATE(_value)	(MPT_RATE_MCS16 <= _value && _value <= MPT_RATE_MCS23)
#define MPT_IS_HT_4S_RATE(_value)	(MPT_RATE_MCS24 <= _value && _value <= MPT_RATE_MCS31)

#define MPT_IS_VHT_RATE(_value)		(MPT_RATE_VHT1SS_MCS0 <= _value && _value <= MPT_RATE_VHT4SS_MCS9)
#define MPT_IS_VHT_1S_RATE(_value)	(MPT_RATE_VHT1SS_MCS0 <= _value && _value <= MPT_RATE_VHT1SS_MCS9)
#define MPT_IS_VHT_2S_RATE(_value)	(MPT_RATE_VHT2SS_MCS0 <= _value && _value <= MPT_RATE_VHT2SS_MCS9)
#define MPT_IS_VHT_3S_RATE(_value)	(MPT_RATE_VHT3SS_MCS0 <= _value && _value <= MPT_RATE_VHT3SS_MCS9)
#define MPT_IS_VHT_4S_RATE(_value)	(MPT_RATE_VHT4SS_MCS0 <= _value && _value <= MPT_RATE_VHT4SS_MCS9)

#define MPT_IS_2SS_RATE(_rate) ((MPT_RATE_MCS8 <= _rate && _rate <= MPT_RATE_MCS15) ||\
							(MPT_RATE_VHT2SS_MCS0 <= _rate && _rate <= MPT_RATE_VHT2SS_MCS9))
#define MPT_IS_3SS_RATE(_rate) ((MPT_RATE_MCS16 <= _rate && _rate <= MPT_RATE_MCS23) ||\
							(MPT_RATE_VHT3SS_MCS0 <= _rate && _rate <= MPT_RATE_VHT3SS_MCS9))
#define MPT_IS_4SS_RATE(_rate) ((MPT_RATE_MCS24 <= _rate && _rate <= MPT_RATE_MCS31) ||\
							(MPT_RATE_VHT4SS_MCS0 <= _rate && _rate <= MPT_RATE_VHT4SS_MCS9))

typedef enum _POWER_MODE_ {
	POWER_LOW = 0,
	POWER_NORMAL
}POWER_MODE;

// The following enumeration is used to define the value of Reg0xD00[30:28] or JaguarReg0x914[18:16].
typedef enum _OFDM_TX_MODE {
	OFDM_ALL_OFF		= 0,	
	OFDM_ContinuousTx	= 1,
	OFDM_SingleCarrier	= 2,
	OFDM_SingleTone 	= 4,
} OFDM_TX_MODE;


#define RX_PKT_BROADCAST	1
#define RX_PKT_DEST_ADDR	2
#define RX_PKT_PHY_MATCH	3

#define Mac_OFDM_OK 			0x00000000
#define Mac_OFDM_Fail			0x10000000
#define Mac_OFDM_FasleAlarm 	0x20000000
#define Mac_CCK_OK				0x30000000
#define Mac_CCK_Fail			0x40000000
#define Mac_CCK_FasleAlarm		0x50000000
#define Mac_HT_OK				0x60000000
#define Mac_HT_Fail 			0x70000000
#define Mac_HT_FasleAlarm		0x90000000
#define Mac_DropPacket			0xA0000000

typedef enum _ENCRY_CTRL_STATE_ {
	HW_CONTROL,		//hw encryption& decryption
	SW_CONTROL,		//sw encryption& decryption
	HW_ENCRY_SW_DECRY,	//hw encryption & sw decryption
	SW_ENCRY_HW_DECRY	//sw encryption & hw decryption
}ENCRY_CTRL_STATE;

typedef enum	_MPT_TXPWR_DEF{
	MPT_CCK,
	MPT_OFDM, // L and HT OFDM
	MPT_OFDM_AND_HT,
	MPT_HT,
	MPT_VHT
}MPT_TXPWR_DEF;

#ifdef CONFIG_RF_POWER_TRIM

#if defined(CONFIG_RTL8723B)
	#define 	REG_RF_BB_GAIN_OFFSET	0x7f
	#define 	RF_GAIN_OFFSET_MASK 	0xfffff
#elif defined(CONFIG_RTL8188E)
	#define 	REG_RF_BB_GAIN_OFFSET	0x55
	#define 	RF_GAIN_OFFSET_MASK 	0xfffff
#else
	#define 	REG_RF_BB_GAIN_OFFSET	0x55
	#define 	RF_GAIN_OFFSET_MASK 	0xfffff
#endif	//CONFIG_RTL8723B

#endif /*CONFIG_RF_POWER_TRIM*/

#define IS_MPT_HT_RATE(_rate)			(_rate >= MPT_RATE_MCS0 && _rate <= MPT_RATE_MCS31)
#define IS_MPT_VHT_RATE(_rate)			(_rate >= MPT_RATE_VHT1SS_MCS0 && _rate <= MPT_RATE_VHT4SS_MCS9)
#define IS_MPT_CCK_RATE(_rate)			(_rate >= MPT_RATE_1M && _rate <= MPT_RATE_11M)
#define IS_MPT_OFDM_RATE(_rate)			(_rate >= MPT_RATE_6M && _rate <= MPT_RATE_54M)
//=======================================================================
//extern struct mp_xmit_frame *alloc_mp_xmitframe(struct mp_priv *pmp_priv);
//extern int free_mp_xmitframe(struct xmit_priv *pxmitpriv, struct mp_xmit_frame *pmp_xmitframe);

extern s32 init_mp_priv(PADAPTER padapter);
extern void free_mp_priv(struct mp_priv *pmp_priv);
extern s32 MPT_InitializeAdapter(PADAPTER padapter, u8 Channel);
extern void MPT_DeInitAdapter(PADAPTER padapter);
extern s32 mp_start_test(PADAPTER padapter);
extern void mp_stop_test(PADAPTER padapter);

extern u32 _read_rfreg(PADAPTER padapter, u8 rfpath, u32 addr, u32 bitmask);
extern void _write_rfreg(PADAPTER padapter, u8 rfpath, u32 addr, u32 bitmask, u32 val);

extern u32 read_macreg(_adapter *padapter, u32 addr, u32 sz);
extern void write_macreg(_adapter *padapter, u32 addr, u32 val, u32 sz);
extern u32 read_bbreg(_adapter *padapter, u32 addr, u32 bitmask);
extern void write_bbreg(_adapter *padapter, u32 addr, u32 bitmask, u32 val);
extern u32 read_rfreg(PADAPTER padapter, u8 rfpath, u32 addr);
extern void write_rfreg(PADAPTER padapter, u8 rfpath, u32 addr, u32 val);

void	SetChannel(PADAPTER pAdapter);
void	SetBandwidth(PADAPTER pAdapter);
int	SetTxPower(PADAPTER pAdapter);
void	SetAntenna(PADAPTER pAdapter);
void	SetDataRate(PADAPTER pAdapter);
void	SetAntenna(PADAPTER pAdapter);
s32	SetThermalMeter(PADAPTER pAdapter, u8 target_ther);
void	GetThermalMeter(PADAPTER pAdapter, u8 *value);
void	SetContinuousTx(PADAPTER pAdapter, u8 bStart);
void	SetSingleCarrierTx(PADAPTER pAdapter, u8 bStart);
void	SetSingleToneTx(PADAPTER pAdapter, u8 bStart);
void	SetCarrierSuppressionTx(PADAPTER pAdapter, u8 bStart);
void	PhySetTxPowerLevel(PADAPTER pAdapter);
void	fill_txdesc_for_mp(PADAPTER padapter, u8 *ptxdesc);
void	SetPacketTx(PADAPTER padapter);
void	SetPacketRx(PADAPTER pAdapter, u8 bStartRx, u8 bAB);
void	ResetPhyRxPktCount(PADAPTER pAdapter);
u32	GetPhyRxPktReceived(PADAPTER pAdapter);
u32	GetPhyRxPktCRC32Error(PADAPTER pAdapter);
s32	SetPowerTracking(PADAPTER padapter, u8 enable);
void	GetPowerTracking(PADAPTER padapter, u8 *enable);
u32	mp_query_psd(PADAPTER pAdapter, u8 *data);



void hal_mpt_SwitchRfSetting(PADAPTER pAdapter);
s32 hal_mpt_SetPowerTracking(PADAPTER padapter, u8 enable);
void hal_mpt_GetPowerTracking(PADAPTER padapter, u8 *enable);
void hal_mpt_CCKTxPowerAdjust(PADAPTER Adapter, BOOLEAN bInCH14);
void hal_mpt_SetChannel(PADAPTER pAdapter);
void hal_mpt_SetBandwidth(PADAPTER pAdapter);
void hal_mpt_SetTxPower(PADAPTER pAdapter);
void hal_mpt_SetDataRate(PADAPTER pAdapter);
void hal_mpt_SetAntenna(PADAPTER pAdapter);
s32 hal_mpt_SetThermalMeter(PADAPTER pAdapter, u8 target_ther);
void hal_mpt_TriggerRFThermalMeter(PADAPTER pAdapter);
u8 hal_mpt_ReadRFThermalMeter(PADAPTER pAdapter);
void hal_mpt_GetThermalMeter(PADAPTER pAdapter, u8 *value);
void hal_mpt_CCKTxPowerAdjustbyIndex(PADAPTER pAdapter, BOOLEAN beven);
void hal_mpt_SetContinuousTx(PADAPTER pAdapter, u8 bStart);
void hal_mpt_SetSingleCarrierTx(PADAPTER pAdapter, u8 bStart);
void hal_mpt_SetSingleToneTx(PADAPTER pAdapter, u8 bStart);
void hal_mpt_SetCarrierSuppressionTx(PADAPTER pAdapter, u8 bStart);
void hal_mpt_SetCCKContinuousTx(PADAPTER pAdapter, u8 bStart);
void hal_mpt_SetOFDMContinuousTx(PADAPTER pAdapter, u8 bStart);
void mpt_ProSetPMacTx(PADAPTER	Adapter);

void MP_PHY_SetRFPathSwitch(PADAPTER pAdapter , BOOLEAN bMain);
ULONG mpt_ProQueryCalTxPower(PADAPTER	pAdapter, u8 RfPath);
void MPT_PwrCtlDM(PADAPTER padapter, u32 bstart);
u8 MptToMgntRate(u32	MptRateIdx);
u8 rtw_mpRateParseFunc(PADAPTER pAdapter, u8 *targetStr);
u32 mp_join(PADAPTER padapter, u8 mode);
u32 hal_mpt_query_phytxok(PADAPTER	pAdapter);

void
PMAC_Get_Pkt_Param(
	PRT_PMAC_TX_INFO	pPMacTxInfo,
	PRT_PMAC_PKT_INFO	pPMacPktInfo
	);
void
CCK_generator(
	PRT_PMAC_TX_INFO	pPMacTxInfo,
	PRT_PMAC_PKT_INFO	pPMacPktInfo
	);
void
PMAC_Nsym_generator(
	PRT_PMAC_TX_INFO	pPMacTxInfo,
	PRT_PMAC_PKT_INFO	pPMacPktInfo
	);
void
L_SIG_generator(
	UINT	N_SYM,		/* Max: 750*/
	PRT_PMAC_TX_INFO	pPMacTxInfo,
	PRT_PMAC_PKT_INFO	pPMacPktInfo
	);

void HT_SIG_generator(
	PRT_PMAC_TX_INFO	pPMacTxInfo,
	PRT_PMAC_PKT_INFO	pPMacPktInfo);

void VHT_SIG_A_generator(
	PRT_PMAC_TX_INFO	pPMacTxInfo,
	PRT_PMAC_PKT_INFO	pPMacPktInfo);

void VHT_SIG_B_generator(
	PRT_PMAC_TX_INFO	pPMacTxInfo);

void VHT_Delimiter_generator(
	PRT_PMAC_TX_INFO	pPMacTxInfo);


int rtw_mp_write_reg(struct net_device *dev,
			struct iw_request_info *info,
			struct iw_point *wrqu, char *extra);
int rtw_mp_read_reg(struct net_device *dev,
			struct iw_request_info *info,
			struct iw_point *wrqu, char *extra);
int rtw_mp_write_rf(struct net_device *dev,
			struct iw_request_info *info,
			struct iw_point *wrqu, char *extra);
int rtw_mp_read_rf(struct net_device *dev,
			struct iw_request_info *info,
			struct iw_point *wrqu, char *extra);
int rtw_mp_start(struct net_device *dev,
			struct iw_request_info *info,
			struct iw_point *wrqu, char *extra);
int rtw_mp_stop(struct net_device *dev,
			struct iw_request_info *info,
			struct iw_point *wrqu, char *extra);
int rtw_mp_rate(struct net_device *dev,
			struct iw_request_info *info,
			struct iw_point *wrqu, char *extra);
int rtw_mp_channel(struct net_device *dev,
			struct iw_request_info *info,
			struct iw_point *wrqu, char *extra);
int rtw_mp_bandwidth(struct net_device *dev,
			struct iw_request_info *info,
			struct iw_point *wrqu, char *extra);
int rtw_mp_txpower_index(struct net_device *dev,
			struct iw_request_info *info,
			struct iw_point *wrqu, char *extra);
int rtw_mp_txpower(struct net_device *dev,
			struct iw_request_info *info,
			struct iw_point *wrqu, char *extra);
int rtw_mp_txpower(struct net_device *dev,
			struct iw_request_info *info,
			struct iw_point *wrqu, char *extra);
int rtw_mp_ant_tx(struct net_device *dev,
			struct iw_request_info *info,
			struct iw_point *wrqu, char *extra);
int rtw_mp_ant_rx(struct net_device *dev,
			struct iw_request_info *info,
			struct iw_point *wrqu, char *extra);
int rtw_set_ctx_destAddr(struct net_device *dev,
			struct iw_request_info *info,
			struct iw_point *wrqu, char *extra);
int rtw_mp_ctx(struct net_device *dev,
			struct iw_request_info *info,
			struct iw_point *wrqu, char *extra);
int rtw_mp_disable_bt_coexist(struct net_device *dev,
			struct iw_request_info *info,
			union iwreq_data *wrqu, char *extra);
int rtw_mp_disable_bt_coexist(struct net_device *dev,
			struct iw_request_info *info,
			union iwreq_data *wrqu, char *extra);
int rtw_mp_arx(struct net_device *dev,
			struct iw_request_info *info,
			struct iw_point *wrqu, char *extra);
int rtw_mp_trx_query(struct net_device *dev,
			struct iw_request_info *info,
			struct iw_point *wrqu, char *extra);
int rtw_mp_pwrtrk(struct net_device *dev,
			struct iw_request_info *info,
			struct iw_point *wrqu, char *extra);
int rtw_mp_psd(struct net_device *dev,
			struct iw_request_info *info,
			struct iw_point *wrqu, char *extra);
int rtw_mp_thermal(struct net_device *dev,
			struct iw_request_info *info,
			struct iw_point *wrqu, char *extra);
int rtw_mp_reset_stats(struct net_device *dev,
			struct iw_request_info *info,
			struct iw_point *wrqu, char *extra);
int rtw_mp_dump(struct net_device *dev,
			struct iw_request_info *info,
			struct iw_point *wrqu, char *extra);
int rtw_mp_phypara(struct net_device *dev,
			struct iw_request_info *info,
			struct iw_point *wrqu, char *extra);
int rtw_mp_SetRFPath(struct net_device *dev,
			struct iw_request_info *info,
			struct iw_point *wrqu, char *extra);
int rtw_mp_QueryDrv(struct net_device *dev,
			struct iw_request_info *info,
			union iwreq_data *wrqu, char *extra);
int rtw_mp_PwrCtlDM(struct net_device *dev,
			struct iw_request_info *info,
			struct iw_point *wrqu, char *extra);
int rtw_mp_getver(struct net_device *dev,
			struct iw_request_info *info,
			union iwreq_data *wrqu, char *extra);
int rtw_mp_mon(struct net_device *dev,
			struct iw_request_info *info,
			union iwreq_data *wrqu, char *extra);
int rtw_efuse_mask_file(struct net_device *dev,
			struct iw_request_info *info,
			union iwreq_data *wrqu, char *extra);
int rtw_efuse_file_map(struct net_device *dev,
			struct iw_request_info *info,
			union iwreq_data *wrqu, char *extra);
int rtw_mp_SetBT(struct net_device *dev,
			struct iw_request_info *info,
			union iwreq_data *wrqu, char *extra);
int rtw_mp_pretx_proc(PADAPTER padapter, u8 bStartTest, char *extra);
int rtw_mp_tx(struct net_device *dev,
			struct iw_request_info *info,
			union iwreq_data *wrqu, char *extra);
int rtw_mp_rx(struct net_device *dev,
			struct iw_request_info *info,
			union iwreq_data *wrqu, char *extra);
int rtw_mp_hwtx(struct net_device *dev,
			struct iw_request_info *info,
			union iwreq_data *wrqu, char *extra);
u8 HwRateToMPTRate(u8 rate);

#endif //_RTW_MP_H_

