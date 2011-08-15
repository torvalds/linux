/*
 * omap_voutlib.h
 *
 * Copyright (C) 2010 Texas Instruments.
 *
 * This file is licensed under the terms of the GNU General Public License
 * version 2. This program is licensed "as is" without any warranty of any
 * kind, whether express or implied.
 *
 */

#ifndef OMAP_VOUTLIB_H
#define OMAP_VOUTLIB_H

void omap_vout_default_crop(struct v4l2_pix_format *pix,
		struct v4l2_framebuffer *fbuf, struct v4l2_rect *crop);

int omap_vout_new_crop(struct v4l2_pix_format *pix,
		struct v4l2_rect *crop, struct v4l2_window *win,
		struct v4l2_framebuffer *fbuf,
		const struct v4l2_rect *new_crop);

int omap_vout_try_window(struct v4l2_framebuffer *fbuf,
		struct v4l2_window *new_win);

int omap_vout_new_window(struct v4l2_rect *crop,
		struct v4l2_window *win, struct v4l2_framebuffer *fbuf,
		struct v4l2_window *new_win);

void omap_vout_new_format(struct v4l2_pix_format *pix,
		struct v4l2_framebuffer *fbuf, struct v4l2_rect *crop,
		struct v4l2_window *win);
unsigned long omap_vout_alloc_buffer(u32 buf_size, u32 *phys_addr);
void omap_vout_free_buffer(unsigned long virtaddr, u32 buf_size);
#endif	/* #ifndef OMAP_VOUTLIB_H */

