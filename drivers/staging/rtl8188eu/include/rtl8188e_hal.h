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
#ifndef __RTL8188E_HAL_H__
#define __RTL8188E_HAL_H__


/* include HAL Related header after HAL Related compiling flags */
#include "rtl8188e_spec.h"
#include "Hal8188EPhyReg.h"
#include "Hal8188EPhyCfg.h"
#include "rtl8188e_rf.h"
#include "rtl8188e_dm.h"
#include "rtl8188e_recv.h"
#include "rtl8188e_xmit.h"
#include "rtl8188e_cmd.h"
#include "Hal8188EPwrSeq.h"
#include "rtl8188e_sreset.h"
#include "rtw_efuse.h"

#include "odm_precomp.h"

/*  Fw Array */
#define Rtl8188E_FwImageArray		Rtl8188EFwImgArray
#define Rtl8188E_FWImgArrayLength	Rtl8188EFWImgArrayLength

#define RTL8188E_FW_UMC_IMG			"rtl8188E\\rtl8188efw.bin"
#define RTL8188E_PHY_REG			"rtl8188E\\PHY_REG_1T.txt"
#define RTL8188E_PHY_RADIO_A			"rtl8188E\\radio_a_1T.txt"
#define RTL8188E_PHY_RADIO_B			"rtl8188E\\radio_b_1T.txt"
#define RTL8188E_AGC_TAB			"rtl8188E\\AGC_TAB_1T.txt"
#define RTL8188E_PHY_MACREG			"rtl8188E\\MAC_REG.txt"
#define RTL8188E_PHY_REG_PG			"rtl8188E\\PHY_REG_PG.txt"
#define RTL8188E_PHY_REG_MP			"rtl8188E\\PHY_REG_MP.txt"

/* 		RTL8188E Power Configuration CMDs for USB/SDIO interfaces */
#define Rtl8188E_NIC_PWR_ON_FLOW		rtl8188E_power_on_flow
#define Rtl8188E_NIC_RF_OFF_FLOW		rtl8188E_radio_off_flow
#define Rtl8188E_NIC_DISABLE_FLOW		rtl8188E_card_disable_flow
#define Rtl8188E_NIC_ENABLE_FLOW		rtl8188E_card_enable_flow
#define Rtl8188E_NIC_SUSPEND_FLOW		rtl8188E_suspend_flow
#define Rtl8188E_NIC_RESUME_FLOW		rtl8188E_resume_flow
#define Rtl8188E_NIC_PDN_FLOW			rtl8188E_hwpdn_flow
#define Rtl8188E_NIC_LPS_ENTER_FLOW		rtl8188E_enter_lps_flow
#define Rtl8188E_NIC_LPS_LEAVE_FLOW		rtl8188E_leave_lps_flow

#define DRVINFO_SZ	4 /*  unit is 8bytes */
#define PageNum_128(_Len)	(u32)(((_Len)>>7) + ((_Len) & 0x7F ? 1 : 0))

/*  download firmware related data structure */
#define FW_8188E_SIZE			0x4000 /* 16384,16k */
#define FW_8188E_START_ADDRESS		0x1000
#define FW_8188E_END_ADDRESS		0x1FFF /* 0x5FFF */

#define MAX_PAGE_SIZE			4096	/*  @ page : 4k bytes */

#define IS_FW_HEADER_EXIST(_pFwHdr)				\
	((le16_to_cpu(_pFwHdr->Signature)&0xFFF0) == 0x92C0 ||	\
	(le16_to_cpu(_pFwHdr->Signature)&0xFFF0) == 0x88C0 ||	\
	(le16_to_cpu(_pFwHdr->Signature)&0xFFF0) == 0x2300 ||	\
	(le16_to_cpu(_pFwHdr->Signature)&0xFFF0) == 0x88E0)

enum firmware_source {
	FW_SOURCE_IMG_FILE = 0,
	FW_SOURCE_HEADER_FILE = 1,		/* from header file */
};

struct rt_firmware {
	enum firmware_source	eFWSource;
	u8			*szFwBuffer;
	u32			ulFwLength;
};

/*  This structure must be careful with byte-ordering */

struct rt_firmware_hdr {
	/*  8-byte alinment required */
	/*  LONG WORD 0 ---- */
	__le16		Signature;	/* 92C0: test chip; 92C,
					 * 88C0: test chip; 88C1: MP A-cut;
					 * 92C1: MP A-cut */
	u8		Category;	/*  AP/NIC and USB/PCI */
	u8		Function;	/*  Reserved for different FW function
					 *  indcation, for further use when
					 *  driver needs to download different
					 *  FW for different conditions */
	__le16		Version;	/*  FW Version */
	u8		Subversion;	/*  FW Subversion, default 0x00 */
	u16		Rsvd1;

