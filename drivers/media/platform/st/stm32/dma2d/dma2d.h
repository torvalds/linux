/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * ST stm32 DMA2D - 2D Graphics Accelerator Driver
 *
 * Copyright (c) 2021 Dillon Min
 * Dillon Min, <dillon.minfei@gmail.com>
 *
 * based on s5p-g2d
 *
 * Copyright (c) 2011 Samsung Electronics Co., Ltd.
 * Kamil Debski, <k.debski@samsung.com>
 */

#ifndef __DMA2D_H__
#define __DMA2D_H__

#include <linux/platform_device.h>
#include <media/v4l2-device.h>
#include <media/v4l2-ctrls.h>

#define DMA2D_NAME "stm-dma2d"
#define BUS_INFO "platform:stm-dma2d"
enum dma2d_op_mode {
	DMA2D_MODE_M2M,
	DMA2D_MODE_M2M_FPC,
	DMA2D_MODE_M2M_BLEND,
	DMA2D_MODE_R2M
};

enum dma2d_cmode {
	/* output pfc cmode from ARGB888 to ARGB4444 */
	DMA2D_CMODE_ARGB8888,
	DMA2D_CMODE_RGB888,
	DMA2D_CMODE_RGB565,
	DMA2D_CMODE_ARGB1555,
	DMA2D_CMODE_ARGB4444,
	/* bg or fg pfc cmode from L8 to A4 */
	DMA2D_CMODE_L8,
	DMA2D_CMODE_AL44,
	DMA2D_CMODE_AL88,
	DMA2D_CMODE_L4,
	DMA2D_CMODE_A8,
	DMA2D_CMODE_A4
};

enum dma2d_alpha_mode {
	DMA2D_ALPHA_MODE_NO_MODIF,
	DMA2D_ALPHA_MODE_REPLACE,
	DMA2D_ALPHA_MODE_COMBINE
};

struct dma2d_fmt {
	u32	fourcc;
	int	depth;
	enum dma2d_cmode cmode;
};

struct dma2d_frame {
	/* Original dimensions */
	u32	width;
	u32	height;
	/* Crop size */
	u32	c_width;
	u32	c_height;
	/* Offset */
	u32	o_width;
	u32	o_height;
	u32	bottom;
	u32	right;
	u16	line_offset;
	/* Image format */
	struct dma2d_fmt *fmt;
	/* [0]: blue
	 * [1]: green
	 * [2]: red
	 * [3]: alpha
	 */
	u8	a_rgb[4];
	/*
	 * AM[1:0] of DMA2D_FGPFCCR
	 */
	enum dma2d_alpha_mode a_mode;
	u32 size;
	unsigned int	sequence;
};

struct dma2d_ctx {
	struct v4l2_fh fh;
	struct dma2d_dev	*dev;
	struct dma2d_frame	cap;
	struct dma2d_frame	out;
	struct dma2d_frame	bg;
	/*
	 * MODE[17:16] of DMA2D_CR
	 */
	enum dma2d_op_mode	op_mode;
	struct v4l2_ctrl_handler ctrl_handler;
	enum v4l2_colorspace	colorspace;
	enum v4l2_ycbcr_encoding ycbcr_enc;
	enum v4l2_xfer_func	xfer_func;
	enum v4l2_quantization	quant;
};

struct dma2d_dev {
	struct v4l2_device	v4l2_dev;
	struct v4l2_m2m_dev	*m2m_dev;
	struct video_device	*vfd;
	/* for device open/close etc */
	struct mutex		mutex;
	/* to avoid the conflict with device running and user setting
	 * at the same time
	 */
	spinlock_t		ctrl_lock;
	atomic_t		num_inst;
	void __iomem		*regs;
	struct clk		*gate;
	struct dma2d_ctx	*curr;
	int irq;
};

void dma2d_start(struct dma2d_dev *d);
u32 dma2d_get_int(struct dma2d_dev *d);
void dma2d_clear_int(struct dma2d_dev *d);
void dma2d_config_out(struct dma2d_dev *d, struct dma2d_frame *frm,
		      dma_addr_t o_addr);
void dma2d_config_fg(struct dma2d_dev *d, struct dma2d_frame *frm,
		     dma_addr_t f_addr);
void dma2d_config_bg(struct dma2d_dev *d, struct dma2d_frame *frm,
		     dma_addr_t b_addr);
void dma2d_config_common(struct dma2d_dev *d, enum dma2d_op_mode op_mode,
			 u16 width, u16 height);

#endif /* __DMA2D_H__ */
