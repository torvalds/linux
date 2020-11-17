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
#ifndef __RTL8192F_HAL_H__
#define __RTL8192F_HAL_H__

#include "hal_data.h"

#include "rtl8192f_spec.h"
#include "rtl8192f_rf.h"
#include "rtl8192f_dm.h"
#include "rtl8192f_recv.h"
#include "rtl8192f_xmit.h"
#include "rtl8192f_cmd.h"
#include "rtl8192f_led.h"
#include "Hal8192FPwrSeq.h"
#include "Hal8192FPhyReg.h"
#include "Hal8192FPhyCfg.h"
#ifdef DBG_CONFIG_ERROR_DETECT
#include "rtl8192f_sreset.h"
#endif
#ifdef CONFIG_LPS_POFF
	#include "rtl8192f_lps_poff.h"
#endif

#define FW_8192F_SIZE		0x8000
#define FW_8192F_START_ADDRESS	0x4000
#define FW_8192F_END_ADDRESS	0x5000 /* brian_zhang@realsil.com.cn */

#define IS_FW_HEADER_EXIST_8192F(_pFwHdr)\
	((le16_to_cpu(_pFwHdr->Signature) & 0xFFF0) == 0x92F0)

typedef struct _RT_FIRMWARE {
	FIRMWARE_SOURCE	eFWSource;
#ifdef CONFIG_EMBEDDED_FWIMG
	u8			*szFwBuffer;
#else
	u8			szFwBuffer[FW_8192F_SIZE];
#endif
	u32			ulFwLength;
} RT_FIRMWARE_8192F, *PRT_FIRMWARE_8192F;

/*
 * This structure must be cared byte-ordering
 *
 * Added by tynli. 2009.12.04. */
typedef struct _RT_8192F_FIRMWARE_HDR {
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
} RT_8192F_FIRMWARE_HDR, *PRT_8192F_FIRMWARE_HDR;
#define DRIVER_EARLY_INT_TIME_8192F		0x05
#define BCN_DMA_ATIME_INT_TIME_8192F		0x02
/* for 8192F
 * TX 64K, RX 16K, Page size 256B for TX*/
#define PAGE_SIZE_TX_8192F			256
#define PAGE_SIZE_RX_8192F			8
#define TX_DMA_SIZE_8192F			0x10000/* 64K(TX) */
#define RX_DMA_SIZE_8192F			0x4000/* 16K(RX) */
#ifdef CONFIG_WOWLAN
	#define RESV_FMWF	(WKFMCAM_SIZE * MAX_WKFM_CAM_NUM) /* 16 entries, for each is 24 bytes*/
#else
	#define RESV_FMWF	0
#endif

#ifdef CONFIG_FW_C2H_DEBUG
	#define RX_DMA_RESERVED_SIZE_8192F	0x100	/* 256B, reserved for c2h debug message */
#else
	#define RX_DMA_RESERVED_SIZE_8192F	0xc0	/* 192B, reserved for tx report 24*8=192*/
#endif
#define RX_DMA_BOUNDARY_8192F\
	(RX_DMA_SIZE_8192F - RX_DMA_RESERVED_SIZE_8192F - 1)


/* Note: We will divide number of page equally for each queue other than public queue! */

/* For General Reserved Page Number(Beacon Queue is reserved page)
 * Beacon:MAX_BEACON_LEN/PAGE_SIZE_TX_8192F
 * PS-Poll:1, Null Data:1,Qos Null Data:1,BT Qos Null Data:1,CTS-2-SELF,LTE QoS Null*/
#define BCNQ_PAGE_NUM_8192F		(MAX_BEACON_LEN/PAGE_SIZE_TX_8192F + 6) /*0x08*/


/* For WoWLan , more reserved page
 * ARP Rsp:1, RWC:1, GTK Info:1,GTK RSP:2,GTK EXT MEM:2, AOAC rpt 1, PNO: 6
 * NS offload: 2 NDP info: 1
 */
#ifdef CONFIG_WOWLAN
	#define WOWLAN_PAGE_NUM_8192F	0x07
#else
	#define WOWLAN_PAGE_NUM_8192F	0x00
#endif

#ifdef CONFIG_PNO_SUPPORT
	#undef WOWLAN_PAGE_NUM_8192F
	#define WOWLAN_PAGE_NUM_8192F	0x15
#endif

#ifdef CONFIG_AP_WOWLAN
	#define AP_WOWLAN_PAGE_NUM_8192F	0x02
#endif

#ifdef DBG_LA_MODE
	#define LA_MODE_PAGE_NUM 0xE0
#endif

#define MAX_RX_DMA_BUFFER_SIZE_8192F	(RX_DMA_SIZE_8192F - RX_DMA_RESERVED_SIZE_8192F)

#ifdef DBG_LA_MODE
	#define TX_TOTAL_PAGE_NUMBER_8192F	(0xFF - LA_MODE_PAGE_NUM)
