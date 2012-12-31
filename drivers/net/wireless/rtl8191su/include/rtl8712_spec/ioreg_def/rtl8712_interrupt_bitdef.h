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
#ifndef __RTL8712_INTERRUPT_BITDEF_H__
#define __RTL8712_INTERRUPT_BITDEF_H__

//HIMR
//HISR
#define	_CPUERR					BIT(29)
#define	_ATIMEND				BIT(28)
#define	_TXBCNOK				BIT(27)
#define	_TXBCNERR				BIT(26)
#define	_BCNDMAINT4				BIT(25)
#define	_BCNDMAINT3				BIT(24)
#define	_BCNDMAINT2				BIT(23)
#define	_BCNDMAINT1				BIT(22)
#define	_BCNDOK4				BIT(21)
#define	_BCNDOK3				BIT(20)
#define	_BCNDOK2				BIT(19)
#define	_BCNDOK1				BIT(18)
#define	_TIMEOUT2				BIT(17)
#define	_TIMEOUT1				BIT(16)
#define	_TXFOVW					BIT(15)
#define	_PSTIMEOUT				BIT(14)
#define	_BCNDMAINT0				BIT(13)
#define	_FOVW					BIT(12)
#define	_RDU					BIT(11)
#define	_RXCMDOK				BIT(10)
#define	_BCNDOK0				BIT(9)
#define	_HIGHDOK				BIT(8)
#define	_COMDOK					BIT(7)
#define	_MGTDOK					BIT(6)
#define	_HCCADOK				BIT(5)
#define	_BKDOK					BIT(4)
#define	_BEDOK					BIT(3)
#define	_VIDOK					BIT(2)
#define	_VODOK					BIT(1)
#define	_RXOK					BIT(0)


#endif	//__RTL8712_INTERRUPT_BITDEF_H__

