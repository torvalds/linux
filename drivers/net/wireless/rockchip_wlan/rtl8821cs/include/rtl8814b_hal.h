/******************************************************************************
 *
 * Copyright(c) 2015 - 2017 Realtek Corporation.
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
#ifndef _RTL8814B_HAL_H_
#define _RTL8814B_HAL_H_

#include <osdep_service.h>		/* BIT(x) */
#include <drv_types.h>			/* PADAPTER */
#include "../hal/halmac/halmac_api.h"	/* MAC REG definition */


#ifdef CONFIG_SUPPORT_TRX_SHARED
#define MAX_RECVBUF_SZ		46080	/* 45KB, TX: (256-64)KB */
#else /* !CONFIG_SUPPORT_TRX_SHARED */
#define MAX_RECVBUF_SZ		24576	/* 24KB, TX: 256KB */
#endif /* !CONFIG_SUPPORT_TRX_SHARED */

#if 0
/*
 * MAC Register definition
 */
#define REG_AFE_XTAL_CTRL	REG_AFE_CTRL1_8814B	/* hal_com.c & phydm */
#define REG_AFE_PLL_CTRL	REG_AFE_CTRL2_8814B	/* hal_com.c & phydm */
#define REG_MAC_PHY_CTRL	REG_AFE_CTRL3_8814B	/* phydm only */
#endif
#define REG_LEDCFG0		REG_LED_CFG_8814B	/* rtw_mp.c */
#if 0
#define MSR			(REG_CR_8814B + 2)	/* rtw_mp.c & hal_com.c */
#define MSR1			REG_CR_EXT_8814B	/* rtw_mp.c & hal_com.c */
#endif
#define REG_C2HEVT_MSG_NORMAL	0x1A0			/* hal_com.c */
#if 0
#define REG_C2HEVT_CLEAR	0x1AF			/* hal_com.c */

#define REG_GPIO_PIN_CTRL_2		REG_GPIO_EXT_CTRL_8814B		/* hal_com.c */
#endif
#define REG_WKFMCAM_NUM		REG_WKFMCAM_CMD_8814B	/* hal_com.c: WOWLAN */
#define REG_WOWLAN_WAKE_REASON	0x01C7 /* hal_com.c: WOWLAN */
#define REG_RXPKTBUF_CTRL	(REG_PKTBUF_DBG_CTRL_8814B + 2)	/* hal_com.c: WOWLAN */
#define REG_RXPKT_NUM		REG_RXDMA_CTRL_8814B	/* hal_com.c: WOWLAN */

/* RXERR_RPT, for rtw_mp.c */
#define RXERR_TYPE_OFDM_PPDU		0
#define RXERR_TYPE_OFDM_FALSE_ALARM	2
#define RXERR_TYPE_OFDM_MPDU_OK		0
#define RXERR_TYPE_OFDM_MPDU_FAIL	1
#define RXERR_TYPE_CCK_PPDU		3
#define RXERR_TYPE_CCK_FALSE_ALARM	5
#define RXERR_TYPE_CCK_MPDU_OK		3
#define RXERR_TYPE_CCK_MPDU_FAIL	4
#define RXERR_TYPE_HT_PPDU		8
#define RXERR_TYPE_HT_FALSE_ALARM	9
#define RXERR_TYPE_HT_MPDU_TOTAL	6
#define RXERR_TYPE_HT_MPDU_OK		6
#define RXERR_TYPE_HT_MPDU_FAIL		7
#define RXERR_TYPE_RX_FULL_DROP		10

#define RXERR_COUNTER_MASK		BIT_MASK_RPT_COUNTER_8814B
#define RXERR_RPT_RST			BIT_RXERR_RPT_RST_8814B
#define _RXERR_RPT_SEL(type)		(BIT_RXERR_RPT_SEL_V1_3_0_8814B(type) \
					| ((type & 0x10) ? BIT_RXERR_RPT_SEL_V1_4_8814B : 0))

/* hal_com.c:rtw_lps_state_chk() */
#define BIT_PWRBIT_OW_EN		BIT_WMAC_TCRPWRMGT_HWDATA_EN_8814B

/*
 * BB Register definition
 */
#define rPMAC_Reset			0x100	/* hal_mp.c */

