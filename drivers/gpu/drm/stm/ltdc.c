// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) STMicroelectronics SA 2017
 *
 * Authors: Philippe Cornu <philippe.cornu@st.com>
 *          Yannick Fertre <yannick.fertre@st.com>
 *          Fabien Dessenne <fabien.dessenne@st.com>
 *          Mickael Reulier <mickael.reulier@st.com>
 */

#include <linux/clk.h>
#include <linux/component.h>
#include <linux/delay.h>
#include <linux/interrupt.h>
#include <linux/module.h>
#include <linux/of_address.h>
#include <linux/of_graph.h>
#include <linux/pinctrl/consumer.h>
#include <linux/platform_device.h>
#include <linux/pm_runtime.h>
#include <linux/reset.h>

#include <drm/drm_atomic.h>
#include <drm/drm_atomic_helper.h>
#include <drm/drm_bridge.h>
#include <drm/drm_device.h>
#include <drm/drm_fb_cma_helper.h>
#include <drm/drm_fourcc.h>
#include <drm/drm_gem_atomic_helper.h>
#include <drm/drm_gem_cma_helper.h>
#include <drm/drm_of.h>
#include <drm/drm_plane_helper.h>
#include <drm/drm_probe_helper.h>
#include <drm/drm_simple_kms_helper.h>
#include <drm/drm_vblank.h>

#include <video/videomode.h>

#include "ltdc.h"

#define NB_CRTC 1
#define CRTC_MASK GENMASK(NB_CRTC - 1, 0)

#define MAX_IRQ 4

#define HWVER_10200 0x010200
#define HWVER_10300 0x010300
#define HWVER_20101 0x020101

/*
 * The address of some registers depends on the HW version: such registers have
 * an extra offset specified with reg_ofs.
 */
#define REG_OFS_NONE	0
#define REG_OFS_4	4		/* Insertion of "Layer Conf. 2" reg */
#define REG_OFS		(ldev->caps.reg_ofs)
#define LAY_OFS		0x80		/* Register Offset between 2 layers */

/* Global register offsets */
#define LTDC_IDR	0x0000		/* IDentification */
#define LTDC_LCR	0x0004		/* Layer Count */
#define LTDC_SSCR	0x0008		/* Synchronization Size Configuration */
#define LTDC_BPCR	0x000C		/* Back Porch Configuration */
#define LTDC_AWCR	0x0010		/* Active Width Configuration */
#define LTDC_TWCR	0x0014		/* Total Width Configuration */
#define LTDC_GCR	0x0018		/* Global Control */
#define LTDC_GC1R	0x001C		/* Global Configuration 1 */
#define LTDC_GC2R	0x0020		/* Global Configuration 2 */
#define LTDC_SRCR	0x0024		/* Shadow Reload Configuration */
#define LTDC_GACR	0x0028		/* GAmma Correction */
#define LTDC_BCCR	0x002C		/* Background Color Configuration */
#define LTDC_IER	0x0034		/* Interrupt Enable */
#define LTDC_ISR	0x0038		/* Interrupt Status */
#define LTDC_ICR	0x003C		/* Interrupt Clear */
#define LTDC_LIPCR	0x0040		/* Line Interrupt Position Conf. */
#define LTDC_CPSR	0x0044		/* Current Position Status */
#define LTDC_CDSR	0x0048		/* Current Display Status */

/* Layer register offsets */
#define LTDC_L1LC1R	(0x80)		/* L1 Layer Configuration 1 */
#define LTDC_L1LC2R	(0x84)		/* L1 Layer Configuration 2 */
#define LTDC_L1CR	(0x84 + REG_OFS)/* L1 Control */
#define LTDC_L1WHPCR	(0x88 + REG_OFS)/* L1 Window Hor Position Config */
#define LTDC_L1WVPCR	(0x8C + REG_OFS)/* L1 Window Vert Position Config */
#define LTDC_L1CKCR	(0x90 + REG_OFS)/* L1 Color Keying Configuration */
#define LTDC_L1PFCR	(0x94 + REG_OFS)/* L1 Pixel Format Configuration */
#define LTDC_L1CACR	(0x98 + REG_OFS)/* L1 Constant Alpha Config */
#define LTDC_L1DCCR	(0x9C + REG_OFS)/* L1 Default Color Configuration */
#define LTDC_L1BFCR	(0xA0 + REG_OFS)/* L1 Blend Factors Configuration */
#define LTDC_L1FBBCR	(0xA4 + REG_OFS)/* L1 FrameBuffer Bus Control */
#define LTDC_L1AFBCR	(0xA8 + REG_OFS)/* L1 AuxFB Control */
#define LTDC_L1CFBAR	(0xAC + REG_OFS)/* L1 Color FrameBuffer Address */
#define LTDC_L1CFBLR	(0xB0 + REG_OFS)/* L1 Color FrameBuffer Length */
#define LTDC_L1CFBLNR	(0xB4 + REG_OFS)/* L1 Color FrameBuffer Line Nb */
#define LTDC_L1AFBAR	(0xB8 + REG_OFS)/* L1 AuxFB Address */
#define LTDC_L1AFBLR	(0xBC + REG_OFS)/* L1 AuxFB Length */
#define LTDC_L1AFBLNR	(0xC0 + REG_OFS)/* L1 AuxFB Line Number */
#define LTDC_L1CLUTWR	(0xC4 + REG_OFS)/* L1 CLUT Write */
#define LTDC_L1YS1R	(0xE0 + REG_OFS)/* L1 YCbCr Scale 1 */
#define LTDC_L1YS2R	(0xE4 + REG_OFS)/* L1 YCbCr Scale 2 */

/* Bit definitions */
#define SSCR_VSH	GENMASK(10, 0)	/* Vertical Synchronization Height */
#define SSCR_HSW	GENMASK(27, 16)	/* Horizontal Synchronization Width */

#define BPCR_AVBP	GENMASK(10, 0)	/* Accumulated Vertical Back Porch */
#define BPCR_AHBP	GENMASK(27, 16)	/* Accumulated Horizontal Back Porch */

#define AWCR_AAH	GENMASK(10, 0)	/* Accumulated Active Height */
#define AWCR_AAW	GENMASK(27, 16)	/* Accumulated Active Width */

#define TWCR_TOTALH	GENMASK(10, 0)	/* TOTAL Height */
#define TWCR_TOTALW	GENMASK(27, 16)	/* TOTAL Width */

#define GCR_LTDCEN	BIT(0)		/* LTDC ENable */
#define GCR_DEN		BIT(16)		/* Dither ENable */
#define GCR_PCPOL	BIT(28)		/* Pixel Clock POLarity-Inverted */
#define GCR_DEPOL	BIT(29)		/* Data Enable POLarity-High */
#define GCR_VSPOL	BIT(30)		/* Vertical Synchro POLarity-High */
#define GCR_HSPOL	BIT(31)		/* Horizontal Synchro POLarity-High */