	/*  LONG WORD 1 ---- */
	u8		Month;	/*  Release time Month field */
	u8		Date;	/*  Release time Date field */
	u8		Hour;	/*  Release time Hour field */
	u8		Minute;	/*  Release time Minute field */
	__le16		RamCodeSize;	/*  The size of RAM code */
	u8		Foundry;
	u8		Rsvd2;

	/*  LONG WORD 2 ---- */
	__le32		SvnIdx;	/*  The SVN entry index */
	u32		Rsvd3;

	/*  LONG WORD 3 ---- */
	u32		Rsvd4;
	u32		Rsvd5;
};

#define DRIVER_EARLY_INT_TIME		0x05
#define BCN_DMA_ATIME_INT_TIME		0x02

enum usb_rx_agg_mode {
	USB_RX_AGG_DISABLE,
	USB_RX_AGG_DMA,
	USB_RX_AGG_USB,
	USB_RX_AGG_MIX
};

#define MAX_RX_DMA_BUFFER_SIZE_88E				\
      0x2400 /* 9k for 88E nornal chip , MaxRxBuff=10k-max(TxReportSize(64*8),
	      * WOLPattern(16*24)) */

#define MAX_TX_REPORT_BUFFER_SIZE		0x0400 /*  1k */


/*  BK, BE, VI, VO, HCCA, MANAGEMENT, COMMAND, HIGH, BEACON. */
#define MAX_TX_QUEUE			9

#define TX_SELE_HQ			BIT(0)		/*  High Queue */
#define TX_SELE_LQ			BIT(1)		/*  Low Queue */
#define TX_SELE_NQ			BIT(2)		/*  Normal Queue */

/*  Note: We will divide number of page equally for each queue other
 *  than public queue! */
/*  22k = 22528 bytes = 176 pages (@page =  128 bytes) */
/*  must reserved about 7 pages for LPS =>  176-7 = 169 (0xA9) */
/*  2*BCN / 1*ps-poll / 1*null-data /1*prob_rsp /1*QOS null-data /1*BT QOS
 *  null-data */

#define TX_TOTAL_PAGE_NUMBER_88E		0xA9/*   169 (21632=> 21k) */

#define TX_PAGE_BOUNDARY_88E (TX_TOTAL_PAGE_NUMBER_88E + 1)

/* Note: For Normal Chip Setting ,modify later */
#define WMM_NORMAL_TX_TOTAL_PAGE_NUMBER			\
	TX_TOTAL_PAGE_NUMBER_88E  /* 0xA9 , 0xb0=>176=>22k */
#define WMM_NORMAL_TX_PAGE_BOUNDARY_88E			\
	(WMM_NORMAL_TX_TOTAL_PAGE_NUMBER + 1) /* 0xA9 */

/* 	Chip specific */
#define CHIP_BONDING_IDENTIFIER(_value)	(((_value)>>22)&0x3)
#define CHIP_BONDING_92C_1T2R	0x1
#define CHIP_BONDING_88C_USB_MCARD	0x2
#define CHIP_BONDING_88C_USB_HP	0x1
#include "HalVerDef.h"
#include "hal_com.h"

/* 	Channel Plan */
enum ChannelPlan {
	CHPL_FCC	= 0,
	CHPL_IC		= 1,
	CHPL_ETSI	= 2,
	CHPL_SPA	= 3,
	CHPL_FRANCE	= 4,
	CHPL_MKK	= 5,
	CHPL_MKK1	= 6,
	CHPL_ISRAEL	= 7,
	CHPL_TELEC	= 8,
	CHPL_GLOBAL	= 9,
	CHPL_WORLD	= 10,
};

struct txpowerinfo24g {
	u8 IndexCCK_Base[MAX_RF_PATH][MAX_CHNL_GROUP_24G];
	u8 IndexBW40_Base[MAX_RF_PATH][MAX_CHNL_GROUP_24G];
	/* If only one tx, only BW20 and OFDM are used. */
	s8 CCK_Diff[MAX_RF_PATH][MAX_TX_COUNT];
	s8 OFDM_Diff[MAX_RF_PATH][MAX_TX_COUNT];
	s8 BW20_Diff[MAX_RF_PATH][MAX_TX_COUNT];
	s8 BW40_Diff[MAX_RF_PATH][MAX_TX_COUNT];
};

