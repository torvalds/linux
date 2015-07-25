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

#ifndef __RTL8192D_SPEC_H__
#define __RTL8192D_SPEC_H__

#include <drv_conf.h>

//============================================================
//       8192D Regsiter offset definition
//============================================================


//============================================================
//
//============================================================

//-----------------------------------------------------
//
//	0x0000h ~ 0x00FFh	System Configuration
//
//-----------------------------------------------------
#define REG_HMEBOX_EXT_0			0x0088
#define REG_HMEBOX_EXT_1			0x008A
#define REG_HMEBOX_EXT_2			0x008C
#define REG_HMEBOX_EXT_3			0x008E
#define REG_MAC_PHY_CTRL_NORMAL	0x00F8
#define REG_MAC0                                	0x0081
#define REG_MAC1                                	0x0053
#define FW_MAC0_ready                      	0x18
#define FW_MAC1_ready                      	0x1A
#define MAC0_ON                                  	BIT7
#define MAC1_ON                                  	BIT0
#define mac0_ready                             	BIT0
#define mac1_ready                             	BIT0

//-----------------------------------------------------
//
//	0x0100h ~ 0x01FFh	MACTOP General Configuration
//
//-----------------------------------------------------

//-----------------------------------------------------
//
//	0x0200h ~ 0x027Fh	TXDMA Configuration
//
//-----------------------------------------------------

//-----------------------------------------------------
//
//	0x0280h ~ 0x02FFh	RXDMA Configuration
//
//-----------------------------------------------------

//-----------------------------------------------------
//
//	0x0300h ~ 0x03FFh	PCIe
//
//-----------------------------------------------------

//-----------------------------------------------------
//
//	0x0400h ~ 0x047Fh	Protocol Configuration
//
//-----------------------------------------------------

//-----------------------------------------------------
//
//	0x0500h ~ 0x05FFh	EDCA Configuration
//
//-----------------------------------------------------

//-----------------------------------------------------
//
//	0x0600h ~ 0x07FFh	WMAC Configuration
//
//-----------------------------------------------------

//-----------------------------------------------------
//
//	0xFE00h ~ 0xFE55h	USB Configuration
//
//-----------------------------------------------------


#ifdef CONFIG_PCI_HCI
#define RT_BCN_INT_MASKS				(IMR_BcnInt | IMR_TBDOK | IMR_TBDER)
#define RT_AC_INT_MASKS				(IMR_VIDOK | IMR_VODOK | IMR_BEDOK|IMR_BKDOK)
#endif

//========================================================
// General definitions
//========================================================

#include "basic_types.h"

#endif

