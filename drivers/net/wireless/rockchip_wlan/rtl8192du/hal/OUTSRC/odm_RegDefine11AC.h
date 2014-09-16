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
 
#ifndef	__ODM_REGDEFINE11AC_H__
#define __ODM_REGDEFINE11AC_H__

//2 RF REG LIST



//2 BB REG LIST
//PAGE 8
#define	ODM_REG_CCK_RPT_FORMAT_11AC	0x804
#define	ODM_REG_BB_RX_PATH_11AC			0x808
#define	ODM_REG_DBG_RPT_11AC				0x8fc
//PAGE 9
#define	ODM_REG_OFDM_FA_RST_11AC		0x9A4
#define	ODM_REG_NHM_TIMER_11AC			0x990
#define	ODM_REG_NHM_TH9_TH10_11AC			0x994
#define	ODM_REG_NHM_TH3_TO_TH0_11AC		0x998
#define	ODM_REG_NHM_TH7_TO_TH4_11AC		0x99c
#define	ODM_REG_NHM_TH8_11AC				0x9a0
#define	ODM_REG_NHM_9E8_11AC				0x9e8
//PAGE A
#define	ODM_REG_CCK_CCA_11AC			0xA0A
#define	ODM_REG_CCK_FA_RST_11AC			0xA2C
#define	ODM_REG_CCK_FA_11AC				0xA5C
//PAGE C
#define	ODM_REG_TRMUX_11AC				0xC08
#define	ODM_REG_IGI_A_11AC				0xC50
//PAGE E
#define	ODM_REG_IGI_B_11AC				0xE50
//PAGE F
#define	ODM_REG_OFDM_FA_11AC			0xF48
#define	ODM_REG_RPT_11AC					0xfa0
#define	ODM_REG_NHM_CNT_11AC			0xfa8
//PAGE 18
#define	ODM_REG_IGI_C_11AC				0x1850
//PAGE 1A
#define	ODM_REG_IGI_D_11AC				0x1A50

//2 MAC REG LIST
#define	ODM_REG_RESP_TX_11AC				0x6D8



//DIG Related
#define	ODM_BIT_IGI_11AC					0xFFFFFFFF
#define	ODM_BIT_CCK_RPT_FORMAT_11AC		BIT16
#define	ODM_BIT_BB_RX_PATH_11AC			0xF

#endif

