/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) Pengutronix e.K.
 * Author: Sven Püschel <s.pueschel@pengutronix.de>
 */
#ifndef __RGA3_HW_H__
#define __RGA3_HW_H__

#include <linux/bits.h>
#include <linux/types.h>

#define RGA3_CMDBUF_SIZE 0xb8

#define RGA3_MIN_WIDTH 128
#define RGA3_MIN_HEIGHT 128
#define RGA3_MAX_INPUT_WIDTH (8192 - 16)
#define RGA3_MAX_INPUT_HEIGHT (8192 - 16)
#define RGA3_MAX_OUTPUT_WIDTH (8192 - 64)
#define RGA3_MAX_OUTPUT_HEIGHT (8192 - 64)
#define RGA3_MAX_SCALING_FACTOR 8
#define RGA3_RESET_TIMEOUT 1000

/* Registers address */
/* sys reg */
#define RGA3_SYS_CTRL				0x000
#define RGA3_CMD_CTRL				0x004
#define RGA3_CMD_ADDR				0x008
#define RGA3_MI_GROUP_CTRL			0x00c
#define RGA3_ARQOS_CTRL				0x010
#define RGA3_VERSION_NUM			0x018
#define RGA3_VERSION_TIM			0x01c
#define RGA3_INT_EN				0x020
#define RGA3_INT_RAW				0x024
#define RGA3_INT_MSK				0x028
#define RGA3_INT_CLR				0x02c
#define RGA3_RO_SRST				0x030
#define RGA3_STATUS0				0x034
#define RGA3_SCAN_CNT				0x038
#define RGA3_CMD_STATE				0x040

/* cmd reg */
#define RGA3_WIN0_RD_CTRL			0x100
#define RGA3_FIRST_CMD_REG			RGA3_WIN0_RD_CTRL
#define RGA3_WIN0_Y_BASE			0x110
#define RGA3_WIN0_U_BASE			0x114
#define RGA3_WIN0_V_BASE			0x118
#define RGA3_WIN0_VIR_STRIDE			0x11c
#define RGA3_WIN0_FBC_OFF			0x120
#define RGA3_WIN0_SRC_SIZE			0x124
#define RGA3_WIN0_ACT_OFF			0x128
#define RGA3_WIN0_ACT_SIZE			0x12c
#define RGA3_WIN0_DST_SIZE			0x130
#define RGA3_WIN0_SCL_FAC			0x134
#define RGA3_WIN0_UV_VIR_STRIDE			0x138
#define RGA3_WIN1_RD_CTRL			0x140
#define RGA3_WIN1_Y_BASE			0x150
#define RGA3_WIN1_U_BASE			0x154
#define RGA3_WIN1_V_BASE			0x158
#define RGA3_WIN1_VIR_STRIDE			0x15c
#define RGA3_WIN1_FBC_OFF			0x160
#define RGA3_WIN1_SRC_SIZE			0x164
#define RGA3_WIN1_ACT_OFF			0x168
#define RGA3_WIN1_ACT_SIZE			0x16c
#define RGA3_WIN1_DST_SIZE			0x170
#define RGA3_WIN1_SCL_FAC			0x174
#define RGA3_WIN1_UV_VIR_STRIDE			0x178
#define RGA3_OVLP_CTRL				0x180
#define RGA3_OVLP_OFF				0x184
#define RGA3_OVLP_TOP_KEY_MIN			0x188
#define RGA3_OVLP_TOP_KEY_MAX			0x18c
#define RGA3_OVLP_TOP_CTRL			0x190
#define RGA3_OVLP_BOT_CTRL			0x194
#define RGA3_OVLP_TOP_ALPHA			0x198
#define RGA3_OVLP_BOT_ALPHA			0x19c
#define RGA3_WR_CTRL				0x1a0
#define RGA3_WR_FBCE_CTRL			0x1a4
#define RGA3_WR_VIR_STRIDE			0x1a8
#define RGA3_WR_PL_VIR_STRIDE			0x1ac
#define RGA3_WR_Y_BASE				0x1b0
#define RGA3_WR_U_BASE				0x1b4
#define RGA3_WR_V_BASE				0x1b8

/* Registers value */
#define RGA3_COLOR_FMT_YUV420		0x0
#define RGA3_COLOR_FMT_YUV422		0x1
#define RGA3_COLOR_FMT_YUV420_10B	0x2
#define RGA3_COLOR_FMT_YUV422_10B	0x3
/*
 * Use memory ordering names
 * instead of the datasheet naming RGB formats in big endian order
 */
