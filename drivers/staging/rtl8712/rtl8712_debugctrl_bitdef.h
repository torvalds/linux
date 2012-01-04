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
#ifndef __RTL8712_DEBUGCTRL_BITDEF_H__
#define __RTL8712_DEBUGCTRL_BITDEF_H__

/*BIST*/
#define	_BIST_RST			BIT(0)

/*LMS*/
#define	_LMS_MSK			0x03

/*WDG_CTRL*/
#define	_OVSEL_MSK			0x0600
#define	_OVSEL_SHT			9
#define	_WDGCLR				BIT(8)
#define	_WDGEN_MSK			0x00FF
#define	_WDGEN_SHT			0

/*INTM*/
#define	_TXTIMER_MSK		0xF000
#define	_TXTIMER_SHT		12
#define	_TXNUM_MSK			0x0F00
#define	_TXNUM_SHT			8
#define	_RXTIMER_MSK		0x00F0
#define	_RXTIMER_SHT		4
#define	_RXNUM_MSK			0x000F
#define	_RXNUM_SHT			0

/*FDLOCKTURN0*/
/*FDLOCKTURN1*/
#define	_TURN1				BIT(0)

/*FDLOCKFLAG0*/
/*FDLOCKFLAG1*/
#define	_LOCKFLAG1_MSK		0x03


#endif /* __RTL8712_DEBUGCTRL_BITDEF_H__ */
