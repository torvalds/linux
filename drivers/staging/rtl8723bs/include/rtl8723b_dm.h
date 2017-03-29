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
