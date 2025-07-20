/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2014 MediaTek Inc.
 * Author: Jie Qiu <jie.qiu@mediatek.com>
 */
#ifndef __MTK_DPI_REGS_H
#define __MTK_DPI_REGS_H

#define DPI_EN			0x00
#define EN				BIT(0)

#define DPI_RET			0x04
#define RST				BIT(0)

#define DPI_INTEN		0x08
#define INT_VSYNC_EN			BIT(0)
#define INT_VDE_EN			BIT(1)
#define INT_UNDERFLOW_EN		BIT(2)

#define DPI_INTSTA		0x0C
#define INT_VSYNC_STA			BIT(0)
#define INT_VDE_STA			BIT(1)
#define INT_UNDERFLOW_STA		BIT(2)

#define DPI_CON			0x10
#define BG_ENABLE			BIT(0)
#define IN_RB_SWAP			BIT(1)
#define INTL_EN				BIT(2)
#define TDFP_EN				BIT(3)
#define CLPF_EN				BIT(4)
#define YUV422_EN			BIT(5)
#define CSC_ENABLE			BIT(6)
#define R601_SEL			BIT(7)
#define EMBSYNC_EN			BIT(8)
#define VS_LODD_EN			BIT(16)
#define VS_LEVEN_EN			BIT(17)
#define VS_RODD_EN			BIT(18)
#define VS_REVEN			BIT(19)
#define FAKE_DE_LODD			BIT(20)
#define FAKE_DE_LEVEN			BIT(21)
#define FAKE_DE_RODD			BIT(22)
#define FAKE_DE_REVEN			BIT(23)

/* DPI_CON: DPI instances */
#define DPI_OUTPUT_1T1P_EN		BIT(24)
#define DPI_INPUT_2P_EN			BIT(25)
/* DPI_CON: DPINTF instances */
#define DPINTF_YUV422_EN		BIT(24)
#define DPINTF_CSC_ENABLE		BIT(26)
#define DPINTF_INPUT_2P_EN		BIT(29)

#define DPI_OUTPUT_SETTING	0x14
#define CH_SWAP				0
#define DPINTF_CH_SWAP			1
#define CH_SWAP_MASK			(0x7 << 0)
#define SWAP_RGB			0x00
#define SWAP_GBR			0x01
#define SWAP_BRG			0x02
#define SWAP_RBG			0x03
#define SWAP_GRB			0x04
#define SWAP_BGR			0x05
#define BIT_SWAP			BIT(3)
#define B_MASK				BIT(4)
#define G_MASK				BIT(5)
#define R_MASK				BIT(6)
#define DE_MASK				BIT(8)
#define HS_MASK				BIT(9)
#define VS_MASK				BIT(10)
#define DE_POL				BIT(12)
#define HSYNC_POL			BIT(13)
#define VSYNC_POL			BIT(14)
#define CK_POL				BIT(15)
#define OEN_OFF				BIT(16)
#define EDGE_SEL			BIT(17)
#define OUT_BIT				18
#define OUT_BIT_MASK			(0x3 << 18)
#define OUT_BIT_8			0x00
#define OUT_BIT_10			0x01
#define OUT_BIT_12			0x02
#define OUT_BIT_16			0x03
#define YC_MAP				20
#define YC_MAP_MASK			(0x7 << 20)
#define YC_MAP_RGB			0x00
#define YC_MAP_CYCY			0x04
#define YC_MAP_YCYC			0x05
#define YC_MAP_CY			0x06
#define YC_MAP_YC			0x07

#define DPI_SIZE		0x18
#define HSIZE				0
#define HSIZE_MASK			(0x1FFF << 0)
#define DPINTF_HSIZE_MASK		(0xFFFF << 0)
#define VSIZE				16
#define VSIZE_MASK			(0x1FFF << 16)
#define DPINTF_VSIZE_MASK		(0xFFFF << 16)

#define DPI_DDR_SETTING		0x1C
#define DDR_EN				BIT(0)
#define DDDR_SEL			BIT(1)
#define DDR_4PHASE			BIT(2)
#define DDR_WIDTH			(0x3 << 4)
#define DDR_PAD_MODE			(0x1 << 8)

#define DPI_TGEN_HWIDTH		0x20
#define HPW				0
#define HPW_MASK			(0xFFF << 0)
#define DPINTF_HPW_MASK			(0xFFFF << 0)

#define DPI_TGEN_HPORCH		0x24
#define HBP				0
#define HBP_MASK			(0xFFF << 0)
#define DPINTF_HBP_MASK			(0xFFFF << 0)
#define HFP				16
#define HFP_MASK			(0xFFF << 16)
#define DPINTF_HFP_MASK			(0xFFFF << 16)

#define DPI_TGEN_VWIDTH		0x28
#define DPI_TGEN_VPORCH		0x2C

#define VSYNC_WIDTH_SHIFT		0
#define VSYNC_WIDTH_MASK		(0xFFF << 0)
#define DPINTF_VSYNC_WIDTH_MASK		(0xFFFF << 0)
#define VSYNC_HALF_LINE_SHIFT		16
#define VSYNC_HALF_LINE_MASK		BIT(16)
#define VSYNC_BACK_PORCH_SHIFT		0
#define VSYNC_BACK_PORCH_MASK		(0xFFF << 0)
#define DPINTF_VSYNC_BACK_PORCH_MASK	(0xFFFF << 0)
#define VSYNC_FRONT_PORCH_SHIFT		16
#define VSYNC_FRONT_PORCH_MASK		(0xFFF << 16)
#define DPINTF_VSYNC_FRONT_PORCH_MASK	(0xFFFF << 16)

