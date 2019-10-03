/* SPDX-License-Identifier: GPL-2.0-only */
/*
 *
 * (C) COPYRIGHT 2012-2013 ARM Limited. All rights reserved.
 *
 * Parts of this file were based on sources as follows:
 *
 * Copyright (c) 2006-2008 Intel Corporation
 * Copyright (c) 2007 Dave Airlie <airlied@linux.ie>
 * Copyright (C) 2011 Texas Instruments
 */

#ifndef _PL111_DRM_H_
#define _PL111_DRM_H_

#include <linux/clk-provider.h>
#include <linux/interrupt.h>

#include <drm/drm_bridge.h>
#include <drm/drm_connector.h>
#include <drm/drm_encoder.h>
#include <drm/drm_gem.h>
#include <drm/drm_panel.h>
#include <drm/drm_simple_kms_helper.h>

#define CLCD_IRQ_NEXTBASE_UPDATE BIT(2)

struct drm_minor;

/**
 * struct pl111_variant_data - encodes IP differences
 * @name: the name of this variant
 * @is_pl110: this is the early PL110 variant
 * @is_lcdc: this is the ST Microelectronics Nomadik LCDC variant
 * @external_bgr: this is the Versatile Pl110 variant with external
 *	BGR/RGB routing
 * @broken_clockdivider: the clock divider is broken and we need to
 *	use the supplied clock directly
 * @broken_vblank: the vblank IRQ is broken on this variant
 * @st_bitmux_control: this variant is using the ST Micro bitmux
 *	extensions to the control register
 * @formats: array of supported pixel formats on this variant
 * @nformats: the length of the array of supported pixel formats
 * @fb_bpp: desired bits per pixel on the default framebuffer
 */
struct pl111_variant_data {
	const char *name;
	bool is_pl110;
	bool is_lcdc;
	bool external_bgr;
	bool broken_clockdivider;
	bool broken_vblank;
	bool st_bitmux_control;
	const u32 *formats;
	unsigned int nformats;
	unsigned int fb_bpp;
};

struct pl111_drm_dev_private {
	struct drm_device *drm;

	struct drm_connector *connector;
	struct drm_panel *panel;
	struct drm_bridge *bridge;
	struct drm_simple_display_pipe pipe;

	void *regs;
	u32 memory_bw;
	u32 ienb;
	u32 ctrl;
	/* The pixel clock (a reference to our clock divider off of CLCDCLK). */
	struct clk *clk;
	/* pl111's internal clock divider. */
	struct clk_hw clk_div;
	/* Lock to sync access to CLCD_TIM2 between the common clock
	 * subsystem and pl111_display_enable().
	 */
	spinlock_t tim2_lock;
	const struct pl111_variant_data *variant;
	void (*variant_display_enable) (struct drm_device *drm, u32 format);
	void (*variant_display_disable) (struct drm_device *drm);
	bool use_device_memory;
};

int pl111_display_init(struct drm_device *dev);
irqreturn_t pl111_irq(int irq, void *data);
int pl111_debugfs_init(struct drm_minor *minor);

#endif /* _PL111_DRM_H_ */