#define GC1R_WBCH	GENMASK(3, 0)	/* Width of Blue CHannel output */
#define GC1R_WGCH	GENMASK(7, 4)	/* Width of Green Channel output */
#define GC1R_WRCH	GENMASK(11, 8)	/* Width of Red Channel output */
#define GC1R_PBEN	BIT(12)		/* Precise Blending ENable */
#define GC1R_DT		GENMASK(15, 14)	/* Dithering Technique */
#define GC1R_GCT	GENMASK(19, 17)	/* Gamma Correction Technique */
#define GC1R_SHREN	BIT(21)		/* SHadow Registers ENabled */
#define GC1R_BCP	BIT(22)		/* Background Colour Programmable */
#define GC1R_BBEN	BIT(23)		/* Background Blending ENabled */
#define GC1R_LNIP	BIT(24)		/* Line Number IRQ Position */
#define GC1R_TP		BIT(25)		/* Timing Programmable */
#define GC1R_IPP	BIT(26)		/* IRQ Polarity Programmable */
#define GC1R_SPP	BIT(27)		/* Sync Polarity Programmable */
#define GC1R_DWP	BIT(28)		/* Dither Width Programmable */
#define GC1R_STREN	BIT(29)		/* STatus Registers ENabled */
#define GC1R_BMEN	BIT(31)		/* Blind Mode ENabled */

#define GC2R_EDCA	BIT(0)		/* External Display Control Ability  */
#define GC2R_STSAEN	BIT(1)		/* Slave Timing Sync Ability ENabled */
#define GC2R_DVAEN	BIT(2)		/* Dual-View Ability ENabled */
#define GC2R_DPAEN	BIT(3)		/* Dual-Port Ability ENabled */
#define GC2R_BW		GENMASK(6, 4)	/* Bus Width (log2 of nb of bytes) */
#define GC2R_EDCEN	BIT(7)		/* External Display Control ENabled */

#define SRCR_IMR	BIT(0)		/* IMmediate Reload */
#define SRCR_VBR	BIT(1)		/* Vertical Blanking Reload */

#define BCCR_BCBLACK	0x00		/* Background Color BLACK */
#define BCCR_BCBLUE	GENMASK(7, 0)	/* Background Color BLUE */
#define BCCR_BCGREEN	GENMASK(15, 8)	/* Background Color GREEN */
#define BCCR_BCRED	GENMASK(23, 16)	/* Background Color RED */
#define BCCR_BCWHITE	GENMASK(23, 0)	/* Background Color WHITE */

#define IER_LIE		BIT(0)		/* Line Interrupt Enable */
#define IER_FUIE	BIT(1)		/* Fifo Underrun Interrupt Enable */
#define IER_TERRIE	BIT(2)		/* Transfer ERRor Interrupt Enable */
#define IER_RRIE	BIT(3)		/* Register Reload Interrupt enable */

#define CPSR_CYPOS	GENMASK(15, 0)	/* Current Y position */

#define ISR_LIF		BIT(0)		/* Line Interrupt Flag */
#define ISR_FUIF	BIT(1)		/* Fifo Underrun Interrupt Flag */
#define ISR_TERRIF	BIT(2)		/* Transfer ERRor Interrupt Flag */
#define ISR_RRIF	BIT(3)		/* Register Reload Interrupt Flag */

#define LXCR_LEN	BIT(0)		/* Layer ENable */
#define LXCR_COLKEN	BIT(1)		/* Color Keying Enable */
#define LXCR_CLUTEN	BIT(4)		/* Color Look-Up Table ENable */

#define LXWHPCR_WHSTPOS	GENMASK(11, 0)	/* Window Horizontal StarT POSition */
#define LXWHPCR_WHSPPOS	GENMASK(27, 16)	/* Window Horizontal StoP POSition */

#define LXWVPCR_WVSTPOS	GENMASK(10, 0)	/* Window Vertical StarT POSition */
#define LXWVPCR_WVSPPOS	GENMASK(26, 16)	/* Window Vertical StoP POSition */

#define LXPFCR_PF	GENMASK(2, 0)	/* Pixel Format */

#define LXCACR_CONSTA	GENMASK(7, 0)	/* CONSTant Alpha */

#define LXBFCR_BF2	GENMASK(2, 0)	/* Blending Factor 2 */
#define LXBFCR_BF1	GENMASK(10, 8)	/* Blending Factor 1 */

#define LXCFBLR_CFBLL	GENMASK(12, 0)	/* Color Frame Buffer Line Length */
#define LXCFBLR_CFBP	GENMASK(28, 16)	/* Color Frame Buffer Pitch in bytes */

#define LXCFBLNR_CFBLN	GENMASK(10, 0)	/* Color Frame Buffer Line Number */

#define CLUT_SIZE	256

#define CONSTA_MAX	0xFF		/* CONSTant Alpha MAX= 1.0 */
#define BF1_PAXCA	0x600		/* Pixel Alpha x Constant Alpha */
#define BF1_CA		0x400		/* Constant Alpha */
#define BF2_1PAXCA	0x007		/* 1 - (Pixel Alpha x Constant Alpha) */
#define BF2_1CA		0x005		/* 1 - Constant Alpha */

#define NB_PF		8		/* Max nb of HW pixel format */

enum ltdc_pix_fmt {
	PF_NONE,
	/* RGB formats */
	PF_ARGB8888,		/* ARGB [32 bits] */
	PF_RGBA8888,		/* RGBA [32 bits] */
	PF_RGB888,		/* RGB [24 bits] */
	PF_RGB565,		/* RGB [16 bits] */
	PF_ARGB1555,		/* ARGB A:1 bit RGB:15 bits [16 bits] */
	PF_ARGB4444,		/* ARGB A:4 bits R/G/B: 4 bits each [16 bits] */
	/* Indexed formats */
	PF_L8,			/* Indexed 8 bits [8 bits] */
	PF_AL44,		/* Alpha:4 bits + indexed 4 bits [8 bits] */
	PF_AL88			/* Alpha:8 bits + indexed 8 bits [16 bits] */
};

/* The index gives the encoding of the pixel format for an HW version */
static const enum ltdc_pix_fmt ltdc_pix_fmt_a0[NB_PF] = {
	PF_ARGB8888,		/* 0x00 */
	PF_RGB888,		/* 0x01 */
	PF_RGB565,		/* 0x02 */
	PF_ARGB1555,		/* 0x03 */
	PF_ARGB4444,		/* 0x04 */
	PF_L8,			/* 0x05 */
	PF_AL44,		/* 0x06 */
	PF_AL88			/* 0x07 */
};

static const enum ltdc_pix_fmt ltdc_pix_fmt_a1[NB_PF] = {
	PF_ARGB8888,		/* 0x00 */
	PF_RGB888,		/* 0x01 */
	PF_RGB565,		/* 0x02 */
	PF_RGBA8888,		/* 0x03 */
	PF_AL44,		/* 0x04 */
	PF_L8,			/* 0x05 */
	PF_ARGB1555,		/* 0x06 */
	PF_ARGB4444		/* 0x07 */
};

