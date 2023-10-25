/* SPDX-License-Identifier: GPL-2.0 */
/*
 * stf_capture.h
 *
 * Starfive Camera Subsystem driver
 *
 * Copyright (C) 2021-2023 StarFive Technology Co., Ltd.
 */

#ifndef STF_CAPTURE_H
#define STF_CAPTURE_H

#include "stf-video.h"

#define VIN_CHANNEL_SEL_EN			0x14
#define VIN_START_ADDR_N			0x18
#define VIN_INRT_PIX_CFG			0x1c
#define VIN_START_ADDR_O			0x20
#define VIN_CFG_REG				0x24

#define U0_VIN_CNFG_AXI_DVP_EN			BIT(2)

#define U0_VIN_CHANNEL_SEL_MASK			GENMASK(3, 0)
#define U0_VIN_AXIWR0_EN			BIT(4)
#define CHANNEL(x)				((x) << 0)

#define U0_VIN_INTR_CLEAN			BIT(0)
#define U0_VIN_INTR_M				BIT(1)
#define U0_VIN_PIX_CNT_END_MASK			GENMASK(12, 2)
#define U0_VIN_PIX_CT_MASK			GENMASK(14, 13)
#define U0_VIN_PIXEL_HEIGH_BIT_SEL_MAKS		GENMASK(16, 15)

#define PIX_CNT_END(x)				((x) << 2)
#define PIX_CT(x)				((x) << 13)
#define PIXEL_HEIGH_BIT_SEL(x)			((x) << 15)

#define U0_VIN_CNFG_DVP_HS_POS			BIT(1)
#define U0_VIN_CNFG_DVP_SWAP_EN			BIT(2)
#define U0_VIN_CNFG_DVP_VS_POS			BIT(3)
#define U0_VIN_CNFG_GEN_EN_AXIRD		BIT(4)
#define U0_VIN_CNFG_ISP_DVP_EN0			BIT(5)
#define U0_VIN_MIPI_BYTE_EN_ISP0(n)		((n) << 6)
#define U0_VIN_MIPI_CHANNEL_SEL0(n)		((n) << 8)
#define U0_VIN_P_I_MIPI_HAEDER_EN0(n)		((n) << 12)
#define U0_VIN_PIX_NUM(n)			((n) << 13)
#define U0_VIN_MIPI_BYTE_EN_ISP0_MASK		GENMASK(7, 6)
#define U0_VIN_MIPI_CHANNEL_SEL0_MASK		GENMASK(11, 8)
#define U0_VIN_P_I_MIPI_HAEDER_EN0_MASK		BIT(12)
#define U0_VIN_PIX_NUM_MASK			GENMASK(16, 13)

enum stf_v_state {
	STF_OUTPUT_OFF,
	STF_OUTPUT_RESERVED,
	STF_OUTPUT_SINGLE,
	STF_OUTPUT_CONTINUOUS,
	STF_OUTPUT_IDLE,
	STF_OUTPUT_STOPPING
};

struct stf_v_buf {
	int active_buf;
	struct stfcamss_buffer *buf[2];
	struct stfcamss_buffer *last_buffer;
	struct list_head pending_bufs;
	struct list_head ready_bufs;
	enum stf_v_state state;
	unsigned int sequence;
	/* protects the above member variables */
	spinlock_t lock;
	atomic_t frame_skip;
};

struct stf_capture {
	struct stfcamss_video video;
	struct stf_v_buf buffers;
	enum stf_capture_type type;
};

irqreturn_t stf_wr_irq_handler(int irq, void *priv);
irqreturn_t stf_isp_irq_handler(int irq, void *priv);
irqreturn_t stf_line_irq_handler(int irq, void *priv);
int stf_capture_register(struct stfcamss *stfcamss,
			 struct v4l2_device *v4l2_dev);
void stf_capture_unregister(struct stfcamss *stfcamss);

#endif
