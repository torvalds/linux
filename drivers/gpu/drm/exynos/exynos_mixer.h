/*
 *
 * Copyright (c) 2011 Samsung Electronics Co., Ltd.
 * Authors:
 *	Seung-Woo Kim <sw0312.kim@samsung.com>
 *	Inki Dae <inki.dae@samsung.com>
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * VA LINUX SYSTEMS AND/OR ITS SUPPLIERS BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 */

#ifndef _EXYNOS_MIXER_H_
#define _EXYNOS_MIXER_H_

#define HDMI_OVERLAY_NUMBER	3

struct hdmi_win_data {
	dma_addr_t		dma_addr;
	void __iomem		*vaddr;
	dma_addr_t		chroma_dma_addr;
	void __iomem		*chroma_vaddr;
	uint32_t		pixel_format;
	unsigned int		bpp;
	unsigned int		crtc_x;
	unsigned int		crtc_y;
	unsigned int		crtc_width;
	unsigned int		crtc_height;
	unsigned int		fb_x;
	unsigned int		fb_y;
	unsigned int		fb_width;
	unsigned int		fb_height;
	unsigned int		mode_width;
	unsigned int		mode_height;
	unsigned int		scan_flags;
};

struct mixer_resources {
	struct device *dev;
	/** interrupt index */
	int irq;
	/** pointer to Mixer registers */
	void __iomem *mixer_regs;
	/** pointer to Video Processor registers */
	void __iomem *vp_regs;
	/** spinlock for protection of registers */
	spinlock_t reg_slock;
	/** other resources */
	struct clk *mixer;
	struct clk *vp;
	struct clk *sclk_mixer;
	struct clk *sclk_hdmi;
	struct clk *sclk_dac;
};

struct mixer_context {
	unsigned int			default_win;
	struct fb_videomode		*default_timing;
	unsigned int			default_bpp;

	/** mixer interrupt */
	unsigned int irq;
	/** current crtc pipe for vblank */
	int pipe;
	/** interlace scan mode */
	bool interlace;
	/** vp enabled status */
	bool vp_enabled;

	/** mixer and vp resources */
	struct mixer_resources mixer_res;

	/** overlay window data */
	struct hdmi_win_data		win_data[HDMI_OVERLAY_NUMBER];
};

#endif
