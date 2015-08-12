/*
 * Copyright (C) 2010-2014 Freescale Semiconductor, Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
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
 */
/*
 * Based on STMP378X PxP driver
 * Copyright 2008-2009 Embedded Alley Solutions, Inc All Rights Reserved.
 */

#ifndef	_MXC_PXP_V4L2_H
#define	_MXC_PXP_V4L2_H

#include <linux/dmaengine.h>
#include <linux/pxp_dma.h>

struct pxp_buffer {
	/* Must be first! */
	struct videobuf_buffer vb;

	/* One descriptor per scatterlist (per frame) */
	struct dma_async_tx_descriptor		*txd;

	struct scatterlist			sg[3];
};

struct dma_mem {
	void *vaddr;
	dma_addr_t paddr;
	size_t size;
};

struct pxps {
	struct platform_device *pdev;

	spinlock_t lock;
	struct mutex mutex;
	int users;

	struct video_device *vdev;

	struct videobuf_queue s0_vbq;
	struct pxp_buffer *active;
	struct list_head outq;
	struct pxp_channel	*pxp_channel[1];	/* We need 1 channel */
	struct pxp_config_data pxp_conf;
	struct dma_mem outbuf;

	int output;

	/* Current S0 configuration */
	struct pxp_data_format *s0_fmt;

	struct fb_info *fbi;
	struct v4l2_framebuffer fb;

	/* Output overlay support */
	int overlay_state;
	int global_alpha_state;
	u8  global_alpha;
	int s1_chromakey_state;
	u32 s1_chromakey;

	int fb_blank;
};

struct pxp_data_format {
	char *name;
	unsigned int bpp;
	u32 fourcc;
	enum v4l2_colorspace colorspace;
};

#endif
