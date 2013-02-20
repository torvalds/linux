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
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110, USA
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

#ifndef __RTL8723E_TABLE__H_
#define __RTL8723E_TABLE__H_

#include <linux/types.h>

#define RTL8723E_PHY_REG_1TARRAY_LENGTH		372
extern u32 RTL8723EPHY_REG_1TARRAY[RTL8723E_PHY_REG_1TARRAY_LENGTH];
#define RTL8723E_PHY_REG_ARRAY_PGLENGTH		336
extern u32 RTL8723EPHY_REG_ARRAY_PG[RTL8723E_PHY_REG_ARRAY_PGLENGTH];
#define Rtl8723ERADIOA_1TARRAYLENGTH		 282
extern u32 RTL8723E_RADIOA_1TARRAY[Rtl8723ERADIOA_1TARRAYLENGTH];
#define RTL8723E_RADIOB_1TARRAYLENGTH		1
extern u32 RTL8723E_RADIOB_1TARRAY[RTL8723E_RADIOB_1TARRAYLENGTH];
#define RTL8723E_MACARRAYLENGTH			172
extern u32 RTL8723EMAC_ARRAY[RTL8723E_MACARRAYLENGTH];
#define RTL8723E_AGCTAB_1TARRAYLENGTH		320
extern u32 RTL8723EAGCTAB_1TARRAY[RTL8723E_AGCTAB_1TARRAYLENGTH];

#endif
