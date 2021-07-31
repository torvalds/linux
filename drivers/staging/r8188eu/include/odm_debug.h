/* SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause */
/* Copyright(c) 2007 - 2011 Realtek Corporation. */

#ifndef	__ODM_DBG_H__
#define __ODM_DBG_H__

/*  */
/*	Define the debug levels */
/*  */
/*	1. DBG_TRACE and DBG_LOUD are used for normal cases. */
/*	They can help SW engineer to develop or trace states changed */
/*	and also help HW enginner to trace every operation to and from HW, */
/*	e.g IO, Tx, Rx. */
/*  */
/*	2. DBG_WARNNING and DBG_SERIOUS are used for unusual or error cases, */
/*	which help us to debug SW or HW. */

/*	Never used in a call to ODM_RT_TRACE()! */
#define ODM_DBG_OFF				1

/*	Fatal bug. */
/*	For example, Tx/Rx/IO locked up, OS hangs, memory access violation, */
/*	resource allocation failed, unexpected HW behavior, HW BUG and so on. */
#define ODM_DBG_SERIOUS				2

/*	Abnormal, rare, or unexpeted cases. */
/*	For example, IRP/Packet/OID canceled, device suprisely unremoved and so on. */
#define ODM_DBG_WARNING				3

/*	Normal case with useful information about current SW or HW state. */
/*	For example, Tx/Rx descriptor to fill, Tx/Rx descr. completed status, */
/*	SW protocol state change, dynamic mechanism state change and so on. */
/*  */
#define ODM_DBG_LOUD					4

/*	Normal case with detail execution flow or information. */
#define ODM_DBG_TRACE					5

/*  Define the tracing components */
/* BB Functions */
#define ODM_COMP_DIG					BIT0
#define ODM_COMP_RA_MASK				BIT1
#define ODM_COMP_DYNAMIC_TXPWR				BIT2
#define ODM_COMP_FA_CNT					BIT3
#define ODM_COMP_RSSI_MONITOR				BIT4
#define ODM_COMP_CCK_PD					BIT5
#define ODM_COMP_ANT_DIV				BIT6
#define ODM_COMP_PWR_SAVE				BIT7
#define ODM_COMP_PWR_TRA				BIT8
#define ODM_COMP_RATE_ADAPTIVE				BIT9
#define ODM_COMP_PATH_DIV				BIT10
#define ODM_COMP_PSD					BIT11
#define ODM_COMP_DYNAMIC_PRICCA				BIT12
#define ODM_COMP_RXHP					BIT13
/* MAC Functions */
#define ODM_COMP_EDCA_TURBO				BIT16
#define ODM_COMP_EARLY_MODE				BIT17
/* RF Functions */
#define ODM_COMP_TX_PWR_TRACK				BIT24
#define ODM_COMP_RX_GAIN_TRACK				BIT25
#define ODM_COMP_CALIBRATION				BIT26
/* Common Functions */
#define ODM_COMP_COMMON					BIT30
#define ODM_COMP_INIT					BIT31

/*------------------------Export Marco Definition---------------------------*/
#define DbgPrint	pr_info
#define RT_PRINTK(fmt, args...)				\
	DbgPrint( "%s(): " fmt, __func__, ## args);

#define ODM_RT_TRACE(pDM_Odm, comp, level, fmt)				\
	if (((comp) & pDM_Odm->DebugComponents) &&			\
	    (level <= pDM_Odm->DebugLevel)) {				\
		if (pDM_Odm->SupportICType == ODM_RTL8192C)		\
			DbgPrint("[ODM-92C] ");				\
		else if (pDM_Odm->SupportICType == ODM_RTL8192D)	\
			DbgPrint("[ODM-92D] ");				\
		else if (pDM_Odm->SupportICType == ODM_RTL8723A)	\
			DbgPrint("[ODM-8723A] ");			\
		else if (pDM_Odm->SupportICType == ODM_RTL8188E)	\
			DbgPrint("[ODM-8188E] ");			\
		else if (pDM_Odm->SupportICType == ODM_RTL8812)		\
			DbgPrint("[ODM-8812] ");			\
		else if (pDM_Odm->SupportICType == ODM_RTL8821)		\
			DbgPrint("[ODM-8821] ");			\
		RT_PRINTK fmt;						\
	}

void ODM_InitDebugSetting(struct odm_dm_struct *pDM_Odm);

#endif	/*  __ODM_DBG_H__ */
