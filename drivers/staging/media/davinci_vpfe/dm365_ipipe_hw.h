/*
 * Copyright (C) 2012 Texas Instruments Inc
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation version 2.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307 USA
 *
 * Contributors:
 *      Manjunath Hadli <manjunath.hadli@ti.com>
 *      Prabhakar Lad <prabhakar.lad@ti.com>
 */

#ifndef _DAVINCI_VPFE_DM365_IPIPE_HW_H
#define _DAVINCI_VPFE_DM365_IPIPE_HW_H

#include "vpfe_mc_capture.h"

#define SET_LOW_ADDR     0x0000ffff
#define SET_HIGH_ADDR    0xffff0000

/* Below are the internal tables */
#define DPC_TB0_START_ADDR	0x8000
#define DPC_TB1_START_ADDR	0x8400

#define GAMMA_R_START_ADDR	0xa800
#define GAMMA_G_START_ADDR	0xb000
#define GAMMA_B_START_ADDR	0xb800

/* RAM table addresses for edge enhancement correction*/
#define YEE_TB_START_ADDR	0x8800

/* RAM table address for GBC LUT */
#define GBCE_TB_START_ADDR	0x9000

/* RAM table for 3D NF LUT */
#define D3L_TB0_START_ADDR	0x9800
#define D3L_TB1_START_ADDR	0x9c00
#define D3L_TB2_START_ADDR	0xa000
#define D3L_TB3_START_ADDR	0xa400

/* IPIPE Register Offsets from the base address */
#define IPIPE_SRC_EN		0x0000
#define IPIPE_SRC_MODE		0x0004
#define IPIPE_SRC_FMT		0x0008
#define IPIPE_SRC_COL		0x000c
#define IPIPE_SRC_VPS		0x0010
#define IPIPE_SRC_VSZ		0x0014
#define IPIPE_SRC_HPS		0x0018
#define IPIPE_SRC_HSZ		0x001c

#define IPIPE_SEL_SBU		0x0020

#define IPIPE_DMA_STA		0x0024
#define IPIPE_GCK_MMR		0x0028
#define IPIPE_GCK_PIX		0x002c
#define IPIPE_RESERVED0		0x0030

/* Defect Correction */
#define DPC_LUT_EN		0x0034
#define DPC_LUT_SEL		0x0038
#define DPC_LUT_ADR		0x003c
#define DPC_LUT_SIZ		0x0040
#define DPC_OTF_EN		0x0044
#define DPC_OTF_TYP		0x0048
#define DPC_OTF_2D_THR_R	0x004c
#define DPC_OTF_2D_THR_GR	0x0050
#define DPC_OTF_2D_THR_GB	0x0054
#define DPC_OTF_2D_THR_B	0x0058
#define DPC_OTF_2C_THR_R	0x005c
#define DPC_OTF_2C_THR_GR	0x0060
#define DPC_OTF_2C_THR_GB	0x0064
#define DPC_OTF_2C_THR_B	0x0068
#define DPC_OTF_3_SHF		0x006c
#define DPC_OTF_3D_THR		0x0070
#define DPC_OTF_3D_SLP		0x0074
#define DPC_OTF_3D_MIN		0x0078
#define DPC_OTF_3D_MAX		0x007c
#define DPC_OTF_3C_THR		0x0080
#define DPC_OTF_3C_SLP		0x0084
#define DPC_OTF_3C_MIN		0x0088
#define DPC_OTF_3C_MAX		0x008c

/* Lense Shading Correction */
#define LSC_VOFT		0x90
#define LSC_VA2			0x94
#define LSC_VA1			0x98
#define LSC_VS			0x9c
#define LSC_HOFT		0xa0
#define LSC_HA2			0xa4
#define LSC_HA1			0xa8
#define LSC_HS			0xac
#define LSC_GAIN_R		0xb0
#define LSC_GAIN_GR		0xb4
#define LSC_GAIN_GB		0xb8
#define LSC_GAIN_B		0xbc
#define LSC_OFT_R		0xc0
#define LSC_OFT_GR		0xc4
#define LSC_OFT_GB		0xc8
#define LSC_OFT_B		0xcc
#define LSC_SHF			0xd0
#define LSC_MAX			0xd4

