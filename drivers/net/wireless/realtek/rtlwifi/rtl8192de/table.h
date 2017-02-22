/******************************************************************************
 *
 * Copyright(c) 2009-2012  Realtek Corporation.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of version 2 of the GNU General Public License as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * The full GNU General Public License is included in this distribution in the
 * file called LICENSE.
 *
 * Contact Information:
 * wlanfae <wlanfae@realtek.com>
 * Realtek Corporation, No. 2, Innovation Road II, Hsinchu Science Park,
 * Hsinchu 300, Taiwan.
 *
 * Larry Finger <Larry.Finger@lwfinger.net>
 *
 * Created on  2010/ 5/18,  1:41
 *****************************************************************************/

#ifndef __RTL92DE_TABLE__H_
#define __RTL92DE_TABLE__H_

/*Created on  2011/ 1/14,  1:35*/

#define PHY_REG_2T_ARRAYLENGTH 380
extern u32 rtl8192de_phy_reg_2tarray[PHY_REG_2T_ARRAYLENGTH];
#define PHY_REG_ARRAY_PG_LENGTH 624
extern u32 rtl8192de_phy_reg_array_pg[PHY_REG_ARRAY_PG_LENGTH];
#define RADIOA_2T_ARRAYLENGTH 378
extern u32 rtl8192de_radioa_2tarray[RADIOA_2T_ARRAYLENGTH];
#define RADIOB_2T_ARRAYLENGTH 384
extern u32 rtl8192de_radiob_2tarray[RADIOB_2T_ARRAYLENGTH];
#define RADIOA_2T_INT_PA_ARRAYLENGTH 378
extern u32 rtl8192de_radioa_2t_int_paarray[RADIOA_2T_INT_PA_ARRAYLENGTH];
#define RADIOB_2T_INT_PA_ARRAYLENGTH 384
extern u32 rtl8192de_radiob_2t_int_paarray[RADIOB_2T_INT_PA_ARRAYLENGTH];
#define MAC_2T_ARRAYLENGTH 160
extern u32 rtl8192de_mac_2tarray[MAC_2T_ARRAYLENGTH];
#define AGCTAB_ARRAYLENGTH 386
extern u32 rtl8192de_agctab_array[AGCTAB_ARRAYLENGTH];
#define AGCTAB_5G_ARRAYLENGTH 194
extern u32 rtl8192de_agctab_5garray[AGCTAB_5G_ARRAYLENGTH];
#define AGCTAB_2G_ARRAYLENGTH 194
extern u32 rtl8192de_agctab_2garray[AGCTAB_2G_ARRAYLENGTH];

#endif
