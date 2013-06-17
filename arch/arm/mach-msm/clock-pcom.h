/* Copyright (c) 2009, Code Aurora Forum. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 and
 * only version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#ifndef __ARCH_ARM_MACH_MSM_CLOCK_PCOM_H
#define __ARCH_ARM_MACH_MSM_CLOCK_PCOM_H

/* clock IDs used by the modem processor */

#define P_ACPU_CLK	0   /* Applications processor clock */
#define P_ADM_CLK	1   /* Applications data mover clock */
#define P_ADSP_CLK	2   /* ADSP clock */
#define P_EBI1_CLK	3   /* External bus interface 1 clock */
#define P_EBI2_CLK	4   /* External bus interface 2 clock */
#define P_ECODEC_CLK	5   /* External CODEC clock */
#define P_EMDH_CLK	6   /* External MDDI host clock */
#define P_GP_CLK	7   /* General purpose clock */
#define P_GRP_3D_CLK	8   /* Graphics clock */
#define P_I2C_CLK	9   /* I2C clock */
#define P_ICODEC_RX_CLK	10  /* Internal CODEX RX clock */
#define P_ICODEC_TX_CLK	11  /* Internal CODEX TX clock */
#define P_IMEM_CLK	12  /* Internal graphics memory clock */
#define P_MDC_CLK	13  /* MDDI client clock */
#define P_MDP_CLK	14  /* Mobile display processor clock */
#define P_PBUS_CLK	15  /* Peripheral bus clock */
#define P_PCM_CLK	16  /* PCM clock */
#define P_PMDH_CLK	17  /* Primary MDDI host clock */
#define P_SDAC_CLK	18  /* Stereo DAC clock */
#define P_SDC1_CLK	19  /* Secure Digital Card clocks */
#define P_SDC1_P_CLK	20
#define P_SDC2_CLK	21
#define P_SDC2_P_CLK	22
#define P_SDC3_CLK	23
#define P_SDC3_P_CLK	24
#define P_SDC4_CLK	25
#define P_SDC4_P_CLK	26
#define P_TSIF_CLK	27  /* Transport Stream Interface clocks */
#define P_TSIF_REF_CLK	28
#define P_TV_DAC_CLK	29  /* TV clocks */
#define P_TV_ENC_CLK	30
#define P_UART1_CLK	31  /* UART clocks */
#define P_UART2_CLK	32
#define P_UART3_CLK	33
#define P_UART1DM_CLK	34
#define P_UART2DM_CLK	35
#define P_USB_HS_CLK	36  /* High speed USB core clock */
#define P_USB_HS_P_CLK	37  /* High speed USB pbus clock */
#define P_USB_OTG_CLK	38  /* Full speed USB clock */
#define P_VDC_CLK	39  /* Video controller clock */
#define P_VFE_MDC_CLK	40  /* Camera / Video Front End clock */
#define P_VFE_CLK	41  /* VFE MDDI client clock */
#define P_MDP_LCDC_PCLK_CLK	42
#define P_MDP_LCDC_PAD_PCLK_CLK 43
#define P_MDP_VSYNC_CLK	44
#define P_SPI_CLK	45
#define P_VFE_AXI_CLK	46
#define P_USB_HS2_CLK	47  /* High speed USB 2 core clock */
#define P_USB_HS2_P_CLK	48  /* High speed USB 2 pbus clock */
#define P_USB_HS3_CLK	49  /* High speed USB 3 core clock */
#define P_USB_HS3_P_CLK	50  /* High speed USB 3 pbus clock */
#define P_GRP_3D_P_CLK	51  /* Graphics pbus clock */
#define P_USB_PHY_CLK	52  /* USB PHY clock */
#define P_USB_HS_CORE_CLK	53  /* High speed USB 1 core clock */
#define P_USB_HS2_CORE_CLK	54  /* High speed USB 2 core clock */
#define P_USB_HS3_CORE_CLK	55  /* High speed USB 3 core clock */
#define P_CAM_M_CLK		56
#define P_CAMIF_PAD_P_CLK	57
#define P_GRP_2D_CLK		58
#define P_GRP_2D_P_CLK		59
#define P_I2S_CLK		60
#define P_JPEG_CLK		61
#define P_JPEG_P_CLK		62
#define P_LPA_CODEC_CLK		63
#define P_LPA_CORE_CLK		64
#define P_LPA_P_CLK		65
#define P_MDC_IO_CLK		66
#define P_MDC_P_CLK		67
#define P_MFC_CLK		68
#define P_MFC_DIV2_CLK		69
#define P_MFC_P_CLK		70
#define P_QUP_I2C_CLK		71
#define P_ROTATOR_IMEM_CLK	72
#define P_ROTATOR_P_CLK		73
#define P_VFE_CAMIF_CLK		74
#define P_VFE_P_CLK		75
#define P_VPE_CLK		76
#define P_I2C_2_CLK		77
#define P_MI2S_CODEC_RX_S_CLK	78
#define P_MI2S_CODEC_RX_M_CLK	79
#define P_MI2S_CODEC_TX_S_CLK	80
#define P_MI2S_CODEC_TX_M_CLK	81
#define P_PMDH_P_CLK		82
#define P_EMDH_P_CLK		83
#define P_SPI_P_CLK		84
#define P_TSIF_P_CLK		85
#define P_MDP_P_CLK		86
#define P_SDAC_M_CLK		87
#define P_MI2S_S_CLK		88
#define P_MI2S_M_CLK		89
#define P_AXI_ROTATOR_CLK	90
#define P_HDMI_CLK		91
#define P_CSI0_CLK		92
#define P_CSI0_VFE_CLK		93
#define P_CSI0_P_CLK		94
#define P_CSI1_CLK		95
#define P_CSI1_VFE_CLK		96
#define P_CSI1_P_CLK		97
#define P_GSBI_CLK		98
#define P_GSBI_P_CLK		99
#define P_CE_CLK		100 /* Crypto engine */
#define P_CODEC_SSBI_CLK	101

#define P_NR_CLKS		102

struct clk_ops;
extern struct clk_ops clk_ops_pcom;

struct pcom_clk_pdata {
	struct clk_lookup *lookup;
	u32 num_lookups;
};

int pc_clk_reset(unsigned id, enum clk_reset_action action);

#define CLK_PCOM(clk_name, clk_id, clk_dev, clk_flags) {	\
	.con_id = clk_name, \
	.dev_id = clk_dev, \
	.clk = &(struct clk){ \
		.id = P_##clk_id, \
		.remote_id = P_##clk_id, \
		.ops = &clk_ops_pcom, \
		.flags = clk_flags, \
		.dbg_name = #clk_id, \
	}, \
	}

#endif
