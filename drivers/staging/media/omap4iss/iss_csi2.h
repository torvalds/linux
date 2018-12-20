/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * TI OMAP4 ISS V4L2 Driver - CSI2 module
 *
 * Copyright (C) 2012 Texas Instruments, Inc.
 *
 * Author: Sergio Aguirre <sergio.a.aguirre@gmail.com>
 */

#ifndef OMAP4_ISS_CSI2_H
#define OMAP4_ISS_CSI2_H

#include <linux/types.h>
#include <linux/videodev2.h>

#include "iss_video.h"

struct iss_csiphy;

/* This is not an exhaustive list */
enum iss_csi2_pix_formats {
	CSI2_PIX_FMT_OTHERS = 0,
	CSI2_PIX_FMT_YUV422_8BIT = 0x1e,
	CSI2_PIX_FMT_YUV422_8BIT_VP = 0x9e,
	CSI2_PIX_FMT_YUV422_8BIT_VP16 = 0xde,
	CSI2_PIX_FMT_RAW10_EXP16 = 0xab,
	CSI2_PIX_FMT_RAW10_EXP16_VP = 0x12f,
	CSI2_PIX_FMT_RAW8 = 0x2a,
	CSI2_PIX_FMT_RAW8_DPCM10_EXP16 = 0x2aa,
	CSI2_PIX_FMT_RAW8_DPCM10_VP = 0x32a,
	CSI2_PIX_FMT_RAW8_VP = 0x12a,
	CSI2_USERDEF_8BIT_DATA1_DPCM10_VP = 0x340,
	CSI2_USERDEF_8BIT_DATA1_DPCM10 = 0x2c0,
	CSI2_USERDEF_8BIT_DATA1 = 0x40,
};

enum iss_csi2_irqevents {
	OCP_ERR_IRQ = 0x4000,
	SHORT_PACKET_IRQ = 0x2000,
	ECC_CORRECTION_IRQ = 0x1000,
	ECC_NO_CORRECTION_IRQ = 0x800,
	COMPLEXIO2_ERR_IRQ = 0x400,
	COMPLEXIO1_ERR_IRQ = 0x200,
	FIFO_OVF_IRQ = 0x100,
	CONTEXT7 = 0x80,
	CONTEXT6 = 0x40,
	CONTEXT5 = 0x20,
	CONTEXT4 = 0x10,
	CONTEXT3 = 0x8,
	CONTEXT2 = 0x4,
	CONTEXT1 = 0x2,
	CONTEXT0 = 0x1,
};

enum iss_csi2_ctx_irqevents {
	CTX_ECC_CORRECTION = 0x100,
	CTX_LINE_NUMBER = 0x80,
	CTX_FRAME_NUMBER = 0x40,
	CTX_CS = 0x20,
	CTX_LE = 0x8,
	CTX_LS = 0x4,
	CTX_FE = 0x2,
	CTX_FS = 0x1,
};

enum iss_csi2_frame_mode {
	ISS_CSI2_FRAME_IMMEDIATE,
	ISS_CSI2_FRAME_AFTERFEC,
};

#define ISS_CSI2_MAX_CTX_NUM	7

struct iss_csi2_ctx_cfg {
	u8 ctxnum;		/* context number 0 - 7 */
	u8 dpcm_decompress;

	/* Fields in CSI2_CTx_CTRL2 - locked by CSI2_CTx_CTRL1.CTX_EN */
	u8 virtual_id;
	u16 format_id;		/* as in CSI2_CTx_CTRL2[9:0] */
	u8 dpcm_predictor;	/* 1: simple, 0: advanced */
	u16 frame;

	/* Fields in CSI2_CTx_CTRL1/3 - Shadowed */
	u16 alpha;
	u16 data_offset;
	u32 ping_addr;
	u32 pong_addr;
	u8 eof_enabled;
	u8 eol_enabled;
	u8 checksum_enabled;
	u8 enabled;
};

struct iss_csi2_timing_cfg {
	u8 ionum;			/* IO1 or IO2 as in CSI2_TIMING */
	unsigned force_rx_mode:1;
	unsigned stop_state_16x:1;
	unsigned stop_state_4x:1;
	u16 stop_state_counter;
};

struct iss_csi2_ctrl_cfg {
	bool vp_clk_enable;
	bool vp_only_enable;
	u8 vp_out_ctrl;
	enum iss_csi2_frame_mode frame_mode;
	bool ecc_enable;
	bool if_enable;
};

#define CSI2_PAD_SINK		0
#define CSI2_PAD_SOURCE		1
#define CSI2_PADS_NUM		2

#define CSI2_OUTPUT_IPIPEIF	BIT(0)
#define CSI2_OUTPUT_MEMORY	BIT(1)

struct iss_csi2_device {
	struct v4l2_subdev subdev;
	struct media_pad pads[CSI2_PADS_NUM];
	struct v4l2_mbus_framefmt formats[CSI2_PADS_NUM];

	struct iss_video video_out;
	struct iss_device *iss;

	u8 available;		/* Is the IP present on the silicon? */

	/* memory resources, as defined in enum iss_mem_resources */
	unsigned int regs1;
	unsigned int regs2;
	/* ISP subclock, as defined in enum iss_isp_subclk_resource */
	unsigned int subclk;

	u32 output; /* output to IPIPEIF, memory or both? */
	bool dpcm_decompress;
	unsigned int frame_skip;

	struct iss_csiphy *phy;
	struct iss_csi2_ctx_cfg contexts[ISS_CSI2_MAX_CTX_NUM + 1];
	struct iss_csi2_timing_cfg timing[2];
	struct iss_csi2_ctrl_cfg ctrl;
	enum iss_pipeline_stream_state state;
	wait_queue_head_t wait;
	atomic_t stopping;
};

void omap4iss_csi2_isr(struct iss_csi2_device *csi2);
int omap4iss_csi2_reset(struct iss_csi2_device *csi2);
int omap4iss_csi2_init(struct iss_device *iss);
int omap4iss_csi2_create_links(struct iss_device *iss);
void omap4iss_csi2_cleanup(struct iss_device *iss);
void omap4iss_csi2_unregister_entities(struct iss_csi2_device *csi2);
int omap4iss_csi2_register_entities(struct iss_csi2_device *csi2,
				    struct v4l2_device *vdev);
#endif	/* OMAP4_ISS_CSI2_H */
