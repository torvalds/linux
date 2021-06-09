// SPDX-License-Identifier: GPL-2.0
/******************************************************************************
 *
 * Copyright(c) 2007 - 2011 Realtek Corporation. All rights reserved.
 *
 ******************************************************************************/

#include "odm_precomp.h"

void ODM_InitDebugSetting(struct dm_odm_t *pDM_Odm)
{
	pDM_Odm->DebugLevel = ODM_DBG_LOUD;

	pDM_Odm->DebugComponents =
/* BB Functions */
/* 		ODM_COMP_DIG					| */
/* 		ODM_COMP_RA_MASK				| */
/* 		ODM_COMP_DYNAMIC_TXPWR		| */
/* 		ODM_COMP_FA_CNT				| */
/* 		ODM_COMP_RSSI_MONITOR			| */
/* 		ODM_COMP_CCK_PD				| */
/* 		ODM_COMP_ANT_DIV				| */
/* 		ODM_COMP_PWR_SAVE				| */
/* 		ODM_COMP_PWR_TRAIN			| */
/* 		ODM_COMP_RATE_ADAPTIVE		| */
/* 		ODM_COMP_PATH_DIV				| */
/* 		ODM_COMP_DYNAMIC_PRICCA		| */
/* 		ODM_COMP_RXHP					| */
/* 		ODM_COMP_MP					| */
/* 		ODM_COMP_CFO_TRACKING		| */

/* MAC Functions */
/* 		ODM_COMP_EDCA_TURBO			| */
/* 		ODM_COMP_EARLY_MODE			| */
/* RF Functions */
/* 		ODM_COMP_TX_PWR_TRACK		| */
/* 		ODM_COMP_RX_GAIN_TRACK		| */
/* 		ODM_COMP_CALIBRATION			| */
/* Common */
/* 		ODM_COMP_COMMON				| */
/* 		ODM_COMP_INIT					| */
/* 		ODM_COMP_PSD					| */
0;
}
