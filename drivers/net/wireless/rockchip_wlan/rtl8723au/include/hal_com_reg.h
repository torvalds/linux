/******************************************************************************
 *
 * Copyright(c) 2007 - 2013 Realtek Corporation. All rights reserved.
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
#ifndef __HAL_COMMON_REG_H__
#define __HAL_COMMON_REG_H__

//-----------------------------------------------------
//
//	0x0400h ~ 0x047Fh	Protocol Configuration
//
//-----------------------------------------------------

/* 92C, 92D */
#define REG_VOQ_INFO	0x0400
#define REG_VIQ_INFO	0x0404
#define REG_BEQ_INFO	0x0408
#define REG_BKQ_INFO	0x040C

/* 88E, 8723A, 8812A, 8821A, 92E, 8723B */
#define REG_Q0_INFO	0x400
#define REG_Q1_INFO	0x404
#define REG_Q2_INFO	0x408
#define REG_Q3_INFO	0x40C

#define REG_MGQ_INFO	0x0410
#define REG_HGQ_INFO	0x0414
#define REG_BCNQ_INFO	0x0418

/* 8723A, 8812A, 8821A, 92E, 8723B */
#define REG_Q4_INFO	0x468
#define REG_Q5_INFO	0x46C
#define REG_Q6_INFO	0x470
#define REG_Q7_INFO	0x474

/* 8723A */
#define REG_MACID_DROP	0x04D0

/* 8723A, 8723B, 92E, 8812A, 8821A */
#define REG_MACID_SLEEP	0x04D4

// Security
#define REG_CAMCMD		0x0670
#define REG_CAMWRITE	0x0674
#define REG_CAMREAD		0x0678
#define REG_CAMDBG		0x067C
#define REG_SECCFG		0x0680

//----------------------------------------------------------------------------
//       CAM Config Setting (offset 0x680, 1 byte)
//----------------------------------------------------------------------------   	       		
#define CAM_VALID				BIT15
#define CAM_NOTVALID			0x0000
#define CAM_USEDK				BIT5

#define CAM_CONTENT_COUNT 	8

#define CAM_NONE				0x0
#define CAM_WEP40				0x01
#define CAM_TKIP				0x02
#define CAM_AES					0x04
#define CAM_WEP104				0x05
#define CAM_SMS4				0x6

#define TOTAL_CAM_ENTRY		32
#define HALF_CAM_ENTRY			16	
       		
#define CAM_CONFIG_USEDK		_TRUE
#define CAM_CONFIG_NO_USEDK	_FALSE

#define CAM_WRITE				BIT16
#define CAM_READ				0x00000000
#define CAM_POLLINIG			BIT31


//2 SECCFG
#define	SCR_TxUseDK				BIT(0)			//Force Tx Use Default Key
#define	SCR_RxUseDK				BIT(1)			//Force Rx Use Default Key
#define	SCR_TxEncEnable			BIT(2)			//Enable Tx Encryption
#define	SCR_RxDecEnable			BIT(3)			//Enable Rx Decryption
#define	SCR_SKByA2				BIT(4)			//Search kEY BY A2
#define	SCR_NoSKMC				BIT(5)			//No Key Search Multicast
#define SCR_TXBCUSEDK			BIT(6)			// Force Tx Broadcast packets Use Default Key
#define SCR_RXBCUSEDK			BIT(7)			// Force Rx Broadcast packets Use Default Key
#define SCR_CHK_KEYID			BIT(8)

#endif //__HAL_COMMON_H__