/* Noise Filter 1. Ofsets from start address given */
#define D2F_1ST			0xd8
#define D2F_EN			0x0
#define D2F_TYP			0x4
#define D2F_THR			0x8
#define D2F_STR			0x28
#define D2F_SPR			0x48
#define D2F_EDG_MIN		0x68
#define D2F_EDG_MAX		0x6c

/* Noise Filter 2 */
#define D2F_2ND			0x148

/* GIC */
#define GIC_EN			0x1b8
#define GIC_TYP			0x1bc
#define GIC_GAN			0x1c0
#define GIC_NFGAN		0x1c4
#define GIC_THR			0x1c8
#define GIC_SLP			0x1cc

/* White Balance */
#define WB2_OFT_R		0x1d0
#define WB2_OFT_GR		0x1d4
#define WB2_OFT_GB		0x1d8
#define WB2_OFT_B		0x1dc
#define WB2_WGN_R		0x1e0
#define WB2_WGN_GR		0x1e4
#define WB2_WGN_GB		0x1e8
#define WB2_WGN_B		0x1ec

/* CFA interpolation */
#define CFA_MODE		0x1f0
#define CFA_2DIR_HPF_THR	0x1f4
#define CFA_2DIR_HPF_SLP	0x1f8
#define CFA_2DIR_MIX_THR	0x1fc
#define CFA_2DIR_MIX_SLP	0x200
#define CFA_2DIR_DIR_THR	0x204
#define CFA_2DIR_DIR_SLP	0x208
#define CFA_2DIR_NDWT		0x20c
#define CFA_MONO_HUE_FRA	0x210
#define CFA_MONO_EDG_THR	0x214
#define CFA_MONO_THR_MIN	0x218
#define CFA_MONO_THR_SLP	0x21c
#define CFA_MONO_SLP_MIN	0x220
#define CFA_MONO_SLP_SLP	0x224
#define CFA_MONO_LPWT		0x228

/* RGB to RGB conversiona - 1st */
#define RGB1_MUL_BASE		0x22c
/* Offsets from base */
#define RGB_MUL_RR		0x0
#define RGB_MUL_GR		0x4
#define RGB_MUL_BR		0x8
#define RGB_MUL_RG		0xc
#define RGB_MUL_GG		0x10
#define RGB_MUL_BG		0x14
#define RGB_MUL_RB		0x18
#define RGB_MUL_GB		0x1c
#define RGB_MUL_BB		0x20
#define RGB_OFT_OR		0x24
#define RGB_OFT_OG		0x28
#define RGB_OFT_OB		0x2c

/* Gamma */
#define GMM_CFG			0x25c

/* RGB to RGB conversiona - 2nd */
#define RGB2_MUL_BASE		0x260

/* 3D LUT */
#define D3LUT_EN		0x290

/* RGB to YUV(YCbCr) conversion */
#define YUV_ADJ			0x294
#define YUV_MUL_RY		0x298
#define YUV_MUL_GY		0x29c
#define YUV_MUL_BY		0x2a0
#define YUV_MUL_RCB		0x2a4
#define YUV_MUL_GCB		0x2a8
#define YUV_MUL_BCB		0x2ac
#define YUV_MUL_RCR		0x2b0
#define YUV_MUL_GCR		0x2b4
#define YUV_MUL_BCR		0x2b8
#define YUV_OFT_Y		0x2bc
#define YUV_OFT_CB		0x2c0
#define YUV_OFT_CR		0x2c4
#define YUV_PHS			0x2c8

/* Global Brightness and Contrast */
#define GBCE_EN			0x2cc
#define GBCE_TYP		0x2d0

/* Edge Enhancer */
#define YEE_EN			0x2d4
#define YEE_TYP			0x2d8
#define YEE_SHF			0x2dc
#define YEE_MUL_00		0x2e0
#define YEE_MUL_01		0x2e4
#define YEE_MUL_02		0x2e8
#define YEE_MUL_10		0x2ec
#define YEE_MUL_11		0x2f0
#define YEE_MUL_12		0x2f4
#define YEE_MUL_20		0x2f8
#define YEE_MUL_21		0x2fc
#define YEE_MUL_22		0x300
#define YEE_THR			0x304
#define YEE_E_GAN		0x308
#define YEE_E_THR1		0x30c
#define YEE_E_THR2		0x310
#define YEE_G_GAN		0x314
#define YEE_G_OFT		0x318

