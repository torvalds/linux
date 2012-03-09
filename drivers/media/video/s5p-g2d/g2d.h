/*
 * Samsung S5P G2D - 2D Graphics Accelerator Driver
 *
 * Copyright (c) 2011 Samsung Electronics Co., Ltd.
 * Kamil Debski, <k.debski@samsung.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2 of the
 * License, or (at your option) any later version
 */

#include <media/v4l2-device.h>
#include <media/v4l2-ctrls.h>

#define G2D_NAME "s5p-g2d"

struct g2d_dev {
	struct v4l2_device	v4l2_dev;
	struct v4l2_m2m_dev	*m2m_dev;
	struct video_device	*vfd;
	struct mutex		mutex;
	spinlock_t		irqlock;
	atomic_t		num_inst;
	struct vb2_alloc_ctx	*alloc_ctx;
	struct resource		*res_regs;
	void __iomem		*regs;
	struct clk		*clk;
	struct clk		*gate;
	struct g2d_ctx		*curr;
	int irq;
	wait_queue_head_t	irq_queue;
};

struct g2d_frame {
	/* Original dimensions */
	u32	width;
	u32	height;
	/* Crop size */
	u32	c_width;
	u32	c_height;
	/* Offset */
	u32	o_width;
	u32	o_height;
	/* Image format */
	struct g2d_fmt *fmt;
	/* Variables that can calculated once and reused */
	u32	stride;
	u32	bottom;
	u32	right;
	u32	size;
};

struct g2d_ctx {
	struct v4l2_fh fh;
	struct g2d_dev		*dev;
	struct v4l2_m2m_ctx     *m2m_ctx;
	struct g2d_frame	in;
	struct g2d_frame	out;
	struct v4l2_ctrl_handler ctrl_handler;
	u32 rop;
};

struct g2d_fmt {
	char	*name;
	u32	fourcc;
	int	depth;
	u32	hw;
};


void g2d_reset(struct g2d_dev *d);
void g2d_set_src_size(struct g2d_dev *d, struct g2d_frame *f);
void g2d_set_src_addr(struct g2d_dev *d, dma_addr_t a);
void g2d_set_dst_size(struct g2d_dev *d, struct g2d_frame *f);
void g2d_set_dst_addr(struct g2d_dev *d, dma_addr_t a);
void g2d_start(struct g2d_dev *d);
void g2d_clear_int(struct g2d_dev *d);
void g2d_set_rop4(struct g2d_dev *d, u32 r);
u32 g2d_cmd_stretch(u32 e);
void g2d_set_cmd(struct g2d_dev *d, u32 c);


