/* SPDX-License-Identifier: GPL-2.0 */
/******************************************************************************
 *
 * Copyright(c) 2007 - 2017 Realtek Corporation.
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
 *****************************************************************************/
#ifndef __RTL8703B_HAL_H__
#define __RTL8703B_HAL_H__

#include "hal_data.h"

#include "rtl8703b_spec.h"
#include "rtl8703b_rf.h"
#include "rtl8703b_dm.h"
#include "rtl8703b_recv.h"
#include "rtl8703b_xmit.h"
#include "rtl8703b_cmd.h"
#include "rtl8703b_led.h"
#include "Hal8703BPwrSeq.h"
#include "Hal8703BPhyReg.h"
#include "Hal8703BPhyCfg.h"
#ifdef DBG_CONFIG_ERROR_DETECT
	#include "rtl8703b_sreset.h"
#endif

#define FW_8703B_SIZE			0x8000
#define FW_8703B_START_ADDRESS	0x1000
#define FW_8703B_END_ADDRESS		0x1FFF /* 0x5FFF */

#define IS_FW_HEADER_EXIST_8703B(_pFwHdr)	((le16_to_cpu(_pFwHdr->Signature) & 0xFFF0) == 0x03B0)

typedef struct _RT_FIRMWARE {
	FIRMWARE_SOURCE	eFWSource;
#ifdef CONFIG_EMBEDDED_FWIMG
	u8			*szFwBuffer;
#else
	u8			szFwBuffer[FW_8703B_SIZE];
#endif
	u32			ulFwLength;
} RT_FIRMWARE_8703B, *PRT_FIRMWARE_8703B;

/*
 * This structure must be cared byte-ordering
 *
 * Added by tynli. 2009.12.04. */
typedef struct _RT_8703B_FIRMWARE_HDR {
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
} RT_8703B_FIRMWARE_HDR, *PRT_8703B_FIRMWARE_HDR;

#define DRIVER_EARLY_INT_TIME_8703B		0x05
#define BCN_DMA_ATIME_INT_TIME_8703B		0x02

/* for 8703B
 * TX 32K, RX 16K, Page size 128B for TX, 8B for RX */
#define PAGE_SIZE_TX_8703B			128
#define PAGE_SIZE_RX_8703B			8

#define TX_DMA_SIZE_8703B			0x8000	/* 32K(TX) */
#define RX_DMA_SIZE_8703B			0x4000	/* 16K(RX) */

#ifdef CONFIG_WOWLAN
	#define RESV_FMWF	(WKFMCAM_SIZE * MAX_WKFM_CAM_NUM) /* 16 entries, for each is 24 bytes*/
#else
	#define RESV_FMWF	0
#endif

#ifdef CONFIG_FW_C2H_DEBUG
	#define RX_DMA_RESERVED_SIZE_8703B	0x100	/* 256B, reserved for c2h debug message */
#else
	#define RX_DMA_RESERVED_SIZE_8703B	0x80	/* 128B, reserved for tx report */
#endif
#define RX_DMA_BOUNDARY_8703B		(RX_DMA_SIZE_8703B - RX_DMA_RESERVED_SIZE_8703B - 1)


/* Note: We will divide number of page equally for each queue other than public queue! */

/* For General Reserved Page Number(Beacon Queue is reserved page)
 * Beacon:MAX_BEACON_LEN/PAGE_SIZE_TX_8703B
 * PS-Poll:1, Null Data:1,Qos Null Data:1,BT Qos Null Data:1,CTS-2-SELF,LTE QoS Null*/

#define BCNQ_PAGE_NUM_8703B		(MAX_BEACON_LEN/PAGE_SIZE_TX_8703B + 6) /*0x08*/

/* For WoWLan , more reserved page
 * ARP Rsp:1, RWC:1, GTK Info:1,GTK RSP:2,GTK EXT MEM:2, AOAC rpt: 1 PNO: 6
 * NS offload: 2NDP info: 1
 */
#ifdef CONFIG_WOWLAN
	#define WOWLAN_PAGE_NUM_8703B	0x0b
#else
	#define WOWLAN_PAGE_NUM_8703B	0x00
#endif

#ifdef CONFIG_PNO_SUPPORT
	#undef WOWLAN_PAGE_NUM_8703B
	#define WOWLAN_PAGE_NUM_8703B	0x15
#endif

#ifdef CONFIG_AP_WOWLAN
	#define AP_WOWLAN_PAGE_NUM_8703B	0x02
