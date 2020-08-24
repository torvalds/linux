/* SPDX-License-Identifier: GPL-2.0 */
/******************************************************************************
 *
 * Copyright(c) 2007 - 2017  Realtek Corporation.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of version 2 of the GNU General Public License as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * The full GNU General Public License is included in this distribution in the
 * file called LICENSE.
 *
 * Contact Information:
 * wlanfae <wlanfae@realtek.com>
 * Realtek Corporation, No. 2, Innovation Road II, Hsinchu Science Park,
 * Hsinchu 300, Taiwan.
 *
 * Larry Finger <Larry.Finger@lwfinger.net>
 *
 *****************************************************************************/
/*************************************************************
 * File Name: odm_reg.h
 *
 * Description:
 *
 * This file is for general register definition.
 *
 *
 ************************************************************/
#ifndef __HAL_ODM_REG_H__
#define __HAL_ODM_REG_H__

/*@
 * Register Definition
 *
 */

/* @MAC REG */
#define	ODM_BB_RESET				0x002
#define	ODM_DUMMY				0x4fe
#define	RF_T_METER_OLD				0x24
#define	RF_T_METER_NEW				0x42

#define	ODM_EDCA_VO_PARAM			0x500
#define	ODM_EDCA_VI_PARAM			0x504
#define	ODM_EDCA_BE_PARAM			0x508
#define	ODM_EDCA_BK_PARAM			0x50C
#define	ODM_TXPAUSE				0x522

/* @LTE_COEX */
#define REG_LTECOEX_CTRL			0x07C0
#define REG_LTECOEX_WRITE_DATA			0x07C4
#define REG_LTECOEX_READ_DATA			0x07C8
#define REG_LTECOEX_PATH_CONTROL		0x70

/* @BB REG */
#define	ODM_FPGA_PHY0_PAGE8			0x800
#define	ODM_PSD_SETTING				0x808
#define	ODM_AFE_SETTING				0x818
#define	ODM_TXAGC_B_6_18			0x830
#define	ODM_TXAGC_B_24_54			0x834
#define	ODM_TXAGC_B_MCS32_5			0x838
#define	ODM_TXAGC_B_MCS0_MCS3			0x83c
#define	ODM_TXAGC_B_MCS4_MCS7			0x848
#define	ODM_TXAGC_B_MCS8_MCS11			0x84c
#define	ODM_ANALOG_REGISTER			0x85c
#define	ODM_RF_INTERFACE_OUTPUT			0x860
#define	ODM_TXAGC_B_MCS12_MCS15			0x868
#define	ODM_TXAGC_B_11_A_2_11			0x86c
#define	ODM_AD_DA_LSB_MASK			0x874
#define	ODM_ENABLE_3_WIRE			0x88c
#define	ODM_PSD_REPORT				0x8b4
#define	ODM_R_ANT_SELECT			0x90c
#define	ODM_CCK_ANT_SELECT			0xa07
#define	ODM_CCK_PD_THRESH			0xa0a
#define	ODM_CCK_RF_REG1				0xa11
#define	ODM_CCK_MATCH_FILTER			0xa20
#define	ODM_CCK_RAKE_MAC			0xa2e
#define	ODM_CCK_CNT_RESET			0xa2d
#define	ODM_CCK_TX_DIVERSITY			0xa2f
#define	ODM_CCK_FA_CNT_MSB			0xa5b
#define	ODM_CCK_FA_CNT_LSB			0xa5c
#define	ODM_CCK_NEW_FUNCTION			0xa75
#define	ODM_OFDM_PHY0_PAGE_C			0xc00
#define	ODM_OFDM_RX_ANT				0xc04
#define	ODM_R_A_RXIQI				0xc14
#define	ODM_R_A_AGC_CORE1			0xc50
#define	ODM_R_A_AGC_CORE2			0xc54
#define	ODM_R_B_AGC_CORE1			0xc58
#define	ODM_R_AGC_PAR				0xc70
#define	ODM_R_HTSTF_AGC_PAR			0xc7c
#define	ODM_TX_PWR_TRAINING_A			0xc90
#define	ODM_TX_PWR_TRAINING_B			0xc98
#define	ODM_OFDM_FA_CNT1			0xcf0
#define	ODM_OFDM_PHY0_PAGE_D			0xd00
#define	ODM_OFDM_FA_CNT2			0xda0
#define	ODM_OFDM_FA_CNT3			0xda4
#define	ODM_OFDM_FA_CNT4			0xda8
#define	ODM_TXAGC_A_6_18			0xe00
#define	ODM_TXAGC_A_24_54			0xe04
#define	ODM_TXAGC_A_1_MCS32			0xe08
#define	ODM_TXAGC_A_MCS0_MCS3			0xe10
#define	ODM_TXAGC_A_MCS4_MCS7			0xe14
#define	ODM_TXAGC_A_MCS8_MCS11			0xe18
#define	ODM_TXAGC_A_MCS12_MCS15			0xe1c