/* Chroma Artifact Reduction */
#define CAR_EN			0x31c
#define CAR_TYP			0x320
#define CAR_SW			0x324
#define CAR_HPF_TYP		0x328
#define CAR_HPF_SHF		0x32c
#define	CAR_HPF_THR		0x330
#define CAR_GN1_GAN		0x334
#define CAR_GN1_SHF		0x338
#define CAR_GN1_MIN		0x33c
#define CAR_GN2_GAN		0x340
#define CAR_GN2_SHF		0x344
#define CAR_GN2_MIN		0x348

/* Chroma Gain Suppression */
#define CGS_EN			0x34c
#define CGS_GN1_L_THR		0x350
#define CGS_GN1_L_GAN		0x354
#define CGS_GN1_L_SHF		0x358
#define CGS_GN1_L_MIN		0x35c
#define CGS_GN1_H_THR		0x360
#define CGS_GN1_H_GAN		0x364
#define CGS_GN1_H_SHF		0x368
#define CGS_GN1_H_MIN		0x36c
#define CGS_GN2_L_THR		0x370
#define CGS_GN2_L_GAN		0x374
#define CGS_GN2_L_SHF		0x378
#define CGS_GN2_L_MIN		0x37c

/* Resizer */
#define RSZ_SRC_EN		0x0
#define RSZ_SRC_MODE		0x4
#define RSZ_SRC_FMT0		0x8
#define RSZ_SRC_FMT1		0xc
#define RSZ_SRC_VPS		0x10
#define RSZ_SRC_VSZ		0x14
#define RSZ_SRC_HPS		0x18
#define RSZ_SRC_HSZ		0x1c
#define RSZ_DMA_RZA		0x20
#define RSZ_DMA_RZB		0x24
#define RSZ_DMA_STA		0x28
#define RSZ_GCK_MMR		0x2c
#define RSZ_RESERVED0		0x30
#define RSZ_GCK_SDR		0x34
#define RSZ_IRQ_RZA		0x38
#define RSZ_IRQ_RZB		0x3c
#define RSZ_YUV_Y_MIN		0x40
#define RSZ_YUV_Y_MAX		0x44
#define RSZ_YUV_C_MIN		0x48
#define RSZ_YUV_C_MAX		0x4c
#define RSZ_YUV_PHS		0x50
#define RSZ_SEQ			0x54

/* Resizer Rescale Parameters */
#define RSZ_EN_A		0x58
#define RSZ_EN_B		0xe8
/* offset of the registers to be added with base register of
   either RSZ0 or RSZ1
*/
#define RSZ_MODE		0x4
#define RSZ_420			0x8
#define RSZ_I_VPS		0xc
#define RSZ_I_HPS		0x10
#define RSZ_O_VSZ		0x14
#define RSZ_O_HSZ		0x18
#define RSZ_V_PHS_Y		0x1c
#define RSZ_V_PHS_C		0x20
#define RSZ_V_DIF		0x24
#define RSZ_V_TYP		0x28
#define RSZ_V_LPF		0x2c
#define RSZ_H_PHS		0x30
#define RSZ_H_PHS_ADJ		0x34
#define RSZ_H_DIF		0x38
#define RSZ_H_TYP		0x3c
#define RSZ_H_LPF		0x40
#define RSZ_DWN_EN		0x44
#define RSZ_DWN_AV		0x48

/* Resizer RGB Conversion Parameters */
#define RSZ_RGB_EN		0x4c
#define RSZ_RGB_TYP		0x50
#define RSZ_RGB_BLD		0x54

