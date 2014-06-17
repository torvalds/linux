/******************************************************************************
 *
 * Copyright(c) 2009-2013  Realtek Corporation.
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

#ifndef __RTL92CE_TABLE__H_
#define __RTL92CE_TABLE__H_

#include <linux/types.h>
#define  RTL8188EEPHY_REG_1TARRAYLEN	382
extern u32 RTL8188EEPHY_REG_1TARRAY[];
#define RTL8188EEPHY_REG_ARRAY_PGLEN	264
extern u32 RTL8188EEPHY_REG_ARRAY_PG[];
#define	RTL8188EE_RADIOA_1TARRAYLEN	190
extern u32 RTL8188EE_RADIOA_1TARRAY[];
#define RTL8188EEMAC_1T_ARRAYLEN	180
extern u32 RTL8188EEMAC_1T_ARRAY[];
#define RTL8188EEAGCTAB_1TARRAYLEN	256
extern u32 RTL8188EEAGCTAB_1TARRAY[];

#endif
