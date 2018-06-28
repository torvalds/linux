/* SPDX-License-Identifier: GPL-2.0 */
/******************************************************************************
 *
 * Copyright(c) 2007 - 2011 Realtek Corporation. All rights reserved.
 *
 ******************************************************************************/
#ifndef __RTL8723B_DM_H__
#define __RTL8723B_DM_H__
/*  */
/*  Description: */
/*  */
/*  This file is for 8723B dynamic mechanism only */
/*  */
/*  */
/*  */

/*  */
/*  structure and define */
/*  */

/*  */
/*  function prototype */
/*  */

void rtl8723b_init_dm_priv(struct adapter *padapter);

void rtl8723b_InitHalDm(struct adapter *padapter);
void rtl8723b_HalDmWatchDog(struct adapter *padapter);
void rtl8723b_HalDmWatchDog_in_LPS(struct adapter *padapter);
void rtl8723b_hal_dm_in_lps(struct adapter *padapter);


#endif