/* Resizer External Memory Parameters */
#define RSZ_SDR_Y_BAD_H		0x58
#define RSZ_SDR_Y_BAD_L		0x5c
#define RSZ_SDR_Y_SAD_H		0x60
#define RSZ_SDR_Y_SAD_L		0x64
#define RSZ_SDR_Y_OFT		0x68
#define RSZ_SDR_Y_PTR_S		0x6c
#define RSZ_SDR_Y_PTR_E		0x70
#define RSZ_SDR_C_BAD_H		0x74
#define RSZ_SDR_C_BAD_L		0x78
#define RSZ_SDR_C_SAD_H		0x7c
#define RSZ_SDR_C_SAD_L		0x80
#define RSZ_SDR_C_OFT		0x84
#define RSZ_SDR_C_PTR_S		0x88
#define RSZ_SDR_C_PTR_E		0x8c

/* Macro for resizer */
#define RSZ_YUV_Y_MIN		0x40
#define RSZ_YUV_Y_MAX		0x44
#define RSZ_YUV_C_MIN		0x48
#define RSZ_YUV_C_MAX		0x4c

#define IPIPE_GCK_MMR_DEFAULT	1
#define IPIPE_GCK_PIX_DEFAULT	0xe
#define RSZ_GCK_MMR_DEFAULT	1
#define RSZ_GCK_SDR_DEFAULT	1

/* LUTDPC */
#define LUTDPC_TBL_256_EN	0
#define LUTDPC_INF_TBL_EN	1
#define LUT_DPC_START_ADDR	0
#define LUT_DPC_H_POS_MASK	0x1fff
#define LUT_DPC_V_POS_MASK	0x1fff
#define LUT_DPC_V_POS_SHIFT	13
#define LUT_DPC_CORR_METH_SHIFT	26
#define LUT_DPC_MAX_SIZE	256
#define LUT_DPC_SIZE_MASK	0x3ff

/* OTFDPC */
#define OTFDPC_DPC2_THR_MASK	0xfff
#define OTF_DET_METHOD_SHIFT	1
#define OTF_DPC3_0_SHF_MASK	3
#define OTF_DPC3_0_THR_SHIFT	6
#define OTF_DPC3_0_THR_MASK	0x3f
#define OTF_DPC3_0_SLP_MASK	0x3f
#define OTF_DPC3_0_DET_MASK	0xfff
#define OTF_DPC3_0_CORR_MASK	0xfff

/* NF (D2F) */
#define D2F_SPR_VAL_MASK		0x1f
#define D2F_SPR_VAL_SHIFT		0
#define D2F_SHFT_VAL_MASK		3
#define D2F_SHFT_VAL_SHIFT		5
#define D2F_SAMPLE_METH_SHIFT		7
#define D2F_APPLY_LSC_GAIN_SHIFT	8
#define D2F_USE_SPR_REG_VAL		0
#define D2F_STR_VAL_MASK		0x1f
#define D2F_THR_VAL_MASK		0x3ff
#define D2F_EDGE_DET_THR_MASK		0x7ff

/* Green Imbalance Correction */
#define GIC_TYP_SHIFT			0
#define GIC_THR_SEL_SHIFT		1
#define	GIC_APPLY_LSC_GAIN_SHIFT	2
#define GIC_GAIN_MASK			0xff
#define GIC_THR_MASK			0xfff
#define GIC_SLOPE_MASK			0xfff
#define GIC_NFGAN_INT_MASK		7
#define GIC_NFGAN_DECI_MASK		0x1f

/* WB */
#define WB_OFFSET_MASK			0xfff
#define WB_GAIN_INT_MASK		0xf
#define WB_GAIN_DECI_MASK		0x1ff

/* CFA */
#define CFA_HPF_THR_2DIR_MASK		0x1fff
#define CFA_HPF_SLOPE_2DIR_MASK		0x3ff
#define CFA_HPF_MIX_THR_2DIR_MASK	0x1fff
#define CFA_HPF_MIX_SLP_2DIR_MASK	0x3ff
#define CFA_DIR_THR_2DIR_MASK		0x3ff
#define CFA_DIR_SLP_2DIR_MASK		0x7f
#define CFA_ND_WT_2DIR_MASK		0x3f
#define CFA_DAA_HUE_FRA_MASK		0x3f
#define CFA_DAA_EDG_THR_MASK		0xff
#define CFA_DAA_THR_MIN_MASK		0x3ff
#define CFA_DAA_THR_SLP_MASK		0x3ff
#define CFA_DAA_SLP_MIN_MASK		0x3ff
#define CFA_DAA_SLP_SLP_MASK		0x3ff
#define CFA_DAA_LP_WT_MASK		0x3f