#define	rFPGA0_RFMOD			0x800
#define rFPGA0_TxInfo			0x804
#define rOFDMCCKEN_Jaguar		0x808	/* hal_mp.c */
#define rFPGA0_TxGainStage		0x80C	/* phydm only */
#define rFPGA0_XA_HSSIParameter1	0x820	/* hal_mp.c */
#define rFPGA0_XA_HSSIParameter2	0x824	/* hal_mp.c */
#define rFPGA0_XB_HSSIParameter1	0x828	/* hal_mp.c */
#define rFPGA0_XB_HSSIParameter2	0x82C	/* hal_mp.c */
#define rTxAGC_B_Rate18_06		0x830
#define rTxAGC_B_Rate54_24		0x834
#define rTxAGC_B_CCK1_55_Mcs32		0x838
#define rCCAonSec_Jaguar		0x838	/* hal_mp.c */
#define rTxAGC_B_Mcs03_Mcs00		0x83C
#define rTxAGC_B_Mcs07_Mcs04		0x848
#define rTxAGC_B_Mcs11_Mcs08		0x84C
#define rFPGA0_XA_RFInterfaceOE		0x860
#define rFPGA0_XB_RFInterfaceOE		0x864
#define rTxAGC_B_Mcs15_Mcs12		0x868
#define rTxAGC_B_CCK11_A_CCK2_11	0x86C
#define rFPGA0_XAB_RFInterfaceSW	0x870
#define rFPGA0_XAB_RFParameter		0x878
#define rFPGA0_AnalogParameter4		0x88C	/* hal_mp.c & phydm */
#define rFPGA0_XB_LSSIReadBack		0x8A4	/* phydm */
#define rHSSIRead_Jaguar		0x8B0	/* RF read addr (rtl8814b_phy.c) */

#define	rC_TxScale_Jaguar2		0x181C  /* Pah_C TX scaling factor (hal_mp.c) */
#define	rC_IGI_Jaguar2			0x1850	/* Initial Gain for path-C (hal_mp.c) */

#define rFPGA1_TxInfo			0x90C	/* hal_mp.c */
#define rSingleTone_ContTx_Jaguar	0x914	/* hal_mp.c */
/* TX BeamForming */
#define REG_BB_TX_PATH_SEL_1_8814B	0x93C	/* rtl8814b_phy.c */
#define REG_BB_TX_PATH_SEL_2_8814B	0x940	/* rtl8814b_phy.c */

/* TX BeamForming */
#define REG_BB_TXBF_ANT_SET_BF1_8814B	0x19AC	/* rtl8814b_phy.c */
#define REG_BB_TXBF_ANT_SET_BF0_8814B	0x19B4	/* rtl8814b_phy.c */

#define rCCK0_System			0xA00
#define rCCK0_AFESetting		0xA04

#define rCCK0_DSPParameter2		0xA1C
#define rCCK0_TxFilter1			0xA20
#define rCCK0_TxFilter2			0xA24
#define rCCK0_DebugPort			0xA28
#define rCCK0_FalseAlarmReport		0xA2C

#define	rD_TxScale_Jaguar2		0x1A1C  /* Path_D TX scaling factor (hal_mp.c) */
#define	rD_IGI_Jaguar2			0x1A50	/* Initial Gain for path-D (hal_mp.c) */

#define rOFDM0_TRxPathEnable		0xC04
#define rOFDM0_TRMuxPar			0xC08
#define rA_TxScale_Jaguar		0xC1C	/* Pah_A TX scaling factor (hal_mp.c) */
#define rOFDM0_RxDetector1		0xC30	/* rtw_mp.c */
#define rOFDM0_ECCAThreshold		0xC4C	/* phydm only */
#define rOFDM0_XAAGCCore1		0xC50	/* phydm only */
#define rA_IGI_Jaguar			0xC50	/* Initial Gain for path-A (hal_mp.c) */
#define rOFDM0_XBAGCCore1		0xC58	/* phydm only */
#define rOFDM0_XATxIQImbalance		0xC80	/* phydm only */
#define rA_LSSIWrite_Jaguar		0xC90	/* RF write addr, LSSI Parameter (rtl8814b_phy.c) */

#define rOFDM1_LSTF			0xD00
#define rOFDM1_TRxPathEnable		0xD04	/* hal_mp.c */
#define rA_PIRead_Jaguar		0xD04	/* RF readback with PI (rtl8814b_phy.c) */
#define rA_SIRead_Jaguar		0xD08	/* RF readback with SI (rtl8814b_phy.c) */
#define rB_PIRead_Jaguar		0xD44	/* RF readback with PI (rtl8814b_phy.c) */
#define rB_SIRead_Jaguar		0xD48	/* RF readback with SI (rtl8814b_phy.c) */

