/* SPDX-License-Identifier: GPL-2.0 */
/******************************************************************************
 *
 * Copyright(c) 2007 - 2011 Realtek Corporation. All rights reserved.
 *
 ******************************************************************************/
#ifndef __RTL8723B_HAL_H__
#define __RTL8723B_HAL_H__

#include "hal_data.h"

#include "rtl8723b_spec.h"
#include "rtl8723b_rf.h"
#include "rtl8723b_dm.h"
#include "rtl8723b_recv.h"
#include "rtl8723b_xmit.h"
#include "rtl8723b_cmd.h"
#include "rtw_mp.h"
#include "hal_pwr_seq.h"
#include "hal_phy_reg_8723b.h"
#include "hal_phy_cfg.h"

/*  */
/* RTL8723B From header */
/*  */

#define FW_8723B_SIZE          0x8000
#define FW_8723B_START_ADDRESS 0x1000
#define FW_8723B_END_ADDRESS   0x1FFF /* 0x5FFF */

#define IS_FW_HEADER_EXIST_8723B(fw_hdr) \
	((le16_to_cpu(fw_hdr->signature) & 0xFFF0) == 0x5300)

struct rt_firmware {
	u32 fw_length;
	u8 *fw_buffer_sz;
};

/* This structure must be carefully byte-ordered. */
struct rt_firmware_hdr {
	/*  8-byte alinment required */

	/*  LONG WORD 0 ---- */
	__le16 signature;  /* 92C0: test chip; 92C, 88C0: test chip;
			    * 88C1: MP A-cut; 92C1: MP A-cut
			    */
	u8 category;	   /* AP/NIC and USB/PCI */
	u8 function;	   /* Reserved for different FW function indications,
			    * for further use when driver needs to download
			    * different FW in different conditions.
			    */
	__le16 version;    /* FW Version */
	__le16 subversion; /* FW Subversion, default 0x00 */

	/*  LONG WORD 1 ---- */
	u8 month;  /* Release time Month field */
	u8 date;   /* Release time Date field */
	u8 hour;   /* Release time Hour field */
	u8 minute; /* Release time Minute field */

	__le16 ram_code_size; /* The size of RAM code */
	__le16 rsvd2;

	/*  LONG WORD 2 ---- */
	__le32 svn_idx;	/* The SVN entry index */
	__le32 rsvd3;

	/*  LONG WORD 3 ---- */
	__le32 rsvd4;
	__le32 rsvd5;
};

#define DRIVER_EARLY_INT_TIME_8723B  0x05
#define BCN_DMA_ATIME_INT_TIME_8723B 0x02

/* for 8723B */
/* TX 32K, RX 16K, Page size 128B for TX, 8B for RX */
#define PAGE_SIZE_TX_8723B 128
#define PAGE_SIZE_RX_8723B 8

#define RX_DMA_SIZE_8723B          0x4000 /* 16K */
#define RX_DMA_RESERVED_SIZE_8723B 0x80   /* 128B, reserved for tx report */
#define RX_DMA_BOUNDARY_8723B \
	(RX_DMA_SIZE_8723B - RX_DMA_RESERVED_SIZE_8723B - 1)

/* Note: We will divide number of pages equally for each queue other than the
 * public queue!
 */

/* For General Reserved Page Number(Beacon Queue is reserved page) */
/* Beacon:2, PS-Poll:1, Null Data:1, Qos Null Data:1, BT Qos Null Data:1 */
#define BCNQ_PAGE_NUM_8723B  0x08
#define BCNQ1_PAGE_NUM_8723B 0x00

#ifdef CONFIG_PNO_SUPPORT
#undef BCNQ1_PAGE_NUM_8723B
#define BCNQ1_PAGE_NUM_8723B 0x00 /* 0x04 */
#endif

#define MAX_RX_DMA_BUFFER_SIZE_8723B 0x2800 /* RX 10K */

/* For WoWLan, more reserved page */
/* ARP Rsp:1, RWC:1, GTK Info:1, GTK RSP:2, GTK EXT MEM:2, PNO: 6 */
#ifdef CONFIG_WOWLAN
#define WOWLAN_PAGE_NUM_8723B 0x07
#else
#define WOWLAN_PAGE_NUM_8723B 0x00
#endif

#ifdef CONFIG_PNO_SUPPORT
#undef WOWLAN_PAGE_NUM_8723B
#define WOWLAN_PAGE_NUM_8723B 0x0d
#endif