#endif

#define TX_TOTAL_PAGE_NUMBER_8703B	(0xFF - BCNQ_PAGE_NUM_8703B - WOWLAN_PAGE_NUM_8703B)
#define TX_PAGE_BOUNDARY_8703B		(TX_TOTAL_PAGE_NUMBER_8703B + 1)

#define WMM_NORMAL_TX_TOTAL_PAGE_NUMBER_8703B	TX_TOTAL_PAGE_NUMBER_8703B
#define WMM_NORMAL_TX_PAGE_BOUNDARY_8703B		(WMM_NORMAL_TX_TOTAL_PAGE_NUMBER_8703B + 1)

/* For Normal Chip Setting
 * (HPQ + LPQ + NPQ + PUBQ) shall be TX_TOTAL_PAGE_NUMBER_8703B */
#define NORMAL_PAGE_NUM_HPQ_8703B		0x0C
#define NORMAL_PAGE_NUM_LPQ_8703B		0x02
#define NORMAL_PAGE_NUM_NPQ_8703B		0x02

/* Note: For Normal Chip Setting, modify later */
#define WMM_NORMAL_PAGE_NUM_HPQ_8703B		0x30
#define WMM_NORMAL_PAGE_NUM_LPQ_8703B		0x20
#define WMM_NORMAL_PAGE_NUM_NPQ_8703B		0x20


#include "HalVerDef.h"
#include "hal_com.h"

#define EFUSE_OOB_PROTECT_BYTES		15

#define HAL_EFUSE_MEMORY

#define HWSET_MAX_SIZE_8703B			256
#define EFUSE_REAL_CONTENT_LEN_8703B		256
#define EFUSE_MAP_LEN_8703B				512
#define EFUSE_MAX_SECTION_8703B			64

#define EFUSE_IC_ID_OFFSET			506	/* For some inferiority IC purpose. added by Roger, 2009.09.02. */
#define AVAILABLE_EFUSE_ADDR(addr)	(addr < EFUSE_REAL_CONTENT_LEN_8703B)

#define EFUSE_ACCESS_ON			0x69
#define EFUSE_ACCESS_OFF			0x00

/* ********************************************************
 *			EFUSE for BT definition
 * ******************************************************** */
#define BANK_NUM		1
#define EFUSE_BT_REAL_BANK_CONTENT_LEN	128
#define EFUSE_BT_REAL_CONTENT_LEN		(EFUSE_BT_REAL_BANK_CONTENT_LEN * BANK_NUM)
#define EFUSE_BT_MAP_LEN				1024	/* 1k bytes */
#define EFUSE_BT_MAX_SECTION			(EFUSE_BT_MAP_LEN / 8)
#define EFUSE_PROTECT_BYTES_BANK		16

typedef enum tag_Package_Definition {
	PACKAGE_DEFAULT,
	PACKAGE_QFN68,
	PACKAGE_TFBGA90,
	PACKAGE_TFBGA80,
	PACKAGE_TFBGA79
} PACKAGE_TYPE_E;

#define INCLUDE_MULTI_FUNC_BT(_Adapter)		(GET_HAL_DATA(_Adapter)->MultiFunc & RT_MULTI_FUNC_BT)
#define INCLUDE_MULTI_FUNC_GPS(_Adapter)	(GET_HAL_DATA(_Adapter)->MultiFunc & RT_MULTI_FUNC_GPS)

/* rtl8703b_hal_init.c */
s32 rtl8703b_FirmwareDownload(PADAPTER padapter, BOOLEAN  bUsedWoWLANFw);
void rtl8703b_FirmwareSelfReset(PADAPTER padapter);
void rtl8703b_InitializeFirmwareVars(PADAPTER padapter);

void rtl8703b_InitAntenna_Selection(PADAPTER padapter);
void rtl8703b_DeinitAntenna_Selection(PADAPTER padapter);
void rtl8703b_CheckAntenna_Selection(PADAPTER padapter);
void rtl8703b_init_default_value(PADAPTER padapter);

s32 rtl8703b_InitLLTTable(PADAPTER padapter);

s32 CardDisableHWSM(PADAPTER padapter, u8 resetMCU);
s32 CardDisableWithoutHWSM(PADAPTER padapter);