#else
	#define TX_TOTAL_PAGE_NUMBER_8192F	(0xFF - BCNQ_PAGE_NUM_8192F - WOWLAN_PAGE_NUM_8192F)
#endif

#define TX_PAGE_BOUNDARY_8192F		(TX_TOTAL_PAGE_NUMBER_8192F + 1)

#define WMM_NORMAL_TX_TOTAL_PAGE_NUMBER_8192F \
	TX_TOTAL_PAGE_NUMBER_8192F
#define WMM_NORMAL_TX_PAGE_BOUNDARY_8192F \
	(WMM_NORMAL_TX_TOTAL_PAGE_NUMBER_8192F + 1)

/* For Normal Chip Setting
 * (HPQ + LPQ + NPQ + PUBQ) shall be TX_TOTAL_PAGE_NUMBER_8192F */
#define NORMAL_PAGE_NUM_HPQ_8192F		0x8
#define NORMAL_PAGE_NUM_LPQ_8192F		0x8
#define NORMAL_PAGE_NUM_NPQ_8192F		0x8
#define NORMAL_PAGE_NUM_EPQ_8192F		0x00

/* Note: For Normal Chip Setting, modify later */
#define WMM_NORMAL_PAGE_NUM_HPQ_8192F		0x30
#define WMM_NORMAL_PAGE_NUM_LPQ_8192F		0x20
#define WMM_NORMAL_PAGE_NUM_NPQ_8192F		0x20
#define WMM_NORMAL_PAGE_NUM_EPQ_8192F		0x00


#include "HalVerDef.h"
#include "hal_com.h"

#define EFUSE_OOB_PROTECT_BYTES 56 /*0x1C8~0x1FF*/

#define HAL_EFUSE_MEMORY
#define HWSET_MAX_SIZE_8192F                512
#define EFUSE_REAL_CONTENT_LEN_8192F        512
#define EFUSE_MAP_LEN_8192F                 512
#define EFUSE_MAX_SECTION_8192F            64

/* For some inferiority IC purpose. added by Roger, 2009.09.02.*/
#define EFUSE_IC_ID_OFFSET			506
#define AVAILABLE_EFUSE_ADDR(addr)	(addr < EFUSE_REAL_CONTENT_LEN_8192F)

#define EFUSE_ACCESS_ON		0x69
#define EFUSE_ACCESS_OFF	0x00

/* ********************************************************
 *			EFUSE for BT definition
 * ******************************************************** */
#define BANK_NUM			1
#define EFUSE_BT_REAL_BANK_CONTENT_LEN	512
#define EFUSE_BT_REAL_CONTENT_LEN	1536/*512 * 3 */
/*	(EFUSE_BT_REAL_BANK_CONTENT_LEN * BANK_NUM)*/
#define EFUSE_BT_MAP_LEN		1024	/* 1k bytes */
#define EFUSE_BT_MAX_SECTION		128 /* 1024/8 */
#define EFUSE_PROTECT_BYTES_BANK	16

typedef enum tag_Package_Definition {
	PACKAGE_DEFAULT,
	PACKAGE_QFN32,
	PACKAGE_QFN40,
	PACKAGE_QFN46
} PACKAGE_TYPE_E;

#define INCLUDE_MULTI_FUNC_BT(_Adapter) \
	(GET_HAL_DATA(_Adapter)->MultiFunc & RT_MULTI_FUNC_BT)
#define INCLUDE_MULTI_FUNC_GPS(_Adapter) \
	(GET_HAL_DATA(_Adapter)->MultiFunc & RT_MULTI_FUNC_GPS)

#ifdef CONFIG_FILE_FWIMG
	extern char *rtw_fw_file_path;
	extern char *rtw_fw_wow_file_path;
	#ifdef CONFIG_MP_INCLUDED
		extern char *rtw_fw_mp_bt_file_path;
	#endif /* CONFIG_MP_INCLUDED */
#endif /* CONFIG_FILE_FWIMG */

/* rtl8192f_hal_init.c */
s32 rtl8192f_FirmwareDownload(PADAPTER padapter, BOOLEAN  bUsedWoWLANFw);
void rtl8192f_FirmwareSelfReset(PADAPTER padapter);
void rtl8192f_InitializeFirmwareVars(PADAPTER padapter);

void rtl8192f_InitAntenna_Selection(PADAPTER padapter);
void rtl8192f_DeinitAntenna_Selection(PADAPTER padapter);
void rtl8192f_CheckAntenna_Selection(PADAPTER padapter);
void rtl8192f_init_default_value(PADAPTER padapter);

s32 rtl8192f_InitLLTTable(PADAPTER padapter);

s32 CardDisableHWSM(PADAPTER padapter, u8 resetMCU);
s32 CardDisableWithoutHWSM(PADAPTER padapter);

