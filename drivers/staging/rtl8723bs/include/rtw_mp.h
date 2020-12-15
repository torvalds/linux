/* SPDX-License-Identifier: GPL-2.0 */
/******************************************************************************
 *
 * Copyright(c) 2007 - 2011 Realtek Corporation. All rights reserved.
 *
 ******************************************************************************/
#ifndef _RTW_MP_H_
#define _RTW_MP_H_

#define MAX_MP_XMITBUF_SZ	2048
#define NR_MP_XMITFRAME		8

struct mp_xmit_frame {
	struct list_head	list;

	struct pkt_attrib attrib;

	_pkt *pkt;

	int frame_tag;

	struct adapter *padapter;

	uint mem[(MAX_MP_XMITBUF_SZ >> 2)];
};

struct mp_wiparam {
	u32 bcompleted;
	u32 act_type;
	u32 io_offset;
	u32 io_value;
};

typedef void(*wi_act_func)(void* padapter);

struct mp_tx {
	u8 stop;
	u32 count, sended;
	u8 payload;
	struct pkt_attrib attrib;
	/* struct tx_desc desc; */
	/* u8 resvdtx[7]; */
	u8 desc[TXDESC_SIZE];
	u8 *pallocated_buf;
	u8 *buf;
	u32 buf_size, write_size;
	void *PktTxThread;
};

#define MP_MAX_LINES		1000
#define MP_MAX_LINES_BYTES	256

typedef void (*MPT_WORK_ITEM_HANDLER)(void *Adapter);
typedef struct _MPT_CONTEXT {
	/*  Indicate if we have started Mass Production Test. */
	bool			bMassProdTest;

	/*  Indicate if the driver is unloading or unloaded. */
	bool			bMptDrvUnload;

	_timer			MPh2c_timeout_timer;
/*  Event used to sync H2c for BT control */

	bool		MptH2cRspEvent;
	bool		MptBtC2hEvent;
	bool		bMPh2c_timeout;

	/* 8190 PCI does not support NDIS_WORK_ITEM. */
	/*  Work Item for Mass Production Test. */
	/* NDIS_WORK_ITEM	MptWorkItem; */
/* 	RT_WORK_ITEM		MptWorkItem; */
	/*  Event used to sync the case unloading driver and MptWorkItem is still in progress. */
/* 	NDIS_EVENT		MptWorkItemEvent; */
	/*  To protect the following variables. */
/* 	NDIS_SPIN_LOCK		MptWorkItemSpinLock; */
	/*  Indicate a MptWorkItem is scheduled and not yet finished. */
	bool			bMptWorkItemInProgress;
	/*  An instance which implements function and context of MptWorkItem. */
	MPT_WORK_ITEM_HANDLER	CurrMptAct;

	/*  1 =Start, 0 =Stop from UI. */
	u32 		MptTestStart;
	/*  _TEST_MODE, defined in MPT_Req2.h */
	u32 		MptTestItem;
	/*  Variable needed in each implementation of CurrMptAct. */
	u32 		MptActType;	/*  Type of action performed in CurrMptAct. */
	/*  The Offset of IO operation is depend of MptActType. */
	u32 		MptIoOffset;
	/*  The Value of IO operation is depend of MptActType. */
	u32 		MptIoValue;
	/*  The RfPath of IO operation is depend of MptActType. */
	u32 		MptRfPath;

	enum WIRELESS_MODE		MptWirelessModeToSw;	/*  Wireless mode to switch. */
	u8 	MptChannelToSw;		/*  Channel to switch. */
	u8 	MptInitGainToSet;	/*  Initial gain to set. */
	u32 		MptBandWidth;		/*  bandwidth to switch. */
	u32 		MptRateIndex;		/*  rate index. */
	/*  Register value kept for Single Carrier Tx test. */
	u8 	btMpCckTxPower;
	/*  Register value kept for Single Carrier Tx test. */
	u8 	btMpOfdmTxPower;
	/*  For MP Tx Power index */
	u8 	TxPwrLevel[2];	/*  rf-A, rf-B */
	u32 		RegTxPwrLimit;
	/*  Content of RCR Regsiter for Mass Production Test. */
	u32 		MptRCR;
	/*  true if we only receive packets with specific pattern. */
	bool			bMptFilterPattern;
	/*  Rx OK count, statistics used in Mass Production Test. */
	u32 		MptRxOkCnt;
	/*  Rx CRC32 error count, statistics used in Mass Production Test. */
	u32 		MptRxCrcErrCnt;

	bool			bCckContTx;	/*  true if we are in CCK Continuous Tx test. */
	bool			bOfdmContTx;	/*  true if we are in OFDM Continuous Tx test. */
	bool			bStartContTx;	/*  true if we have start Continuous Tx test. */
	/*  true if we are in Single Carrier Tx test. */
	bool			bSingleCarrier;
	/*  true if we are in Carrier Suppression Tx Test. */
	bool			bCarrierSuppression;
	/* true if we are in Single Tone Tx test. */
	bool			bSingleTone;

	/*  ACK counter asked by K.Y.. */
	bool			bMptEnableAckCounter;
	u32 		MptAckCounter;

	/*  SD3 Willis For 8192S to save 1T/2T RF table for ACUT	Only fro ACUT delete later ~~~! */
	/* s8		BufOfLines[2][MAX_LINES_HWCONFIG_TXT][MAX_BYTES_LINE_HWCONFIG_TXT]; */
	/* s8			BufOfLines[2][MP_MAX_LINES][MP_MAX_LINES_BYTES]; */
	/* s32			RfReadLine[2]; */

	u8 APK_bound[2];	/* for APK	path A/path B */
	bool		bMptIndexEven;

	u8 backup0xc50;
	u8 backup0xc58;
	u8 backup0xc30;
	u8 backup0x52_RF_A;
	u8 backup0x52_RF_B;

	u32 		backup0x58_RF_A;
	u32 		backup0x58_RF_B;

	u8 	h2cReqNum;
	u8 	c2hBuf[32];

    u8          btInBuf[100];
	u32 		mptOutLen;
    u8          mptOutBuf[100];

} MPT_CONTEXT, *PMPT_CONTEXT;
/* endif */

