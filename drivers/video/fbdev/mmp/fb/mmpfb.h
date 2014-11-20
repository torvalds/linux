/*
 * linux/drivers/video/mmp/fb/mmpfb.h
 * Framebuffer driver for Marvell Display controller.
 *
 * Copyright (C) 2012 Marvell Technology Group Ltd.
 * Authors: Zhou Zhu <zzhu3@marvell.com>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2 of the License, or (at your
 * option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 */

#ifndef _MMP_FB_H_
#define _MMP_FB_H_

#include <video/mmp_disp.h>
#include <linux/fb.h>

/* LCD controller private state. */
struct mmpfb_info {
	struct device	*dev;
	int	id;
	const char	*name;

	struct fb_info	*fb_info;
	/* basicaly videomode is for output */
	struct fb_videomode	mode;
	int	pix_fmt;

	void	*fb_start;
	int	fb_size;
	dma_addr_t	fb_start_dma;

	struct mmp_overlay	*overlay;
	struct mmp_path	*path;

	struct mutex	access_ok;

	unsigned int		pseudo_palette[16];
	int output_fmt;
};

#define MMPFB_DEFAULT_SIZE (PAGE_ALIGN(1920 * 1080 * 4 * 2))
#endif /* _MMP_FB_H_ */