#define RGA3_COLOR_FMT_BGR565		0x4
#define RGA3_COLOR_FMT_BGR888		0x5
#define RGA3_COLOR_FMT_FIRST_HAS_ALPHA	RGA3_COLOR_FMT_BGRA8888
#define RGA3_COLOR_FMT_BGRA8888		0x6
#define RGA3_COLOR_FMT_LAST_OUTPUT	RGA3_COLOR_FMT_BGRA8888
/* the following are only supported as inputs */
#define RGA3_COLOR_FMT_ABGR8888		0x7
/*
 * the following seem to be unnecessary,
 * as they can be achieved with RB swaps
 */
#define RGA3_COLOR_FMT_RGBA8888		0x8
#define RGA3_COLOR_FMT_ARGB8888		0x9

#define RGA3_RDWR_FORMAT_SEMI_PLANAR	0x1
#define RGA3_RDWR_FORMAT_INTERLEAVED	0x2

#define RGA3_CMD_MODE_MASTER 0x1

#define RGA3_WIN_CSC_MODE_BT601_L 0x0
#define RGA3_WIN_CSC_MODE_BT709_L 0x1
#define RGA3_WIN_CSC_MODE_BT601_F 0x2
#define RGA3_WIN_CSC_MODE_BT2020_L 0x3

/* RGA masks */
/* SYS_CTRL */
#define RGA3_CCLK_SRESET BIT(4)
#define RGA3_ACLK_SRESET BIT(3)
#define RGA3_CMD_MODE BIT(1)

/* CMD_CTRL */
#define RGA3_CMD_LINE_START_PULSE BIT(0)

/* VERSION_NUM */
#define RGA3_VERSION_NUM_MAJOR GENMASK(31, 28)
#define RGA3_VERSION_NUM_MINOR GENMASK(27, 20)

/* INT_* */
#define RGA3_INT_FRM_DONE BIT(0)
#define RGA3_INT_DMA_READ_BUS_ERR BIT(2)
#define RGA3_INT_WIN0_FBC_DEC_ERR BIT(5)
#define RGA3_INT_WIN0_HOR_ERR BIT(6)
#define RGA3_INT_WIN0_VER_ERR BIT(7)
#define RGA3_INT_WR_VER_ERR BIT(13)
#define RGA3_INT_WR_HOR_ERR BIT(14)
#define RGA3_INT_WR_BUS_ERR BIT(15)
#define RGA3_INT_WIN0_IN_FIFO_WR_ERR BIT(16)
#define RGA3_INT_WIN0_IN_FIFO_RD_ERR BIT(17)
#define RGA3_INT_WIN0_HOR_FIFO_WR_ERR BIT(18)
#define RGA3_INT_WIN0_HOR_FIFO_RD_ERR BIT(19)
#define RGA3_INT_WIN0_VER_FIFO_WR_ERR BIT(20)
#define RGA3_INT_WIN0_VER_FIFO_RD_ERR BIT(21)

/* RO_SRST */
#define RGA3_RO_SRST_DONE GENMASK(5, 0)

/* *_SIZE */
#define RGA3_HEIGHT GENMASK(28, 16)
#define RGA3_WIDTH GENMASK(12, 0)

/* SCL_FAC */
#define RGA3_SCALE_VER_FAC GENMASK(31, 16)
#define RGA3_SCALE_HOR_FAC GENMASK(15, 0)

/* WINx_CTRL */
#define RGA3_WIN_CSC_MODE GENMASK(27, 26)
#define RGA3_WIN_R2Y BIT(25)
#define RGA3_WIN_Y2R BIT(24)
#define RGA3_WIN_SCALE_VER_UP BIT(23)
#define RGA3_WIN_SCALE_VER_BYPASS BIT(22)
#define RGA3_WIN_SCALE_HOR_UP BIT(21)
#define RGA3_WIN_SCALE_HOR_BYPASS BIT(20)
#define RGA3_WIN_YC_SWAP BIT(13)
#define RGA3_WIN_RBUV_SWAP BIT(12)
#define RGA3_WIN_RD_FORMAT GENMASK(9, 8)
#define RGA3_WIN_PIC_FORMAT GENMASK(7, 4)
#define RGA3_WIN_ENABLE BIT(0)

/* COLOR_CTRL */
#define RGA3_OVLP_GLOBAL_ALPHA GENMASK(23, 16)
#define RGA3_OVLP_COLOR_MODE BIT(0)

/* ALPHA_CTRL */
#define RGA3_ALPHA_SELECT_MODE BIT(4)
#define RGA3_ALPHA_BLEND_MODE GENMASK(3, 2)

/* WR_CTRL */
#define RGA3_WR_YC_SWAP BIT(20)
#define RGA3_WR_SW_OUTSTANDING_MAX GENMASK(18, 13)
#define RGA3_WR_RBUV_SWAP BIT(12)
#define RGA3_WR_FORMAT GENMASK(9, 8)
#define RGA3_WR_PIC_FORMAT GENMASK(7, 4)

struct rga3_fmt {
	u32 fourcc;
	u8 hw_format;
	bool rbuv_swap;
	bool yc_swap;
};

#endif
