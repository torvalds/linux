/*
 *  linux/drivers/video/vt8500lcdfb.h
 *
 *  Copyright (C) 2010 Alexey Charkov <alchark@gmail.com>
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
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