static const u64 ltdc_format_modifiers[] = {
	DRM_FORMAT_MOD_LINEAR,
	DRM_FORMAT_MOD_INVALID
};

static inline u32 reg_read(void __iomem *base, u32 reg)
{
	return readl_relaxed(base + reg);
}

static inline void reg_write(void __iomem *base, u32 reg, u32 val)
{
	writel_relaxed(val, base + reg);
}

static inline void reg_set(void __iomem *base, u32 reg, u32 mask)
{
	reg_write(base, reg, reg_read(base, reg) | mask);
}

static inline void reg_clear(void __iomem *base, u32 reg, u32 mask)
{
	reg_write(base, reg, reg_read(base, reg) & ~mask);
}

static inline void reg_update_bits(void __iomem *base, u32 reg, u32 mask,
				   u32 val)
{
	reg_write(base, reg, (reg_read(base, reg) & ~mask) | val);
}

static inline struct ltdc_device *crtc_to_ltdc(struct drm_crtc *crtc)
{
	return (struct ltdc_device *)crtc->dev->dev_private;
}

static inline struct ltdc_device *plane_to_ltdc(struct drm_plane *plane)
{
	return (struct ltdc_device *)plane->dev->dev_private;
}

static inline struct ltdc_device *encoder_to_ltdc(struct drm_encoder *enc)
{
	return (struct ltdc_device *)enc->dev->dev_private;
}

static inline enum ltdc_pix_fmt to_ltdc_pixelformat(u32 drm_fmt)
{
	enum ltdc_pix_fmt pf;

	switch (drm_fmt) {
	case DRM_FORMAT_ARGB8888:
	case DRM_FORMAT_XRGB8888:
		pf = PF_ARGB8888;
		break;
	case DRM_FORMAT_RGBA8888:
	case DRM_FORMAT_RGBX8888:
		pf = PF_RGBA8888;
		break;
	case DRM_FORMAT_RGB888:
		pf = PF_RGB888;
		break;
	case DRM_FORMAT_RGB565:
		pf = PF_RGB565;
		break;
	case DRM_FORMAT_ARGB1555:
	case DRM_FORMAT_XRGB1555:
		pf = PF_ARGB1555;
		break;
	case DRM_FORMAT_ARGB4444:
	case DRM_FORMAT_XRGB4444:
		pf = PF_ARGB4444;
		break;
	case DRM_FORMAT_C8:
		pf = PF_L8;
		break;
	default:
		pf = PF_NONE;
		break;
		/* Note: There are no DRM_FORMAT for AL44 and AL88 */
	}

	return pf;
}

static inline u32 to_drm_pixelformat(enum ltdc_pix_fmt pf)
{
	switch (pf) {
	case PF_ARGB8888:
		return DRM_FORMAT_ARGB8888;
	case PF_RGBA8888:
		return DRM_FORMAT_RGBA8888;
	case PF_RGB888:
		return DRM_FORMAT_RGB888;
	case PF_RGB565:
		return DRM_FORMAT_RGB565;
	case PF_ARGB1555:
		return DRM_FORMAT_ARGB1555;
	case PF_ARGB4444:
		return DRM_FORMAT_ARGB4444;
	case PF_L8:
		return DRM_FORMAT_C8;
	case PF_AL44:		/* No DRM support */
	case PF_AL88:		/* No DRM support */
	case PF_NONE:
	default:
		return 0;
	}
}

static inline u32 get_pixelformat_without_alpha(u32 drm)
{
	switch (drm) {
	case DRM_FORMAT_ARGB4444:
		return DRM_FORMAT_XRGB4444;
	case DRM_FORMAT_RGBA4444:
		return DRM_FORMAT_RGBX4444;
	case DRM_FORMAT_ARGB1555:
		return DRM_FORMAT_XRGB1555;
	case DRM_FORMAT_RGBA5551:
		return DRM_FORMAT_RGBX5551;
	case DRM_FORMAT_ARGB8888:
		return DRM_FORMAT_XRGB8888;
	case DRM_FORMAT_RGBA8888:
		return DRM_FORMAT_RGBX8888;
	default:
		return 0;
	}
}

static irqreturn_t ltdc_irq_thread(int irq, void *arg)
{
	struct drm_device *ddev = arg;
	struct ltdc_device *ldev = ddev->dev_private;
	struct drm_crtc *crtc = drm_crtc_from_index(ddev, 0);

	/* Line IRQ : trigger the vblank event */
	if (ldev->irq_status & ISR_LIF)
		drm_crtc_handle_vblank(crtc);

	/* Save FIFO Underrun & Transfer Error status */
	mutex_lock(&ldev->err_lock);
	if (ldev->irq_status & ISR_FUIF)
		ldev->error_status |= ISR_FUIF;
	if (ldev->irq_status & ISR_TERRIF)
		ldev->error_status |= ISR_TERRIF;
	mutex_unlock(&ldev->err_lock);

	return IRQ_HANDLED;
}

static irqreturn_t ltdc_irq(int irq, void *arg)
{
	struct drm_device *ddev = arg;
	struct ltdc_device *ldev = ddev->dev_private;

	/* Read & Clear the interrupt status */
	ldev->irq_status = reg_read(ldev->regs, LTDC_ISR);
	reg_write(ldev->regs, LTDC_ICR, ldev->irq_status);

	return IRQ_WAKE_THREAD;
}

/*
 * DRM_CRTC
 */

static void ltdc_crtc_update_clut(struct drm_crtc *crtc)
{
	struct ltdc_device *ldev = crtc_to_ltdc(crtc);
	struct drm_color_lut *lut;
	u32 val;
	int i;

	if (!crtc->state->color_mgmt_changed || !crtc->state->gamma_lut)
		return;

	lut = (struct drm_color_lut *)crtc->state->gamma_lut->data;

	for (i = 0; i < CLUT_SIZE; i++, lut++) {
		val = ((lut->red << 8) & 0xff0000) | (lut->green & 0xff00) |
			(lut->blue >> 8) | (i << 24);
		reg_write(ldev->regs, LTDC_L1CLUTWR, val);
	}
}

static void ltdc_crtc_atomic_enable(struct drm_crtc *crtc,
				    struct drm_atomic_state *state)
{
	struct ltdc_device *ldev = crtc_to_ltdc(crtc);
	struct drm_device *ddev = crtc->dev;

	DRM_DEBUG_DRIVER("\n");

	pm_runtime_get_sync(ddev->dev);

	/* Sets the background color value */
	reg_write(ldev->regs, LTDC_BCCR, BCCR_BCBLACK);

	/* Enable IRQ */
	reg_set(ldev->regs, LTDC_IER, IER_RRIE | IER_FUIE | IER_TERRIE);

	/* Commit shadow registers = update planes at next vblank */
	reg_set(ldev->regs, LTDC_SRCR, SRCR_VBR);

	drm_crtc_vblank_on(crtc);
}

static void ltdc_crtc_atomic_disable(struct drm_crtc *crtc,
				     struct drm_atomic_state *state)
{
	struct ltdc_device *ldev = crtc_to_ltdc(crtc);
	struct drm_device *ddev = crtc->dev;