#define EFUSE_REAL_CONTENT_LEN		512
#define EFUSE_MAX_SECTION		16
#define EFUSE_IC_ID_OFFSET		506 /* For some inferior IC purpose*/
#define AVAILABLE_EFUSE_ADDR(addr)	(addr < EFUSE_REAL_CONTENT_LEN)
/*  To prevent out of boundary programming case, */
/*  leave 1byte and program full section */
/*  9bytes + 1byt + 5bytes and pre 1byte. */
/*  For worst case: */
/*  | 1byte|----8bytes----|1byte|--5bytes--| */
/*  |         |            Reserved(14bytes)	      | */

/*  PG data exclude header, dummy 6 bytes frome CP test and reserved 1byte. */
#define EFUSE_OOB_PROTECT_BYTES			15

#define		HWSET_MAX_SIZE_88E		512

#define		EFUSE_REAL_CONTENT_LEN_88E	256
#define		EFUSE_MAP_LEN_88E		512
#define EFUSE_MAP_LEN			EFUSE_MAP_LEN_88E
#define		EFUSE_MAX_SECTION_88E		64
#define		EFUSE_MAX_WORD_UNIT_88E		4
#define		EFUSE_IC_ID_OFFSET_88E		506
#define		AVAILABLE_EFUSE_ADDR_88E(addr)			\
	(addr < EFUSE_REAL_CONTENT_LEN_88E)
/*  To prevent out of boundary programming case, leave 1byte and program
 *  full section */
/*  9bytes + 1byt + 5bytes and pre 1byte. */
/*  For worst case: */
/*  | 2byte|----8bytes----|1byte|--7bytes--| 92D */
/*  PG data exclude header, dummy 7 bytes frome CP test and reserved 1byte. */
#define		EFUSE_OOB_PROTECT_BYTES_88E	18
#define		EFUSE_PROTECT_BYTES_BANK_88E	16

/* 			EFUSE for BT definition */
#define EFUSE_BT_REAL_CONTENT_LEN	1536	/*  512*3 */
#define EFUSE_BT_MAP_LEN		1024	/*  1k bytes */
#define EFUSE_BT_MAX_SECTION		128	/*  1024/8 */

#define EFUSE_PROTECT_BYTES_BANK	16

/*  For RTL8723 WiFi/BT/GPS multi-function configuration. */
enum rt_multi_func {
	RT_MULTI_FUNC_NONE = 0x00,
	RT_MULTI_FUNC_WIFI = 0x01,
	RT_MULTI_FUNC_BT = 0x02,
	RT_MULTI_FUNC_GPS = 0x04,
};

/*  For RTL8723 regulator mode. */
enum rt_regulator_mode {
	RT_SWITCHING_REGULATOR = 0,
	RT_LDO_REGULATOR = 1,
};

struct hal_data_8188e {
	struct HAL_VERSION	VersionID;
	enum rt_multi_func MultiFunc; /*  For multi-function consideration. */
	enum rt_regulator_mode RegulatorMode; /*  switching regulator or LDO */
	u16	CustomerID;

	u16	FirmwareVersion;
	u16	FirmwareVersionRev;
	u16	FirmwareSubVersion;
	u16	FirmwareSignature;
	u8	PGMaxGroup;
	/* current WIFI_PHY values */
	u32	ReceiveConfig;
	enum wireless_mode CurrentWirelessMode;
	enum ht_channel_width CurrentChannelBW;
	u8	CurrentChannel;
	u8	nCur40MhzPrimeSC;/*  Control channel sub-carrier */

	u16	BasicRateSet;

	/* rf_ctrl */
	u8	rf_chip;
	u8	rf_type;
	u8	NumTotalRFPath;

	u8	BoardType;

	/*  EEPROM setting. */
	u16	EEPROMVID;
	u16	EEPROMPID;
	u16	EEPROMSVID;
	u16	EEPROMSDID;
	u8	EEPROMCustomerID;
	u8	EEPROMSubCustomerID;
	u8	EEPROMVersion;
	u8	EEPROMRegulatory;

	u8	bTXPowerDataReadFromEEPORM;
	u8	EEPROMThermalMeter;
	u8	bAPKThermalMeterIgnore;

	bool	EepromOrEfuse;
	/* 92C:256bytes, 88E:512bytes, we use union set (512bytes) */
	u8	EfuseMap[2][HWSET_MAX_SIZE_512];
	u8	EfuseUsedPercentage;
	struct efuse_hal	EfuseHal;