#define DPI_BG_HCNTL		0x30
#define BG_RIGHT			(0x1FFF << 0)
#define BG_LEFT				(0x1FFF << 16)

#define DPI_BG_VCNTL		0x34
#define BG_BOT				(0x1FFF << 0)
#define BG_TOP				(0x1FFF << 16)

#define DPI_BG_COLOR		0x38
#define BG_B				(0xF << 0)
#define BG_G				(0xF << 8)
#define BG_R				(0xF << 16)

#define DPI_FIFO_CTL		0x3C
#define FIFO_VALID_SET			(0x1F << 0)
#define FIFO_RST_SEL			(0x1 << 8)

#define DPI_STATUS		0x40
#define VCOUNTER			(0x1FFF << 0)
#define DPI_BUSY			BIT(16)
#define OUTEN				BIT(17)
#define FIELD				BIT(20)
#define TDLR				BIT(21)

#define DPI_TMODE		0x44
#define DPI_OEN_ON			BIT(0)

#define DPI_CHECKSUM		0x48
#define DPI_CHECKSUM_MASK		(0xFFFFFF << 0)
#define DPI_CHECKSUM_READY		BIT(30)
#define DPI_CHECKSUM_EN			BIT(31)

#define DPI_DUMMY		0x50
#define DPI_DUMMY_MASK			(0xFFFFFFFF << 0)

#define DPI_TGEN_VWIDTH_LEVEN	0x68
#define DPI_TGEN_VPORCH_LEVEN	0x6C
#define DPI_TGEN_VWIDTH_RODD	0x70
#define DPI_TGEN_VPORCH_RODD	0x74
#define DPI_TGEN_VWIDTH_REVEN	0x78
#define DPI_TGEN_VPORCH_REVEN	0x7C

#define DPI_ESAV_VTIMING_LODD	0x80
#define ESAV_VOFST_LODD			(0xFFF << 0)
#define ESAV_VWID_LODD			(0xFFF << 16)

#define DPI_ESAV_VTIMING_LEVEN	0x84
#define ESAV_VOFST_LEVEN		(0xFFF << 0)
#define ESAV_VWID_LEVEN			(0xFFF << 16)

#define DPI_ESAV_VTIMING_RODD	0x88
#define ESAV_VOFST_RODD			(0xFFF << 0)
#define ESAV_VWID_RODD			(0xFFF << 16)

#define DPI_ESAV_VTIMING_REVEN	0x8C
#define ESAV_VOFST_REVEN		(0xFFF << 0)
#define ESAV_VWID_REVEN			(0xFFF << 16)

#define DPI_ESAV_FTIMING	0x90
#define ESAV_FOFST_ODD			(0xFFF << 0)
#define ESAV_FOFST_EVEN			(0xFFF << 16)

#define DPI_CLPF_SETTING	0x94
#define CLPF_TYPE			(0x3 << 0)
#define ROUND_EN			BIT(4)

#define DPI_Y_LIMIT		0x98
#define Y_LIMINT_BOT			0
#define Y_LIMINT_BOT_MASK		(0xFFF << 0)
#define Y_LIMINT_TOP			16
#define Y_LIMINT_TOP_MASK		(0xFFF << 16)

#define DPI_C_LIMIT		0x9C
#define C_LIMIT_BOT			0
#define C_LIMIT_BOT_MASK		(0xFFF << 0)
#define C_LIMIT_TOP			16
#define C_LIMIT_TOP_MASK		(0xFFF << 16)

#define DPI_YUV422_SETTING	0xA0
#define UV_SWAP				BIT(0)
#define CR_DELSEL			BIT(4)
#define CB_DELSEL			BIT(5)
#define Y_DELSEL			BIT(6)
#define DE_DELSEL			BIT(7)

#define DPI_EMBSYNC_SETTING	0xA4
#define EMBSYNC_R_CR_EN			BIT(0)
#define EMPSYNC_G_Y_EN			BIT(1)
#define EMPSYNC_B_CB_EN			BIT(2)
#define ESAV_F_INV			BIT(4)
#define ESAV_V_INV			BIT(5)
#define ESAV_H_INV			BIT(6)
#define ESAV_CODE_MAN			BIT(8)
#define VS_OUT_SEL			(0x7 << 12)

#define DPI_ESAV_CODE_SET0	0xA8
#define ESAV_CODE0			(0xFFF << 0)
#define ESAV_CODE1			(0xFFF << 16)

#define DPI_ESAV_CODE_SET1	0xAC
#define ESAV_CODE2			(0xFFF << 0)
#define ESAV_CODE3_MSB			BIT(16)

#define EDGE_SEL_EN			BIT(5)
#define H_FRE_2N			BIT(25)

#define DPI_MATRIX_SET		0xB4
#define INT_MATRIX_SEL_MASK		GENMASK(4, 0)
#define MATRIX_SEL_RGB_TO_JPEG		0
#define MATRIX_SEL_RGB_TO_BT601		2

#define DPI_PATTERN0		0xf00
#define DPI_PAT_EN			BIT(0)
#define DPI_PAT_SEL			GENMASK(6, 4)

#endif /* __MTK_DPI_REGS_H */
