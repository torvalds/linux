/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Copyright (C) 2006-2009 Texas Instruments Inc
 */
#ifndef _DM644X_CCDC_REGS_H
#define _DM644X_CCDC_REGS_H

/**************************************************************************\
* Register OFFSET Definitions
\**************************************************************************/
#define CCDC_PID				0x0
#define CCDC_PCR				0x4
#define CCDC_SYN_MODE				0x8
#define CCDC_HD_VD_WID				0xc
#define CCDC_PIX_LINES				0x10
#define CCDC_HORZ_INFO				0x14
#define CCDC_VERT_START				0x18
#define CCDC_VERT_LINES				0x1c
#define CCDC_CULLING				0x20
#define CCDC_HSIZE_OFF				0x24
#define CCDC_SDOFST				0x28
#define CCDC_SDR_ADDR				0x2c
#define CCDC_CLAMP				0x30
#define CCDC_DCSUB				0x34
#define CCDC_COLPTN				0x38
#define CCDC_BLKCMP				0x3c
#define CCDC_FPC				0x40
#define CCDC_FPC_ADDR				0x44
#define CCDC_VDINT				0x48
#define CCDC_ALAW				0x4c
#define CCDC_REC656IF				0x50
#define CCDC_CCDCFG				0x54
#define CCDC_FMTCFG				0x58
#define CCDC_FMT_HORZ				0x5c
#define CCDC_FMT_VERT				0x60
#define CCDC_FMT_ADDR0				0x64
#define CCDC_FMT_ADDR1				0x68
#define CCDC_FMT_ADDR2				0x6c
#define CCDC_FMT_ADDR3				0x70
#define CCDC_FMT_ADDR4				0x74
#define CCDC_FMT_ADDR5				0x78
#define CCDC_FMT_ADDR6				0x7c
#define CCDC_FMT_ADDR7				0x80
#define CCDC_PRGEVEN_0				0x84
#define CCDC_PRGEVEN_1				0x88
#define CCDC_PRGODD_0				0x8c
#define CCDC_PRGODD_1				0x90
#define CCDC_VP_OUT				0x94
#define CCDC_REG_END				0x98

