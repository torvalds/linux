/*
 * Copyright (C) 2017 Linus Walleij <linus.walleij@linaro.org>
 * Parts of this file were based on sources as follows:
 *
 * Copyright (C) 2006-2008 Intel Corporation
 * Copyright (C) 2007 Amos Lee <amos_lee@storlinksemi.com>
 * Copyright (C) 2007 Dave Airlie <airlied@linux.ie>
 * Copyright (C) 2011 Texas Instruments
 * Copyright (C) 2017 Eric Anholt
 *
 * This program is free software and is provided to you under the terms of the
 * GNU General Public License version 2 as published by the Free Software
 * Foundation, and any use by you of this program is subject to the terms of
 * such GNU licence.
 */

#ifndef _TVE200_DRM_H_
#define _TVE200_DRM_H_

/* Bits 2-31 are valid physical base addresses */
#define TVE200_Y_FRAME_BASE_ADDR	0x00
#define TVE200_U_FRAME_BASE_ADDR	0x04
#define TVE200_V_FRAME_BASE_ADDR	0x08

#define TVE200_INT_EN			0x0C
#define TVE200_INT_CLR			0x10
#define TVE200_INT_STAT			0x14
#define TVE200_INT_BUS_ERR		BIT(7)
#define TVE200_INT_V_STATUS		BIT(6) /* vertical blank */
#define TVE200_INT_V_NEXT_FRAME		BIT(5)
#define TVE200_INT_U_NEXT_FRAME		BIT(4)
#define TVE200_INT_Y_NEXT_FRAME		BIT(3)
#define TVE200_INT_V_FIFO_UNDERRUN	BIT(2)
#define TVE200_INT_U_FIFO_UNDERRUN	BIT(1)
#define TVE200_INT_Y_FIFO_UNDERRUN	BIT(0)
#define TVE200_FIFO_UNDERRUNS		(TVE200_INT_V_FIFO_UNDERRUN | \
					 TVE200_INT_U_FIFO_UNDERRUN | \
					 TVE200_INT_Y_FIFO_UNDERRUN)

#define TVE200_CTRL			0x18
#define TVE200_CTRL_YUV420		BIT(31)
#define TVE200_CTRL_CSMODE		BIT(30)
#define TVE200_CTRL_NONINTERLACE	BIT(28) /* 0 = non-interlace CCIR656 */
#define TVE200_CTRL_TVCLKP		BIT(27) /* Inverted clock phase */
/* Bits 24..26 define the burst size after arbitration on the bus */
#define TVE200_CTRL_BURST_4_WORDS	(0 << 24)
#define TVE200_CTRL_BURST_8_WORDS	(1 << 24)
#define TVE200_CTRL_BURST_16_WORDS	(2 << 24)
#define TVE200_CTRL_BURST_32_WORDS	(3 << 24)
#define TVE200_CTRL_BURST_64_WORDS	(4 << 24)
#define TVE200_CTRL_BURST_128_WORDS	(5 << 24)
#define TVE200_CTRL_BURST_256_WORDS	(6 << 24)
#define TVE200_CTRL_BURST_0_WORDS	(7 << 24) /* ? */
/*
 * Bits 16..23 is the retry count*16 before issueing a new AHB transfer
 * on the AHB bus.
 */
#define TVE200_CTRL_RETRYCNT_MASK	GENMASK(23, 16)
#define TVE200_CTRL_RETRYCNT_16		(1 << 16)
#define TVE200_CTRL_BBBP		BIT(15) /* 0 = little-endian */
/* Bits 12..14 define the YCbCr ordering */
#define TVE200_CTRL_YCBCRODR_CB0Y0CR0Y1	(0 << 12)
#define TVE200_CTRL_YCBCRODR_Y0CB0Y1CR0	(1 << 12)
#define TVE200_CTRL_YCBCRODR_CR0Y0CB0Y1	(2 << 12)
#define TVE200_CTRL_YCBCRODR_Y1CB0Y0CR0	(3 << 12)
#define TVE200_CTRL_YCBCRODR_CR0Y1CB0Y0	(4 << 12)
#define TVE200_CTRL_YCBCRODR_Y1CR0Y0CB0	(5 << 12)
#define TVE200_CTRL_YCBCRODR_CB0Y1CR0Y0	(6 << 12)
#define TVE200_CTRL_YCBCRODR_Y0CR0Y1CB0	(7 << 12)
/* Bits 10..11 define the input resolution (framebuffer size) */
#define TVE200_CTRL_IPRESOL_CIF		(0 << 10)
#define TVE200_CTRL_IPRESOL_VGA		(1 << 10)
#define TVE200_CTRL_IPRESOL_D1		(2 << 10)
#define TVE200_CTRL_NTSC		BIT(9) /* 0 = PAL, 1 = NTSC */
#define TVE200_CTRL_INTERLACE		BIT(8) /* 1 = interlace, only for D1 */
#define TVE200_IPDMOD_RGB555		(0 << 6) /* TVE200_CTRL_YUV420 = 0 */
#define TVE200_IPDMOD_RGB565		(1 << 6)
#define TVE200_IPDMOD_RGB888		(2 << 6)
#define TVE200_IPDMOD_YUV420		(2 << 6) /* TVE200_CTRL_YUV420 = 1 */
#define TVE200_IPDMOD_YUV422		(3 << 6)
/* Bits 4 & 5 define when to fire the vblank IRQ */
#define TVE200_VSTSTYPE_VSYNC		(0 << 4) /* start of vsync */
#define TVE200_VSTSTYPE_VBP		(1 << 4) /* start of v back porch */
#define TVE200_VSTSTYPE_VAI		(2 << 4) /* start of v active image */
#define TVE200_VSTSTYPE_VFP		(3 << 4) /* start of v front porch */
#define TVE200_VSTSTYPE_BITS		(BIT(4) | BIT(5))
#define TVE200_BGR			BIT(1) /* 0 = RGB, 1 = BGR */
#define TVE200_TVEEN			BIT(0) /* Enable TVE block */

#define TVE200_CTRL_2			0x1c
#define TVE200_CTRL_3			0x20

#define TVE200_CTRL_4			0x24
#define TVE200_CTRL_4_RESET		BIT(0) /* triggers reset of TVE200 */

#include <drm/drm_gem.h>
#include <drm/drm_simple_kms_helper.h>

struct tve200_drm_dev_private {
	struct drm_device *drm;

	struct drm_connector *connector;
	struct drm_panel *panel;
	struct drm_bridge *bridge;
	struct drm_simple_display_pipe pipe;
	struct drm_fbdev_cma *fbdev;

	void *regs;
	struct clk *pclk;
	struct clk *clk;
};

#define to_tve200_connector(x) \
	container_of(x, struct tve200_drm_connector, connector)

int tve200_display_init(struct drm_device *dev);
int tve200_enable_vblank(struct drm_device *drm, unsigned int crtc);
void tve200_disable_vblank(struct drm_device *drm, unsigned int crtc);
irqreturn_t tve200_irq(int irq, void *data);
int tve200_connector_init(struct drm_device *dev);
int tve200_encoder_init(struct drm_device *dev);
int tve200_dumb_create(struct drm_file *file_priv,
		      struct drm_device *dev,
		      struct drm_mode_create_dumb *args);

#endif /* _TVE200_DRM_H_ */
