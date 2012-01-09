/******************************************************************************
 *
 * Copyright(c) 2007 - 2010 Realtek Corporation. All rights reserved.
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
 * Modifications for inclusion into the Linux staging tree are
 * Copyright(c) 2010 Larry Finger. All rights reserved.
 *
 * Contact information:
 * WLAN FAE <wlanfae@realtek.com>
 * Larry Finger <Larry.Finger@lwfinger.net>
 *
 ******************************************************************************/
#ifndef __RTL8712_RATECTRL_REGDEF_H__
#define __RTL8712_RATECTRL_REGDEF_H__

#define INIMCS_SEL			(RTL8712_RATECTRL_ + 0x00)
#define INIRTSMCS_SEL		(RTL8712_RATECTRL_ + 0x20)
#define RRSR				(RTL8712_RATECTRL_ + 0x21)
#define ARFR0				(RTL8712_RATECTRL_ + 0x24)
#define ARFR1				(RTL8712_RATECTRL_ + 0x28)
#define ARFR2				(RTL8712_RATECTRL_ + 0x2C)
#define ARFR3				(RTL8712_RATECTRL_ + 0x30)
#define ARFR4				(RTL8712_RATECTRL_ + 0x34)
#define ARFR5				(RTL8712_RATECTRL_ + 0x38)
#define ARFR6				(RTL8712_RATECTRL_ + 0x3C)
#define ARFR7				(RTL8712_RATECTRL_ + 0x40)
#define AGGLEN_LMT_H		(RTL8712_RATECTRL_ + 0x47)
#define AGGLEN_LMT_L		(RTL8712_RATECTRL_ + 0x48)
#define DARFRC				(RTL8712_RATECTRL_ + 0x50)
#define RARFRC				(RTL8712_RATECTRL_ + 0x58)
#define MCS_TXAGC0			(RTL8712_RATECTRL_ + 0x60)
#define MCS_TXAGC1			(RTL8712_RATECTRL_ + 0x61)
#define MCS_TXAGC2			(RTL8712_RATECTRL_ + 0x62)
#define MCS_TXAGC3			(RTL8712_RATECTRL_ + 0x63)
#define MCS_TXAGC4			(RTL8712_RATECTRL_ + 0x64)
#define MCS_TXAGC5			(RTL8712_RATECTRL_ + 0x65)
#define MCS_TXAGC6			(RTL8712_RATECTRL_ + 0x66)
#define MCS_TXAGC7			(RTL8712_RATECTRL_ + 0x67)
#define CCK_TXAGC			(RTL8712_RATECTRL_ + 0x68)


#endif	/*__RTL8712_RATECTRL_REGDEF_H__*/