	DRM_DEBUG_DRIVER("\n");

	drm_crtc_vblank_off(crtc);

	/* disable IRQ */
	reg_clear(ldev->regs, LTDC_IER, IER_RRIE | IER_FUIE | IER_TERRIE);

	/* immediately commit disable of layers before switching off LTDC */
	reg_set(ldev->regs, LTDC_SRCR, SRCR_IMR);

	pm_runtime_put_sync(ddev->dev);
}

#define CLK_TOLERANCE_HZ 50

static enum drm_mode_status
ltdc_crtc_mode_valid(struct drm_crtc *crtc,
		     const struct drm_display_mode *mode)
{
	struct ltdc_device *ldev = crtc_to_ltdc(crtc);
	int target = mode->clock * 1000;
	int target_min = target - CLK_TOLERANCE_HZ;
	int target_max = target + CLK_TOLERANCE_HZ;
	int result;

	result = clk_round_rate(ldev->pixel_clk, target);

	DRM_DEBUG_DRIVER("clk rate target %d, available %d\n", target, result);

	/* Filter modes according to the max frequency supported by the pads */
	if (result > ldev->caps.pad_max_freq_hz)
		return MODE_CLOCK_HIGH;

	/*
	 * Accept all "preferred" modes:
	 * - this is important for panels because panel clock tolerances are
	 *   bigger than hdmi ones and there is no reason to not accept them
	 *   (the fps may vary a little but it is not a problem).
	 * - the hdmi preferred mode will be accepted too, but userland will
	 *   be able to use others hdmi "valid" modes if necessary.
	 */
	if (mode->type & DRM_MODE_TYPE_PREFERRED)
		return MODE_OK;

	/*
	 * Filter modes according to the clock value, particularly useful for
	 * hdmi modes that require precise pixel clocks.
	 */
	if (result < target_min || result > target_max)
		return MODE_CLOCK_RANGE;

	return MODE_OK;
}

static bool ltdc_crtc_mode_fixup(struct drm_crtc *crtc,
				 const struct drm_display_mode *mode,
				 struct drm_display_mode *adjusted_mode)
{
	struct ltdc_device *ldev = crtc_to_ltdc(crtc);
	int rate = mode->clock * 1000;

	if (clk_set_rate(ldev->pixel_clk, rate) < 0) {
		DRM_ERROR("Cannot set rate (%dHz) for pixel clk\n", rate);
		return false;
	}

	adjusted_mode->clock = clk_get_rate(ldev->pixel_clk) / 1000;

	DRM_DEBUG_DRIVER("requested clock %dkHz, adjusted clock %dkHz\n",
			 mode->clock, adjusted_mode->clock);

	return true;
}

static void ltdc_crtc_mode_set_nofb(struct drm_crtc *crtc)
{
	struct ltdc_device *ldev = crtc_to_ltdc(crtc);
	struct drm_device *ddev = crtc->dev;
	struct drm_connector_list_iter iter;
	struct drm_connector *connector = NULL;
	struct drm_encoder *encoder = NULL, *en_iter;
	struct drm_bridge *bridge = NULL, *br_iter;
	struct drm_display_mode *mode = &crtc->state->adjusted_mode;
	u32 hsync, vsync, accum_hbp, accum_vbp, accum_act_w, accum_act_h;
	u32 total_width, total_height;
	u32 bus_flags = 0;
	u32 val;
	int ret;

	/* get encoder from crtc */
	drm_for_each_encoder(en_iter, ddev)
		if (en_iter->crtc == crtc) {
			encoder = en_iter;
			break;
		}

	if (encoder) {
		/* get bridge from encoder */
		list_for_each_entry(br_iter, &encoder->bridge_chain, chain_node)
			if (br_iter->encoder == encoder) {
				bridge = br_iter;
				break;
			}

		/* Get the connector from encoder */
		drm_connector_list_iter_begin(ddev, &iter);
		drm_for_each_connector_iter(connector, &iter)
			if (connector->encoder == encoder)
				break;
		drm_connector_list_iter_end(&iter);
	}

	if (bridge && bridge->timings)
		bus_flags = bridge->timings->input_bus_flags;
	else if (connector)
		bus_flags = connector->display_info.bus_flags;

	if (!pm_runtime_active(ddev->dev)) {
		ret = pm_runtime_get_sync(ddev->dev);
		if (ret) {
			DRM_ERROR("Failed to set mode, cannot get sync\n");
			return;
		}
	}

	DRM_DEBUG_DRIVER("CRTC:%d mode:%s\n", crtc->base.id, mode->name);
	DRM_DEBUG_DRIVER("Video mode: %dx%d", mode->hdisplay, mode->vdisplay);
	DRM_DEBUG_DRIVER(" hfp %d hbp %d hsl %d vfp %d vbp %d vsl %d\n",
			 mode->hsync_start - mode->hdisplay,
			 mode->htotal - mode->hsync_end,
			 mode->hsync_end - mode->hsync_start,
			 mode->vsync_start - mode->vdisplay,
			 mode->vtotal - mode->vsync_end,
			 mode->vsync_end - mode->vsync_start);

	/* Convert video timings to ltdc timings */
	hsync = mode->hsync_end - mode->hsync_start - 1;
	vsync = mode->vsync_end - mode->vsync_start - 1;
	accum_hbp = mode->htotal - mode->hsync_start - 1;
	accum_vbp = mode->vtotal - mode->vsync_start - 1;
	accum_act_w = accum_hbp + mode->hdisplay;
	accum_act_h = accum_vbp + mode->vdisplay;
	total_width = mode->htotal - 1;
	total_height = mode->vtotal - 1;

	/* Configures the HS, VS, DE and PC polarities. Default Active Low */
	val = 0;

	if (mode->flags & DRM_MODE_FLAG_PHSYNC)
		val |= GCR_HSPOL;

	if (mode->flags & DRM_MODE_FLAG_PVSYNC)
		val |= GCR_VSPOL;

	if (bus_flags & DRM_BUS_FLAG_DE_LOW)
		val |= GCR_DEPOL;

	if (bus_flags & DRM_BUS_FLAG_PIXDATA_DRIVE_NEGEDGE)
		val |= GCR_PCPOL;

	reg_update_bits(ldev->regs, LTDC_GCR,
			GCR_HSPOL | GCR_VSPOL | GCR_DEPOL | GCR_PCPOL, val);

	/* Set Synchronization size */
	val = (hsync << 16) | vsync;
	reg_update_bits(ldev->regs, LTDC_SSCR, SSCR_VSH | SSCR_HSW, val);

	/* Set Accumulated Back porch */
	val = (accum_hbp << 16) | accum_vbp;
	reg_update_bits(ldev->regs, LTDC_BPCR, BPCR_AVBP | BPCR_AHBP, val);

	/* Set Accumulated Active Width */
	val = (accum_act_w << 16) | accum_act_h;
	reg_update_bits(ldev->regs, LTDC_AWCR, AWCR_AAW | AWCR_AAH, val);

	/* Set total width & height */
	val = (total_width << 16) | total_height;
	reg_update_bits(ldev->regs, LTDC_TWCR, TWCR_TOTALH | TWCR_TOTALW, val);

	reg_write(ldev->regs, LTDC_LIPCR, (accum_act_h + 1));
}

