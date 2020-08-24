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
#ifndef __RTL8710B_HAL_H__
#define __RTL8710B_HAL_H__

#include "hal_data.h"

#include "rtl8710b_spec.h"
#include "rtl8710b_rf.h"
#include "rtl8710b_dm.h"
#include "rtl8710b_recv.h"
#include "rtl8710b_xmit.h"
#include "rtl8710b_cmd.h"
#include "rtl8710b_led.h"
#include "Hal8710BPwrSeq.h"
#include "Hal8710BPhyReg.h"
#include "Hal8710BPhyCfg.h"
#ifdef DBG_CONFIG_ERROR_DETECT
	#include "rtl8710b_sreset.h"
#endif
#ifdef CONFIG_LPS_POFF
	#include "rtl8710b_lps_poff.h"
#endif

#define FW_8710B_SIZE		0x8000
#define FW_8710B_START_ADDRESS	0x1000
#define FW_8710B_END_ADDRESS	0x1FFF /* 0x5FFF */

typedef struct _RT_FIRMWARE {
	FIRMWARE_SOURCE	eFWSource;
#ifdef CONFIG_EMBEDDED_FWIMG
	u8			*szFwBuffer;
#else
	u8			szFwBuffer[FW_8710B_SIZE];
#endif
	u32			ulFwLength;
} RT_FIRMWARE_8710B, *PRT_FIRMWARE_8710B;

/*
 * This structure must be cared byte-ordering
 *
 * Added by tynli. 2009.12.04. */
typedef struct _RT_8710B_FIRMWARE_HDR {
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
} RT_8710B_FIRMWARE_HDR, *PRT_8710B_FIRMWARE_HDR;

#define DRIVER_EARLY_INT_TIME_8710B		0x05
#define BCN_DMA_ATIME_INT_TIME_8710B		0x02

/* for 8710B
 * TX 32K, RX 16K, Page size 128B for TX, 8B for RX */
#define PAGE_SIZE_TX_8710B			128
#define PAGE_SIZE_RX_8710B			8

#define TX_DMA_SIZE_8710B			0x8000	/* 32K(TX) */
#define RX_DMA_SIZE_8710B			0x4000	/* 16K(RX) */

#ifdef CONFIG_WOWLAN
	#define RESV_FMWF	(WKFMCAM_SIZE * MAX_WKFM_CAM_NUM) /* 16 entries, for each is 24 bytes*/
#else
	#define RESV_FMWF	0
#endif

#ifdef CONFIG_FW_C2H_DEBUG
	#define RX_DMA_RESERVED_SIZE_8710B	0x100	/* 256B, reserved for c2h debug message */
#else
	#define RX_DMA_RESERVED_SIZE_8710B	0x80	/* 128B, reserved for tx report */
#endif
#define RX_DMA_BOUNDARY_8710B\
	(RX_DMA_SIZE_8710B - RX_DMA_RESERVED_SIZE_8710B - 1)


/* Note: We will divide number of page equally for each queue other than public queue! */

/* For General Reserved Page Number(Beacon Queue is reserved page)
 * Beacon:MAX_BEACON_LEN/PAGE_SIZE_TX_8710B
 * PS-Poll:1, Null Data:1,Qos Null Data:1,BT Qos Null Data:1,CTS-2-SELF,LTE QoS Null*/
#define BCNQ_PAGE_NUM_8710B	(MAX_BEACON_LEN/PAGE_SIZE_TX_8710B + 6) /*0x08*/


/* For WoWLan , more reserved page
 * ARP Rsp:1, RWC:1, GTK Info:1,GTK RSP:2,GTK EXT MEM:2, AOAC rpt 1, PNO: 6
 * NS offload: 2 NDP info: 1
 */
#ifdef CONFIG_WOWLAN
	#define WOWLAN_PAGE_NUM_8710B	0x0b
#else
	#define WOWLAN_PAGE_NUM_8710B	0x00
#endif

#ifdef CONFIG_PNO_SUPPORT
	#undef WOWLAN_PAGE_NUM_8710B
	#define WOWLAN_PAGE_NUM_8710B	0x15
