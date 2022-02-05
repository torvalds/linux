/* SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause */
/* Copyright(c) 2007 - 2011 Realtek Corporation. */

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
#include "rtw_efuse.h"
#include "odm_types.h"
#include "odm.h"
#include "odm_HWConfig.h"
#include "odm_RegDefine11N.h"
#include "HalPhyRf_8188e.h"
#include "Hal8188ERateAdaptive.h"
#include "HalHWImg8188E_MAC.h"
#include "HalHWImg8188E_RF.h"
#include "HalHWImg8188E_BB.h"
#include "odm_RegConfig8188E.h"
#include "odm_RTL8188E.h"

/* 		RTL8188E Power Configuration CMDs for USB/SDIO interfaces */
#define Rtl8188E_NIC_PWR_ON_FLOW		rtl8188E_power_on_flow
#define Rtl8188E_NIC_DISABLE_FLOW		rtl8188E_card_disable_flow
#define Rtl8188E_NIC_LPS_ENTER_FLOW		rtl8188E_enter_lps_flow

#define DRVINFO_SZ	4 /*  unit is 8bytes */
#define PageNum_128(_Len)	(u32)(((_Len)>>7) + ((_Len) & 0x7F ? 1 : 0))

/*  download firmware related data structure */
#define FW_8188E_SIZE			0x4000 /* 16384,16k */
#define FW_8188E_START_ADDRESS		0x1000

#define MAX_PAGE_SIZE			4096	/*  @ page : 4k bytes */

#define IS_FW_HEADER_EXIST(_pFwHdr)				\
	((le16_to_cpu(_pFwHdr->Signature)&0xFFF0) == 0x92C0 ||	\
	(le16_to_cpu(_pFwHdr->Signature)&0xFFF0) == 0x88C0 ||	\
	(le16_to_cpu(_pFwHdr->Signature)&0xFFF0) == 0x2300 ||	\
	(le16_to_cpu(_pFwHdr->Signature)&0xFFF0) == 0x88E0)

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
	u8 IndexCCK_Base[RF_PATH_MAX][MAX_CHNL_GROUP_24G];
	u8 IndexBW40_Base[RF_PATH_MAX][MAX_CHNL_GROUP_24G];
	/* If only one tx, only BW20 and OFDM are used. */
	s8 CCK_Diff[RF_PATH_MAX][MAX_TX_COUNT];
	s8 OFDM_Diff[RF_PATH_MAX][MAX_TX_COUNT];
	s8 BW20_Diff[RF_PATH_MAX][MAX_TX_COUNT];
	s8 BW40_Diff[RF_PATH_MAX][MAX_TX_COUNT];
};

#define EFUSE_REAL_CONTENT_LEN		512
#define AVAILABLE_EFUSE_ADDR(addr)	(addr < EFUSE_REAL_CONTENT_LEN)

#define		EFUSE_REAL_CONTENT_LEN_88E	256
#define		EFUSE_MAP_LEN_88E		512
#define		EFUSE_MAX_SECTION_88E		64
/*  To prevent out of boundary programming case, leave 1byte and program
 *  full section */
/*  9bytes + 1byt + 5bytes and pre 1byte. */
/*  For worst case: */
/*  | 2byte|----8bytes----|1byte|--7bytes--| 92D */
/*  PG data exclude header, dummy 7 bytes frome CP test and reserved 1byte. */
#define		EFUSE_OOB_PROTECT_BYTES_88E	18

#define EFUSE_PROTECT_BYTES_BANK	16

struct hal_data_8188e {
	struct HAL_VERSION	VersionID;
	u16	FirmwareVersion;
	u16	FirmwareVersionRev;
	u16	FirmwareSubVersion;
	u16	FirmwareSignature;
	u8	PGMaxGroup;
	/* current WIFI_PHY values */
	u32	ReceiveConfig;
	enum ht_channel_width CurrentChannelBW;
	u8	CurrentChannel;
	u8	nCur40MhzPrimeSC;/*  Control channel sub-carrier */

	u16	BasicRateSet;

	u8	EEPROMRegulatory;
	u8	EEPROMThermalMeter;

	u8	Index24G_CCK_Base[CHANNEL_MAX_NUMBER];
	u8	Index24G_BW40_Base[CHANNEL_MAX_NUMBER];
	/* If only one tx, only BW20 and OFDM are used. */
	s8	OFDM_24G_Diff[MAX_TX_COUNT];
	s8	BW20_24G_Diff[MAX_TX_COUNT];