static void ltdc_crtc_atomic_flush(struct drm_crtc *crtc,
				   struct drm_atomic_state *state)
{
	struct ltdc_device *ldev = crtc_to_ltdc(crtc);
	struct drm_device *ddev = crtc->dev;
	struct drm_pending_vblank_event *event = crtc->state->event;

	DRM_DEBUG_ATOMIC("\n");

	ltdc_crtc_update_clut(crtc);

	/* Commit shadow registers = update planes at next vblank */
	reg_set(ldev->regs, LTDC_SRCR, SRCR_VBR);

	if (event) {
		crtc->state->event = NULL;

		spin_lock_irq(&ddev->event_lock);
		if (drm_crtc_vblank_get(crtc) == 0)
			drm_crtc_arm_vblank_event(crtc, event);
		else
			drm_crtc_send_vblank_event(crtc, event);
		spin_unlock_irq(&ddev->event_lock);
	}
}

static bool ltdc_crtc_get_scanout_position(struct drm_crtc *crtc,
					   bool in_vblank_irq,
					   int *vpos, int *hpos,
					   ktime_t *stime, ktime_t *etime,
					   const struct drm_display_mode *mode)
{
	struct drm_device *ddev = crtc->dev;
	struct ltdc_device *ldev = ddev->dev_private;
	int line, vactive_start, vactive_end, vtotal;

	if (stime)
		*stime = ktime_get();

	/* The active area starts after vsync + front porch and ends
	 * at vsync + front porc + display size.
	 * The total height also include back porch.
	 * We have 3 possible cases to handle:
	 * - line < vactive_start: vpos = line - vactive_start and will be
	 * negative
	 * - vactive_start < line < vactive_end: vpos = line - vactive_start
	 * and will be positive
	 * - line > vactive_end: vpos = line - vtotal - vactive_start
	 * and will negative
	 *
	 * Computation for the two first cases are identical so we can
	 * simplify the code and only test if line > vactive_end
	 */
	if (pm_runtime_active(ddev->dev)) {
		line = reg_read(ldev->regs, LTDC_CPSR) & CPSR_CYPOS;
		vactive_start = reg_read(ldev->regs, LTDC_BPCR) & BPCR_AVBP;
		vactive_end = reg_read(ldev->regs, LTDC_AWCR) & AWCR_AAH;
		vtotal = reg_read(ldev->regs, LTDC_TWCR) & TWCR_TOTALH;

		if (line > vactive_end)
			*vpos = line - vtotal - vactive_start;
		else
			*vpos = line - vactive_start;
	} else {
		*vpos = 0;
	}

	*hpos = 0;

	if (etime)
		*etime = ktime_get();

	return true;
}

static const struct drm_crtc_helper_funcs ltdc_crtc_helper_funcs = {
	.mode_valid = ltdc_crtc_mode_valid,
	.mode_fixup = ltdc_crtc_mode_fixup,
	.mode_set_nofb = ltdc_crtc_mode_set_nofb,
	.atomic_flush = ltdc_crtc_atomic_flush,
	.atomic_enable = ltdc_crtc_atomic_enable,
	.atomic_disable = ltdc_crtc_atomic_disable,
	.get_scanout_position = ltdc_crtc_get_scanout_position,
};

static int ltdc_crtc_enable_vblank(struct drm_crtc *crtc)
{
	struct ltdc_device *ldev = crtc_to_ltdc(crtc);
	struct drm_crtc_state *state = crtc->state;

	DRM_DEBUG_DRIVER("\n");

	if (state->enable)
		reg_set(ldev->regs, LTDC_IER, IER_LIE);
	else
		return -EPERM;

	return 0;
}

static void ltdc_crtc_disable_vblank(struct drm_crtc *crtc)
{
	struct ltdc_device *ldev = crtc_to_ltdc(crtc);

	DRM_DEBUG_DRIVER("\n");
	reg_clear(ldev->regs, LTDC_IER, IER_LIE);
}

static const struct drm_crtc_funcs ltdc_crtc_funcs = {
	.destroy = drm_crtc_cleanup,
	.set_config = drm_atomic_helper_set_config,
	.page_flip = drm_atomic_helper_page_flip,
	.reset = drm_atomic_helper_crtc_reset,
	.atomic_duplicate_state = drm_atomic_helper_crtc_duplicate_state,
	.atomic_destroy_state = drm_atomic_helper_crtc_destroy_state,
	.enable_vblank = ltdc_crtc_enable_vblank,
	.disable_vblank = ltdc_crtc_disable_vblank,
	.get_vblank_timestamp = drm_crtc_vblank_helper_get_vblank_timestamp,
};

/*
 * DRM_PLANE
 */

static int ltdc_plane_atomic_check(struct drm_plane *plane,
				   struct drm_atomic_state *state)
{
	struct drm_plane_state *new_plane_state = drm_atomic_get_new_plane_state(state,
										 plane);
	struct drm_framebuffer *fb = new_plane_state->fb;
	u32 src_w, src_h;

	DRM_DEBUG_DRIVER("\n");

	if (!fb)
		return 0;

	/* convert src_ from 16:16 format */
	src_w = new_plane_state->src_w >> 16;
	src_h = new_plane_state->src_h >> 16;

	/* Reject scaling */
	if (src_w != new_plane_state->crtc_w || src_h != new_plane_state->crtc_h) {
		DRM_ERROR("Scaling is not supported");
		return -EINVAL;
	}

	return 0;
}

static void ltdc_plane_atomic_update(struct drm_plane *plane,
				     struct drm_atomic_state *state)
{
	struct ltdc_device *ldev = plane_to_ltdc(plane);
	struct drm_plane_state *newstate = drm_atomic_get_new_plane_state(state,
									  plane);
	struct drm_framebuffer *fb = newstate->fb;
	u32 lofs = plane->index * LAY_OFS;
	u32 x0 = newstate->crtc_x;
	u32 x1 = newstate->crtc_x + newstate->crtc_w - 1;
	u32 y0 = newstate->crtc_y;
	u32 y1 = newstate->crtc_y + newstate->crtc_h - 1;
	u32 src_x, src_y, src_w, src_h;
	u32 val, pitch_in_bytes, line_length, paddr, ahbp, avbp, bpcr;
	enum ltdc_pix_fmt pf;

	if (!newstate->crtc || !fb) {
		DRM_DEBUG_DRIVER("fb or crtc NULL");
		return;
	}

	/* convert src_ from 16:16 format */
	src_x = newstate->src_x >> 16;
	src_y = newstate->src_y >> 16;
	src_w = newstate->src_w >> 16;
	src_h = newstate->src_h >> 16;

