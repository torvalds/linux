/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * TI AM437x Image Sensor Interface Registers
 *
 * Copyright (C) 2013 - 2014 Texas Instruments, Inc.
 *
 * Benoit Parrot <bparrot@ti.com>
 * Lad, Prabhakar <prabhakar.csengg@gmail.com>
 */

#ifndef AM437X_VPFE_REGS_H
#define AM437X_VPFE_REGS_H

/* VPFE module register offset */
#define VPFE_REVISION				0x0
#define VPFE_PCR				0x4
#define VPFE_SYNMODE				0x8
#define VPFE_HD_VD_WID				0xc
#define VPFE_PIX_LINES				0x10
#define VPFE_HORZ_INFO				0x14
#define VPFE_VERT_START				0x18
#define VPFE_VERT_LINES				0x1c
#define VPFE_CULLING				0x20
#define VPFE_HSIZE_OFF				0x24
#define VPFE_SDOFST				0x28
#define VPFE_SDR_ADDR				0x2c
#define VPFE_CLAMP				0x30
#define VPFE_DCSUB				0x34
#define VPFE_COLPTN				0x38
#define VPFE_BLKCMP				0x3c
#define VPFE_VDINT				0x48
#define VPFE_ALAW				0x4c
#define VPFE_REC656IF				0x50
#define VPFE_CCDCFG				0x54
#define VPFE_DMA_CNTL				0x98
#define VPFE_SYSCONFIG				0x104
#define VPFE_CONFIG				0x108
#define VPFE_IRQ_EOI				0x110
#define VPFE_IRQ_STS_RAW			0x114
#define VPFE_IRQ_STS				0x118
#define VPFE_IRQ_EN_SET				0x11c
#define VPFE_IRQ_EN_CLR				0x120
#define VPFE_REG_END				0x124

/* Define bit fields within selected registers */
#define VPFE_FID_POL_MASK			1
#define VPFE_FID_POL_SHIFT			4
#define VPFE_HD_POL_MASK			1
#define VPFE_HD_POL_SHIFT			3
#define VPFE_VD_POL_MASK			1
#define VPFE_VD_POL_SHIFT			2
#define VPFE_HSIZE_OFF_MASK			0xffffffe0
#define VPFE_32BYTE_ALIGN_VAL			31
#define VPFE_FRM_FMT_MASK			0x1
#define VPFE_FRM_FMT_SHIFT			7
#define VPFE_DATA_SZ_MASK			7
#define VPFE_DATA_SZ_SHIFT			8
#define VPFE_PIX_FMT_MASK			3
#define VPFE_PIX_FMT_SHIFT			12
#define VPFE_VP2SDR_DISABLE			0xfffbffff
#define VPFE_WEN_ENABLE				BIT(17)
#define VPFE_SDR2RSZ_DISABLE			0xfff7ffff
#define VPFE_VDHDEN_ENABLE			BIT(16)
#define VPFE_LPF_ENABLE				BIT(14)
#define VPFE_ALAW_ENABLE			BIT(3)
#define VPFE_ALAW_GAMMA_WD_MASK			7
#define VPFE_BLK_CLAMP_ENABLE			BIT(31)
#define VPFE_BLK_SGAIN_MASK			0x1f
#define VPFE_BLK_ST_PXL_MASK			0x7fff
#define VPFE_BLK_ST_PXL_SHIFT			10
#define VPFE_BLK_SAMPLE_LN_MASK			7
#define VPFE_BLK_SAMPLE_LN_SHIFT		28
#define VPFE_BLK_SAMPLE_LINE_MASK		7
#define VPFE_BLK_SAMPLE_LINE_SHIFT		25
#define VPFE_BLK_DC_SUB_MASK			0x03fff
#define VPFE_BLK_COMP_MASK			0xff
#define VPFE_BLK_COMP_GB_COMP_SHIFT		8
#define VPFE_BLK_COMP_GR_COMP_SHIFT		16
#define VPFE_BLK_COMP_R_COMP_SHIFT		24
#define VPFE_LATCH_ON_VSYNC_DISABLE		BIT(15)
#define VPFE_DATA_PACK_ENABLE			BIT(11)
#define VPFE_HORZ_INFO_SPH_SHIFT		16
#define VPFE_VERT_START_SLV0_SHIFT		16
#define VPFE_VDINT_VDINT0_SHIFT			16
#define VPFE_VDINT_VDINT1_MASK			0xffff
#define VPFE_PPC_RAW				1
#define VPFE_DCSUB_DEFAULT_VAL			0
#define VPFE_CLAMP_DEFAULT_VAL			0
#define VPFE_COLPTN_VAL				0xbb11bb11
#define VPFE_TWO_BYTES_PER_PIXEL		2
#define VPFE_INTERLACED_IMAGE_INVERT		0x4b6d
#define VPFE_INTERLACED_NO_IMAGE_INVERT		0x0249
#define VPFE_PROGRESSIVE_IMAGE_INVERT		0x4000
#define VPFE_PROGRESSIVE_NO_IMAGE_INVERT	0
#define VPFE_INTERLACED_HEIGHT_SHIFT		1
#define VPFE_SYN_MODE_INPMOD_SHIFT		12
#define VPFE_SYN_MODE_INPMOD_MASK		3
#define VPFE_SYN_MODE_8BITS			(7 << 8)
#define VPFE_SYN_MODE_10BITS			(6 << 8)
#define VPFE_SYN_MODE_11BITS			(5 << 8)
#define VPFE_SYN_MODE_12BITS			(4 << 8)
#define VPFE_SYN_MODE_13BITS			(3 << 8)
#define VPFE_SYN_MODE_14BITS			(2 << 8)
#define VPFE_SYN_MODE_15BITS			(1 << 8)
#define VPFE_SYN_MODE_16BITS			(0 << 8)
#define VPFE_SYN_FLDMODE_MASK			1
#define VPFE_SYN_FLDMODE_SHIFT			7
#define VPFE_REC656IF_BT656_EN			3
#define VPFE_SYN_MODE_VD_POL_NEGATIVE		BIT(2)
#define VPFE_CCDCFG_Y8POS_SHIFT			11
#define VPFE_CCDCFG_BW656_10BIT			BIT(5)
#define VPFE_SDOFST_FIELD_INTERLEAVED		0x249
#define VPFE_NO_CULLING				0xffff00ff
#define VPFE_VDINT0				BIT(0)
#define VPFE_VDINT1				BIT(1)
#define VPFE_VDINT2				BIT(2)
#define VPFE_DMA_CNTL_OVERFLOW			BIT(31)

#define VPFE_CONFIG_PCLK_INV_SHIFT		0
#define VPFE_CONFIG_PCLK_INV_MASK		1
#define VPFE_CONFIG_PCLK_INV_NOT_INV		0
#define VPFE_CONFIG_PCLK_INV_INV		1
#define VPFE_CONFIG_EN_SHIFT			1
#define VPFE_CONFIG_EN_MASK			2
#define VPFE_CONFIG_EN_DISABLE			0
#define VPFE_CONFIG_EN_ENABLE			1
#define VPFE_CONFIG_ST_SHIFT			2
#define VPFE_CONFIG_ST_MASK			4
#define VPFE_CONFIG_ST_OCP_ACTIVE		0
#define VPFE_CONFIG_ST_OCP_STANDBY		1

#endif		/* AM437X_VPFE_REGS_H */