/* E-Fuse */
#define EFUSE_MAP_SIZE		512

#define EFUSE_MAX_SIZE		512
/* end of E-Fuse */

/* define RTPRIV_IOCTL_MP					(SIOCIWFIRSTPRIV + 0x17) */
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
#ifdef CONFIG_WOWLAN
	MP_WOW_ENABLE,
#endif
#ifdef CONFIG_AP_WOWLAN
	MP_AP_WOW_ENABLE,
#endif
	MP_NULL,
	MP_GET_TXPOWER_INX,
};

struct mp_priv {
	struct adapter *papdater;

	/* Testing Flag */
	u32 mode;/* 0 for normal type packet, 1 for loopback packet (16bytes TXCMD) */

	u32 prev_fw_state;

	/* OID cmd handler */
	struct mp_wiparam workparam;
/* 	u8 act_in_progress; */

	/* Tx Section */
	u8 TID;
	u32 tx_pktcount;
	u32 pktInterval;
	struct mp_tx tx;

	/* Rx Section */
	u32 rx_bssidpktcount;
	u32 rx_pktcount;
	u32 rx_pktcount_filter_out;
	u32 rx_crcerrpktcount;
	u32 rx_pktloss;
	bool  rx_bindicatePkt;
	struct recv_stat rxstat;

	/* RF/BB relative */
	u8 channel;
	u8 bandwidth;
	u8 prime_channel_offset;
	u8 txpoweridx;
	u8 txpoweridx_b;
	u8 rateidx;
	u32 preamble;
/* 	u8 modem; */
	u32 CrystalCap;
/* 	u32 curr_crystalcap; */

	u16 antenna_tx;
	u16 antenna_rx;
/* 	u8 curr_rfpath; */

	u8 check_mp_pkt;

	u8 bSetTxPower;
/* 	uint ForcedDataRate; */
	u8 mp_dm;
	u8 mac_filter[ETH_ALEN];
	u8 bmac_filter;

	struct wlan_network mp_network;
	NDIS_802_11_MAC_ADDRESS network_macaddr;

	u8 *pallocated_mp_xmitframe_buf;
	u8 *pmp_xmtframe_buf;
	struct __queue free_mp_xmitqueue;
	u32 free_mp_xmitframe_cnt;
	bool bSetRxBssid;
	bool bTxBufCkFail;

	MPT_CONTEXT MptCtx;

	u8 *TXradomBuffer;
};

typedef struct _IOCMD_STRUCT_ {
	u8 cmdclass;
	u16 value;
	u8 index;
} IOCMD_STRUCT;

struct rf_reg_param {
	u32 path;
	u32 offset;
	u32 value;
};

struct bb_reg_param {
	u32 offset;
	u32 value;
};

#define LOWER	true
#define RAISE	false

/* Hardware Registers */
#define BB_REG_BASE_ADDR		0x800

