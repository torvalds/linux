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
#ifndef __RTL8188F_HAL_H__
#define __RTL8188F_HAL_H__

#include "hal_data.h"

#include "rtl8188f_spec.h"
#include "rtl8188f_rf.h"
#include "rtl8188f_dm.h"
#include "rtl8188f_recv.h"
#include "rtl8188f_xmit.h"
#include "rtl8188f_cmd.h"
#include "rtl8188f_led.h"
#include "Hal8188FPwrSeq.h"
#include "Hal8188FPhyReg.h"
#include "Hal8188FPhyCfg.h"
#ifdef DBG_CONFIG_ERROR_DETECT
	#include "rtl8188f_sreset.h"
#endif


/* ---------------------------------------------------------------------
 *		RTL8188F From file
 * --------------------------------------------------------------------- */
#define RTL8188F_FW_IMG					"rtl8188f/FW_NIC.bin"
#define RTL8188F_FW_WW_IMG				"rtl8188f/FW_WoWLAN.bin"
#define RTL8188F_PHY_REG					"rtl8188f/PHY_REG.txt"
#define RTL8188F_PHY_RADIO_A				"rtl8188f/RadioA.txt"
#define RTL8188F_PHY_RADIO_B				"rtl8188f/RadioB.txt"
#define RTL8188F_TXPWR_TRACK				"rtl8188f/TxPowerTrack.txt"
#define RTL8188F_AGC_TAB					"rtl8188f/AGC_TAB.txt"
#define RTL8188F_PHY_MACREG 				"rtl8188f/MAC_REG.txt"
#define RTL8188F_PHY_REG_PG				"rtl8188f/PHY_REG_PG.txt"
#define RTL8188F_PHY_REG_MP				"rtl8188f/PHY_REG_MP.txt"
#define RTL8188F_TXPWR_LMT 				"rtl8188f/TXPWR_LMT.txt"

/* ---------------------------------------------------------------------
 *		RTL8188F From header
 * --------------------------------------------------------------------- */

#if MP_DRIVER == 1
	#define Rtl8188F_FwBTImgArray				Rtl8188FFwBTImgArray
	#define Rtl8188F_FwBTImgArrayLength		Rtl8188FFwBTImgArrayLength

	#define Rtl8188F_FwMPImageArray			Rtl8188FFwMPImgArray
	#define Rtl8188F_FwMPImgArrayLength		Rtl8188FMPImgArrayLength

	#define Rtl8188F_PHY_REG_Array_MP			Rtl8188F_PHYREG_Array_MP
	#define Rtl8188F_PHY_REG_Array_MPLength	Rtl8188F_PHYREG_Array_MPLength
#endif


#define FW_8188F_SIZE			0x8000
#define FW_8188F_START_ADDRESS	0x1000
#define FW_8188F_END_ADDRESS		0x1FFF /* 0x5FFF */

#define IS_FW_HEADER_EXIST_8188F(_pFwHdr)	((le16_to_cpu(_pFwHdr->Signature) & 0xFFF0) == 0x88F0)

typedef struct _RT_FIRMWARE {
	FIRMWARE_SOURCE	eFWSource;
#ifdef CONFIG_EMBEDDED_FWIMG
	u8			*szFwBuffer;
#else
	u8			szFwBuffer[FW_8188F_SIZE];
#endif
	u32			ulFwLength;
} RT_FIRMWARE_8188F, *PRT_FIRMWARE_8188F;

/*
 * This structure must be cared byte-ordering
 *
 * Added by tynli. 2009.12.04. */
typedef struct _RT_8188F_FIRMWARE_HDR {
	/* 8-byte alinment required */

	/* --- LONG WORD 0 ---- */
	u16		Signature;	/* 92C0: test chip; 92C, 88C0: test chip; 88C1: MP A-cut; 92C1: MP A-cut */
	u8		Category;	/* AP/NIC and USB/PCI */
	u8		Function;	/* Reserved for different FW function indcation, for further use when driver needs to download different FW in different conditions */
	u16		Version;		/* FW Version */
	u16		Subversion;	/* FW Subversion, default 0x00 */

	/* --- LONG WORD 1 ---- */
	u8		Month;	/* Release time Month field */
	u8		Date;	/* Release time Date field */
	u8		Hour;	/* Release time Hour field */
	u8		Minute;	/* Release time Minute field */
	u16		RamCodeSize;	/* The size of RAM code */
	u16		Rsvd2;

	/* --- LONG WORD 2 ---- */
	u32		SvnIdx;	/* The SVN entry index */
	u32		Rsvd3;

	/* --- LONG WORD 3 ---- */
	u32		Rsvd4;
	u32		Rsvd5;
} RT_8188F_FIRMWARE_HDR, *PRT_8188F_FIRMWARE_HDR;

