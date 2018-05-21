/******************************************************************************
 *
 * Copyright(c) 2007 - 2017 Realtek Corporation.
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
 *****************************************************************************/

#ifndef	__ODM_REGDEFINE11AC_H__
#define __ODM_REGDEFINE11AC_H__

/* 2 RF REG LIST */



/* 2 BB REG LIST
 * PAGE 8 */
#define	ODM_REG_CCK_RPT_FORMAT_11AC	0x804
#define	ODM_REG_BB_RX_PATH_11AC			0x808
#define	ODM_REG_BB_TX_PATH_11AC			0x80c
#define	ODM_REG_BB_ATC_11AC				0x860
#define	ODM_REG_EDCCA_POWER_CAL		0x8dc
#define	ODM_REG_DBG_RPT_11AC			0x8fc
/* PAGE 9 */
#define	ODM_REG_EDCCA_DOWN_OPT			0x900
#define	ODM_REG_ACBB_EDCCA_ENHANCE		0x944
#define	odm_adc_trigger_jaguar2			0x95C	/*ADC sample mode*/
#define	ODM_REG_OFDM_FA_RST_11AC		0x9A4
#define	ODM_REG_CCX_PERIOD_11AC			0x990
#define	ODM_REG_NHM_TH9_TH10_11AC		0x994
#define	ODM_REG_CLM_11AC					0x994
#define	ODM_REG_NHM_TH3_TO_TH0_11AC	0x998
#define	ODM_REG_NHM_TH7_TO_TH4_11AC	0x99c
#define	ODM_REG_NHM_TH8_11AC			0x9a0
#define	ODM_REG_NHM_9E8_11AC			0x9e8
#define	ODM_REG_CSI_CONTENT_VALUE		0x9b4
/* PAGE A */
#define	ODM_REG_CCK_CCA_11AC			0xA0A
#define	ODM_REG_CCK_FA_RST_11AC			0xA2C
#define	ODM_REG_CCK_FA_11AC				0xA5C
/* PAGE B */
#define	ODM_REG_RST_RPT_11AC				0xB58
/* PAGE C */
#define	ODM_REG_TRMUX_11AC				0xC08
#define	ODM_REG_IGI_A_11AC				0xC50
/* PAGE E */
#define	ODM_REG_IGI_B_11AC				0xE50
#define	ODM_REG_TRMUX_11AC_B			0xE08
/* PAGE F */
#define	ODM_REG_CCK_CRC32_CNT_11AC		0xF04
#define	ODM_REG_CCK_CCA_CNT_11AC		0xF08
#define	ODM_REG_VHT_CRC32_CNT_11AC		0xF0c
#define	ODM_REG_HT_CRC32_CNT_11AC		0xF10
#define	ODM_REG_OFDM_CRC32_CNT_11AC	0xF14
#define	ODM_REG_OFDM_FA_11AC			0xF48
#define	ODM_REG_RPT_11AC					0xfa0
#define	ODM_REG_CLM_RESULT_11AC			0xfa4
#define	ODM_REG_NHM_CNT_11AC			0xfa8
#define ODM_REG_NHM_DUR_READY_11AC      0xfb4

#define	ODM_REG_NHM_CNT7_TO_CNT4_11AC   0xfac
#define	ODM_REG_NHM_CNT11_TO_CNT8_11AC  0xfb0
#define	ODM_REG_OFDM_FA_TYPE2_11AC		0xFD0
/* PAGE 18 */
#define	ODM_REG_IGI_C_11AC				0x1850
/* PAGE 1A */
#define	ODM_REG_IGI_D_11AC				0x1A50

/* 2 MAC REG LIST */
#define	ODM_REG_RESP_TX_11AC				0x6D8



/* DIG Related */
#define	ODM_BIT_IGI_11AC					0x0000007F
#define	ODM_BIT_CCK_RPT_FORMAT_11AC		BIT(16)
#define	ODM_BIT_BB_RX_PATH_11AC			0xF
#define	ODM_BIT_BB_TX_PATH_11AC			0xF
#define	ODM_BIT_BB_ATC_11AC				BIT(14)

#endif
