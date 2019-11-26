/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Allwinner Deinterlace driver
 *
 * Copyright (C) 2019 Jernej Skrabec <jernej.skrabec@siol.net>
 */

#ifndef _SUN8I_DEINTERLACE_H_
#define _SUN8I_DEINTERLACE_H_

#include <media/v4l2-device.h>
#include <media/v4l2-mem2mem.h>
#include <media/videobuf2-v4l2.h>
#include <media/videobuf2-dma-contig.h>

#include <linux/platform_device.h>

#define DEINTERLACE_NAME		"sun8i-di"

#define DEINTERLACE_MOD_ENABLE			0x00
#define DEINTERLACE_MOD_ENABLE_EN			BIT(0)

#define DEINTERLACE_FRM_CTRL			0x04
#define DEINTERLACE_FRM_CTRL_REG_READY			BIT(0)
#define DEINTERLACE_FRM_CTRL_WB_EN			BIT(2)
#define DEINTERLACE_FRM_CTRL_OUT_CTRL			BIT(11)
#define DEINTERLACE_FRM_CTRL_START			BIT(16)
#define DEINTERLACE_FRM_CTRL_COEF_ACCESS		BIT(23)

#define DEINTERLACE_BYPASS			0x08
#define DEINTERLACE_BYPASS_CSC				BIT(1)

#define DEINTERLACE_AGTH_SEL			0x0c
#define DEINTERLACE_AGTH_SEL_LINEBUF			BIT(8)

#define DEINTERLACE_LINT_CTRL			0x10
#define DEINTERLACE_TRD_PRELUMA			0x1c
#define DEINTERLACE_BUF_ADDR0			0x20
#define DEINTERLACE_BUF_ADDR1			0x24
#define DEINTERLACE_BUF_ADDR2			0x28

#define DEINTERLACE_FIELD_CTRL			0x2c
#define DEINTERLACE_FIELD_CTRL_FIELD_CNT(v)		((v) & 0xff)
#define DEINTERLACE_FIELD_CTRL_FIELD_CNT_MSK		(0xff)

#define DEINTERLACE_TB_OFFSET0			0x30
#define DEINTERLACE_TB_OFFSET1			0x34
#define DEINTERLACE_TB_OFFSET2			0x38
#define DEINTERLACE_TRD_PRECHROMA		0x3c
#define DEINTERLACE_LINE_STRIDE0		0x40
#define DEINTERLACE_LINE_STRIDE1		0x44
#define DEINTERLACE_LINE_STRIDE2		0x48

#define DEINTERLACE_IN_FMT			0x4c
#define DEINTERLACE_IN_FMT_PS(v)			((v) & 3)
#define DEINTERLACE_IN_FMT_FMT(v)			(((v) & 7) << 4)
#define DEINTERLACE_IN_FMT_MOD(v)			(((v) & 7) << 8)

#define DEINTERLACE_WB_ADDR0			0x50
#define DEINTERLACE_WB_ADDR1			0x54
#define DEINTERLACE_WB_ADDR2			0x58

#define DEINTERLACE_OUT_FMT			0x5c
#define DEINTERLACE_OUT_FMT_FMT(v)			((v) & 0xf)
#define DEINTERLACE_OUT_FMT_PS(v)			(((v) & 3) << 5)

#define DEINTERLACE_INT_ENABLE			0x60
#define DEINTERLACE_INT_ENABLE_WB_EN			BIT(7)

#define DEINTERLACE_INT_STATUS			0x64
#define DEINTERLACE_INT_STATUS_WRITEBACK		BIT(7)

#define DEINTERLACE_STATUS			0x68
#define DEINTERLACE_STATUS_COEF_STATUS			BIT(11)
#define DEINTERLACE_STATUS_WB_ERROR			BIT(12)

#define DEINTERLACE_CSC_COEF			0x70 /* 12 registers */

#define DEINTERLACE_CTRL			0xa0
#define DEINTERLACE_CTRL_EN				BIT(0)
#define DEINTERLACE_CTRL_FLAG_OUT_EN			BIT(8)
#define DEINTERLACE_CTRL_MODE_PASSTROUGH		(0 << 16)
#define DEINTERLACE_CTRL_MODE_WEAVE			(1 << 16)
#define DEINTERLACE_CTRL_MODE_BOB			(2 << 16)
#define DEINTERLACE_CTRL_MODE_MIXED			(3 << 16)
#define DEINTERLACE_CTRL_DIAG_INTP_EN			BIT(24)
#define DEINTERLACE_CTRL_TEMP_DIFF_EN			BIT(25)