/* MP variables */
enum MP_MODE {
	MP_OFF,
	MP_ON,
	MP_ERR,
	MP_CONTINUOUS_TX,
	MP_SINGLE_CARRIER_TX,
	MP_CARRIER_SUPPRISSION_TX,
	MP_SINGLE_TONE_TX,
	MP_PACKET_TX,
	MP_PACKET_RX
};

#define MAX_RF_PATH_NUMS	RF_PATH_MAX

extern u8 mpdatarate[NumRates];

/* MP set force data rate base on the definition. */
enum MPT_RATE_INDEX {
	/* CCK rate. */
	MPT_RATE_1M = 0,	/* 0 */
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
	/* VHT rate. Total: 20*/
	MPT_RATE_VHT1SS_MCS0 = 100,/*  To reserve MCS16~MCS31, the index starts from #100. */
	MPT_RATE_VHT1SS_MCS1, /*  #101 */
	MPT_RATE_VHT1SS_MCS2,
	MPT_RATE_VHT1SS_MCS3,
	MPT_RATE_VHT1SS_MCS4,
	MPT_RATE_VHT1SS_MCS5,
	MPT_RATE_VHT1SS_MCS6, /*  #106 */
	MPT_RATE_VHT1SS_MCS7,
	MPT_RATE_VHT1SS_MCS8,
	MPT_RATE_VHT1SS_MCS9,
	MPT_RATE_VHT2SS_MCS0,
	MPT_RATE_VHT2SS_MCS1, /*  #111 */
	MPT_RATE_VHT2SS_MCS2,
	MPT_RATE_VHT2SS_MCS3,
	MPT_RATE_VHT2SS_MCS4,
	MPT_RATE_VHT2SS_MCS5,
	MPT_RATE_VHT2SS_MCS6, /*  #116 */
	MPT_RATE_VHT2SS_MCS7,
	MPT_RATE_VHT2SS_MCS8,
	MPT_RATE_VHT2SS_MCS9,
	MPT_RATE_LAST
};

#define MAX_TX_PWR_INDEX_N_MODE 64	/*  0x3F */

enum POWER_MODE {
	POWER_LOW = 0,
	POWER_NORMAL
};

/*  The following enumeration is used to define the value of Reg0xD00[30:28] or JaguarReg0x914[18:16]. */
enum OFDM_TX_MODE {
	OFDM_ALL_OFF		= 0,
	OFDM_ContinuousTx	= 1,
	OFDM_SingleCarrier	= 2,
	OFDM_SingleTone		= 4,
};

#define RX_PKT_BROADCAST	1
#define RX_PKT_DEST_ADDR	2
#define RX_PKT_PHY_MATCH	3

#define Mac_OFDM_OK			0x00000000
#define Mac_OFDM_Fail			0x10000000
#define Mac_OFDM_FasleAlarm	0x20000000
#define Mac_CCK_OK				0x30000000
#define Mac_CCK_Fail			0x40000000
#define Mac_CCK_FasleAlarm		0x50000000
#define Mac_HT_OK				0x60000000
#define Mac_HT_Fail			0x70000000
#define Mac_HT_FasleAlarm		0x90000000
#define Mac_DropPacket			0xA0000000

enum ENCRY_CTRL_STATE {
	HW_CONTROL,		/* hw encryption& decryption */
	SW_CONTROL,		/* sw encryption& decryption */
	HW_ENCRY_SW_DECRY,	/* hw encryption & sw decryption */
	SW_ENCRY_HW_DECRY	/* sw encryption & hw decryption */
};

enum MPT_TXPWR_DEF {
	MPT_CCK,
	MPT_OFDM, /*  L and HT OFDM */
	MPT_VHT_OFDM
};

#define		REG_RF_BB_GAIN_OFFSET	0x7f
#define		RF_GAIN_OFFSET_MASK	0xfffff

/*  */
/* struct mp_xmit_frame *alloc_mp_xmitframe(struct mp_priv *pmp_priv); */
/* int free_mp_xmitframe(struct xmit_priv *pxmitpriv, struct mp_xmit_frame *pmp_xmitframe); */

s32 init_mp_priv(struct adapter *padapter);
void free_mp_priv(struct mp_priv *pmp_priv);
s32 MPT_InitializeAdapter(struct adapter *padapter, u8 Channel);
void MPT_DeInitAdapter(struct adapter *padapter);
s32 mp_start_test(struct adapter *padapter);
void mp_stop_test(struct adapter *padapter);