/* RGB2RGB */
#define RGB2RGB_1_OFST_MASK		0x1fff
#define RGB2RGB_1_GAIN_INT_MASK		0xf
#define RGB2RGB_GAIN_DECI_MASK		0xff
#define RGB2RGB_2_OFST_MASK		0x7ff
#define RGB2RGB_2_GAIN_INT_MASK		0x7

/* Gamma */
#define GAMMA_BYPR_SHIFT		0
#define GAMMA_BYPG_SHIFT		1
#define GAMMA_BYPB_SHIFT		2
#define GAMMA_TBL_SEL_SHIFT		4
#define GAMMA_TBL_SIZE_SHIFT		5
#define GAMMA_MASK			0x3ff
#define GAMMA_SHIFT			10

/* 3D LUT */
#define D3_LUT_ENTRY_MASK		0x3ff
#define D3_LUT_ENTRY_R_SHIFT		20
#define D3_LUT_ENTRY_G_SHIFT		10
#define D3_LUT_ENTRY_B_SHIFT		0

/* Lumina adj */
#define	LUM_ADJ_CONTR_SHIFT		0
#define	LUM_ADJ_BRIGHT_SHIFT		8

/* RGB2YCbCr */
#define RGB2YCBCR_OFST_MASK		0x7ff
#define RGB2YCBCR_COEF_INT_MASK		0xf
#define RGB2YCBCR_COEF_DECI_MASK	0xff

/* GBCE */
#define GBCE_Y_VAL_MASK			0xff
#define GBCE_GAIN_VAL_MASK		0x3ff
#define GBCE_ENTRY_SHIFT		10

/* Edge Enhancements */
#define YEE_HALO_RED_EN_SHIFT		1
#define YEE_HPF_SHIFT_MASK		0xf
#define YEE_COEF_MASK			0x3ff
#define YEE_THR_MASK			0x3f
#define YEE_ES_GAIN_MASK		0xfff
#define YEE_ES_THR1_MASK		0xfff
#define YEE_ENTRY_SHIFT			9
#define YEE_ENTRY_MASK			0x1ff

/* CAR */
#define CAR_MF_THR			0xff
#define CAR_SW1_SHIFT			8
#define CAR_GAIN1_SHFT_MASK		7
#define CAR_GAIN_MIN_MASK		0x1ff
#define CAR_GAIN2_SHFT_MASK		0xf
#define CAR_HPF_SHIFT_MASK		3

/* CGS */
#define CAR_SHIFT_MASK			3

/* Resizer */
#define RSZ_BYPASS_SHIFT		1
#define RSZ_SRC_IMG_FMT_SHIFT		1
#define RSZ_SRC_Y_C_SEL_SHIFT		2
#define IPIPE_RSZ_VPS_MASK		0xffff
#define IPIPE_RSZ_HPS_MASK		0xffff
#define IPIPE_RSZ_VSZ_MASK		0x1fff
#define IPIPE_RSZ_HSZ_MASK		0x1fff
#define RSZ_HPS_MASK			0x1fff
#define RSZ_VPS_MASK			0x1fff
#define RSZ_O_HSZ_MASK			0x1fff
#define RSZ_O_VSZ_MASK			0x1fff
#define RSZ_V_PHS_MASK			0x3fff
#define RSZ_V_DIF_MASK			0x3fff