#define DRIVER_EARLY_INT_TIME_8188F		0x05
#define BCN_DMA_ATIME_INT_TIME_8188F		0x02

/* for 8188F
 * TX 32K, RX 16K, Page size 128B for TX, 8B for RX */
#define PAGE_SIZE_TX_8188F			128
#define PAGE_SIZE_RX_8188F			8

#define RX_DMA_SIZE_8188F			0x4000	/* 16K */
#ifdef CONFIG_FW_C2H_DEBUG
	#define RX_DMA_RESERVED_SIZE_8188F	0x100	/* 256B, reserved for c2h debug message */
#else
	#define RX_DMA_RESERVED_SIZE_8188F	0x80	/* 128B, reserved for tx report */
#endif

#ifdef CONFIG_WOWLAN
	#define RESV_FMWF	(WKFMCAM_SIZE * MAX_WKFM_NUM) /* 16 entries, for each is 24 bytes*/
#else
	#define RESV_FMWF	0
#endif

#define RX_DMA_BOUNDARY_8188F		(RX_DMA_SIZE_8188F - RX_DMA_RESERVED_SIZE_8188F - 1)

/* Note: We will divide number of page equally for each queue other than public queue! */

/* For General Reserved Page Number(Beacon Queue is reserved page)
 * Beacon:2, PS-Poll:1, Null Data:1,Qos Null Data:1,BT Qos Null Data:1 */
#define BCNQ_PAGE_NUM_8188F		0x08
#ifdef CONFIG_CONCURRENT_MODE
	#define BCNQ1_PAGE_NUM_8188F		0x08 /* 0x04 */
#else
	#define BCNQ1_PAGE_NUM_8188F		0x00
#endif

#ifdef CONFIG_PNO_SUPPORT
	#undef BCNQ1_PAGE_NUM_8188F
	#define BCNQ1_PAGE_NUM_8188F		0x00 /* 0x04 */
#endif

/* For WoWLan , more reserved page
 * ARP Rsp:1, RWC:1, GTK Info:1,GTK RSP:2,GTK EXT MEM:2, PNO: 6 */
#ifdef CONFIG_WOWLAN
	#define WOWLAN_PAGE_NUM_8188F	0x07
#else
	#define WOWLAN_PAGE_NUM_8188F	0x00
#endif

#ifdef CONFIG_PNO_SUPPORT
	#undef WOWLAN_PAGE_NUM_8188F
	#define WOWLAN_PAGE_NUM_8188F	0x15
#endif

#ifdef CONFIG_AP_WOWLAN
	#define AP_WOWLAN_PAGE_NUM_8188F	0x02
#endif

#define TX_TOTAL_PAGE_NUMBER_8188F	(0xFF - BCNQ_PAGE_NUM_8188F - BCNQ1_PAGE_NUM_8188F - WOWLAN_PAGE_NUM_8188F)
#define TX_PAGE_BOUNDARY_8188F		(TX_TOTAL_PAGE_NUMBER_8188F + 1)

#define WMM_NORMAL_TX_TOTAL_PAGE_NUMBER_8188F	TX_TOTAL_PAGE_NUMBER_8188F
#define WMM_NORMAL_TX_PAGE_BOUNDARY_8188F		(WMM_NORMAL_TX_TOTAL_PAGE_NUMBER_8188F + 1)

/* For Normal Chip Setting
 * (HPQ + LPQ + NPQ + PUBQ) shall be TX_TOTAL_PAGE_NUMBER_8188F */
#define NORMAL_PAGE_NUM_HPQ_8188F		0x0C
#define NORMAL_PAGE_NUM_LPQ_8188F		0x02
#define NORMAL_PAGE_NUM_NPQ_8188F		0x02