	DRM_DEBUG_DRIVER("plane:%d fb:%d (%dx%d)@(%d,%d) -> (%dx%d)@(%d,%d)\n",
			 plane->base.id, fb->base.id,
			 src_w, src_h, src_x, src_y,
			 newstate->crtc_w, newstate->crtc_h,
			 newstate->crtc_x, newstate->crtc_y);

	bpcr = reg_read(ldev->regs, LTDC_BPCR);
	ahbp = (bpcr & BPCR_AHBP) >> 16;
	avbp = bpcr & BPCR_AVBP;

	/* Configures the horizontal start and stop position */
	val = ((x1 + 1 + ahbp) << 16) + (x0 + 1 + ahbp);
	reg_update_bits(ldev->regs, LTDC_L1WHPCR + lofs,
			LXWHPCR_WHSTPOS | LXWHPCR_WHSPPOS, val);

	/* Configures the vertical start and stop position */
	val = ((y1 + 1 + avbp) << 16) + (y0 + 1 + avbp);
	reg_update_bits(ldev->regs, LTDC_L1WVPCR + lofs,
			LXWVPCR_WVSTPOS | LXWVPCR_WVSPPOS, val);

	/* Specifies the pixel format */
	pf = to_ltdc_pixelformat(fb->format->format);
	for (val = 0; val < NB_PF; val++)
		if (ldev->caps.pix_fmt_hw[val] == pf)
			break;

	if (val == NB_PF) {
		DRM_ERROR("Pixel format %.4s not supported\n",
			  (char *)&fb->format->format);
		val = 0;	/* set by default ARGB 32 bits */
	}
	reg_update_bits(ldev->regs, LTDC_L1PFCR + lofs, LXPFCR_PF, val);

	/* Configures the color frame buffer pitch in bytes & line length */
	pitch_in_bytes = fb->pitches[0];
	line_length = fb->format->cpp[0] *
		      (x1 - x0 + 1) + (ldev->caps.bus_width >> 3) - 1;
	val = ((pitch_in_bytes << 16) | line_length);
	reg_update_bits(ldev->regs, LTDC_L1CFBLR + lofs,
			LXCFBLR_CFBLL | LXCFBLR_CFBP, val);

	/* Specifies the constant alpha value */
	val = CONSTA_MAX;
	reg_update_bits(ldev->regs, LTDC_L1CACR + lofs, LXCACR_CONSTA, val);

	/* Specifies the blending factors */
	val = BF1_PAXCA | BF2_1PAXCA;
	if (!fb->format->has_alpha)
		val = BF1_CA | BF2_1CA;

	/* Manage hw-specific capabilities */
	if (ldev->caps.non_alpha_only_l1 &&
	    plane->type != DRM_PLANE_TYPE_PRIMARY)
		val = BF1_PAXCA | BF2_1PAXCA;

	reg_update_bits(ldev->regs, LTDC_L1BFCR + lofs,
			LXBFCR_BF2 | LXBFCR_BF1, val);

	/* Configures the frame buffer line number */
	val = y1 - y0 + 1;
	reg_update_bits(ldev->regs, LTDC_L1CFBLNR + lofs, LXCFBLNR_CFBLN, val);

	/* Sets the FB address */
	paddr = (u32)drm_fb_cma_get_gem_addr(fb, newstate, 0);

	DRM_DEBUG_DRIVER("fb: phys 0x%08x", paddr);
	reg_write(ldev->regs, LTDC_L1CFBAR + lofs, paddr);

	/* Enable layer and CLUT if needed */
	val = fb->format->format == DRM_FORMAT_C8 ? LXCR_CLUTEN : 0;
	val |= LXCR_LEN;
	reg_update_bits(ldev->regs, LTDC_L1CR + lofs,
			LXCR_LEN | LXCR_CLUTEN, val);

	ldev->plane_fpsi[plane->index].counter++;

	mutex_lock(&ldev->err_lock);
	if (ldev->error_status & ISR_FUIF) {
		DRM_WARN("ltdc fifo underrun: please verify display mode\n");
		ldev->error_status &= ~ISR_FUIF;
	}
	if (ldev->error_status & ISR_TERRIF) {
		DRM_WARN("ltdc transfer error\n");
		ldev->error_status &= ~ISR_TERRIF;
	}
	mutex_unlock(&ldev->err_lock);
}

static void ltdc_plane_atomic_disable(struct drm_plane *plane,
				      struct drm_atomic_state *state)
{
	struct drm_plane_state *oldstate = drm_atomic_get_old_plane_state(state,
									  plane);
	struct ltdc_device *ldev = plane_to_ltdc(plane);
	u32 lofs = plane->index * LAY_OFS;

	/* disable layer */
	reg_clear(ldev->regs, LTDC_L1CR + lofs, LXCR_LEN);

	DRM_DEBUG_DRIVER("CRTC:%d plane:%d\n",
			 oldstate->crtc->base.id, plane->base.id);
}

static void ltdc_plane_atomic_print_state(struct drm_printer *p,
					  const struct drm_plane_state *state)
{
	struct drm_plane *plane = state->plane;
	struct ltdc_device *ldev = plane_to_ltdc(plane);
	struct fps_info *fpsi = &ldev->plane_fpsi[plane->index];
	int ms_since_last;
	ktime_t now;

	now = ktime_get();
	ms_since_last = ktime_to_ms(ktime_sub(now, fpsi->last_timestamp));

	drm_printf(p, "\tuser_updates=%dfps\n",
		   DIV_ROUND_CLOSEST(fpsi->counter * 1000, ms_since_last));

	fpsi->last_timestamp = now;
	fpsi->counter = 0;
}

static bool ltdc_plane_format_mod_supported(struct drm_plane *plane,
					    u32 format,
					    u64 modifier)
{
	if (modifier == DRM_FORMAT_MOD_LINEAR)
		return true;

	return false;
}

static const struct drm_plane_funcs ltdc_plane_funcs = {
	.update_plane = drm_atomic_helper_update_plane,
	.disable_plane = drm_atomic_helper_disable_plane,
	.destroy = drm_plane_cleanup,
	.reset = drm_atomic_helper_plane_reset,
	.atomic_duplicate_state = drm_atomic_helper_plane_duplicate_state,
	.atomic_destroy_state = drm_atomic_helper_plane_destroy_state,
	.atomic_print_state = ltdc_plane_atomic_print_state,
	.format_mod_supported = ltdc_plane_format_mod_supported,
};

static const struct drm_plane_helper_funcs ltdc_plane_helper_funcs = {
	.atomic_check = ltdc_plane_atomic_check,
	.atomic_update = ltdc_plane_atomic_update,
	.atomic_disable = ltdc_plane_atomic_disable,
};

static struct drm_plane *ltdc_plane_create(struct drm_device *ddev,
					   enum drm_plane_type type)
{
	unsigned long possible_crtcs = CRTC_MASK;
	struct ltdc_device *ldev = ddev->dev_private;
	struct device *dev = ddev->dev;
	struct drm_plane *plane;
	unsigned int i, nb_fmt = 0;
	u32 formats[NB_PF * 2];
	u32 drm_fmt, drm_fmt_no_alpha;
	const u64 *modifiers = ltdc_format_modifiers;
	int ret;