#endif

#ifdef CONFIG_AP_WOWLAN
	#define AP_WOWLAN_PAGE_NUM_8710B	0x02
#endif

#define TX_TOTAL_PAGE_NUMBER_8710B\
	(0xFF - BCNQ_PAGE_NUM_8710B -WOWLAN_PAGE_NUM_8710B)
#define TX_PAGE_BOUNDARY_8710B		(TX_TOTAL_PAGE_NUMBER_8710B + 1)

#define WMM_NORMAL_TX_TOTAL_PAGE_NUMBER_8710B	TX_TOTAL_PAGE_NUMBER_8710B
#define WMM_NORMAL_TX_PAGE_BOUNDARY_8710B\
	(WMM_NORMAL_TX_TOTAL_PAGE_NUMBER_8710B + 1)

/* For Normal Chip Setting
 * (HPQ + LPQ + NPQ + PUBQ) shall be TX_TOTAL_PAGE_NUMBER_8710B */
#define NORMAL_PAGE_NUM_HPQ_8710B		0x0C
#define NORMAL_PAGE_NUM_LPQ_8710B		0x02
#define NORMAL_PAGE_NUM_NPQ_8710B		0x02
#define NORMAL_PAGE_NUM_EPQ_8710B		0x04

/* Note: For Normal Chip Setting, modify later */
#define WMM_NORMAL_PAGE_NUM_HPQ_8710B		0x30
#define WMM_NORMAL_PAGE_NUM_LPQ_8710B		0x20
#define WMM_NORMAL_PAGE_NUM_NPQ_8710B		0x20
#define WMM_NORMAL_PAGE_NUM_EPQ_8710B		0x00


#include "HalVerDef.h"
#include "hal_com.h"

#define EFUSE_OOB_PROTECT_BYTES (96 + 1)

#define HAL_EFUSE_MEMORY
#define HWSET_MAX_SIZE_8710B                512
#define EFUSE_REAL_CONTENT_LEN_8710B        512
#define EFUSE_MAP_LEN_8710B                 512
#define EFUSE_MAX_SECTION_8710B             64

/* For some inferiority IC purpose. added by Roger, 2009.09.02.*/
#define EFUSE_IC_ID_OFFSET			506
#define AVAILABLE_EFUSE_ADDR(addr)	(addr < EFUSE_REAL_CONTENT_LEN_8710B)

#define EFUSE_ACCESS_ON	0x69
#define EFUSE_ACCESS_OFF	0x00

#define   PACKAGE_QFN32_S           0
#define   PACKAGE_QFN48M_S        1    //definiton 8188GU Dongle Package, Efuse Physical Address 0xF8 = 0xFE
#define   PACKAGE_QFN48_S  	       2
#define   PACKAGE_QFN64_S  	       3     
#define   PACKAGE_QFN32_U  		4    
#define   PACKAGE_QFN48M_U  	5   //definiton 8188GU Dongle Package, Efuse Physical Address 0xF8 = 0xEE
#define   PACKAGE_QFN48_U  		6 
#define   PACKAGE_QFN68_U  		7

typedef enum _PACKAGE_TYPE_E
{
    PACKAGE_DEFAULT,
    PACKAGE_QFN68,
    PACKAGE_TFBGA90,
    PACKAGE_TFBGA80,
    PACKAGE_TFBGA79
}PACKAGE_TYPE_E;

#define INCLUDE_MULTI_FUNC_GPS(_Adapter) \
	(GET_HAL_DATA(_Adapter)->MultiFunc & RT_MULTI_FUNC_GPS)

#ifdef CONFIG_FILE_FWIMG
	extern char *rtw_fw_file_path;
	extern char *rtw_fw_wow_file_path;
	#ifdef CONFIG_MP_INCLUDED
		extern char *rtw_fw_mp_bt_file_path;
	#endif /* CONFIG_MP_INCLUDED */
#endif /* CONFIG_FILE_FWIMG */