/* RF REG */
#define	ODM_GAIN_SETTING			0x00
#define	ODM_CHANNEL				0x18
#define	ODM_RF_T_METER				0x24
#define	ODM_RF_T_METER_92D			0x42
#define	ODM_RF_T_METER_88E			0x42
#define	ODM_RF_T_METER_92E			0x42
#define	ODM_RF_T_METER_8812			0x42
#define	REG_RF_TX_GAIN_OFFSET			0x55

/* @ant Detect Reg */
#define	ODM_DPDT				0x300

/* PSD Init */
#define	ODM_PSDREG				0x808

/* @92D path Div */
#define	PATHDIV_REG				0xB30
#define	PATHDIV_TRI				0xBA0


/*@
 * Bitmap Definition
 */
#if (DM_ODM_SUPPORT_TYPE & (ODM_AP))
	/* TX AGC */
	#define		REG_TX_AGC_A_CCK_11_CCK_1_JAGUAR		0xc20
	#define		REG_TX_AGC_A_OFDM18_OFDM6_JAGUAR		0xc24
	#define		REG_TX_AGC_A_OFDM54_OFDM24_JAGUAR		0xc28
	#define		REG_TX_AGC_A_MCS3_MCS0_JAGUAR			0xc2c
	#define		REG_TX_AGC_A_MCS7_MCS4_JAGUAR			0xc30
	#define		REG_TX_AGC_A_MCS11_MCS8_JAGUAR			0xc34
	#define		REG_TX_AGC_A_MCS15_MCS12_JAGUAR			0xc38
	#define		REG_TX_AGC_A_NSS1_INDEX3_NSS1_INDEX0_JAGUAR	0xc3c
	#define		REG_TX_AGC_A_NSS1_INDEX7_NSS1_INDEX4_JAGUAR	0xc40
	#define		REG_TX_AGC_A_NSS2_INDEX1_NSS1_INDEX8_JAGUAR	0xc44
	#define		REG_TX_AGC_A_NSS2_INDEX5_NSS2_INDEX2_JAGUAR	0xc48
	#define		REG_TX_AGC_A_NSS2_INDEX9_NSS2_INDEX6_JAGUAR	0xc4c
	#if defined(CONFIG_WLAN_HAL_8814AE)
		#define		REG_TX_AGC_A_MCS19_MCS16_JAGUAR		0xcd8
		#define		REG_TX_AGC_A_MCS23_MCS20_JAGUAR		0xcdc
		#define		REG_TX_AGC_A_NSS3_INDEX3_NSS3_INDEX0_JAGUAR	0xce0
		#define		REG_TX_AGC_A_NSS3_INDEX7_NSS3_INDEX4_JAGUAR	0xce4
		#define		REG_TX_AGC_A_NSS3_INDEX9_NSS3_INDEX8_JAGUAR	0xce8
	#endif
	#define		REG_TX_AGC_B_CCK_11_CCK_1_JAGUAR		0xe20
	#define		REG_TX_AGC_B_OFDM18_OFDM6_JAGUAR		0xe24
	#define		REG_TX_AGC_B_OFDM54_OFDM24_JAGUAR		0xe28
	#define		REG_TX_AGC_B_MCS3_MCS0_JAGUAR			0xe2c
	#define		REG_TX_AGC_B_MCS7_MCS4_JAGUAR			0xe30
	#define		REG_TX_AGC_B_MCS11_MCS8_JAGUAR			0xe34
	#define		REG_TX_AGC_B_MCS15_MCS12_JAGUAR			0xe38
	#define		REG_TX_AGC_B_NSS1_INDEX3_NSS1_INDEX0_JAGUAR	0xe3c
	#define		REG_TX_AGC_B_NSS1_INDEX7_NSS1_INDEX4_JAGUAR	0xe40
	#define		REG_TX_AGC_B_NSS2_INDEX1_NSS1_INDEX8_JAGUAR	0xe44
	#define		REG_TX_AGC_B_NSS2_INDEX5_NSS2_INDEX2_JAGUAR	0xe48
	#define		REG_TX_AGC_B_NSS2_INDEX9_NSS2_INDEX6_JAGUAR	0xe4c
	#if defined(CONFIG_WLAN_HAL_8814AE)
		#define		REG_TX_AGC_B_MCS19_MCS16_JAGUAR		0xed8
		#define		REG_TX_AGC_B_MCS23_MCS20_JAGUAR		0xedc
		#define		REG_TX_AGC_B_NSS3_INDEX3_NSS3_INDEX0_JAGUAR	0xee0
		#define		REG_TX_AGC_B_NSS3_INDEX7_NSS3_INDEX4_JAGUAR	0xee4
		#define		REG_TX_AGC_B_NSS3_INDEX9_NSS3_INDEX8_JAGUAR	0xee8
		#define		REG_TX_AGC_C_CCK_11_CCK_1_JAGUAR	0x1820
		#define		REG_TX_AGC_C_OFDM18_OFDM6_JAGUAR	0x1824
		#define		REG_TX_AGC_C_OFDM54_OFDM24_JAGUAR	0x1828
		#define		REG_TX_AGC_C_MCS3_MCS0_JAGUAR		0x182c
		#define		REG_TX_AGC_C_MCS7_MCS4_JAGUAR		0x1830
		#define		REG_TX_AGC_C_MCS11_MCS8_JAGUAR		0x1834
		#define		REG_TX_AGC_C_MCS15_MCS12_JAGUAR		0x1838
		#define		REG_TX_AGC_C_NSS1_INDEX3_NSS1_INDEX0_JAGUAR	0x183c
		#define		REG_TX_AGC_C_NSS1_INDEX7_NSS1_INDEX4_JAGUAR	0x1840
		#define		REG_TX_AGC_C_NSS2_INDEX1_NSS1_INDEX8_JAGUAR	0x1844
		#define		REG_TX_AGC_C_NSS2_INDEX5_NSS2_INDEX2_JAGUAR	0x1848
		#define		REG_TX_AGC_C_NSS2_INDEX9_NSS2_INDEX6_JAGUAR	0x184c
		#define		REG_TX_AGC_C_MCS19_MCS16_JAGUAR		0x18d8
		#define		REG_TX_AGC_C_MCS23_MCS20_JAGUAR		0x18dc
		#define		REG_TX_AGC_C_NSS3_INDEX3_NSS3_INDEX0_JAGUAR	0x18e0
		#define		REG_TX_AGC_C_NSS3_INDEX7_NSS3_INDEX4_JAGUAR	0x18e4
		#define		REG_TX_AGC_C_NSS3_INDEX9_NSS3_INDEX8_JAGUAR	0x18e8
		#define		REG_TX_AGC_D_CCK_11_CCK_1_JAGUAR	0x1a20
		#define		REG_TX_AGC_D_OFDM18_OFDM6_JAGUAR	0x1a24
		#define		REG_TX_AGC_D_OFDM54_OFDM24_JAGUAR	0x1a28
		#define		REG_TX_AGC_D_MCS3_MCS0_JAGUAR		0x1a2c
		#define		REG_TX_AGC_D_MCS7_MCS4_JAGUAR		0x1a30
		#define		REG_TX_AGC_D_MCS11_MCS8_JAGUAR		0x1a34
		#define		REG_TX_AGC_D_MCS15_MCS12_JAGUAR		0x1a38
		#define		REG_TX_AGC_D_NSS1_INDEX3_NSS1_INDEX0_JAGUAR	0x1a3c
		#define		REG_TX_AGC_D_NSS1_INDEX7_NSS1_INDEX4_JAGUAR	0x1a40
		#define		REG_TX_AGC_D_NSS2_INDEX1_NSS1_INDEX8_JAGUAR	0x1a44
		#define		REG_TX_AGC_D_NSS2_INDEX5_NSS2_INDEX2_JAGUAR	0x1a48
		#define		REG_TX_AGC_D_NSS2_INDEX9_NSS2_INDEX6_JAGUAR	0x1a4c
		#define		REG_TX_AGC_D_MCS19_MCS16_JAGUAR		0x1ad8
		#define		REG_TX_AGC_D_MCS23_MCS20_JAGUAR		0x1adc
		#define		REG_TX_AGC_D_NSS3_INDEX3_NSS3_INDEX0_JAGUAR	0x1ae0
		#define		REG_TX_AGC_D_NSS3_INDEX7_NSS3_INDEX4_JAGUAR	0x1ae4
		#define		REG_TX_AGC_D_NSS3_INDEX9_NSS3_INDEX8_JAGUAR	0x1ae8
	#endif

	#define		is_tx_agc_byte0_jaguar	0xff
	#define		is_tx_agc_byte1_jaguar	0xff00
	#define		is_tx_agc_byte2_jaguar	0xff0000
	#define		is_tx_agc_byte3_jaguar	0xff000000