/* Note: For Normal Chip Setting, modify later */
#define WMM_NORMAL_PAGE_NUM_HPQ_8188F		0x30
#define WMM_NORMAL_PAGE_NUM_LPQ_8188F		0x20
#define WMM_NORMAL_PAGE_NUM_NPQ_8188F		0x20


#include "HalVerDef.h"
#include "hal_com.h"

#define EFUSE_OOB_PROTECT_BYTES		15

#define HAL_EFUSE_MEMORY

#define HWSET_MAX_SIZE_8188F			512
#define EFUSE_REAL_CONTENT_LEN_8188F		512
#define EFUSE_MAP_LEN_8188F				512
#define EFUSE_MAX_SECTION_8188F			64

#define EFUSE_IC_ID_OFFSET			506	/* For some inferiority IC purpose. added by Roger, 2009.09.02. */
#define AVAILABLE_EFUSE_ADDR(addr)	(addr < EFUSE_REAL_CONTENT_LEN_8188F)

#define EFUSE_ACCESS_ON			0x69	/* For RTL8188 only. */
#define EFUSE_ACCESS_OFF			0x00	/* For RTL8188 only. */

/* ********************************************************
 *			EFUSE for BT definition
 * ******************************************************** */
#define EFUSE_BT_REAL_BANK_CONTENT_LEN	512
#define EFUSE_BT_REAL_CONTENT_LEN		1536	/* 512*3 */
#define EFUSE_BT_MAP_LEN				1024	/* 1k bytes */
#define EFUSE_BT_MAX_SECTION			128		/* 1024/8 */

#define EFUSE_PROTECT_BYTES_BANK		16

typedef struct _C2H_EVT_HDR {
	u8	CmdID;
	u8	CmdLen;
	u8	CmdSeq;
} __attribute__((__packed__)) C2H_EVT_HDR, *PC2H_EVT_HDR;

#define INCLUDE_MULTI_FUNC_BT(_Adapter)		(GET_HAL_DATA(_Adapter)->MultiFunc & RT_MULTI_FUNC_BT)
#define INCLUDE_MULTI_FUNC_GPS(_Adapter)	(GET_HAL_DATA(_Adapter)->MultiFunc & RT_MULTI_FUNC_GPS)

/* rtl8188a_hal_init.c */
s32 rtl8188f_FirmwareDownload(PADAPTER padapter, BOOLEAN  bUsedWoWLANFw);
void rtl8188f_FirmwareSelfReset(PADAPTER padapter);
void rtl8188f_InitializeFirmwareVars(PADAPTER padapter);

void rtl8188f_InitAntenna_Selection(PADAPTER padapter);
void rtl8188f_DeinitAntenna_Selection(PADAPTER padapter);
void rtl8188f_CheckAntenna_Selection(PADAPTER padapter);
void rtl8188f_init_default_value(PADAPTER padapter);

s32 rtl8188f_InitLLTTable(PADAPTER padapter);

s32 CardDisableHWSM(PADAPTER padapter, u8 resetMCU);
s32 CardDisableWithoutHWSM(PADAPTER padapter);

/* EFuse */
u8 GetEEPROMSize8188F(PADAPTER padapter);
void Hal_InitPGData(PADAPTER padapter, u8 *PROMContent);
void Hal_EfuseParseIDCode(PADAPTER padapter, u8 *hwinfo);
void Hal_EfuseParseTxPowerInfo_8188F(PADAPTER padapter, u8 *PROMContent, BOOLEAN AutoLoadFail);
/* void Hal_EfuseParseBTCoexistInfo_8188F(PADAPTER padapter, u8 *hwinfo, BOOLEAN AutoLoadFail); */
void Hal_EfuseParseEEPROMVer_8188F(PADAPTER padapter, u8 *hwinfo, BOOLEAN AutoLoadFail);
void Hal_EfuseParseChnlPlan_8188F(PADAPTER padapter, u8 *hwinfo, BOOLEAN AutoLoadFail);
void Hal_EfuseParseCustomerID_8188F(PADAPTER padapter, u8 *hwinfo, BOOLEAN AutoLoadFail);
void Hal_EfuseParsePowerSavingMode_8188F(PADAPTER pAdapter, u8 *hwinfo, BOOLEAN AutoLoadFail);
void Hal_EfuseParseAntennaDiversity_8188F(PADAPTER padapter, u8 *hwinfo, BOOLEAN AutoLoadFail);
void Hal_EfuseParseXtal_8188F(PADAPTER pAdapter, u8 *hwinfo, u8 AutoLoadFail);
void Hal_EfuseParseThermalMeter_8188F(PADAPTER padapter, u8 *hwinfo, u8 AutoLoadFail);
void Hal_EfuseParseKFreeData_8188F(PADAPTER pAdapter, u8 *hwinfo, BOOLEAN AutoLoadFail);