/* rtl8710b_hal_init.c */
s32 rtl8710b_FirmwareDownload(PADAPTER padapter, BOOLEAN  bUsedWoWLANFw);
void rtl8710b_FirmwareSelfReset(PADAPTER padapter);
void rtl8710b_InitializeFirmwareVars(PADAPTER padapter);

void rtl8710b_InitAntenna_Selection(PADAPTER padapter);
void rtl8710b_DeinitAntenna_Selection(PADAPTER padapter);
void rtl8710b_CheckAntenna_Selection(PADAPTER padapter);
void rtl8710b_init_default_value(PADAPTER padapter);


u32 indirect_read32_8710b(PADAPTER padapter, u32 regaddr);
void indirect_write32_8710b(PADAPTER padapter, u32 regaddr, u32 data);
u32 hal_query_syson_reg_8710b(PADAPTER padapter, u32 regaddr, u32 bitmask);
void hal_set_syson_reg_8710b(PADAPTER padapter, u32 regaddr, u32 bitmask, u32 data);
#define HAL_SetSYSOnReg hal_set_syson_reg_8710b


/* EFuse */
u8 GetEEPROMSize8710B(PADAPTER padapter);

#if 0
void Hal_InitPGData(PADAPTER padapter, u8 *PROMContent);
void Hal_EfuseParseIDCode(PADAPTER padapter, u8 *hwinfo);
void Hal_EfuseParseTxPowerInfo_8710B(PADAPTER padapter,
				     u8 *PROMContent, BOOLEAN AutoLoadFail);
void Hal_EfuseParseEEPROMVer_8710B(PADAPTER padapter,
				   u8 *hwinfo, BOOLEAN AutoLoadFail);
void Hal_EfuseParsePackageType_8710B(PADAPTER pAdapter,
				     u8 *hwinfo, BOOLEAN AutoLoadFail);
void Hal_EfuseParseChnlPlan_8710B(PADAPTER padapter,
				  u8 *hwinfo, BOOLEAN AutoLoadFail);
void Hal_EfuseParseCustomerID_8710B(PADAPTER padapter,
				    u8 *hwinfo, BOOLEAN AutoLoadFail);
void Hal_EfuseParseAntennaDiversity_8710B(PADAPTER padapter,
		u8 *hwinfo, BOOLEAN AutoLoadFail);
void Hal_EfuseParseXtal_8710B(PADAPTER pAdapter,
			      u8 *hwinfo, u8 AutoLoadFail);
void Hal_EfuseParseThermalMeter_8710B(PADAPTER padapter,
				      u8 *hwinfo, u8 AutoLoadFail);
void Hal_EfuseParseBoardType_8710B(PADAPTER Adapter,
				   u8	*PROMContent, BOOLEAN AutoloadFail);
#endif

void rtl8710b_set_hal_ops(struct hal_ops *pHalFunc);
void init_hal_spec_8710b(_adapter *adapter);
u8 SetHwReg8710B(PADAPTER padapter, u8 variable, u8 *val);
void GetHwReg8710B(PADAPTER padapter, u8 variable, u8 *val);
u8 SetHalDefVar8710B(PADAPTER padapter, HAL_DEF_VARIABLE variable, void *pval);
u8 GetHalDefVar8710B(PADAPTER padapter, HAL_DEF_VARIABLE variable, void *pval);

/* register */
void rtl8710b_InitBeaconParameters(PADAPTER padapter);
void rtl8710b_InitBeaconMaxError(PADAPTER padapter, u8 InfraMode);
void _8051Reset8710(PADAPTER padapter);

void rtl8710b_start_thread(_adapter *padapter);
void rtl8710b_stop_thread(_adapter *padapter);

#ifdef CONFIG_GPIO_WAKEUP
	void HalSetOutPutGPIO(PADAPTER padapter, u8 index, u8 OutPutValue);
#endif

void CCX_FwC2HTxRpt_8710b(PADAPTER padapter, u8 *pdata, u8 len);

u8 MRateToHwRate8710B(u8 rate);
u8 HwRateToMRate8710B(u8 rate);

#ifdef CONFIG_USB_HCI
	void rtl8710b_cal_txdesc_chksum(struct tx_desc *ptxdesc);
#endif


#endif