/* EFuse */
u8 GetEEPROMSize8192F(PADAPTER padapter);
void Hal_InitPGData(PADAPTER padapter, u8 *PROMContent);
void Hal_EfuseParseIDCode(PADAPTER padapter, u8 *hwinfo);
void Hal_EfuseParseTxPowerInfo_8192F(PADAPTER padapter,
					u8 *PROMContent, BOOLEAN AutoLoadFail);
/*
void Hal_EfuseParseBTCoexistInfo_8192F(PADAPTER padapter,
				       u8 *hwinfo, BOOLEAN AutoLoadFail);
*/
void Hal_EfuseParseEEPROMVer_8192F(PADAPTER padapter,
				   u8 *hwinfo, BOOLEAN AutoLoadFail);
void Hal_EfuseParseChnlPlan_8192F(PADAPTER padapter,
				  u8 *hwinfo, BOOLEAN AutoLoadFail);
void Hal_EfuseParseCustomerID_8192F(PADAPTER padapter,
				    u8 *hwinfo, BOOLEAN AutoLoadFail);
void Hal_EfuseParseAntennaDiversity_8192F(PADAPTER padapter,
		u8 *hwinfo, BOOLEAN AutoLoadFail);
void Hal_EfuseParseXtal_8192F(PADAPTER pAdapter,
			      u8 *hwinfo, u8 AutoLoadFail);
void Hal_EfuseParseThermalMeter_8192F(PADAPTER padapter,
				      u8 *hwinfo, u8 AutoLoadFail);
void Hal_EfuseParseVoltage_8192F(PADAPTER pAdapter,
				 u8 *hwinfo, BOOLEAN	AutoLoadFail);
void Hal_EfuseParseBoardType_8192F(PADAPTER Adapter,
				   u8	*PROMContent, BOOLEAN AutoloadFail);
u8	Hal_ReadRFEType_8192F(PADAPTER Adapter, u8 *PROMContent, BOOLEAN AutoloadFail);
void rtl8192f_set_hal_ops(struct hal_ops *pHalFunc);
void init_hal_spec_8192f(_adapter *adapter);
u8 SetHwReg8192F(PADAPTER padapter, u8 variable, u8 *val);
void GetHwReg8192F(PADAPTER padapter, u8 variable, u8 *val);
u8 SetHalDefVar8192F(PADAPTER padapter, HAL_DEF_VARIABLE variable, void *pval);
u8 GetHalDefVar8192F(PADAPTER padapter, HAL_DEF_VARIABLE variable, void *pval);

/* register */
void rtl8192f_InitBeaconParameters(PADAPTER padapter);
void rtl8192f_InitBeaconMaxError(PADAPTER padapter, u8 InfraMode);

void _InitMacAPLLSetting_8192F(PADAPTER Adapter);
void _8051Reset8192F(PADAPTER padapter);
#ifdef CONFIG_WOWLAN
	void Hal_DetectWoWMode(PADAPTER pAdapter);
#endif /* CONFIG_WOWLAN */

void rtl8192f_start_thread(_adapter *padapter);
void rtl8192f_stop_thread(_adapter *padapter);

#if defined(CONFIG_CHECK_BT_HANG) && defined(CONFIG_BT_COEXIST)
	void rtl8192fs_init_checkbthang_workqueue(_adapter *adapter);
	void rtl8192fs_free_checkbthang_workqueue(_adapter *adapter);
	void rtl8192fs_cancle_checkbthang_workqueue(_adapter *adapter);
	void rtl8192fs_hal_check_bt_hang(_adapter *adapter);
#endif

#ifdef CONFIG_GPIO_WAKEUP
	void HalSetOutPutGPIO(PADAPTER padapter, u8 index, u8 OutPutValue);
#endif
#ifdef CONFIG_MP_INCLUDED
int FirmwareDownloadBT(PADAPTER Adapter, PRT_MP_FIRMWARE pFirmware);
#endif
void CCX_FwC2HTxRpt_8192f(PADAPTER padapter, u8 *pdata, u8 len);

u8 MRateToHwRate8192F(u8 rate);
u8 HwRateToMRate8192F(u8 rate);

#if defined(CONFIG_CHECK_BT_HANG) && defined(CONFIG_BT_COEXIST)
	void check_bt_status_work(void *data);
#endif


void rtl8192f_cal_txdesc_chksum(struct tx_desc *ptxdesc);

#ifdef CONFIG_AMPDU_PRETX_CD
void rtl8192f_pretx_cd_config(_adapter *adapter);
#endif

#ifdef CONFIG_PCI_HCI
	BOOLEAN	InterruptRecognized8192FE(PADAPTER Adapter);
	void	UpdateInterruptMask8192FE(PADAPTER Adapter, u32 AddMSR, u32 AddMSR1, u32 RemoveMSR, u32 RemoveMSR1);
	void InitMAC_TRXBD_8192FE(PADAPTER Adapter);

	u16 get_txbd_rw_reg(u16 ff_hwaddr);
#endif

#endif