#if 0 /* Do not need for rtl8188f */
	VOID Hal_EfuseParseVoltage_8188F(PADAPTER pAdapter, u8 *hwinfo, BOOLEAN	AutoLoadFail);
#endif

#ifdef CONFIG_C2H_PACKET_EN
	void rtl8188f_c2h_packet_handler(PADAPTER padapter, u8 *pbuf, u16 length);
#endif

void rtl8188f_set_pll_ref_clk_sel(_adapter *adapter, u8 sel);

void rtl8188f_set_hal_ops(struct hal_ops *pHalFunc);
void init_hal_spec_8188f(_adapter *adapter);
void SetHwReg8188F(PADAPTER padapter, u8 variable, u8 *val);
void GetHwReg8188F(PADAPTER padapter, u8 variable, u8 *val);
#ifdef CONFIG_C2H_PACKET_EN
	void SetHwRegWithBuf8188F(PADAPTER padapter, u8 variable, u8 *pbuf, int len);
#endif /* CONFIG_C2H_PACKET_EN */
u8 SetHalDefVar8188F(PADAPTER padapter, HAL_DEF_VARIABLE variable, void *pval);
u8 GetHalDefVar8188F(PADAPTER padapter, HAL_DEF_VARIABLE variable, void *pval);

/* register */
void rtl8188f_InitBeaconParameters(PADAPTER padapter);
void rtl8188f_InitBeaconMaxError(PADAPTER padapter, u8 InfraMode);
void	_InitBurstPktLen_8188FS(PADAPTER Adapter);
void _8051Reset8188(PADAPTER padapter);
#ifdef CONFIG_WOWLAN
	void Hal_DetectWoWMode(PADAPTER pAdapter);
#endif /* CONFIG_WOWLAN */

void rtl8188f_start_thread(_adapter *padapter);
void rtl8188f_stop_thread(_adapter *padapter);

#if defined(CONFIG_CHECK_BT_HANG) && defined(CONFIG_BT_COEXIST)
	void rtl8188fs_init_checkbthang_workqueue(_adapter *adapter);
	void rtl8188fs_free_checkbthang_workqueue(_adapter *adapter);
	void rtl8188fs_cancle_checkbthang_workqueue(_adapter *adapter);
	void rtl8188fs_hal_check_bt_hang(_adapter *adapter);
#endif

#ifdef CONFIG_GPIO_WAKEUP
	void HalSetOutPutGPIO(PADAPTER padapter, u8 index, u8 OutPutValue);
#endif

int FirmwareDownloadBT(IN PADAPTER Adapter, PRT_MP_FIRMWARE pFirmware);

void CCX_FwC2HTxRpt_8188f(PADAPTER padapter, u8 *pdata, u8 len);
#ifdef CONFIG_FW_C2H_DEBUG
	void Debug_FwC2H_8188f(PADAPTER padapter, u8 *pdata, u8 len);
#endif /* CONFIG_FW_C2H_DEBUG */
s32 c2h_id_filter_ccx_8188f(u8 *buf);
s32 c2h_handler_8188f(PADAPTER padapter, u8 *pC2hEvent);
u8 MRateToHwRate8188F(u8  rate);
u8 HwRateToMRate8188F(u8	 rate);

#ifdef CONFIG_PCI_HCI
	BOOLEAN	InterruptRecognized8188FE(PADAPTER Adapter);
	VOID	UpdateInterruptMask8188FE(PADAPTER Adapter, u32 AddMSR, u32 AddMSR1, u32 RemoveMSR, u32 RemoveMSR1);
#endif

#endif