#define RSZA_H_FLIP_SHIFT		0
#define RSZA_V_FLIP_SHIFT		1
#define RSZB_H_FLIP_SHIFT		2
#define RSZB_V_FLIP_SHIFT		3
#define RSZ_A				0
#define RSZ_B				1
#define RSZ_CEN_SHIFT			1
#define RSZ_YEN_SHIFT			0
#define RSZ_TYP_Y_SHIFT			0
#define RSZ_TYP_C_SHIFT			1
#define RSZ_LPF_INT_MASK		0x3f
#define RSZ_LPF_INT_MASK		0x3f
#define RSZ_LPF_INT_C_SHIFT		6
#define RSZ_H_PHS_MASK			0x3fff
#define RSZ_H_DIF_MASK			0x3fff
#define RSZ_DIFF_DOWN_THR		256
#define RSZ_DWN_SCALE_AV_SZ_V_SHIFT	3
#define RSZ_DWN_SCALE_AV_SZ_MASK	7
#define RSZ_RGB_MSK1_SHIFT		2
#define RSZ_RGB_MSK0_SHIFT		1
#define RSZ_RGB_TYP_SHIFT		0
#define RSZ_RGB_ALPHA_MASK		0xff

static inline u32 regr_ip(void *__iomem addr, u32 offset)
{
	return readl(addr + offset);
}

static inline void regw_ip(void *__iomem addr, u32 val, u32 offset)
{
	writel(val, addr + offset);
}

static inline u32 w_ip_table(void *__iomem addr, u32 val, u32 offset)
{
	writel(val, addr + offset);

	return val;
}

static inline u32 regr_rsz(void *__iomem addr, u32 offset)
{
	return readl(addr + offset);
}

static inline u32 regw_rsz(void *__iomem addr, u32 val, u32 offset)
{
	writel(val, addr + offset);

	return val;
}

int config_ipipe_hw(struct vpfe_ipipe_device *ipipe);
int resizer_set_outaddr(void *__iomem rsz_base, struct resizer_params *params,
			int resize_no, unsigned int address);
int rsz_enable(void *__iomem rsz_base, int rsz_id, int enable);
void rsz_src_enable(void *__iomem rsz_base, int enable);
void rsz_set_in_pix_format(unsigned char y_c);
int config_rsz_hw(struct vpfe_resizer_device *resizer,
		  struct resizer_params *config);
void ipipe_set_d2f_regs(void *__iomem base_addr, unsigned int id,
	struct vpfe_ipipe_nf *noise_filter);
void ipipe_set_rgb2rgb_regs(void *__iomem base_addr, unsigned int id,
	struct vpfe_ipipe_rgb2rgb *rgb);
void ipipe_set_yuv422_conv_regs(void *__iomem base_addr,
	struct vpfe_ipipe_yuv422_conv *conv);
void ipipe_set_lum_adj_regs(void *__iomem base_addr,
	struct ipipe_lum_adj *lum_adj);
void ipipe_set_rgb2ycbcr_regs(void *__iomem base_addr,
	struct vpfe_ipipe_rgb2yuv *yuv);
void ipipe_set_lutdpc_regs(void *__iomem base_addr,
	void *__iomem isp5_base_addr, struct vpfe_ipipe_lutdpc *lutdpc);
void ipipe_set_otfdpc_regs(void *__iomem base_addr,
	struct vpfe_ipipe_otfdpc *otfdpc);
void ipipe_set_3d_lut_regs(void *__iomem base_addr,
	void *__iomem isp5_base_addr, struct vpfe_ipipe_3d_lut *lut_3d);
void ipipe_set_gamma_regs(void *__iomem base_addr,
	void *__iomem isp5_base_addr, struct vpfe_ipipe_gamma *gamma);
void ipipe_set_ee_regs(void *__iomem base_addr,
	void *__iomem isp5_base_addr, struct vpfe_ipipe_yee *ee);
void ipipe_set_gbce_regs(void *__iomem base_addr,
	void *__iomem isp5_base_addr, struct vpfe_ipipe_gbce *gbce);
void ipipe_set_gic_regs(void *__iomem base_addr, struct vpfe_ipipe_gic *gic);
void ipipe_set_cfa_regs(void *__iomem base_addr, struct vpfe_ipipe_cfa *cfa);
void ipipe_set_car_regs(void *__iomem base_addr, struct vpfe_ipipe_car *car);
void ipipe_set_cgs_regs(void *__iomem base_addr, struct vpfe_ipipe_cgs *cgs);
void ipipe_set_wb_regs(void *__iomem base_addr, struct vpfe_ipipe_wb *wb);

#endif		/* _DAVINCI_VPFE_DM365_IPIPE_HW_H */