/* EFuse */
u8 GetEEPROMSize8703B(PADAPTER padapter);
void Hal_InitPGData(PADAPTER padapter, u8 *PROMContent);
void Hal_EfuseParseIDCode(PADAPTER padapter, u8 *hwinfo);
void Hal_EfuseParseTxPowerInfo_8703B(PADAPTER padapter, u8 *PROMContent, BOOLEAN AutoLoadFail);
void Hal_EfuseParseBTCoexistInfo_8703B(PADAPTER padapter, u8 *hwinfo, BOOLEAN AutoLoadFail);
void Hal_EfuseParseEEPROMVer_8703B(PADAPTER padapter, u8 *hwinfo, BOOLEAN AutoLoadFail);
void Hal_EfuseParseChnlPlan_8703B(PADAPTER padapter, u8 *hwinfo, BOOLEAN AutoLoadFail);
void Hal_EfuseParseCustomerID_8703B(PADAPTER padapter, u8 *hwinfo, BOOLEAN AutoLoadFail);
void Hal_EfuseParseAntennaDiversity_8703B(PADAPTER padapter, u8 *hwinfo, BOOLEAN AutoLoadFail);
void Hal_EfuseParseXtal_8703B(PADAPTER pAdapter, u8 *hwinfo, u8 AutoLoadFail);
void Hal_EfuseParseThermalMeter_8703B(PADAPTER padapter, u8 *hwinfo, u8 AutoLoadFail);
void Hal_EfuseParseVoltage_8703B(PADAPTER pAdapter, u8 *hwinfo, BOOLEAN	AutoLoadFail);
void Hal_EfuseParseBoardType_8703B(PADAPTER Adapter,	u8	*PROMContent, BOOLEAN AutoloadFail);

void rtl8703b_set_hal_ops(struct hal_ops *pHalFunc);
void init_hal_spec_8703b(_adapter *adapter);
u8 SetHwReg8703B(PADAPTER padapter, u8 variable, u8 *val);
void GetHwReg8703B(PADAPTER padapter, u8 variable, u8 *val);
u8 SetHalDefVar8703B(PADAPTER padapter, HAL_DEF_VARIABLE variable, void *pval);
u8 GetHalDefVar8703B(PADAPTER padapter, HAL_DEF_VARIABLE variable, void *pval);

/* register */
void rtl8703b_InitBeaconParameters(PADAPTER padapter);
void rtl8703b_InitBeaconMaxError(PADAPTER padapter, u8 InfraMode);
void	_InitBurstPktLen_8703BS(PADAPTER Adapter);
void _InitLTECoex_8703BS(PADAPTER Adapter);
void _InitMacAPLLSetting_8703B(PADAPTER Adapter);
void _8051Reset8703(PADAPTER padapter);
#ifdef CONFIG_WOWLAN
	void Hal_DetectWoWMode(PADAPTER pAdapter);
#endif /* CONFIG_WOWLAN */

void rtl8703b_start_thread(_adapter *padapter);
void rtl8703b_stop_thread(_adapter *padapter);

#if defined(CONFIG_CHECK_BT_HANG) && defined(CONFIG_BT_COEXIST)
	void rtl8703bs_init_checkbthang_workqueue(_adapter *adapter);
	void rtl8703bs_free_checkbthang_workqueue(_adapter *adapter);
	void rtl8703bs_cancle_checkbthang_workqueue(_adapter *adapter);
	void rtl8703bs_hal_check_bt_hang(_adapter *adapter);
#endif

#ifdef CONFIG_GPIO_WAKEUP
	void HalSetOutPutGPIO(PADAPTER padapter, u8 index, u8 OutPutValue);
#endif
#ifdef CONFIG_MP_INCLUDED
int FirmwareDownloadBT(PADAPTER Adapter, PRT_MP_FIRMWARE pFirmware);
#endif
void CCX_FwC2HTxRpt_8703b(PADAPTER padapter, u8 *pdata, u8 len);

u8 MRateToHwRate8703B(u8  rate);
u8 HwRateToMRate8703B(u8	 rate);

void Hal_ReadRFGainOffset(PADAPTER pAdapter, u8 *hwinfo, BOOLEAN AutoLoadFail);

#ifdef CONFIG_PCI_HCI
	BOOLEAN	InterruptRecognized8703BE(PADAPTER Adapter);
	void	UpdateInterruptMask8703BE(PADAPTER Adapter, u32 AddMSR, u32 AddMSR1, u32 RemoveMSR, u32 RemoveMSR1);
#endif

#endif
