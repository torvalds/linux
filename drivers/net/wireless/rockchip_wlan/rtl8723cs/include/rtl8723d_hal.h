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
#ifndef __RTL8723D_HAL_H__
#define __RTL8723D_HAL_H__

#include "hal_data.h"

#include "rtl8723d_spec.h"
#include "rtl8723d_rf.h"
#include "rtl8723d_dm.h"
#include "rtl8723d_recv.h"
#include "rtl8723d_xmit.h"
#include "rtl8723d_cmd.h"
#include "rtl8723d_led.h"
#include "Hal8723DPwrSeq.h"
#include "Hal8723DPhyReg.h"
#include "Hal8723DPhyCfg.h"
#ifdef DBG_CONFIG_ERROR_DETECT
	#include "rtl8723d_sreset.h"
#endif
#ifdef CONFIG_LPS_POFF
	#include "rtl8723d_lps_poff.h"
#endif

#define FW_8723D_SIZE		0x8000
#define FW_8723D_START_ADDRESS	0x1000
#define FW_8723D_END_ADDRESS	0x1FFF /* 0x5FFF */

#define IS_FW_HEADER_EXIST_8723D(_pFwHdr)\
	((le16_to_cpu(_pFwHdr->Signature) & 0xFFF0) == 0x23D0)

typedef struct _RT_FIRMWARE {
	FIRMWARE_SOURCE	eFWSource;
#ifdef CONFIG_EMBEDDED_FWIMG
	u8			*szFwBuffer;
#else
	u8			szFwBuffer[FW_8723D_SIZE];
#endif
	u32			ulFwLength;
} RT_FIRMWARE_8723D, *PRT_FIRMWARE_8723D;

/*
 * This structure must be cared byte-ordering
 *
 * Added by tynli. 2009.12.04. */
typedef struct _RT_8723D_FIRMWARE_HDR {
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
} RT_8723D_FIRMWARE_HDR, *PRT_8723D_FIRMWARE_HDR;

#define DRIVER_EARLY_INT_TIME_8723D		0x05
#define BCN_DMA_ATIME_INT_TIME_8723D		0x02

/* for 8723D
 * TX 32K, RX 16K, Page size 128B for TX, 8B for RX */
#define PAGE_SIZE_TX_8723D			128
#define PAGE_SIZE_RX_8723D			8

#define TX_DMA_SIZE_8723D			0x8000	/* 32K(TX) */
#define RX_DMA_SIZE_8723D			0x4000	/* 16K(RX) */

#ifdef CONFIG_WOWLAN
	#define RESV_FMWF	(WKFMCAM_SIZE * MAX_WKFM_CAM_NUM) /* 16 entries, for each is 24 bytes*/
#else
	#define RESV_FMWF	0
#endif

#ifdef CONFIG_FW_C2H_DEBUG
	#define RX_DMA_RESERVED_SIZE_8723D	0x100	/* 256B, reserved for c2h debug message */
#else
	#define RX_DMA_RESERVED_SIZE_8723D	0x80	/* 128B, reserved for tx report */
#endif
#define RX_DMA_BOUNDARY_8723D\
	(RX_DMA_SIZE_8723D - RX_DMA_RESERVED_SIZE_8723D - 1)


/* Note: We will divide number of page equally for each queue other than public queue! */

/* For General Reserved Page Number(Beacon Queue is reserved page)
 * Beacon:2, PS-Poll:1, Null Data:1,Qos Null Data:1,BT Qos Null Data:1 */
#define BCNQ_PAGE_NUM_8723D		0x08
#ifdef CONFIG_CONCURRENT_MODE
	#define BCNQ1_PAGE_NUM_8723D		0x08 /* 0x04 */
#else
	#define BCNQ1_PAGE_NUM_8723D		0x00
#endif

#ifdef CONFIG_PNO_SUPPORT
	#undef BCNQ1_PAGE_NUM_8723D
	#define BCNQ1_PAGE_NUM_8723D		0x00 /* 0x04 */
#endif

/* For WoWLan , more reserved page
 * ARP Rsp:1, RWC:1, GTK Info:1,GTK RSP:2,GTK EXT MEM:2, AOAC rpt 1, PNO: 6 */
#ifdef CONFIG_WOWLAN
	#define WOWLAN_PAGE_NUM_8723D	0x08
#else
	#define WOWLAN_PAGE_NUM_8723D	0x00
#endif

#ifdef CONFIG_PNO_SUPPORT
	#undef WOWLAN_PAGE_NUM_8723D
	#define WOWLAN_PAGE_NUM_8723D	0x15
#endif

