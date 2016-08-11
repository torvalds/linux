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
//============================================================
// File Name: odm_reg.h
//
// Description:
//
// This file is for general register definition.
//
//
//============================================================
#ifndef	__HAL_ODM_REG_H__
#define __HAL_ODM_REG_H__

//
// Register Definition
//

//MAC REG
#define	ODM_BB_RESET					0x002
#define	ODM_DUMMY						0x4fe
#define	RF_T_METER_OLD				0x24
#define	RF_T_METER_NEW				0x42

#define	ODM_EDCA_VO_PARAM			0x500
#define	ODM_EDCA_VI_PARAM			0x504
#define	ODM_EDCA_BE_PARAM			0x508
#define	ODM_EDCA_BK_PARAM			0x50C
#define	ODM_TXPAUSE					0x522

//BB REG
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

//RF REG
#define	ODM_GAIN_SETTING				0x00
#define	ODM_CHANNEL					0x18
#define	ODM_RF_T_METER				0x24
#define	ODM_RF_T_METER_92D			0x42
#define	ODM_RF_T_METER_88E			0x42
#define	ODM_RF_T_METER_92E			0x42
#define	ODM_RF_T_METER_8812			0x42

//Ant Detect Reg
#define	ODM_DPDT						0x300

//PSD Init
#define	ODM_PSDREG					0x808

//92D Path Div
#define	PATHDIV_REG					0xB30
#define	PATHDIV_TRI					0xBA0