	/* Get supported pixel formats */
	for (i = 0; i < NB_PF; i++) {
		drm_fmt = to_drm_pixelformat(ldev->caps.pix_fmt_hw[i]);
		if (!drm_fmt)
			continue;
		formats[nb_fmt++] = drm_fmt;

		/* Add the no-alpha related format if any & supported */
		drm_fmt_no_alpha = get_pixelformat_without_alpha(drm_fmt);
		if (!drm_fmt_no_alpha)
			continue;

		/* Manage hw-specific capabilities */
		if (ldev->caps.non_alpha_only_l1 &&
		    type != DRM_PLANE_TYPE_PRIMARY)
			continue;

		formats[nb_fmt++] = drm_fmt_no_alpha;
	}

	plane = devm_kzalloc(dev, sizeof(*plane), GFP_KERNEL);
	if (!plane)
		return NULL;

	ret = drm_universal_plane_init(ddev, plane, possible_crtcs,
				       &ltdc_plane_funcs, formats, nb_fmt,
				       modifiers, type, NULL);
	if (ret < 0)
		return NULL;

	drm_plane_helper_add(plane, &ltdc_plane_helper_funcs);

	DRM_DEBUG_DRIVER("plane:%d created\n", plane->base.id);

	return plane;
}

static void ltdc_plane_destroy_all(struct drm_device *ddev)
{
	struct drm_plane *plane, *plane_temp;

	list_for_each_entry_safe(plane, plane_temp,
				 &ddev->mode_config.plane_list, head)
		drm_plane_cleanup(plane);
}

static int ltdc_crtc_init(struct drm_device *ddev, struct drm_crtc *crtc)
{
	struct ltdc_device *ldev = ddev->dev_private;
	struct drm_plane *primary, *overlay;
	unsigned int i;
	int ret;

	primary = ltdc_plane_create(ddev, DRM_PLANE_TYPE_PRIMARY);
	if (!primary) {
		DRM_ERROR("Can not create primary plane\n");
		return -EINVAL;
	}

	ret = drm_crtc_init_with_planes(ddev, crtc, primary, NULL,
					&ltdc_crtc_funcs, NULL);
	if (ret) {
		DRM_ERROR("Can not initialize CRTC\n");
		goto cleanup;
	}

	drm_crtc_helper_add(crtc, &ltdc_crtc_helper_funcs);

	drm_mode_crtc_set_gamma_size(crtc, CLUT_SIZE);
	drm_crtc_enable_color_mgmt(crtc, 0, false, CLUT_SIZE);

	DRM_DEBUG_DRIVER("CRTC:%d created\n", crtc->base.id);

	/* Add planes. Note : the first layer is used by primary plane */
	for (i = 1; i < ldev->caps.nb_layers; i++) {
		overlay = ltdc_plane_create(ddev, DRM_PLANE_TYPE_OVERLAY);
		if (!overlay) {
			ret = -ENOMEM;
			DRM_ERROR("Can not create overlay plane %d\n", i);
			goto cleanup;
		}
	}

	return 0;

cleanup:
	ltdc_plane_destroy_all(ddev);
	return ret;
}

static void ltdc_encoder_disable(struct drm_encoder *encoder)
{
	struct drm_device *ddev = encoder->dev;
	struct ltdc_device *ldev = ddev->dev_private;

	DRM_DEBUG_DRIVER("\n");

	/* Disable LTDC */
	reg_clear(ldev->regs, LTDC_GCR, GCR_LTDCEN);

	/* Set to sleep state the pinctrl whatever type of encoder */
	pinctrl_pm_select_sleep_state(ddev->dev);
}

static void ltdc_encoder_enable(struct drm_encoder *encoder)
{
	struct drm_device *ddev = encoder->dev;
	struct ltdc_device *ldev = ddev->dev_private;

	DRM_DEBUG_DRIVER("\n");

	/* Enable LTDC */
	reg_set(ldev->regs, LTDC_GCR, GCR_LTDCEN);
}

static void ltdc_encoder_mode_set(struct drm_encoder *encoder,
				  struct drm_display_mode *mode,
				  struct drm_display_mode *adjusted_mode)
{
	struct drm_device *ddev = encoder->dev;

	DRM_DEBUG_DRIVER("\n");

	/*
	 * Set to default state the pinctrl only with DPI type.
	 * Others types like DSI, don't need pinctrl due to
	 * internal bridge (the signals do not come out of the chipset).
	 */
	if (encoder->encoder_type == DRM_MODE_ENCODER_DPI)
		pinctrl_pm_select_default_state(ddev->dev);
}

static const struct drm_encoder_helper_funcs ltdc_encoder_helper_funcs = {
	.disable = ltdc_encoder_disable,
	.enable = ltdc_encoder_enable,
	.mode_set = ltdc_encoder_mode_set,
};

static int ltdc_encoder_init(struct drm_device *ddev, struct drm_bridge *bridge)
{
	struct drm_encoder *encoder;
	int ret;

	encoder = devm_kzalloc(ddev->dev, sizeof(*encoder), GFP_KERNEL);
	if (!encoder)
		return -ENOMEM;

	encoder->possible_crtcs = CRTC_MASK;
	encoder->possible_clones = 0;	/* No cloning support */

	drm_simple_encoder_init(ddev, encoder, DRM_MODE_ENCODER_DPI);

	drm_encoder_helper_add(encoder, &ltdc_encoder_helper_funcs);

	ret = drm_bridge_attach(encoder, bridge, NULL, 0);
	if (ret) {
		if (ret != -EPROBE_DEFER)
			drm_encoder_cleanup(encoder);
		return ret;
	}

	DRM_DEBUG_DRIVER("Bridge encoder:%d created\n", encoder->base.id);

	return 0;
}

static int ltdc_get_caps(struct drm_device *ddev)
{
	struct ltdc_device *ldev = ddev->dev_private;
	u32 bus_width_log2, lcr, gc2r;

	/*
	 * at least 1 layer must be managed & the number of layers
	 * must not exceed LTDC_MAX_LAYER
	 */
	lcr = reg_read(ldev->regs, LTDC_LCR);

	ldev->caps.nb_layers = clamp((int)lcr, 1, LTDC_MAX_LAYER);

	/* set data bus width */
	gc2r = reg_read(ldev->regs, LTDC_GC2R);
	bus_width_log2 = (gc2r & GC2R_BW) >> 4;
	ldev->caps.bus_width = 8 << bus_width_log2;
	ldev->caps.hw_version = reg_read(ldev->regs, LTDC_IDR);

	switch (ldev->caps.hw_version) {
	case HWVER_10200:
	case HWVER_10300:
		ldev->caps.reg_ofs = REG_OFS_NONE;
		ldev->caps.pix_fmt_hw = ltdc_pix_fmt_a0;
		/*
		 * Hw older versions support non-alpha color formats derived
		 * from native alpha color formats only on the primary layer.
		 * For instance, RG16 native format without alpha works fine
		 * on 2nd layer but XR24 (derived color format from AR24)
		 * does not work on 2nd layer.
		 */
		ldev->caps.non_alpha_only_l1 = true;
		ldev->caps.pad_max_freq_hz = 90000000;
		if (ldev->caps.hw_version == HWVER_10200)
			ldev->caps.pad_max_freq_hz = 65000000;
		ldev->caps.nb_irq = 2;
		break;
	case HWVER_20101:
		ldev->caps.reg_ofs = REG_OFS_4;
		ldev->caps.pix_fmt_hw = ltdc_pix_fmt_a1;
		ldev->caps.non_alpha_only_l1 = false;
		ldev->caps.pad_max_freq_hz = 150000000;
		ldev->caps.nb_irq = 4;
		break;
	default:
		return -ENODEV;
	}

	return 0;
}

