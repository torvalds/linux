/*
 * omap_vout_vrfb.h
 *
 * Copyright (C) 2010 Texas Instruments.
 *
 * This file is licensed under the terms of the GNU General Public License
 * version 2. This program is licensed "as is" without any warranty of any
 * kind, whether express or implied.
 *
 */

#ifndef OMAP_VOUT_VRFB_H
#define OMAP_VOUT_VRFB_H

#ifdef CONFIG_VIDEO_OMAP2_VOUT_VRFB
void omap_vout_free_vrfb_buffers(struct omap_vout_device *vout);
int omap_vout_setup_vrfb_bufs(struct platform_device *pdev, int vid_num,
			bool static_vrfb_allocation);
void omap_vout_release_vrfb(struct omap_vout_device *vout);
int omap_vout_vrfb_buffer_setup(struct omap_vout_device *vout,
			unsigned int *count, unsigned int startindex);
int omap_vout_prepare_vrfb(struct omap_vout_device *vout,
			struct vb2_buffer *vb);
void omap_vout_calculate_vrfb_offset(struct omap_vout_device *vout);
#else
static inline void omap_vout_free_vrfb_buffers(struct omap_vout_device *vout) { };
static inline int omap_vout_setup_vrfb_bufs(struct platform_device *pdev, int vid_num,
			bool static_vrfb_allocation)
		{ return 0; };
static inline void omap_vout_release_vrfb(struct omap_vout_device *vout) { };
static inline int omap_vout_vrfb_buffer_setup(struct omap_vout_device *vout,
			unsigned int *count, unsigned int startindex)
		{ return 0; };
static inline int omap_vout_prepare_vrfb(struct omap_vout_device *vout,
			struct vb2_buffer *vb)
		{ return 0; };
static inline void omap_vout_calculate_vrfb_offset(struct omap_vout_device *vout) { };
#endif

#endif
