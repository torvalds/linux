/******************************************************************************
 *
 * Copyright(c) 2016 - 2017 Realtek Corporation.
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
#ifndef __RTL8812C_DM_H__
#define __RTL8812C_DM_H__

void rtl8821c_phy_init_dm_priv(PADAPTER);
void rtl8821c_phy_deinit_dm_priv(PADAPTER);
void rtl8821c_phy_init_haldm(PADAPTER);
void rtl8821c_phy_haldm_watchdog(PADAPTER);

#endif
