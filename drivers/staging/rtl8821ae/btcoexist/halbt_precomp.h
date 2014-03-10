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
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110, USA
 *
 *
 ******************************************************************************/
#ifndef	__HALBT_PRECOMP_H__
#define __HALBT_PRECOMP_H__
/*************************************************************
 * include files
 *************************************************************/
#include "../wifi.h"
#include "../efuse.h"
#include "../base.h"
#include "../regd.h"
#include "../cam.h"
#include "../ps.h"
#include "../pci.h"
#include "../rtl8821ae/reg.h"
#include "../rtl8821ae/def.h"
#include "../rtl8821ae/phy.h"
#include "../rtl8821ae/dm.h"
#include "../rtl8821ae/fw.h"
#include "../rtl8821ae/led.h"
#include "../rtl8821ae/hw.h"
#include "../rtl8821ae/pwrseqcmd.h"
#include "../rtl8821ae/pwrseq.h"

#include "halbtcoutsrc.h"


#include "halbtc8192e2ant.h"
#include "halbtc8723b1ant.h"
#include "halbtc8723b2ant.h"



#define GetDefaultAdapter(padapter)	padapter


#define BIT0	0x00000001
#define BIT1	0x00000002
#define BIT2	0x00000004
#define BIT3	0x00000008
#define BIT4	0x00000010
#define BIT5	0x00000020
#define BIT6	0x00000040
#define BIT7	0x00000080
#define BIT8	0x00000100
#define BIT9	0x00000200
#define BIT10	0x00000400
#define BIT11	0x00000800
#define BIT12	0x00001000
#define BIT13	0x00002000
#define BIT14	0x00004000
#define BIT15	0x00008000
#define BIT16	0x00010000
#define BIT17	0x00020000
#define BIT18	0x00040000
#define BIT19	0x00080000
#define BIT20	0x00100000
#define BIT21	0x00200000
#define BIT22	0x00400000
#define BIT23	0x00800000
#define BIT24	0x01000000
#define BIT25	0x02000000
#define BIT26	0x04000000
#define BIT27	0x08000000
#define BIT28	0x10000000
#define BIT29	0x20000000
#define BIT30	0x40000000
#define BIT31	0x80000000

#define	MASKBYTE0                	0xff
#define	MASKBYTE1                	0xff00
#define	MASKBYTE2                	0xff0000
#define	MASKBYTE3                	0xff000000
#define	MASKHWORD                	0xffff0000
#define	MASKLWORD                	0x0000ffff
#define	MASKDWORD			0xffffffff
#define	MASK12BITS			0xfff
#define	MASKH4BITS			0xf0000000
#define MASKOFDM_D			0xffc00000
#define	MASKCCK				0x3f3f3f3f

#endif	/* __HALBT_PRECOMP_H__ */
