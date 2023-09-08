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

/*
 * CLCD Controller Internal Register addresses
 */
#define CLCD_TIM0		0x00000000
#define CLCD_TIM1		0x00000004
#define CLCD_TIM2		0x00000008
#define CLCD_TIM3		0x0000000c
#define CLCD_UBAS		0x00000010
#define CLCD_LBAS		0x00000014

#define CLCD_PL110_IENB		0x00000018
#define CLCD_PL110_CNTL		0x0000001c
#define CLCD_PL110_STAT		0x00000020
#define CLCD_PL110_INTR		0x00000024
#define CLCD_PL110_UCUR		0x00000028
#define CLCD_PL110_LCUR		0x0000002C

#define CLCD_PL111_CNTL		0x00000018
#define CLCD_PL111_IENB		0x0000001c
#define CLCD_PL111_RIS		0x00000020
#define CLCD_PL111_MIS		0x00000024
#define CLCD_PL111_ICR		0x00000028
#define CLCD_PL111_UCUR		0x0000002c
#define CLCD_PL111_LCUR		0x00000030

#define CLCD_PALL		0x00000200
#define CLCD_PALETTE		0x00000200

#define TIM2_PCD_LO_MASK	GENMASK(4, 0)
#define TIM2_PCD_LO_BITS	5
#define TIM2_CLKSEL		(1 << 5)
#define TIM2_ACB_MASK		GENMASK(10, 6)
#define TIM2_IVS		(1 << 11)
#define TIM2_IHS		(1 << 12)
#define TIM2_IPC		(1 << 13)
#define TIM2_IOE		(1 << 14)
#define TIM2_BCD		(1 << 26)
#define TIM2_PCD_HI_MASK	GENMASK(31, 27)
#define TIM2_PCD_HI_BITS	5
#define TIM2_PCD_HI_SHIFT	27

#define CNTL_LCDEN		(1 << 0)
#define CNTL_LCDBPP1		(0 << 1)
#define CNTL_LCDBPP2		(1 << 1)
#define CNTL_LCDBPP4		(2 << 1)
#define CNTL_LCDBPP8		(3 << 1)
#define CNTL_LCDBPP16		(4 << 1)
#define CNTL_LCDBPP16_565	(6 << 1)
#define CNTL_LCDBPP16_444	(7 << 1)
#define CNTL_LCDBPP24		(5 << 1)
#define CNTL_LCDBW		(1 << 4)
#define CNTL_LCDTFT		(1 << 5)
#define CNTL_LCDMONO8		(1 << 6)
#define CNTL_LCDDUAL		(1 << 7)
#define CNTL_BGR		(1 << 8)
#define CNTL_BEBO		(1 << 9)
#define CNTL_BEPO		(1 << 10)
#define CNTL_LCDPWR		(1 << 11)
#define CNTL_LCDVCOMP(x)	((x) << 12)
#define CNTL_LDMAFIFOTIME	(1 << 15)
#define CNTL_WATERMARK		(1 << 16)

/* ST Microelectronics variant bits */
#define CNTL_ST_1XBPP_444	0x0
#define CNTL_ST_1XBPP_5551	(1 << 17)
#define CNTL_ST_1XBPP_565	(1 << 18)
#define CNTL_ST_CDWID_12	0x0
#define CNTL_ST_CDWID_16	(1 << 19)
#define CNTL_ST_CDWID_18	(1 << 20)
#define CNTL_ST_CDWID_24	((1 << 19) | (1 << 20))
#define CNTL_ST_CEAEN		(1 << 21)
#define CNTL_ST_LCDBPP24_PACKED	(6 << 1)

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
 * @fb_depth: desired depth per pixel on the default framebuffer
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
	unsigned int fb_depth;
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
void pl111_debugfs_init(struct drm_minor *minor);

#endif /* _PL111_DRM_H_ */