#ifdef CONFIG_AP_WOWLAN
#define AP_WOWLAN_PAGE_NUM_8723B 0x02
#endif

#define TX_TOTAL_PAGE_NUMBER_8723B     \
	(0xFF - BCNQ_PAGE_NUM_8723B  - \
		BCNQ1_PAGE_NUM_8723B - \
		WOWLAN_PAGE_NUM_8723B)
#define TX_PAGE_BOUNDARY_8723B (TX_TOTAL_PAGE_NUMBER_8723B + 1)

#define WMM_NORMAL_TX_TOTAL_PAGE_NUMBER_8723B TX_TOTAL_PAGE_NUMBER_8723B
#define WMM_NORMAL_TX_PAGE_BOUNDARY_8723B \
	(WMM_NORMAL_TX_TOTAL_PAGE_NUMBER_8723B + 1)

/* For Normal Chip Setting */
/* (HPQ + LPQ + NPQ + PUBQ) shall be TX_TOTAL_PAGE_NUMBER_8723B */
#define NORMAL_PAGE_NUM_HPQ_8723B 0x0C
#define NORMAL_PAGE_NUM_LPQ_8723B 0x02
#define NORMAL_PAGE_NUM_NPQ_8723B 0x02

/*  Note: For Normal Chip Setting, modify later */
#define WMM_NORMAL_PAGE_NUM_HPQ_8723B 0x30
#define WMM_NORMAL_PAGE_NUM_LPQ_8723B 0x20
#define WMM_NORMAL_PAGE_NUM_NPQ_8723B 0x20

#include "HalVerDef.h"
#include "hal_com.h"

#define EFUSE_OOB_PROTECT_BYTES 15

#define HAL_EFUSE_MEMORY

#define HWSET_MAX_SIZE_8723B         512
#define EFUSE_REAL_CONTENT_LEN_8723B 512
#define EFUSE_MAP_LEN_8723B          512
#define EFUSE_MAX_SECTION_8723B      64

#define EFUSE_IC_ID_OFFSET 506 /* For some inferiority IC purpose.
				* Added by Roger, 2009.09.02.
				*/
#define AVAILABLE_EFUSE_ADDR(addr) (addr < EFUSE_REAL_CONTENT_LEN_8723B)

#define EFUSE_ACCESS_ON  0x69 /* For RTL8723 only. */
#define EFUSE_ACCESS_OFF 0x00 /* For RTL8723 only. */

/*  */
/* EFUSE for BT definition */
/*  */
#define EFUSE_BT_REAL_BANK_CONTENT_LEN 512
#define EFUSE_BT_REAL_CONTENT_LEN      1536 /* 512*3 */
#define EFUSE_BT_MAP_LEN               1024 /* 1k bytes */
#define EFUSE_BT_MAX_SECTION           128  /* 1024/8 */

#define EFUSE_PROTECT_BYTES_BANK 16

/* Description: Determine the types of C2H events that are the same in driver
 * and FW; First constructed by tynli. 2009.10.09.
 */
typedef enum _C2H_EVT {
	C2H_DBG = 0,
	C2H_TSF = 1,
	C2H_AP_RPT_RSP = 2,
	C2H_CCX_TX_RPT = 3, /* The FW notify the report
			     * of the specific tx packet.
			     */
	C2H_BT_RSSI = 4,
	C2H_BT_OP_MODE = 5,
	C2H_EXT_RA_RPT = 6,
	C2H_8723B_BT_INFO = 9,
	C2H_HW_INFO_EXCH = 10,
	C2H_8723B_BT_MP_INFO = 11,
	MAX_C2HEVENT
} C2H_EVT;

typedef struct _C2H_EVT_HDR {
	u8 CmdID;
	u8 CmdLen;
	u8 CmdSeq;
} __attribute__((__packed__)) C2H_EVT_HDR, *PC2H_EVT_HDR;

typedef enum tag_Package_Definition {
	PACKAGE_DEFAULT,
	PACKAGE_QFN68,
	PACKAGE_TFBGA90,
	PACKAGE_TFBGA80,
	PACKAGE_TFBGA79
} PACKAGE_TYPE_E;

#define INCLUDE_MULTI_FUNC_BT(_Adapter)  \
	(GET_HAL_DATA(_Adapter)->MultiFunc & RT_MULTI_FUNC_BT)
#define INCLUDE_MULTI_FUNC_GPS(_Adapter) \
	(GET_HAL_DATA(_Adapter)->MultiFunc & RT_MULTI_FUNC_GPS)