void ltdc_suspend(struct drm_device *ddev)
{
	struct ltdc_device *ldev = ddev->dev_private;

	DRM_DEBUG_DRIVER("\n");
	clk_disable_unprepare(ldev->pixel_clk);
}

int ltdc_resume(struct drm_device *ddev)
{
	struct ltdc_device *ldev = ddev->dev_private;
	int ret;

	DRM_DEBUG_DRIVER("\n");

	ret = clk_prepare_enable(ldev->pixel_clk);
	if (ret) {
		DRM_ERROR("failed to enable pixel clock (%d)\n", ret);
		return ret;
	}

	return 0;
}

int ltdc_load(struct drm_device *ddev)
{
	struct platform_device *pdev = to_platform_device(ddev->dev);
	struct ltdc_device *ldev = ddev->dev_private;
	struct device *dev = ddev->dev;
	struct device_node *np = dev->of_node;
	struct drm_bridge *bridge;
	struct drm_panel *panel;
	struct drm_crtc *crtc;
	struct reset_control *rstc;
	struct resource *res;
	int irq, i, nb_endpoints;
	int ret = -ENODEV;

	DRM_DEBUG_DRIVER("\n");

	/* Get number of endpoints */
	nb_endpoints = of_graph_get_endpoint_count(np);
	if (!nb_endpoints)
		return -ENODEV;

	ldev->pixel_clk = devm_clk_get(dev, "lcd");
	if (IS_ERR(ldev->pixel_clk)) {
		if (PTR_ERR(ldev->pixel_clk) != -EPROBE_DEFER)
			DRM_ERROR("Unable to get lcd clock\n");
		return PTR_ERR(ldev->pixel_clk);
	}

	if (clk_prepare_enable(ldev->pixel_clk)) {
		DRM_ERROR("Unable to prepare pixel clock\n");
		return -ENODEV;
	}

	/* Get endpoints if any */
	for (i = 0; i < nb_endpoints; i++) {
		ret = drm_of_find_panel_or_bridge(np, 0, i, &panel, &bridge);

		/*
		 * If at least one endpoint is -ENODEV, continue probing,
		 * else if at least one endpoint returned an error
		 * (ie -EPROBE_DEFER) then stop probing.
		 */
		if (ret == -ENODEV)
			continue;
		else if (ret)
			goto err;

		if (panel) {
			bridge = drm_panel_bridge_add_typed(panel,
							    DRM_MODE_CONNECTOR_DPI);
			if (IS_ERR(bridge)) {
				DRM_ERROR("panel-bridge endpoint %d\n", i);
				ret = PTR_ERR(bridge);
				goto err;
			}
		}

		if (bridge) {
			ret = ltdc_encoder_init(ddev, bridge);
			if (ret) {
				if (ret != -EPROBE_DEFER)
					DRM_ERROR("init encoder endpoint %d\n", i);
				goto err;
			}
		}
	}

	rstc = devm_reset_control_get_exclusive(dev, NULL);

	mutex_init(&ldev->err_lock);

	if (!IS_ERR(rstc)) {
		reset_control_assert(rstc);
		usleep_range(10, 20);
		reset_control_deassert(rstc);
	}

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	ldev->regs = devm_ioremap_resource(dev, res);
	if (IS_ERR(ldev->regs)) {
		DRM_ERROR("Unable to get ltdc registers\n");
		ret = PTR_ERR(ldev->regs);
		goto err;
	}

	/* Disable interrupts */
	reg_clear(ldev->regs, LTDC_IER,
		  IER_LIE | IER_RRIE | IER_FUIE | IER_TERRIE);

	ret = ltdc_get_caps(ddev);
	if (ret) {
		DRM_ERROR("hardware identifier (0x%08x) not supported!\n",
			  ldev->caps.hw_version);
		goto err;
	}

	DRM_DEBUG_DRIVER("ltdc hw version 0x%08x\n", ldev->caps.hw_version);

	for (i = 0; i < ldev->caps.nb_irq; i++) {
		irq = platform_get_irq(pdev, i);
		if (irq < 0) {
			ret = irq;
			goto err;
		}

		ret = devm_request_threaded_irq(dev, irq, ltdc_irq,
						ltdc_irq_thread, IRQF_ONESHOT,
						dev_name(dev), ddev);
		if (ret) {
			DRM_ERROR("Failed to register LTDC interrupt\n");
			goto err;
		}

	}

	crtc = devm_kzalloc(dev, sizeof(*crtc), GFP_KERNEL);
	if (!crtc) {
		DRM_ERROR("Failed to allocate crtc\n");
		ret = -ENOMEM;
		goto err;
	}

	ret = ltdc_crtc_init(ddev, crtc);
	if (ret) {
		DRM_ERROR("Failed to init crtc\n");
		goto err;
	}

	ret = drm_vblank_init(ddev, NB_CRTC);
	if (ret) {
		DRM_ERROR("Failed calling drm_vblank_init()\n");
		goto err;
	}

	clk_disable_unprepare(ldev->pixel_clk);

	pinctrl_pm_select_sleep_state(ddev->dev);

	pm_runtime_enable(ddev->dev);

	return 0;
err:
	for (i = 0; i < nb_endpoints; i++)
		drm_of_panel_bridge_remove(ddev->dev->of_node, 0, i);

	clk_disable_unprepare(ldev->pixel_clk);

	return ret;
}

void ltdc_unload(struct drm_device *ddev)
{
	struct device *dev = ddev->dev;
	int nb_endpoints, i;

	DRM_DEBUG_DRIVER("\n");

	nb_endpoints = of_graph_get_endpoint_count(dev->of_node);

	for (i = 0; i < nb_endpoints; i++)
		drm_of_panel_bridge_remove(ddev->dev->of_node, 0, i);

	pm_runtime_disable(ddev->dev);
}

MODULE_AUTHOR("Philippe Cornu <philippe.cornu@st.com>");
MODULE_AUTHOR("Yannick Fertre <yannick.fertre@st.com>");
MODULE_AUTHOR("Fabien Dessenne <fabien.dessenne@st.com>");
MODULE_AUTHOR("Mickael Reulier <mickael.reulier@st.com>");
MODULE_DESCRIPTION("STMicroelectronics ST DRM LTDC driver");
MODULE_LICENSE("GPL v2");