	u8	Index24G_CCK_Base[MAX_RF_PATH][CHANNEL_MAX_NUMBER];
	u8	Index24G_BW40_Base[MAX_RF_PATH][CHANNEL_MAX_NUMBER];
	/* If only one tx, only BW20 and OFDM are used. */
	s8	CCK_24G_Diff[MAX_RF_PATH][MAX_TX_COUNT];
	s8	OFDM_24G_Diff[MAX_RF_PATH][MAX_TX_COUNT];
	s8	BW20_24G_Diff[MAX_RF_PATH][MAX_TX_COUNT];
	s8	BW40_24G_Diff[MAX_RF_PATH][MAX_TX_COUNT];

	u8	TxPwrLevelCck[RF_PATH_MAX][CHANNEL_MAX_NUMBER];
	/*  For HT 40MHZ pwr */
	u8	TxPwrLevelHT40_1S[RF_PATH_MAX][CHANNEL_MAX_NUMBER];
	/*  For HT 40MHZ pwr */
	u8	TxPwrLevelHT40_2S[RF_PATH_MAX][CHANNEL_MAX_NUMBER];
	/*  HT 20<->40 Pwr diff */
	u8	TxPwrHt20Diff[RF_PATH_MAX][CHANNEL_MAX_NUMBER];
	/*  For HT<->legacy pwr diff */
	u8	TxPwrLegacyHtDiff[RF_PATH_MAX][CHANNEL_MAX_NUMBER];
	/*  For power group */
	u8	PwrGroupHT20[RF_PATH_MAX][CHANNEL_MAX_NUMBER];
	u8	PwrGroupHT40[RF_PATH_MAX][CHANNEL_MAX_NUMBER];

	u8	LegacyHTTxPowerDiff;/*  Legacy to HT rate power diff */
	/*  The current Tx Power Level */
	u8	CurrentCckTxPwrIdx;
	u8	CurrentOfdm24GTxPwrIdx;
	u8	CurrentBW2024GTxPwrIdx;
	u8	CurrentBW4024GTxPwrIdx;


	/*  Read/write are allow for following hardware information variables */
	u8	framesync;
	u32	framesyncC34;
	u8	framesyncMonitor;
	u8	DefaultInitialGain[4];
	u8	pwrGroupCnt;
	u32	MCSTxPowerLevelOriginalOffset[MAX_PG_GROUP][16];
	u32	CCKTxPowerLevelOriginalOffset;

	u8	CrystalCap;
	u32	AntennaTxPath;			/*  Antenna path Tx */
	u32	AntennaRxPath;			/*  Antenna path Rx */
	u8	BluetoothCoexist;
	u8	ExternalPA;

	u8	bLedOpenDrain; /* Open-drain support for controlling the LED.*/

	u8	b1x1RecvCombine;	/*  for 1T1R receive combining */

	u32	AcParam_BE; /* Original parameter for BE, use for EDCA turbo. */

	struct bb_reg_def PHYRegDef[4];	/* Radio A/B/C/D */

	u32	RfRegChnlVal[2];

	/* RDG enable */
	bool	 bRDGEnable;

	/* for host message to fw */
	u8	LastHMEBoxNum;

	u8	fw_ractrl;
	u8	RegTxPause;
	/*  Beacon function related global variable. */
	u32	RegBcnCtrlVal;
	u8	RegFwHwTxQCtrl;
	u8	RegReg542;
	u8	RegCR_1;

	struct dm_priv	dmpriv;
	struct odm_dm_struct odmpriv;
	struct sreset_priv srestpriv;

	u8	CurAntenna;
	u8	AntDivCfg;
	u8	TRxAntDivType;


	u8	bDumpRxPkt;/* for debug */
	u8	bDumpTxPkt;/* for debug */
	u8	FwRsvdPageStartOffset; /* Reserve page start offset except
					*  beacon in TxQ. */

	/*  2010/08/09 MH Add CU power down mode. */
	bool		pwrdown;

	/*  Add for dual MAC  0--Mac0 1--Mac1 */
	u32	interfaceIndex;

	u8	OutEpQueueSel;
	u8	OutEpNumber;

	/*  Add for USB aggreation mode dynamic shceme. */
	bool		UsbRxHighSpeedMode;

	/*  2010/11/22 MH Add for slim combo debug mode selective. */
	/*  This is used for fix the drawback of CU TSMC-A/UMC-A cut.
	 * HW auto suspend ability. Close BT clock. */
	bool		SlimComboDbg;

