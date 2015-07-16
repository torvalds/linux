/******************************************************************************
 *
 * Copyright(c) 2009-2010  Realtek Corporation.
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
 * Created on  2010/ 5/18,  1:41
 *
 * Larry Finger <Larry.Finger@lwfinger.net>
 *
 *****************************************************************************/

#ifndef __RTL8821AE_TABLE__H_
#define __RTL8821AE_TABLE__H_

#include <linux/types.h>
#define  RTL8821AEPHY_REG_1TARRAYLEN	344
extern u32 RTL8821AE_PHY_REG_ARRAY[];
#define  RTL8812AEPHY_REG_1TARRAYLEN	490
extern u32 RTL8812AE_PHY_REG_ARRAY[];
#define RTL8821AEPHY_REG_ARRAY_PGLEN	90
extern u32 RTL8821AE_PHY_REG_ARRAY_PG[];
#define RTL8812AEPHY_REG_ARRAY_PGLEN	276
extern u32 RTL8812AE_PHY_REG_ARRAY_PG[];
/* #define	RTL8723BE_RADIOA_1TARRAYLEN	206 */
/* extern u8 *RTL8821AE_TXPWR_LMT_ARRAY[]; */
#define	RTL8812AE_RADIOA_1TARRAYLEN	1264
extern u32 RTL8812AE_RADIOA_ARRAY[];
#define	RTL8812AE_RADIOB_1TARRAYLEN	1240
extern u32 RTL8812AE_RADIOB_ARRAY[];
#define	RTL8821AE_RADIOA_1TARRAYLEN	1176
extern u32 RTL8821AE_RADIOA_ARRAY[];
#define RTL8821AEMAC_1T_ARRAYLEN		194
extern u32 RTL8821AE_MAC_REG_ARRAY[];
#define RTL8812AEMAC_1T_ARRAYLEN		214
extern u32 RTL8812AE_MAC_REG_ARRAY[];
#define RTL8821AEAGCTAB_1TARRAYLEN		382
extern u32 RTL8821AE_AGC_TAB_ARRAY[];
#define RTL8812AEAGCTAB_1TARRAYLEN		1312
extern u32 RTL8812AE_AGC_TAB_ARRAY[];
#define RTL8812AE_TXPWR_LMT_ARRAY_LEN		3948
extern u8 *RTL8812AE_TXPWR_LMT[];
#define RTL8821AE_TXPWR_LMT_ARRAY_LEN		3948
extern u8 *RTL8821AE_TXPWR_LMT[];
#endif
