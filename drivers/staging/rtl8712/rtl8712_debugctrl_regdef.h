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
 *
 ******************************************************************************/
#ifndef __RTL8712_DEBUGCTRL_REGDEF_H__
#define __RTL8712_DEBUGCTRL_REGDEF_H__

#define BIST			(RTL8712_DEBUGCTRL_ + 0x00)
#define DBS			(RTL8712_DEBUGCTRL_ + 0x04)
#define LMS			(RTL8712_DEBUGCTRL_ + 0x05)
#define CPUINST			(RTL8712_DEBUGCTRL_ + 0x08)
#define CPUCAUSE		(RTL8712_DEBUGCTRL_ + 0x0C)
#define LBUS_ERR_ADDR		(RTL8712_DEBUGCTRL_ + 0x10)
#define LBUS_ERR_CMD		(RTL8712_DEBUGCTRL_ + 0x14)
#define LBUS_ERR_DATA_L		(RTL8712_DEBUGCTRL_ + 0x18)
#define LBUS_ERR_DATA_H		(RTL8712_DEBUGCTRL_ + 0x1C)
#define LBUS_EXCEPTION_ADDR	(RTL8712_DEBUGCTRL_ + 0x20)
#define WDG_CTRL		(RTL8712_DEBUGCTRL_ + 0x24)
#define INTMTU			(RTL8712_DEBUGCTRL_ + 0x28)
#define INTM			(RTL8712_DEBUGCTRL_ + 0x2A)
#define FDLOCKTURN0		(RTL8712_DEBUGCTRL_ + 0x2C)
#define FDLOCKTURN1		(RTL8712_DEBUGCTRL_ + 0x2D)
#define FDLOCKFLAG0		(RTL8712_DEBUGCTRL_ + 0x2E)
#define FDLOCKFLAG1		(RTL8712_DEBUGCTRL_ + 0x2F)
#define TRXPKTBUF_DBG_DATA	(RTL8712_DEBUGCTRL_ + 0x30)
#define TRXPKTBUF_DBG_CTRL	(RTL8712_DEBUGCTRL_ + 0x38)
#define DPLL_MON		(RTL8712_DEBUGCTRL_ + 0x3A)

#endif /* __RTL8712_DEBUGCTRL_REGDEF_H__ */