#define DEINTERLACE_DIAG_INTP			0xa4
#define DEINTERLACE_DIAG_INTP_TH0(v)			((v) & 0x7f)
#define DEINTERLACE_DIAG_INTP_TH0_MSK			(0x7f)
#define DEINTERLACE_DIAG_INTP_TH1(v)			(((v) & 0x7f) << 8)
#define DEINTERLACE_DIAG_INTP_TH1_MSK			(0x7f << 8)
#define DEINTERLACE_DIAG_INTP_TH3(v)			(((v) & 0xff) << 24)
#define DEINTERLACE_DIAG_INTP_TH3_MSK			(0xff << 24)

#define DEINTERLACE_TEMP_DIFF			0xa8
#define DEINTERLACE_TEMP_DIFF_SAD_CENTRAL_TH(v)		((v) & 0x7f)
#define DEINTERLACE_TEMP_DIFF_SAD_CENTRAL_TH_MSK	(0x7f)
#define DEINTERLACE_TEMP_DIFF_AMBIGUITY_TH(v)		(((v) & 0x7f) << 8)
#define DEINTERLACE_TEMP_DIFF_AMBIGUITY_TH_MSK		(0x7f << 8)
#define DEINTERLACE_TEMP_DIFF_DIRECT_DITHER_TH(v)	(((v) & 0x7ff) << 16)
#define DEINTERLACE_TEMP_DIFF_DIRECT_DITHER_TH_MSK	(0x7ff << 16)

#define DEINTERLACE_LUMA_TH			0xac
#define DEINTERLACE_LUMA_TH_MIN_LUMA(v)			((v) & 0xff)
#define DEINTERLACE_LUMA_TH_MIN_LUMA_MSK		(0xff)
#define DEINTERLACE_LUMA_TH_MAX_LUMA(v)			(((v) & 0xff) << 8)
#define DEINTERLACE_LUMA_TH_MAX_LUMA_MSK		(0xff << 8)
#define DEINTERLACE_LUMA_TH_AVG_LUMA_SHIFT(v)		(((v) & 0xff) << 16)
#define DEINTERLACE_LUMA_TH_AVG_LUMA_SHIFT_MSK		(0xff << 16)
#define DEINTERLACE_LUMA_TH_PIXEL_STATIC(v)		(((v) & 3) << 24)
#define DEINTERLACE_LUMA_TH_PIXEL_STATIC_MSK		(3 << 24)

#define DEINTERLACE_SPAT_COMP			0xb0
#define DEINTERLACE_SPAT_COMP_TH2(v)			((v) & 0xff)
#define DEINTERLACE_SPAT_COMP_TH2_MSK			(0xff)
#define DEINTERLACE_SPAT_COMP_TH3(v)			(((v) & 0xff) << 16)
#define DEINTERLACE_SPAT_COMP_TH3_MSK			(0xff << 16)

#define DEINTERLACE_CHROMA_DIFF			0xb4
#define DEINTERLACE_CHROMA_DIFF_TH(v)			((v) & 0xff)
#define DEINTERLACE_CHROMA_DIFF_TH_MSK			(0xff)
#define DEINTERLACE_CHROMA_DIFF_LUMA(v)			(((v) & 0x3f) << 16)
#define DEINTERLACE_CHROMA_DIFF_LUMA_MSK		(0x3f << 16)
#define DEINTERLACE_CHROMA_DIFF_CHROMA(v)		(((v) & 0x3f) << 24)
#define DEINTERLACE_CHROMA_DIFF_CHROMA_MSK		(0x3f << 24)

#define DEINTERLACE_PRELUMA			0xb8
#define DEINTERLACE_PRECHROMA			0xbc
#define DEINTERLACE_TILE_FLAG0			0xc0
#define DEINTERLACE_TILE_FLAG1			0xc4
#define DEINTERLACE_FLAG_LINE_STRIDE		0xc8
#define DEINTERLACE_FLAG_SEQ			0xcc

#define DEINTERLACE_WB_LINE_STRIDE_CTRL		0xd0
#define DEINTERLACE_WB_LINE_STRIDE_CTRL_EN		BIT(0)