	/*  HT 20<->40 Pwr diff */
	u8	TxPwrHt20Diff[RF_PATH_MAX][CHANNEL_MAX_NUMBER];
	/*  For HT<->legacy pwr diff */
	u8	TxPwrLegacyHtDiff[RF_PATH_MAX][CHANNEL_MAX_NUMBER];
	/*  For power group */
	u8	PwrGroupHT20[RF_PATH_MAX][CHANNEL_MAX_NUMBER];
	u8	PwrGroupHT40[RF_PATH_MAX][CHANNEL_MAX_NUMBER];

	/*  The current Tx Power Level */
	u8	CurrentCckTxPwrIdx;
	u8	CurrentOfdm24GTxPwrIdx;
	u8	CurrentBW2024GTxPwrIdx;
	u8	CurrentBW4024GTxPwrIdx;

	/*  Read/write are allow for following hardware information variables */
	u8	pwrGroupCnt;
	u32	MCSTxPowerLevelOriginalOffset[MAX_PG_GROUP][16];

	u8	CrystalCap;
	u8	ExternalPA;

	u32	AcParam_BE; /* Original parameter for BE, use for EDCA turbo. */

	struct bb_reg_def PHYRegDef[2];	/* Radio A/B */

	u32	RfRegChnlVal[2];

	/* for host message to fw */
	u8	LastHMEBoxNum;

	u8	fw_ractrl;
	u8	RegFwHwTxQCtrl;
	u8	RegReg542;
	u8	RegCR_1;

	struct dm_priv	dmpriv;
	struct odm_dm_struct odmpriv;

	u8	CurAntenna;
	u8	AntDivCfg;
	u8	TRxAntDivType;

	u8	bDumpRxPkt;/* for debug */
	u8	bDumpTxPkt;/* for debug */

	u8	OutEpQueueSel;
	u8	OutEpNumber;

	u16	EfuseUsedBytes;

	struct P2P_PS_Offload_t	p2p_ps_offload;

	/*  Auto FSM to Turn On, include clock, isolation, power control
	 *  for MAC only */
	u8	bMacPwrCtrlOn;

	u32	UsbBulkOutSize;

	u8	UsbTxAggMode;
	u8	UsbTxAggDescNum;

	enum usb_rx_agg_mode UsbRxAggMode;
	u8	UsbRxAggBlockCount;	/*  USB Block count. Block size is
					 * 512-byte in high speed and 64-byte
					 * in full speed */
	u8	UsbRxAggBlockTimeout;
	u8	UsbRxAggPageCount;	/*  8192C DMA page count */
	u8	UsbRxAggPageTimeout;
};

/*  rtl8188e_hal_init.c */
s32 rtl8188e_FirmwareDownload(struct adapter *padapter);
void _8051Reset88E(struct adapter *padapter);
void rtl8188e_InitializeFirmwareVars(struct adapter *padapter);

s32 InitLLTTable(struct adapter *padapter, u8 txpktbuf_bndy);

/*  EFuse */
u8 GetEEPROMSize8188E(struct adapter *padapter);
void Hal_EfuseParseIDCode88E(struct adapter *padapter, u8 *hwinfo);
void Hal_ReadTxPowerInfo88E(struct adapter *padapter, u8 *hwinfo,
			    bool AutoLoadFail);

void rtl8188e_EfuseParseChnlPlan(struct adapter *padapter, u8 *hwinfo,
				 bool AutoLoadFail);
void Hal_ReadAntennaDiversity88E(struct adapter *pAdapter,u8 *PROMContent,
				 bool AutoLoadFail);
void Hal_ReadThermalMeter_88E(struct adapter *	dapter, u8 *PROMContent,
			      bool AutoloadFail);
void Hal_EfuseParseXtal_8188E(struct adapter *pAdapter, u8 *hwinfo,
			      bool AutoLoadFail);
void Hal_ReadPowerSavingMode88E(struct adapter *pAdapter, u8 *hwinfo,
				bool AutoLoadFail);

void rtl8188e_read_chip_version(struct adapter *padapter);

s32 rtl8188e_iol_efuse_patch(struct adapter *padapter);
void rtw_cancel_all_timer(struct adapter *padapter);

#endif /* __RTL8188E_HAL_H__ */