#ifdef CONFIG_AP_WOWLAN
	#define AP_WOWLAN_PAGE_NUM_8723D	0x02
#endif

#define TX_TOTAL_PAGE_NUMBER_8723D\
	(0xFF - BCNQ_PAGE_NUM_8723D - BCNQ1_PAGE_NUM_8723D - WOWLAN_PAGE_NUM_8723D)
#define TX_PAGE_BOUNDARY_8723D		(TX_TOTAL_PAGE_NUMBER_8723D + 1)

#define WMM_NORMAL_TX_TOTAL_PAGE_NUMBER_8723D	TX_TOTAL_PAGE_NUMBER_8723D
#define WMM_NORMAL_TX_PAGE_BOUNDARY_8723D\
	(WMM_NORMAL_TX_TOTAL_PAGE_NUMBER_8723D + 1)

/* For Normal Chip Setting
 * (HPQ + LPQ + NPQ + PUBQ) shall be TX_TOTAL_PAGE_NUMBER_8723D */
#define NORMAL_PAGE_NUM_HPQ_8723D		0x0C
#define NORMAL_PAGE_NUM_LPQ_8723D		0x02
#define NORMAL_PAGE_NUM_NPQ_8723D		0x02

/* Note: For Normal Chip Setting, modify later */
#define WMM_NORMAL_PAGE_NUM_HPQ_8723D		0x30
#define WMM_NORMAL_PAGE_NUM_LPQ_8723D		0x20
#define WMM_NORMAL_PAGE_NUM_NPQ_8723D		0x20


#include "HalVerDef.h"
#include "hal_com.h"

#define EFUSE_OOB_PROTECT_BYTES (96 + 1)

#define HAL_EFUSE_MEMORY
#define HWSET_MAX_SIZE_8723D                512
#define EFUSE_REAL_CONTENT_LEN_8723D        512
#define EFUSE_MAP_LEN_8723D                 512
#define EFUSE_MAX_SECTION_8723D             64

/* For some inferiority IC purpose. added by Roger, 2009.09.02.*/
#define EFUSE_IC_ID_OFFSET			506
#define AVAILABLE_EFUSE_ADDR(addr)	(addr < EFUSE_REAL_CONTENT_LEN_8723D)

#define EFUSE_ACCESS_ON		0x69
#define EFUSE_ACCESS_OFF	0x00

/* ********************************************************
 *			EFUSE for BT definition
 * ******************************************************** */
#define BANK_NUM			1
#define EFUSE_BT_REAL_BANK_CONTENT_LEN	128
#define EFUSE_BT_REAL_CONTENT_LEN	\
	(EFUSE_BT_REAL_BANK_CONTENT_LEN * BANK_NUM)
#define EFUSE_BT_MAP_LEN		1024	/* 1k bytes */
#define EFUSE_BT_MAX_SECTION		(EFUSE_BT_MAP_LEN / 8)
#define EFUSE_PROTECT_BYTES_BANK	16

