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
 ******************************************************************************/
/*  */
/*  File Name: odm_reg.h */
/*  */
/*  Description: */
/*  */
/*  This file is for general register definition. */
/*  */
/*  */
/*  */
#ifndef	__HAL_ODM_REG_H__
#define __HAL_ODM_REG_H__

/*  */
/*  Register Definition */
/*  */

/* MAC REG */
#define	ODM_BB_RESET					0x002
#define	ODM_DUMMY						0x4fe
#define	ODM_EDCA_VO_PARAM			0x500
#define	ODM_EDCA_VI_PARAM			0x504
#define	ODM_EDCA_BE_PARAM			0x508
#define	ODM_EDCA_BK_PARAM			0x50C
#define	ODM_TXPAUSE					0x522

/* BB REG */
#define	ODM_FPGA_PHY0_PAGE8			0x800
#define	ODM_PSD_SETTING				0x808
#define	ODM_AFE_SETTING				0x818
#define	ODM_TXAGC_B_6_18				0x830
#define	ODM_TXAGC_B_24_54			0x834
#define	ODM_TXAGC_B_MCS32_5			0x838
#define	ODM_TXAGC_B_MCS0_MCS3		0x83c
#define	ODM_TXAGC_B_MCS4_MCS7		0x848
#define	ODM_TXAGC_B_MCS8_MCS11		0x84c
#define	ODM_ANALOG_REGISTER			0x85c
#define	ODM_RF_INTERFACE_OUTPUT		0x860
#define	ODM_TXAGC_B_MCS12_MCS15	0x868
#define	ODM_TXAGC_B_11_A_2_11		0x86c
#define	ODM_AD_DA_LSB_MASK			0x874
#define	ODM_ENABLE_3_WIRE			0x88c
#define	ODM_PSD_REPORT				0x8b4
#define	ODM_R_ANT_SELECT				0x90c
#define	ODM_CCK_ANT_SELECT			0xa07
#define	ODM_CCK_PD_THRESH			0xa0a
#define	ODM_CCK_RF_REG1				0xa11
#define	ODM_CCK_MATCH_FILTER			0xa20
#define	ODM_CCK_RAKE_MAC				0xa2e
#define	ODM_CCK_CNT_RESET			0xa2d
#define	ODM_CCK_TX_DIVERSITY			0xa2f
#define	ODM_CCK_FA_CNT_MSB			0xa5b
#define	ODM_CCK_FA_CNT_LSB			0xa5c
#define	ODM_CCK_NEW_FUNCTION		0xa75
#define	ODM_OFDM_PHY0_PAGE_C		0xc00
#define	ODM_OFDM_RX_ANT				0xc04
#define	ODM_R_A_RXIQI					0xc14
#define	ODM_R_A_AGC_CORE1			0xc50
#define	ODM_R_A_AGC_CORE2			0xc54
#define	ODM_R_B_AGC_CORE1			0xc58
#define	ODM_R_AGC_PAR					0xc70
#define	ODM_R_HTSTF_AGC_PAR			0xc7c
#define	ODM_TX_PWR_TRAINING_A		0xc90
#define	ODM_TX_PWR_TRAINING_B		0xc98
#define	ODM_OFDM_FA_CNT1				0xcf0
#define	ODM_OFDM_PHY0_PAGE_D		0xd00
#define	ODM_OFDM_FA_CNT2				0xda0
#define	ODM_OFDM_FA_CNT3				0xda4
#define	ODM_OFDM_FA_CNT4				0xda8
#define	ODM_TXAGC_A_6_18				0xe00
#define	ODM_TXAGC_A_24_54			0xe04
#define	ODM_TXAGC_A_1_MCS32			0xe08
#define	ODM_TXAGC_A_MCS0_MCS3		0xe10
#define	ODM_TXAGC_A_MCS4_MCS7		0xe14
#define	ODM_TXAGC_A_MCS8_MCS11		0xe18
#define	ODM_TXAGC_A_MCS12_MCS15		0xe1c

/* RF REG */
#define	ODM_GAIN_SETTING				0x00
#define	ODM_CHANNEL					0x18

/* Ant Detect Reg */
#define	ODM_DPDT						0x300

/* PSD Init */
#define	ODM_PSDREG					0x808

/* 92D Path Div */
#define	PATHDIV_REG					0xB30
#define	PATHDIV_TRI					0xBA0


/*  */
/*  Bitmap Definition */
/*  */

#define	BIT_FA_RESET					BIT(0)



#endif