#define DEINTERLACE_WB_LINE_STRIDE0		0xd4
#define DEINTERLACE_WB_LINE_STRIDE1		0xd8
#define DEINTERLACE_WB_LINE_STRIDE2		0xdc
#define DEINTERLACE_TRD_CTRL			0xe0
#define DEINTERLACE_TRD_BUF_ADDR0		0xe4
#define DEINTERLACE_TRD_BUF_ADDR1		0xe8
#define DEINTERLACE_TRD_BUF_ADDR2		0xec
#define DEINTERLACE_TRD_TB_OFF0			0xf0
#define DEINTERLACE_TRD_TB_OFF1			0xf4
#define DEINTERLACE_TRD_TB_OFF2			0xf8
#define DEINTERLACE_TRD_WB_STRIDE		0xfc
#define DEINTERLACE_CH0_IN_SIZE			0x100
#define DEINTERLACE_CH0_OUT_SIZE		0x104
#define DEINTERLACE_CH0_HORZ_FACT		0x108
#define DEINTERLACE_CH0_VERT_FACT		0x10c
#define DEINTERLACE_CH0_HORZ_PHASE		0x110
#define DEINTERLACE_CH0_VERT_PHASE0		0x114
#define DEINTERLACE_CH0_VERT_PHASE1		0x118
#define DEINTERLACE_CH0_HORZ_TAP0		0x120
#define DEINTERLACE_CH0_HORZ_TAP1		0x124
#define DEINTERLACE_CH0_VERT_TAP		0x128
#define DEINTERLACE_CH1_IN_SIZE			0x200
#define DEINTERLACE_CH1_OUT_SIZE		0x204
#define DEINTERLACE_CH1_HORZ_FACT		0x208
#define DEINTERLACE_CH1_VERT_FACT		0x20c
#define DEINTERLACE_CH1_HORZ_PHASE		0x210
#define DEINTERLACE_CH1_VERT_PHASE0		0x214
#define DEINTERLACE_CH1_VERT_PHASE1		0x218
#define DEINTERLACE_CH1_HORZ_TAP0		0x220
#define DEINTERLACE_CH1_HORZ_TAP1		0x224
#define DEINTERLACE_CH1_VERT_TAP		0x228
#define DEINTERLACE_CH0_HORZ_COEF0		0x400 /* 32 registers */
#define DEINTERLACE_CH0_HORZ_COEF1		0x480 /* 32 registers */
#define DEINTERLACE_CH0_VERT_COEF		0x500 /* 32 registers */
#define DEINTERLACE_CH1_HORZ_COEF0		0x600 /* 32 registers */
#define DEINTERLACE_CH1_HORZ_COEF1		0x680 /* 32 registers */
#define DEINTERLACE_CH1_VERT_COEF		0x700 /* 32 registers */
#define DEINTERLACE_CH3_HORZ_COEF0		0x800 /* 32 registers */
#define DEINTERLACE_CH3_HORZ_COEF1		0x880 /* 32 registers */
#define DEINTERLACE_CH3_VERT_COEF		0x900 /* 32 registers */

#define DEINTERLACE_MIN_WIDTH	2U
#define DEINTERLACE_MIN_HEIGHT	2U
#define DEINTERLACE_MAX_WIDTH	2048U
#define DEINTERLACE_MAX_HEIGHT	1100U

#define DEINTERLACE_MODE_UV_COMBINED	2

#define DEINTERLACE_IN_FMT_YUV420	2

#define DEINTERLACE_OUT_FMT_YUV420SP	13

#define DEINTERLACE_PS_UVUV		0
#define DEINTERLACE_PS_VUVU		1

#define DEINTERLACE_IDENTITY_COEF	0x4000

#define DEINTERLACE_SIZE(w, h)	(((h) - 1) << 16 | ((w) - 1))

struct deinterlace_ctx {
	struct v4l2_fh		fh;
	struct deinterlace_dev	*dev;

	struct v4l2_pix_format	src_fmt;
	struct v4l2_pix_format	dst_fmt;

	void			*flag1_buf;
	dma_addr_t		flag1_buf_dma;

	void			*flag2_buf;
	dma_addr_t		flag2_buf_dma;

	struct vb2_v4l2_buffer	*prev;

	unsigned int		first_field;
	unsigned int		field;

	int			aborting;
};

struct deinterlace_dev {
	struct v4l2_device	v4l2_dev;
	struct video_device	vfd;
	struct device		*dev;
	struct v4l2_m2m_dev	*m2m_dev;

	/* Device file mutex */
	struct mutex		dev_mutex;

	void __iomem		*base;

	struct clk		*bus_clk;
	struct clk		*mod_clk;
	struct clk		*ram_clk;

	struct reset_control	*rstc;
};

#endif