//
// Bitmap Definition
//
#if(DM_ODM_SUPPORT_TYPE & (ODM_AP))
// TX AGC 
#define		rTxAGC_A_CCK11_CCK1_JAguar	0xc20
#define		rTxAGC_A_Ofdm18_Ofdm6_JAguar	0xc24
#define		rTxAGC_A_Ofdm54_Ofdm24_JAguar	0xc28
#define		rTxAGC_A_MCS3_MCS0_JAguar	0xc2c
#define		rTxAGC_A_MCS7_MCS4_JAguar	0xc30
#define		rTxAGC_A_MCS11_MCS8_JAguar	0xc34
#define		rTxAGC_A_MCS15_MCS12_JAguar	0xc38
#define		rTxAGC_A_Nss1Index3_Nss1Index0_JAguar	0xc3c
#define		rTxAGC_A_Nss1Index7_Nss1Index4_JAguar	0xc40
#define		rTxAGC_A_Nss2Index1_Nss1Index8_JAguar	0xc44
#define		rTxAGC_A_Nss2Index5_Nss2Index2_JAguar	0xc48
#define		rTxAGC_A_Nss2Index9_Nss2Index6_JAguar	0xc4c
#if defined(CONFIG_WLAN_HAL_8814AE)
#define		rTxAGC_A_MCS19_MCS16_JAguar	0xcd8
#define		rTxAGC_A_MCS23_MCS20_JAguar	0xcdc
#define		rTxAGC_A_Nss3Index3_Nss3Index0_JAguar	0xce0
#define		rTxAGC_A_Nss3Index7_Nss3Index4_JAguar	0xce4
#define		rTxAGC_A_Nss3Index9_Nss3Index8_JAguar	0xce8
#endif
#define		rTxAGC_B_CCK11_CCK1_JAguar	0xe20
#define		rTxAGC_B_Ofdm18_Ofdm6_JAguar	0xe24
#define		rTxAGC_B_Ofdm54_Ofdm24_JAguar	0xe28
#define		rTxAGC_B_MCS3_MCS0_JAguar	0xe2c
#define		rTxAGC_B_MCS7_MCS4_JAguar	0xe30
#define		rTxAGC_B_MCS11_MCS8_JAguar	0xe34
#define		rTxAGC_B_MCS15_MCS12_JAguar	0xe38
#define		rTxAGC_B_Nss1Index3_Nss1Index0_JAguar	0xe3c
#define		rTxAGC_B_Nss1Index7_Nss1Index4_JAguar	0xe40
#define		rTxAGC_B_Nss2Index1_Nss1Index8_JAguar	0xe44
#define		rTxAGC_B_Nss2Index5_Nss2Index2_JAguar	0xe48
#define		rTxAGC_B_Nss2Index9_Nss2Index6_JAguar	0xe4c
#if defined(CONFIG_WLAN_HAL_8814AE)
#define		rTxAGC_B_MCS19_MCS16_JAguar	0xed8
#define		rTxAGC_B_MCS23_MCS20_JAguar	0xedc
#define		rTxAGC_B_Nss3Index3_Nss3Index0_JAguar	0xee0
#define		rTxAGC_B_Nss3Index7_Nss3Index4_JAguar	0xee4
#define		rTxAGC_B_Nss3Index9_Nss3Index8_JAguar	0xee8
#define		rTxAGC_C_CCK11_CCK1_JAguar	0x1820
#define		rTxAGC_C_Ofdm18_Ofdm6_JAguar	0x1824
#define		rTxAGC_C_Ofdm54_Ofdm24_JAguar	0x1828
#define		rTxAGC_C_MCS3_MCS0_JAguar	0x182c
#define		rTxAGC_C_MCS7_MCS4_JAguar	0x1830
#define		rTxAGC_C_MCS11_MCS8_JAguar	0x1834
#define		rTxAGC_C_MCS15_MCS12_JAguar	0x1838
#define		rTxAGC_C_Nss1Index3_Nss1Index0_JAguar	0x183c
#define		rTxAGC_C_Nss1Index7_Nss1Index4_JAguar	0x1840
#define		rTxAGC_C_Nss2Index1_Nss1Index8_JAguar	0x1844
#define		rTxAGC_C_Nss2Index5_Nss2Index2_JAguar	0x1848
#define		rTxAGC_C_Nss2Index9_Nss2Index6_JAguar	0x184c
#define		rTxAGC_C_MCS19_MCS16_JAguar	0x18d8
#define		rTxAGC_C_MCS23_MCS20_JAguar	0x18dc
#define		rTxAGC_C_Nss3Index3_Nss3Index0_JAguar	0x18e0
#define		rTxAGC_C_Nss3Index7_Nss3Index4_JAguar	0x18e4
#define		rTxAGC_C_Nss3Index9_Nss3Index8_JAguar	0x18e8
#define		rTxAGC_D_CCK11_CCK1_JAguar	0x1a20
#define		rTxAGC_D_Ofdm18_Ofdm6_JAguar	0x1a24
#define		rTxAGC_D_Ofdm54_Ofdm24_JAguar	0x1a28
#define		rTxAGC_D_MCS3_MCS0_JAguar	0x1a2c
#define		rTxAGC_D_MCS7_MCS4_JAguar	0x1a30
#define		rTxAGC_D_MCS11_MCS8_JAguar	0x1a34
#define		rTxAGC_D_MCS15_MCS12_JAguar	0x1a38
#define		rTxAGC_D_Nss1Index3_Nss1Index0_JAguar	0x1a3c
#define		rTxAGC_D_Nss1Index7_Nss1Index4_JAguar	0x1a40
#define		rTxAGC_D_Nss2Index1_Nss1Index8_JAguar	0x1a44
#define		rTxAGC_D_Nss2Index5_Nss2Index2_JAguar	0x1a48
#define		rTxAGC_D_Nss2Index9_Nss2Index6_JAguar	0x1a4c
#define		rTxAGC_D_MCS19_MCS16_JAguar	0x1ad8
#define		rTxAGC_D_MCS23_MCS20_JAguar	0x1adc
#define		rTxAGC_D_Nss3Index3_Nss3Index0_JAguar	0x1ae0
#define		rTxAGC_D_Nss3Index7_Nss3Index4_JAguar	0x1ae4
#define		rTxAGC_D_Nss3Index9_Nss3Index8_JAguar	0x1ae8
#endif

#define		bTxAGC_byte0_Jaguar	0xff
#define		bTxAGC_byte1_Jaguar	0xff00
#define		bTxAGC_byte2_Jaguar	0xff0000
#define		bTxAGC_byte3_Jaguar	0xff000000
#endif

#define	BIT_FA_RESET					BIT0



#endif

