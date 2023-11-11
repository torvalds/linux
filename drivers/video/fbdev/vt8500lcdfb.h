/* SPDX-License-Identifier: GPL-2.0-only */
/*
 *  linux/drivers/video/vt8500lcdfb.h
 *
 *  Copyright (C) 2010 Alexey Charkov <alchark@gmail.com>
 */

struct vt8500lcd_info {
	struct fb_info		fb;
	void __iomem		*regbase;
	void __iomem		*palette_cpu;
	dma_addr_t		palette_phys;
	size_t			palette_size;
	wait_queue_head_t	wait;
};

static int bpp_values[] = {
	1,
	2,
	4,
	8,
	12,
	16,
	18,
	24,
};