/*  rtl8723a_hal_init.c */
s32 rtl8723b_FirmwareDownload(struct adapter *padapter, bool  bUsedWoWLANFw);
void rtl8723b_FirmwareSelfReset(struct adapter *padapter);
void rtl8723b_InitializeFirmwareVars(struct adapter *padapter);

void rtl8723b_InitAntenna_Selection(struct adapter *padapter);
void rtl8723b_init_default_value(struct adapter *padapter);

s32 rtl8723b_InitLLTTable(struct adapter *padapter);

/*  EFuse */
u8 GetEEPROMSize8723B(struct adapter *padapter);
void Hal_InitPGData(struct adapter *padapter, u8 *PROMContent);
void Hal_EfuseParseIDCode(struct adapter *padapter, u8 *hwinfo);
void Hal_EfuseParseTxPowerInfo_8723B(struct adapter *padapter, u8 *PROMContent,
				     bool AutoLoadFail);
void Hal_EfuseParseBTCoexistInfo_8723B(struct adapter *padapter, u8 *hwinfo,
				       bool AutoLoadFail);
void Hal_EfuseParseEEPROMVer_8723B(struct adapter *padapter, u8 *hwinfo,
				   bool AutoLoadFail);
void Hal_EfuseParseChnlPlan_8723B(struct adapter *padapter, u8 *hwinfo,
				  bool AutoLoadFail);
void Hal_EfuseParseCustomerID_8723B(struct adapter *padapter, u8 *hwinfo,
				    bool AutoLoadFail);
void Hal_EfuseParseAntennaDiversity_8723B(struct adapter *padapter, u8 *hwinfo,
					  bool AutoLoadFail);
void Hal_EfuseParseXtal_8723B(struct adapter *padapter, u8 *hwinfo,
			      bool AutoLoadFail);
void Hal_EfuseParseThermalMeter_8723B(struct adapter *padapter, u8 *hwinfo,
				      u8 AutoLoadFail);
void Hal_EfuseParsePackageType_8723B(struct adapter *padapter, u8 *hwinfo,
				     bool AutoLoadFail);
void Hal_EfuseParseVoltage_8723B(struct adapter *padapter, u8 *hwinfo,
				 bool AutoLoadFail);

void C2HPacketHandler_8723B(struct adapter *padapter, u8 *pbuffer, u16 length);

void rtl8723b_set_hal_ops(struct hal_ops *pHalFunc);
void SetHwReg8723B(struct adapter *padapter, u8 variable, u8 *val);
void GetHwReg8723B(struct adapter *padapter, u8 variable, u8 *val);
u8 SetHalDefVar8723B(struct adapter *padapter, enum HAL_DEF_VARIABLE variable,
		     void *pval);
u8 GetHalDefVar8723B(struct adapter *padapter, enum HAL_DEF_VARIABLE variable,
		     void *pval);

/*  register */
void rtl8723b_InitBeaconParameters(struct adapter *padapter);
void _InitBurstPktLen_8723BS(struct adapter *adapter);
void _8051Reset8723(struct adapter *padapter);
#ifdef CONFIG_WOWLAN
void Hal_DetectWoWMode(struct adapter *padapter);
#endif /* CONFIG_WOWLAN */

void rtl8723b_start_thread(struct adapter *padapter);
void rtl8723b_stop_thread(struct adapter *padapter);

#if defined(CONFIG_CHECK_BT_HANG)
void rtl8723bs_init_checkbthang_workqueue(struct adapter *adapter);
void rtl8723bs_free_checkbthang_workqueue(struct adapter *adapter);
void rtl8723bs_cancle_checkbthang_workqueue(struct adapter *adapter);
void rtl8723bs_hal_check_bt_hang(struct adapter *adapter);
#endif

#ifdef CONFIG_GPIO_WAKEUP
void HalSetOutPutGPIO(struct adapter *padapter, u8 index, u8 OutPutValue);
#endif

int FirmwareDownloadBT(struct adapter *adapter, struct rt_firmware *firmware);

void CCX_FwC2HTxRpt_8723b(struct adapter *padapter, u8 *pdata, u8 len);
s32 c2h_id_filter_ccx_8723b(u8 *buf);
s32 c2h_handler_8723b(struct adapter *padapter, u8 *pC2hEvent);
u8 MRateToHwRate8723B(u8 rate);
u8 HwRateToMRate8723B(u8 rate);

void Hal_ReadRFGainOffset(struct adapter *padapter, u8 *hwinfo,
			  bool AutoLoadFail);

#endif