typedef enum tag_Package_Definition {
	PACKAGE_DEFAULT,
	PACKAGE_QFN68,
	PACKAGE_TFBGA90,
	PACKAGE_TFBGA80,
	PACKAGE_TFBGA79
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

/* rtl8723d_hal_init.c */
s32 rtl8723d_FirmwareDownload(PADAPTER padapter, BOOLEAN  bUsedWoWLANFw);
void rtl8723d_FirmwareSelfReset(PADAPTER padapter);
void rtl8723d_InitializeFirmwareVars(PADAPTER padapter);

void rtl8723d_InitAntenna_Selection(PADAPTER padapter);
void rtl8723d_DeinitAntenna_Selection(PADAPTER padapter);
void rtl8723d_CheckAntenna_Selection(PADAPTER padapter);
void rtl8723d_init_default_value(PADAPTER padapter);

s32 rtl8723d_InitLLTTable(PADAPTER padapter);

s32 CardDisableHWSM(PADAPTER padapter, u8 resetMCU);
s32 CardDisableWithoutHWSM(PADAPTER padapter);

/* EFuse */
u8 GetEEPROMSize8723D(PADAPTER padapter);
void Hal_InitPGData(PADAPTER padapter, u8 *PROMContent);
void Hal_EfuseParseIDCode(PADAPTER padapter, u8 *hwinfo);
void Hal_EfuseParseTxPowerInfo_8723D(PADAPTER padapter,
				     u8 *PROMContent, BOOLEAN AutoLoadFail);
void Hal_EfuseParseBTCoexistInfo_8723D(PADAPTER padapter,
				       u8 *hwinfo, BOOLEAN AutoLoadFail);
void Hal_EfuseParseEEPROMVer_8723D(PADAPTER padapter,
				   u8 *hwinfo, BOOLEAN AutoLoadFail);
void Hal_EfuseParsePackageType_8723D(PADAPTER pAdapter,
				     u8 *hwinfo, BOOLEAN AutoLoadFail);
void Hal_EfuseParseChnlPlan_8723D(PADAPTER padapter,
				  u8 *hwinfo, BOOLEAN AutoLoadFail);
void Hal_EfuseParseCustomerID_8723D(PADAPTER padapter,
				    u8 *hwinfo, BOOLEAN AutoLoadFail);
void Hal_EfuseParseAntennaDiversity_8723D(PADAPTER padapter,
		u8 *hwinfo, BOOLEAN AutoLoadFail);
void Hal_EfuseParseXtal_8723D(PADAPTER pAdapter,
			      u8 *hwinfo, u8 AutoLoadFail);
void Hal_EfuseParseThermalMeter_8723D(PADAPTER padapter,
				      u8 *hwinfo, u8 AutoLoadFail);
VOID Hal_EfuseParseVoltage_8723D(PADAPTER pAdapter,
				 u8 *hwinfo, BOOLEAN	AutoLoadFail);
VOID Hal_EfuseParseBoardType_8723D(PADAPTER Adapter,
				   u8	*PROMContent, BOOLEAN AutoloadFail);

void rtl8723d_set_hal_ops(struct hal_ops *pHalFunc);
void init_hal_spec_8723d(_adapter *adapter);
void SetHwReg8723D(PADAPTER padapter, u8 variable, u8 *val);
void GetHwReg8723D(PADAPTER padapter, u8 variable, u8 *val);
u8 SetHalDefVar8723D(PADAPTER padapter, HAL_DEF_VARIABLE variable, void *pval);
u8 GetHalDefVar8723D(PADAPTER padapter, HAL_DEF_VARIABLE variable, void *pval);

/* register */
void rtl8723d_InitBeaconParameters(PADAPTER padapter);
void rtl8723d_InitBeaconMaxError(PADAPTER padapter, u8 InfraMode);
void _InitBurstPktLen_8723DS(PADAPTER Adapter);
void _InitLTECoex_8723DS(PADAPTER Adapter);
void _InitMacAPLLSetting_8723D(PADAPTER Adapter);
void _8051Reset8723(PADAPTER padapter);
#ifdef CONFIG_WOWLAN
	void Hal_DetectWoWMode(PADAPTER pAdapter);
#endif /* CONFIG_WOWLAN */

void rtl8723d_start_thread(_adapter *padapter);
void rtl8723d_stop_thread(_adapter *padapter);

#if defined(CONFIG_CHECK_BT_HANG) && defined(CONFIG_BT_COEXIST)
	void rtl8723ds_init_checkbthang_workqueue(_adapter *adapter);
	void rtl8723ds_free_checkbthang_workqueue(_adapter *adapter);
	void rtl8723ds_cancle_checkbthang_workqueue(_adapter *adapter);
	void rtl8723ds_hal_check_bt_hang(_adapter *adapter);
#endif

#ifdef CONFIG_GPIO_WAKEUP
	void HalSetOutPutGPIO(PADAPTER padapter, u8 index, u8 OutPutValue);
#endif
#ifdef CONFIG_MP_INCLUDED
int FirmwareDownloadBT(IN PADAPTER Adapter, PRT_MP_FIRMWARE pFirmware);
#endif
void CCX_FwC2HTxRpt_8723d(PADAPTER padapter, u8 *pdata, u8 len);

u8 MRateToHwRate8723D(u8 rate);
u8 HwRateToMRate8723D(u8 rate);

void Hal_ReadRFGainOffset(PADAPTER pAdapter, u8 *hwinfo, BOOLEAN AutoLoadFail);

#if defined(CONFIG_CHECK_BT_HANG) && defined(CONFIG_BT_COEXIST)
	void check_bt_status_work(void *data);
#endif

#ifdef CONFIG_USB_HCI
	void rtl8723d_cal_txdesc_chksum(struct tx_desc *ptxdesc);
#endif

#ifdef CONFIG_PCI_HCI
	BOOLEAN	InterruptRecognized8723DE(PADAPTER Adapter);
	VOID	UpdateInterruptMask8723DE(PADAPTER Adapter, u32 AddMSR, u32 AddMSR1, u32 RemoveMSR, u32 RemoveMSR1);
	u16 get_txbufdesc_idx_hwaddr(u16 ff_hwaddr);
#endif

#endif