#define rTxAGC_A_Rate18_06		0xE00
#define rTxAGC_A_Rate54_24		0xE04
#define rTxAGC_A_CCK1_Mcs32		0xE08
#define rTxAGC_A_Mcs03_Mcs00		0xE10
#define rTxAGC_A_Mcs07_Mcs04		0xE14
#define rTxAGC_A_Mcs11_Mcs08		0xE18
#define rTxAGC_A_Mcs15_Mcs12		0xE1C
#define rB_TxScale_Jaguar		0xE1C	/* Path_B TX scaling factor (hal_mp.c) */
#define rB_IGI_Jaguar			0xE50	/* Initial Gain for path-B (hal_mp.c) */
#define rB_LSSIWrite_Jaguar		0xE90	/* RF write addr, LSSI Parameter (rtl8814b_phy.c) */
/* RFE */
#define rA_RFE_Pinmux_Jaguar	0xCB0	/* hal_mp.c */
#define	rB_RFE_Pinmux_Jaguar	0xEB0	/* Path_B RFE control pinmux */
#define	rA_RFE_Inv_Jaguar		0xCB4	/* Path_A RFE cotrol */  
#define	rB_RFE_Inv_Jaguar		0xEB4	/* Path_B RFE control */
#define	rA_RFE_Jaguar			0xCB8 	/* Path_A RFE cotrol */  
#define	rB_RFE_Jaguar			0xEB8	/* Path_B RFE control */
#define	rA_RFE_Inverse_Jaguar	0xCBC	/* Path_A RFE control inverse */
#define	rB_RFE_Inverse_Jaguar	0xEBC	/* Path_B RFE control inverse */
#define	r_ANTSEL_SW_Jaguar		0x900	/* ANTSEL SW Control */
#define	bMask_RFEInv_Jaguar	0x3FF00000
#define	bMask_AntselPathFollow_Jaguar 0x00030000

#define		rC_RFE_Pinmux_Jaguar	0x18B4	/* Path_C RFE cotrol pinmux*/
#define		rD_RFE_Pinmux_Jaguar	0x1AB4	/* Path_D RFE cotrol pinmux*/
#define		rA_RFE_Sel_Jaguar2		0x1990

/* Page1(0x100) */
#define bBBResetB			0x100

/* Page8(0x800) */
#define bCCKEn				0x1000000
#define bOFDMEn				0x2000000
/* Reg 0x80C rFPGA0_TxGainStage */
#define bXBTxAGC			0xF00
#define bXCTxAGC			0xF000
#define bXDTxAGC			0xF0000

/* PageA(0xA00) */
#define bCCKBBMode			0x3

#define bCCKScramble			0x8
#define bCCKTxRate			0x3000

/* General */
#define bMaskByte0		0xFF		/* mp, rtw_odm.c & phydm */
#define bMaskByte1		0xFF00		/* hal_mp.c & phydm */
#define bMaskByte2		0xFF0000	/* hal_mp.c & phydm */
#define bMaskByte3		0xFF000000	/* hal_mp.c & phydm */
#define bMaskHWord		0xFFFF0000	/* hal_com.c, rtw_mp.c */
#define bMaskLWord		0x0000FFFF	/* mp, hal_com.c & phydm */
#define bMaskDWord		0xFFFFFFFF	/* mp, hal, rtw_odm.c & phydm */

#define bEnable			0x1		/* hal_mp.c, rtw_mp.c */
#define bDisable		0x0		/* rtw_mp.c */

#define MAX_STALL_TIME		50		/* unit: us, hal_com_phycfg.c */

#define Rx_Smooth_Factor	20		/* phydm only */

/*
 * RF Register definition
 */
#define RF_AC			0x00
#define RF_AC_Jaguar		0x00	/* hal_mp.c */
#define RF_CHNLBW		0x18	/* rtl8814b_phy.c */
#define RF_ModeTableAddr	0x30	/* rtl8814b_phy.c */
#define RF_ModeTableData0	0x31	/* rtl8814b_phy.c */
#define RF_ModeTableData1	0x32	/* rtl8814b_phy.c */
#define RF_0x52			0x52
#define RF_WeLut_Jaguar		0xEF	/* rtl8814b_phy.c */

/* General Functions */
void rtl8814b_init_hal_spec(PADAPTER);				/* hal/hal_com.c */

#ifdef CONFIG_MP_INCLUDED
/* MP Functions */
#include <rtw_mp.h>		/* struct mp_priv */
void rtl8814b_prepare_mp_txdesc(PADAPTER, struct mp_priv *);	/* rtw_mp.c */
void rtl8814b_mp_config_rfpath(PADAPTER);			/* hal_mp.c */
#endif
void hw_var_set_dl_rsvd_page(PADAPTER adapter, u8 mstatus);

#ifdef CONFIG_USB_HCI
#include <rtl8814bu_hal.h>
#elif defined(CONFIG_PCI_HCI)
#include <rtl8814be_hal.h>
#endif

#endif /* _RTL8814B_HAL_H_ */