u32 _read_rfreg(struct adapter *padapter, u8 rfpath, u32 addr, u32 bitmask);
void _write_rfreg(struct adapter *padapter, u8 rfpath, u32 addr, u32 bitmask, u32 val);

u32 read_macreg(struct adapter *padapter, u32 addr, u32 sz);
void write_macreg(struct adapter *padapter, u32 addr, u32 val, u32 sz);
u32 read_bbreg(struct adapter *padapter, u32 addr, u32 bitmask);
void write_bbreg(struct adapter *padapter, u32 addr, u32 bitmask, u32 val);
u32 read_rfreg(struct adapter *padapter, u8 rfpath, u32 addr);
void write_rfreg(struct adapter *padapter, u8 rfpath, u32 addr, u32 val);

void SetChannel(struct adapter *padapter);
void SetBandwidth(struct adapter *padapter);
int SetTxPower(struct adapter *padapter);
void SetAntennaPathPower(struct adapter *padapter);
void SetDataRate(struct adapter *padapter);

void SetAntenna(struct adapter *padapter);

s32 SetThermalMeter(struct adapter *padapter, u8 target_ther);
void GetThermalMeter(struct adapter *padapter, u8 *value);

void SetContinuousTx(struct adapter *padapter, u8 bStart);
void SetSingleCarrierTx(struct adapter *padapter, u8 bStart);
void SetSingleToneTx(struct adapter *padapter, u8 bStart);
void SetCarrierSuppressionTx(struct adapter *padapter, u8 bStart);
void PhySetTxPowerLevel(struct adapter *padapter);

void fill_txdesc_for_mp(struct adapter *padapter, u8 *ptxdesc);
void SetPacketTx(struct adapter *padapter);
void SetPacketRx(struct adapter *padapter, u8 bStartRx);

void ResetPhyRxPktCount(struct adapter *padapter);
u32 GetPhyRxPktReceived(struct adapter *padapter);
u32 GetPhyRxPktCRC32Error(struct adapter *padapter);

s32	SetPowerTracking(struct adapter *padapter, u8 enable);
void GetPowerTracking(struct adapter *padapter, u8 *enable);

u32 mp_query_psd(struct adapter *padapter, u8 *data);

void Hal_SetAntenna(struct adapter *padapter);
void Hal_SetBandwidth(struct adapter *padapter);

void Hal_SetTxPower(struct adapter *padapter);
void Hal_SetCarrierSuppressionTx(struct adapter *padapter, u8 bStart);
void Hal_SetSingleToneTx(struct adapter *padapter, u8 bStart);
void Hal_SetSingleCarrierTx(struct adapter *padapter, u8 bStart);
void Hal_SetContinuousTx(struct adapter *padapter, u8 bStart);
void Hal_SetBandwidth(struct adapter *padapter);

void Hal_SetDataRate(struct adapter *padapter);
void Hal_SetChannel(struct adapter *padapter);
void Hal_SetAntennaPathPower(struct adapter *padapter);
s32 Hal_SetThermalMeter(struct adapter *padapter, u8 target_ther);
s32 Hal_SetPowerTracking(struct adapter *padapter, u8 enable);
void Hal_GetPowerTracking(struct adapter *padapter, u8 * enable);
void Hal_GetThermalMeter(struct adapter *padapter, u8 *value);
void Hal_mpt_SwitchRfSetting(struct adapter *padapter);
void Hal_MPT_CCKTxPowerAdjust(struct adapter * Adapter, bool bInCH14);
void Hal_MPT_CCKTxPowerAdjustbyIndex(struct adapter *padapter, bool beven);
void Hal_SetCCKTxPower(struct adapter *padapter, u8 * TxPower);
void Hal_SetOFDMTxPower(struct adapter *padapter, u8 * TxPower);
void Hal_TriggerRFThermalMeter(struct adapter *padapter);
u8 Hal_ReadRFThermalMeter(struct adapter *padapter);
void Hal_SetCCKContinuousTx(struct adapter *padapter, u8 bStart);
void Hal_SetOFDMContinuousTx(struct adapter *padapter, u8 bStart);
void Hal_ProSetCrystalCap(struct adapter *padapter, u32 CrystalCapVal);
void MP_PHY_SetRFPathSwitch(struct adapter *padapter, bool bMain);
u32 mpt_ProQueryCalTxPower(struct adapter *padapter, u8 RfPath);
void MPT_PwrCtlDM(struct adapter *padapter, u32 bstart);
u8 MptToMgntRate(u32 MptRateIdx);

#endif /* _RTW_MP_H_ */