/***************************************************************
*	Define for various register bit mask and shifts for CCDC
****************************************************************/
#define CCDC_FID_POL_MASK			1
#define CCDC_FID_POL_SHIFT			4
#define CCDC_HD_POL_MASK			1
#define CCDC_HD_POL_SHIFT			3
#define CCDC_VD_POL_MASK			1
#define CCDC_VD_POL_SHIFT			2
#define CCDC_HSIZE_OFF_MASK			0xffffffe0
#define CCDC_32BYTE_ALIGN_VAL			31
#define CCDC_FRM_FMT_MASK			0x1
#define CCDC_FRM_FMT_SHIFT			7
#define CCDC_DATA_SZ_MASK			7
#define CCDC_DATA_SZ_SHIFT			8
#define CCDC_PIX_FMT_MASK			3
#define CCDC_PIX_FMT_SHIFT			12
#define CCDC_VP2SDR_DISABLE			0xFFFBFFFF
#define CCDC_WEN_ENABLE				BIT(17)
#define CCDC_SDR2RSZ_DISABLE			0xFFF7FFFF
#define CCDC_VDHDEN_ENABLE			BIT(16)
#define CCDC_LPF_ENABLE				BIT(14)
#define CCDC_ALAW_ENABLE			BIT(3)
#define CCDC_ALAW_GAMMA_WD_MASK			7
#define CCDC_BLK_CLAMP_ENABLE			BIT(31)
#define CCDC_BLK_SGAIN_MASK			0x1F
#define CCDC_BLK_ST_PXL_MASK			0x7FFF
#define CCDC_BLK_ST_PXL_SHIFT			10
#define CCDC_BLK_SAMPLE_LN_MASK			7
#define CCDC_BLK_SAMPLE_LN_SHIFT		28
#define CCDC_BLK_SAMPLE_LINE_MASK		7
#define CCDC_BLK_SAMPLE_LINE_SHIFT		25
#define CCDC_BLK_DC_SUB_MASK			0x03FFF
#define CCDC_BLK_COMP_MASK			0xFF
#define CCDC_BLK_COMP_GB_COMP_SHIFT		8
#define CCDC_BLK_COMP_GR_COMP_SHIFT		16
#define CCDC_BLK_COMP_R_COMP_SHIFT		24
#define CCDC_LATCH_ON_VSYNC_DISABLE		BIT(15)
#define CCDC_FPC_ENABLE				BIT(15)
#define CCDC_FPC_DISABLE			0
#define CCDC_FPC_FPC_NUM_MASK			0x7FFF
#define CCDC_DATA_PACK_ENABLE			BIT(11)
#define CCDC_FMTCFG_VPIN_MASK			7
#define CCDC_FMTCFG_VPIN_SHIFT			12
#define CCDC_FMT_HORZ_FMTLNH_MASK		0x1FFF
#define CCDC_FMT_HORZ_FMTSPH_MASK		0x1FFF
#define CCDC_FMT_HORZ_FMTSPH_SHIFT		16
#define CCDC_FMT_VERT_FMTLNV_MASK		0x1FFF
#define CCDC_FMT_VERT_FMTSLV_MASK		0x1FFF
#define CCDC_FMT_VERT_FMTSLV_SHIFT		16
#define CCDC_VP_OUT_VERT_NUM_MASK		0x3FFF
#define CCDC_VP_OUT_VERT_NUM_SHIFT		17
#define CCDC_VP_OUT_HORZ_NUM_MASK		0x1FFF
#define CCDC_VP_OUT_HORZ_NUM_SHIFT		4
#define CCDC_VP_OUT_HORZ_ST_MASK		0xF
#define CCDC_HORZ_INFO_SPH_SHIFT		16
#define CCDC_VERT_START_SLV0_SHIFT		16
#define CCDC_VDINT_VDINT0_SHIFT			16
#define CCDC_VDINT_VDINT1_MASK			0xFFFF
#define CCDC_PPC_RAW				1
#define CCDC_DCSUB_DEFAULT_VAL			0
#define CCDC_CLAMP_DEFAULT_VAL			0
#define CCDC_ENABLE_VIDEO_PORT			0x8000
#define CCDC_DISABLE_VIDEO_PORT			0
#define CCDC_COLPTN_VAL				0xBB11BB11
#define CCDC_TWO_BYTES_PER_PIXEL		2
#define CCDC_INTERLACED_IMAGE_INVERT		0x4B6D
#define CCDC_INTERLACED_NO_IMAGE_INVERT		0x0249
#define CCDC_PROGRESSIVE_IMAGE_INVERT		0x4000
#define CCDC_PROGRESSIVE_NO_IMAGE_INVERT	0
#define CCDC_INTERLACED_HEIGHT_SHIFT		1
#define CCDC_SYN_MODE_INPMOD_SHIFT		12
#define CCDC_SYN_MODE_INPMOD_MASK		3
#define CCDC_SYN_MODE_8BITS			(7 << 8)
#define CCDC_SYN_MODE_10BITS			(6 << 8)
#define CCDC_SYN_MODE_11BITS			(5 << 8)
#define CCDC_SYN_MODE_12BITS			(4 << 8)
#define CCDC_SYN_MODE_13BITS			(3 << 8)
#define CCDC_SYN_MODE_14BITS			(2 << 8)
#define CCDC_SYN_MODE_15BITS			(1 << 8)
#define CCDC_SYN_MODE_16BITS			(0 << 8)
#define CCDC_SYN_FLDMODE_MASK			1
#define CCDC_SYN_FLDMODE_SHIFT			7
#define CCDC_REC656IF_BT656_EN			3
#define CCDC_SYN_MODE_VD_POL_NEGATIVE		BIT(2)
#define CCDC_CCDCFG_Y8POS_SHIFT			11
#define CCDC_CCDCFG_BW656_10BIT			BIT(5)
#define CCDC_SDOFST_FIELD_INTERLEAVED		0x249
#define CCDC_NO_CULLING				0xffff00ff
#endif