	u16	EfuseUsedBytes;

#ifdef CONFIG_88EU_P2P
	struct P2P_PS_Offload_t	p2p_ps_offload;
#endif

	/*  Auto FSM to Turn On, include clock, isolation, power control
	 *  for MAC only */
	u8	bMacPwrCtrlOn;

	u32	UsbBulkOutSize;

	/*  Interrupt relatd register information. */
	u32	IntArray[3];/* HISR0,HISR1,HSISR */
	u32	IntrMask[3];
	u8	C2hArray[16];
	u8	UsbTxAggMode;
	u8	UsbTxAggDescNum;
	u16	HwRxPageSize;		/*  Hardware setting */
	u32	MaxUsbRxAggBlock;

	enum usb_rx_agg_mode UsbRxAggMode;
	u8	UsbRxAggBlockCount;	/*  USB Block count. Block size is
					 * 512-byte in high speed and 64-byte
					 * in full speed */
	u8	UsbRxAggBlockTimeout;
	u8	UsbRxAggPageCount;	/*  8192C DMA page count */
	u8	UsbRxAggPageTimeout;
};

#define GET_HAL_DATA(__pAdapter)				\
	((struct hal_data_8188e *)((__pAdapter)->HalData))
#define GET_RF_TYPE(priv)		(GET_HAL_DATA(priv)->rf_type)

#define INCLUDE_MULTI_FUNC_BT(_Adapter)				\
	(GET_HAL_DATA(_Adapter)->MultiFunc & RT_MULTI_FUNC_BT)
#define INCLUDE_MULTI_FUNC_GPS(_Adapter)			\
	(GET_HAL_DATA(_Adapter)->MultiFunc & RT_MULTI_FUNC_GPS)

/*  rtl8188e_hal_init.c */
s32 rtl8188e_FirmwareDownload(struct adapter *padapter);
void _8051Reset88E(struct adapter *padapter);
void rtl8188e_InitializeFirmwareVars(struct adapter *padapter);


s32 InitLLTTable(struct adapter *padapter, u8 txpktbuf_bndy);

/*  EFuse */
u8 GetEEPROMSize8188E(struct adapter *padapter);
void Hal_InitPGData88E(struct adapter *padapter);
void Hal_EfuseParseIDCode88E(struct adapter *padapter, u8 *hwinfo);
void Hal_ReadTxPowerInfo88E(struct adapter *padapter, u8 *hwinfo,
			    bool AutoLoadFail);

void Hal_EfuseParseEEPROMVer88E(struct adapter *padapter, u8 *hwinfo,
				bool AutoLoadFail);
void rtl8188e_EfuseParseChnlPlan(struct adapter *padapter, u8 *hwinfo,
				 bool AutoLoadFail);
void Hal_EfuseParseCustomerID88E(struct adapter *padapter, u8 *hwinfo,
				 bool AutoLoadFail);
void Hal_ReadAntennaDiversity88E(struct adapter *pAdapter, u8 *PROMContent,
				 bool AutoLoadFail);
void Hal_ReadThermalMeter_88E(struct adapter *dapter, u8 *PROMContent,
			      bool AutoloadFail);
void Hal_EfuseParseXtal_8188E(struct adapter *pAdapter, u8 *hwinfo,
			      bool AutoLoadFail);
void Hal_EfuseParseBoardType88E(struct adapter *pAdapter, u8 *hwinfo,
				bool AutoLoadFail);
void Hal_ReadPowerSavingMode88E(struct adapter *pAdapter, u8 *hwinfo,
				bool AutoLoadFail);

bool HalDetectPwrDownMode88E(struct adapter *Adapter);

void Hal_InitChannelPlan(struct adapter *padapter);
void rtl8188e_set_hal_ops(struct hal_ops *pHalFunc);

/*  register */
void SetBcnCtrlReg(struct adapter *padapter, u8 SetBits, u8 ClearBits);

void rtl8188e_clone_haldata(struct adapter *dst, struct adapter *src);
void rtl8188e_start_thread(struct adapter *padapter);
void rtl8188e_stop_thread(struct adapter *padapter);

void rtw_IOL_cmd_tx_pkt_buf_dump(struct adapter  *Adapter, int len);
s32 rtl8188e_iol_efuse_patch(struct adapter *padapter);
void rtw_cancel_all_timer(struct adapter *padapter);
void _ps_open_RF(struct adapter *adapt);

#endif /* __RTL8188E_HAL_H__ */