#if defined(CONFIG_WLAN_HAL_8198F) || defined(CONFIG_WLAN_HAL_8822CE) ||\
defined(CONFIG_WLAN_HAL_8814BE) || defined(CONFIG_WLAN_HAL_8812FE) ||\
defined(CONFIG_WLAN_HAL_8197G)
		#define REG_TX_AGC_CCK_11_CCK_1_JAGUAR3		0x3a00
		#define REG_TX_AGC_OFDM_18_CCK_6_JAGUAR3	0x3a04
		#define	REG_TX_AGC_OFDM_54_CCK_24_JAGUAR3	0x3a08
		#define	REG_TX_AGC_MCS3_0_JAGUAR3		0x3a0c
		#define	REG_TX_AGC_MCS7_4_JAGUAR3		0x3a10
		#define	REG_TX_AGC_MCS11_8_JAGUAR3		0x3a14
		#define	REG_TX_AGC_MCS15_12_JAGUAR3		0x3a18
		#define	REG_TX_AGC_MCS19_16_JAGUAR3		0x3a1c
		#define	REG_TX_AGC_MCS23_20_JAGUAR3		0x3a20
		#define	REG_TX_AGC_MCS27_24_JAGUAR3		0x3a24
		#define	REG_TX_AGC_MCS31_28_JAGUAR3		0x3a28
		#define	REG_TX_AGC_VHT_Nss1_MCS3_0_JAGUAR3	0x3a2c
		#define	REG_TX_AGC_VHT_Nss1_MCS7_4_JAGUAR3	0x3a30
		#define	REG_TX_AGC_VHT_NSS2_MCS1_NSS1_MCS8_JAGUAR3	0x3a34
		#define	REG_TX_AGC_VHT_Nss2_MCS5_2_JAGUAR3	0x3a38
		#define	REG_TX_AGC_VHT_Nss2_MCS9_6_JAGUAR3	0x3a3c
		#define	REG_TX_AGC_VHT_Nss3_MCS3_0_JAGUAR3	0x3a40
		#define	REG_TX_AGC_VHT_Nss3_MCS7_4_JAGUAR3	0x3a44
		#define	REG_TX_AGC_VHT_Nss4_MCS1_Nss3_MCS8_JAGUAR3	0x3a48
		#define	REG_TX_AGC_VHT_Nss4_MCS5_2_JAGUAR3	0x3a4c
		#define	REG_TX_AGC_VHT_Nss4_MCS9_6_JAGUAR3	0x3a50
#endif
#endif

#define	BIT_FA_RESET					BIT(0)

#endif
