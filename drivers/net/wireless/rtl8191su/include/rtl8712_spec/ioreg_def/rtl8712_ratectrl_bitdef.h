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
#ifndef __RTL8712_RATECTRL_BITDEF_H__
#define __RTL8712_RATECTRL_BITDEF_H__

//INIRTSMCS_SEL
#define	_INIRTSMCS_SEL_MSK		0x3F

// RRSR
#define	_RRSR_SHORT			BIT(23)
#define	_RRSR_RSC_MSK		0x600000
#define	_RRSR_RSC_SHT		21
#define	_RRSR_BITMAP_MSK	0x0FFFFF
#define	_RRSR_BITMAP_SHT	0

// AGGLEN_LMT_H
#define	_AGGLMT_MCS32_MSK			0xF0
#define	_AGGLMT_MCS32_SHT			4
#define	_AGGLMT_MCS15_SGI_MSK		0x0F
#define	_AGGLMT_MCS15_SGI_SHT		0

// DARFRC
// RARFRC
// MCS_TXAGC
// CCK_TXAGC
#define	_CCK_MSK			0xFF00
#define	_CCK_SHT			8
#define	_BARKER_MSK			0x00FF
#define	_BARKER_SHT			0

#endif	//	__RTL8712_RATECTRL_BITDEF_H__
