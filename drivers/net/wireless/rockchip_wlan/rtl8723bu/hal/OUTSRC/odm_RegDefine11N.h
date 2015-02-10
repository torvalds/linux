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
 
#ifndef	__ODM_REGDEFINE11N_H__
#define __ODM_REGDEFINE11N_H__


//2 RF REG LIST
#define	ODM_REG_RF_MODE_11N				0x00
#define	ODM_REG_RF_0B_11N				0x0B
#define	ODM_REG_CHNBW_11N				0x18
#define	ODM_REG_T_METER_11N				0x24
#define	ODM_REG_RF_25_11N				0x25
#define	ODM_REG_RF_26_11N				0x26
#define	ODM_REG_RF_27_11N				0x27
#define	ODM_REG_RF_2B_11N				0x2B
#define	ODM_REG_RF_2C_11N				0x2C
#define	ODM_REG_RXRF_A3_11N				0x3C
#define	ODM_REG_T_METER_92D_11N			0x42
#define	ODM_REG_T_METER_88E_11N			0x42



//2 BB REG LIST
//PAGE 8
#define	ODM_REG_BB_CTRL_11N				0x800
#define	ODM_REG_RF_PIN_11N				0x804
#define	ODM_REG_PSD_CTRL_11N				0x808
#define	ODM_REG_TX_ANT_CTRL_11N			0x80C
#define	ODM_REG_BB_PWR_SAV5_11N		0x818
#define	ODM_REG_CCK_RPT_FORMAT_11N		0x824
#define	ODM_REG_RX_DEFUALT_A_11N		0x858
#define	ODM_REG_RX_DEFUALT_B_11N		0x85A
#define	ODM_REG_BB_PWR_SAV3_11N		0x85C
#define	ODM_REG_ANTSEL_CTRL_11N			0x860
#define	ODM_REG_RX_ANT_CTRL_11N			0x864
#define	ODM_REG_PIN_CTRL_11N				0x870
#define	ODM_REG_BB_PWR_SAV1_11N		0x874
#define	ODM_REG_ANTSEL_PATH_11N			0x878
#define	ODM_REG_BB_3WIRE_11N			0x88C
#define	ODM_REG_SC_CNT_11N				0x8C4
#define	ODM_REG_PSD_DATA_11N				0x8B4
#define	ODM_REG_PSD_DATA_11N				0x8B4
#define	ODM_REG_NHM_TIMER_11N			0x894
#define	ODM_REG_NHM_TH9_TH10_11N		0x890
#define	ODM_REG_NHM_TH3_TO_TH0_11N		0x898
#define	ODM_REG_NHM_TH7_TO_TH4_11N		0x89c
#define	ODM_REG_NHM_CNT_11N				0x8d8
//PAGE 9
#define	ODM_REG_DBG_RPT_11N				0x908
#define	ODM_REG_ANT_MAPPING1_11N		0x914
#define	ODM_REG_ANT_MAPPING2_11N		0x918
//PAGE A
#define	ODM_REG_CCK_ANTDIV_PARA1_11N	0xA00
#define	ODM_REG_CCK_CCA_11N				0xA0A
#define	ODM_REG_CCK_ANTDIV_PARA2_11N	0xA0C
#define	ODM_REG_CCK_ANTDIV_PARA3_11N	0xA10
#define	ODM_REG_CCK_ANTDIV_PARA4_11N	0xA14
#define	ODM_REG_CCK_FILTER_PARA1_11N	0xA22
#define	ODM_REG_CCK_FILTER_PARA2_11N	0xA23
#define	ODM_REG_CCK_FILTER_PARA3_11N	0xA24
#define	ODM_REG_CCK_FILTER_PARA4_11N	0xA25
#define	ODM_REG_CCK_FILTER_PARA5_11N	0xA26
#define	ODM_REG_CCK_FILTER_PARA6_11N	0xA27
#define	ODM_REG_CCK_FILTER_PARA7_11N	0xA28
#define	ODM_REG_CCK_FILTER_PARA8_11N	0xA29
#define	ODM_REG_CCK_FA_RST_11N			0xA2C
#define	ODM_REG_CCK_FA_MSB_11N			0xA58
#define	ODM_REG_CCK_FA_LSB_11N			0xA5C
#define	ODM_REG_CCK_CCA_CNT_11N			0xA60
#define	ODM_REG_BB_PWR_SAV4_11N		0xA74
//PAGE B
#define	ODM_REG_LNA_SWITCH_11N			0xB2C
#define	ODM_REG_PATH_SWITCH_11N			0xB30
#define	ODM_REG_RSSI_CTRL_11N			0xB38
#define	ODM_REG_CONFIG_ANTA_11N			0xB68
#define	ODM_REG_RSSI_BT_11N				0xB9C
//PAGE C
#define	ODM_REG_OFDM_FA_HOLDC_11N		0xC00
#define	ODM_REG_BB_RX_PATH_11N			0xC04
#define	ODM_REG_TRMUX_11N				0xC08
#define	ODM_REG_OFDM_FA_RSTC_11N		0xC0C
#define	ODM_REG_RXIQI_MATRIX_11N			0xC14
#define	ODM_REG_TXIQK_MATRIX_LSB1_11N	0xC4C
#define	ODM_REG_IGI_A_11N					0xC50
#define	ODM_REG_ANTDIV_PARA2_11N		0xC54
#define	ODM_REG_IGI_B_11N					0xC58
#define	ODM_REG_ANTDIV_PARA3_11N		0xC5C
#define   ODM_REG_L1SBD_PD_CH_11N			0XC6C
#define	ODM_REG_BB_PWR_SAV2_11N		0xC70
#define	ODM_REG_RX_OFF_11N				0xC7C
#define	ODM_REG_TXIQK_MATRIXA_11N		0xC80
#define	ODM_REG_TXIQK_MATRIXB_11N		0xC88
#define	ODM_REG_TXIQK_MATRIXA_LSB2_11N	0xC94
#define	ODM_REG_TXIQK_MATRIXB_LSB2_11N	0xC9C
#define	ODM_REG_RXIQK_MATRIX_LSB_11N	0xCA0
#define	ODM_REG_ANTDIV_PARA1_11N		0xCA4
#define	ODM_REG_OFDM_FA_TYPE1_11N		0xCF0
//PAGE D
#define	ODM_REG_OFDM_FA_RSTD_11N		0xD00
#define	ODM_REG_BB_ATC_11N				0xD2C
#define	ODM_REG_OFDM_FA_TYPE2_11N		0xDA0
#define	ODM_REG_OFDM_FA_TYPE3_11N		0xDA4
#define	ODM_REG_OFDM_FA_TYPE4_11N		0xDA8
#define	ODM_REG_RPT_11N					0xDF4
//PAGE E
#define	ODM_REG_TXAGC_A_6_18_11N		0xE00
#define	ODM_REG_TXAGC_A_24_54_11N		0xE04
#define	ODM_REG_TXAGC_A_1_MCS32_11N	0xE08
#define	ODM_REG_TXAGC_A_MCS0_3_11N		0xE10
#define	ODM_REG_TXAGC_A_MCS4_7_11N		0xE14
#define	ODM_REG_TXAGC_A_MCS8_11_11N	0xE18
#define	ODM_REG_TXAGC_A_MCS12_15_11N	0xE1C
#define	ODM_REG_FPGA0_IQK_11N			0xE28
#define	ODM_REG_TXIQK_TONE_A_11N		0xE30
#define	ODM_REG_RXIQK_TONE_A_11N		0xE34
#define	ODM_REG_TXIQK_PI_A_11N			0xE38
#define	ODM_REG_RXIQK_PI_A_11N			0xE3C
#define	ODM_REG_TXIQK_11N				0xE40
#define	ODM_REG_RXIQK_11N				0xE44
#define	ODM_REG_IQK_AGC_PTS_11N			0xE48
#define	ODM_REG_IQK_AGC_RSP_11N			0xE4C
#define	ODM_REG_BLUETOOTH_11N			0xE6C
#define	ODM_REG_RX_WAIT_CCA_11N			0xE70
#define	ODM_REG_TX_CCK_RFON_11N			0xE74
#define	ODM_REG_TX_CCK_BBON_11N			0xE78
#define	ODM_REG_OFDM_RFON_11N			0xE7C
#define	ODM_REG_OFDM_BBON_11N			0xE80
#define 	ODM_REG_TX2RX_11N				0xE84
#define	ODM_REG_TX2TX_11N				0xE88
#define	ODM_REG_RX_CCK_11N				0xE8C
#define	ODM_REG_RX_OFDM_11N				0xED0
#define	ODM_REG_RX_WAIT_RIFS_11N		0xED4
#define	ODM_REG_RX2RX_11N				0xED8
#define	ODM_REG_STANDBY_11N				0xEDC
#define	ODM_REG_SLEEP_11N				0xEE0
#define	ODM_REG_PMPD_ANAEN_11N			0xEEC
#define	ODM_REG_IGI_C_11N					0xF84
#define	ODM_REG_IGI_D_11N					0xF88

//2 MAC REG LIST
#define	ODM_REG_BB_RST_11N				0x02
#define	ODM_REG_ANTSEL_PIN_11N			0x4C
#define	ODM_REG_EARLY_MODE_11N			0x4D0
#define	ODM_REG_RSSI_MONITOR_11N		0x4FE
#define	ODM_REG_EDCA_VO_11N				0x500
#define	ODM_REG_EDCA_VI_11N				0x504
#define	ODM_REG_EDCA_BE_11N				0x508
#define	ODM_REG_EDCA_BK_11N				0x50C
#define	ODM_REG_TXPAUSE_11N				0x522
#define	ODM_REG_RESP_TX_11N				0x6D8
#define	ODM_REG_ANT_TRAIN_PARA1_11N		0x7b0
#define	ODM_REG_ANT_TRAIN_PARA2_11N		0x7b4


//DIG Related
#define	ODM_BIT_IGI_11N					0x0000007F
#define	ODM_BIT_CCK_RPT_FORMAT_11N		BIT9
#define	ODM_BIT_BB_RX_PATH_11N			0xF
#define	ODM_BIT_BB_ATC_11N				BIT11

#endif

