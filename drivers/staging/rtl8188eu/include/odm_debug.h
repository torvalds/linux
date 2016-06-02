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
 ******************************************************************************/


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
#define ODM_COMP_DIG					BIT(0)
#define ODM_COMP_RA_MASK				BIT(1)
#define ODM_COMP_DYNAMIC_TXPWR				BIT(2)
#define ODM_COMP_FA_CNT					BIT(3)
#define ODM_COMP_RSSI_MONITOR				BIT(4)
#define ODM_COMP_CCK_PD					BIT(5)
#define ODM_COMP_ANT_DIV				BIT(6)
#define ODM_COMP_PWR_SAVE				BIT(7)
#define ODM_COMP_PWR_TRA				BIT(8)
#define ODM_COMP_RATE_ADAPTIVE				BIT(9)
#define ODM_COMP_PATH_DIV				BIT(10)
#define ODM_COMP_PSD					BIT(11)
#define ODM_COMP_DYNAMIC_PRICCA				BIT(12)
#define ODM_COMP_RXHP					BIT(13)
/* MAC Functions */
#define ODM_COMP_EDCA_TURBO				BIT(16)
#define ODM_COMP_EARLY_MODE				BIT(17)
/* RF Functions */
#define ODM_COMP_TX_PWR_TRACK				BIT(24)
#define ODM_COMP_RX_GAIN_TRACK				BIT(25)
#define ODM_COMP_CALIBRATION				BIT(26)
/* Common Functions */
#define ODM_COMP_COMMON					BIT(30)
#define ODM_COMP_INIT					BIT(31)

/*------------------------Export Marco Definition---------------------------*/
#define RT_PRINTK(fmt, args...)				\
	pr_info("%s(): " fmt, __func__, ## args);

#ifndef ASSERT
	#define ASSERT(expr)
#endif

#define ODM_RT_TRACE(pDM_Odm, comp, level, fmt)				\
	if (((comp) & pDM_Odm->DebugComponents) &&			\
	    (level <= pDM_Odm->DebugLevel)) {				\
		pr_info("[ODM-8188E] ");				\
		RT_PRINTK fmt;						\
	}

#define ODM_RT_ASSERT(pDM_Odm, expr, fmt)				\
	if (!(expr)) {							\
		pr_info("Assertion failed! %s at ......\n", #expr);	\
		pr_info("      ......%s,%s,line=%d\n", __FILE__,	\
			__func__, __LINE__);				\
		RT_PRINTK fmt;						\
		ASSERT(false);						\
	}

void ODM_InitDebugSetting(struct odm_dm_struct *pDM_Odm);

#endif	/*  __ODM_DBG_H__ */
