/*
 * Copyright (C) STMicroelectronics SA 2017
 *
 * Authors: Philippe Cornu <philippe.cornu@st.com>
 *          Yannick Fertre <yannick.fertre@st.com>
 *          Fabien Dessenne <fabien.dessenne@st.com>
 *          Mickael Reulier <mickael.reulier@st.com>
 *
 * License terms:  GNU General Public License (GPL), version 2
 */

#include <linux/clk.h>
#include <linux/component.h>
#include <linux/of_address.h>
#include <linux/of_graph.h>
#include <linux/reset.h>

#include <drm/drm_atomic.h>
#include <drm/drm_atomic_helper.h>
#include <drm/drm_crtc_helper.h>
#include <drm/drm_fb_cma_helper.h>
#include <drm/drm_gem_cma_helper.h>
#include <drm/drm_of.h>
#include <drm/drm_panel.h>
#include <drm/drm_plane_helper.h>

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
#define REG_OFS_4	4 /* Insertion of "Layer Configuration 2" reg */
#define REG_OFS		(ldev->caps.reg_ofs)
#define LAY_OFS		0x80	/* Register Offset between 2 layers */

/* Global register offsets */
#define LTDC_IDR	0x0000 /* IDentification */
#define LTDC_LCR	0x0004 /* Layer Count */
#define LTDC_SSCR	0x0008 /* Synchronization Size Configuration */
#define LTDC_BPCR	0x000C /* Back Porch Configuration */
#define LTDC_AWCR	0x0010 /* Active Width Configuration */
#define LTDC_TWCR	0x0014 /* Total Width Configuration */
#define LTDC_GCR	0x0018 /* Global Control */
#define LTDC_GC1R	0x001C /* Global Configuration 1 */
#define LTDC_GC2R	0x0020 /* Global Configuration 2 */
#define LTDC_SRCR	0x0024 /* Shadow Reload Configuration */
#define LTDC_GACR	0x0028 /* GAmma Correction */
#define LTDC_BCCR	0x002C /* Background Color Configuration */
#define LTDC_IER	0x0034 /* Interrupt Enable */
#define LTDC_ISR	0x0038 /* Interrupt Status */
#define LTDC_ICR	0x003C /* Interrupt Clear */
#define LTDC_LIPCR	0x0040 /* Line Interrupt Position Configuration */
#define LTDC_CPSR	0x0044 /* Current Position Status */
#define LTDC_CDSR	0x0048 /* Current Display Status */

/* Layer register offsets */
#define LTDC_L1LC1R	(0x0080)	   /* L1 Layer Configuration 1 */
#define LTDC_L1LC2R	(0x0084)	   /* L1 Layer Configuration 2 */
#define LTDC_L1CR	(0x0084 + REG_OFS) /* L1 Control */
#define LTDC_L1WHPCR	(0x0088 + REG_OFS) /* L1 Window Hor Position Config */
#define LTDC_L1WVPCR	(0x008C + REG_OFS) /* L1 Window Vert Position Config */
#define LTDC_L1CKCR	(0x0090 + REG_OFS) /* L1 Color Keying Configuration */
#define LTDC_L1PFCR	(0x0094 + REG_OFS) /* L1 Pixel Format Configuration */
#define LTDC_L1CACR	(0x0098 + REG_OFS) /* L1 Constant Alpha Config */
#define LTDC_L1DCCR	(0x009C + REG_OFS) /* L1 Default Color Configuration */
#define LTDC_L1BFCR	(0x00A0 + REG_OFS) /* L1 Blend Factors Configuration */
#define LTDC_L1FBBCR	(0x00A4 + REG_OFS) /* L1 FrameBuffer Bus Control */
#define LTDC_L1AFBCR	(0x00A8 + REG_OFS) /* L1 AuxFB Control */
#define LTDC_L1CFBAR	(0x00AC + REG_OFS) /* L1 Color FrameBuffer Address */
#define LTDC_L1CFBLR	(0x00B0 + REG_OFS) /* L1 Color FrameBuffer Length */
#define LTDC_L1CFBLNR	(0x00B4 + REG_OFS) /* L1 Color FrameBuffer Line Nb */
#define LTDC_L1AFBAR	(0x00B8 + REG_OFS) /* L1 AuxFB Address */
#define LTDC_L1AFBLR	(0x00BC + REG_OFS) /* L1 AuxFB Length */
#define LTDC_L1AFBLNR	(0x00C0 + REG_OFS) /* L1 AuxFB Line Number */
#define LTDC_L1CLUTWR	(0x00C4 + REG_OFS) /* L1 CLUT Write */
#define LTDC_L1YS1R	(0x00E0 + REG_OFS) /* L1 YCbCr Scale 1 */
#define LTDC_L1YS2R	(0x00E4 + REG_OFS) /* L1 YCbCr Scale 2 */

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
#define GCR_PCPOL	BIT(28)		/* Pixel Clock POLarity */
#define GCR_DEPOL	BIT(29)		/* Data Enable POLarity */
#define GCR_VSPOL	BIT(30)		/* Vertical Synchro POLarity */
#define GCR_HSPOL	BIT(31)		/* Horizontal Synchro POLarity */

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

#define LXCFBLNR_CFBLN	GENMASK(10, 0)	 /* Color Frame Buffer Line Number */

#define HSPOL_AL   0		/* Horizontal Sync POLarity Active Low */
#define VSPOL_AL   0		/* Vertical Sync POLarity Active Low */
#define DEPOL_AL   0		/* Data Enable POLarity Active Low */
#define PCPOL_IPC  0		/* Input Pixel Clock */
#define HSPOL_AH   GCR_HSPOL	/* Horizontal Sync POLarity Active High */
#define VSPOL_AH   GCR_VSPOL	/* Vertical Sync POLarity Active High */
#define DEPOL_AH   GCR_DEPOL	/* Data Enable POLarity Active High */
#define PCPOL_IIPC GCR_PCPOL	/* Inverted Input Pixel Clock */
#define CONSTA_MAX 0xFF		/* CONSTant Alpha MAX= 1.0 */
#define BF1_PAXCA  0x600	/* Pixel Alpha x Constant Alpha */
#define BF1_CA     0x400	/* Constant Alpha */
#define BF2_1PAXCA 0x007	/* 1 - (Pixel Alpha x Constant Alpha) */
#define BF2_1CA	   0x005	/* 1 - Constant Alpha */

#define NB_PF           8       /* Max nb of HW pixel format */

enum ltdc_pix_fmt {
	PF_NONE,
	/* RGB formats */
	PF_ARGB8888,    /* ARGB [32 bits] */
	PF_RGBA8888,    /* RGBA [32 bits] */
	PF_RGB888,      /* RGB [24 bits] */
	PF_RGB565,      /* RGB [16 bits] */
	PF_ARGB1555,    /* ARGB A:1 bit RGB:15 bits [16 bits] */
	PF_ARGB4444,    /* ARGB A:4 bits R/G/B: 4 bits each [16 bits] */
	/* Indexed formats */
	PF_L8,          /* Indexed 8 bits [8 bits] */
	PF_AL44,        /* Alpha:4 bits + indexed 4 bits [8 bits] */
	PF_AL88         /* Alpha:8 bits + indexed 8 bits [16 bits] */
};

/* The index gives the encoding of the pixel format for an HW version */
static const enum ltdc_pix_fmt ltdc_pix_fmt_a0[NB_PF] = {
	PF_ARGB8888,	/* 0x00 */
	PF_RGB888,	/* 0x01 */
	PF_RGB565,	/* 0x02 */
	PF_ARGB1555,	/* 0x03 */
	PF_ARGB4444,	/* 0x04 */
	PF_L8,		/* 0x05 */
	PF_AL44,	/* 0x06 */
	PF_AL88		/* 0x07 */
};

static const enum ltdc_pix_fmt ltdc_pix_fmt_a1[NB_PF] = {
	PF_ARGB8888,	/* 0x00 */
	PF_RGB888,	/* 0x01 */
	PF_RGB565,	/* 0x02 */
	PF_RGBA8888,	/* 0x03 */
	PF_AL44,	/* 0x04 */
	PF_L8,		/* 0x05 */
	PF_ARGB1555,	/* 0x06 */
	PF_ARGB4444	/* 0x07 */
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

static inline struct ltdc_device *connector_to_ltdc(struct drm_connector *con)
{
	return (struct ltdc_device *)con->dev->dev_private;
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
	case PF_AL44: /* No DRM support */
	case PF_AL88: /* No DRM support */
	case PF_NONE:
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

static void ltdc_crtc_load_lut(struct drm_crtc *crtc)
{
	struct ltdc_device *ldev = crtc_to_ltdc(crtc);
	unsigned int i, lay;

	for (lay = 0; lay < ldev->caps.nb_layers; lay++)
		for (i = 0; i < 256; i++)
			reg_write(ldev->regs, LTDC_L1CLUTWR + lay * LAY_OFS,
				  ldev->clut[i]);
}

static void ltdc_crtc_atomic_enable(struct drm_crtc *crtc,
				    struct drm_crtc_state *old_state)
{
	struct ltdc_device *ldev = crtc_to_ltdc(crtc);

	DRM_DEBUG_DRIVER("\n");

	/* Sets the background color value */
	reg_write(ldev->regs, LTDC_BCCR, BCCR_BCBLACK);

	/* Enable IRQ */
	reg_set(ldev->regs, LTDC_IER, IER_RRIE | IER_FUIE | IER_TERRIE);

	/* Immediately commit the planes */
	reg_set(ldev->regs, LTDC_SRCR, SRCR_IMR);

	/* Enable LTDC */
	reg_set(ldev->regs, LTDC_GCR, GCR_LTDCEN);

	drm_crtc_vblank_on(crtc);
}

static void ltdc_crtc_atomic_disable(struct drm_crtc *crtc,
				     struct drm_crtc_state *old_state)
{
	struct ltdc_device *ldev = crtc_to_ltdc(crtc);

	DRM_DEBUG_DRIVER("\n");

	drm_crtc_vblank_off(crtc);

	/* disable LTDC */
	reg_clear(ldev->regs, LTDC_GCR, GCR_LTDCEN);

	/* disable IRQ */
	reg_clear(ldev->regs, LTDC_IER, IER_RRIE | IER_FUIE | IER_TERRIE);

	/* immediately commit disable of layers before switching off LTDC */
	reg_set(ldev->regs, LTDC_SRCR, SRCR_IMR);
}

static void ltdc_crtc_mode_set_nofb(struct drm_crtc *crtc)
{
	struct ltdc_device *ldev = crtc_to_ltdc(crtc);
	struct drm_display_mode *mode = &crtc->state->adjusted_mode;
	struct videomode vm;
	int rate = mode->clock * 1000;
	u32 hsync, vsync, accum_hbp, accum_vbp, accum_act_w, accum_act_h;
	u32 total_width, total_height;
	u32 val;

	drm_display_mode_to_videomode(mode, &vm);

	DRM_DEBUG_DRIVER("CRTC:%d mode:%s\n", crtc->base.id, mode->name);
	DRM_DEBUG_DRIVER("Video mode: %dx%d", vm.hactive, vm.vactive);
	DRM_DEBUG_DRIVER(" hfp %d hbp %d hsl %d vfp %d vbp %d vsl %d\n",
			 vm.hfront_porch, vm.hback_porch, vm.hsync_len,
			 vm.vfront_porch, vm.vback_porch, vm.vsync_len);

	/* Convert video timings to ltdc timings */
	hsync = vm.hsync_len - 1;
	vsync = vm.vsync_len - 1;
	accum_hbp = hsync + vm.hback_porch;
	accum_vbp = vsync + vm.vback_porch;
	accum_act_w = accum_hbp + vm.hactive;
	accum_act_h = accum_vbp + vm.vactive;
	total_width = accum_act_w + vm.hfront_porch;
	total_height = accum_act_h + vm.vfront_porch;

	clk_disable(ldev->pixel_clk);

	if (clk_set_rate(ldev->pixel_clk, rate) < 0) {
		DRM_ERROR("Cannot set rate (%dHz) for pixel clk\n", rate);
		return;
	}

	clk_enable(ldev->pixel_clk);

	/* Configures the HS, VS, DE and PC polarities. */
	val = HSPOL_AL | VSPOL_AL | DEPOL_AL | PCPOL_IPC;

	if (vm.flags & DISPLAY_FLAGS_HSYNC_HIGH)
		val |= HSPOL_AH;

	if (vm.flags & DISPLAY_FLAGS_VSYNC_HIGH)
		val |= VSPOL_AH;

	if (vm.flags & DISPLAY_FLAGS_DE_HIGH)
		val |= DEPOL_AH;

	if (vm.flags & DISPLAY_FLAGS_PIXDATA_NEGEDGE)
		val |= PCPOL_IIPC;

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
				   struct drm_crtc_state *old_crtc_state)
{
	struct ltdc_device *ldev = crtc_to_ltdc(crtc);
	struct drm_pending_vblank_event *event = crtc->state->event;

	DRM_DEBUG_ATOMIC("\n");

	/* Commit shadow registers = update planes at next vblank */
	reg_set(ldev->regs, LTDC_SRCR, SRCR_VBR);

	if (event) {
		crtc->state->event = NULL;

		spin_lock_irq(&crtc->dev->event_lock);
		if (drm_crtc_vblank_get(crtc) == 0)
			drm_crtc_arm_vblank_event(crtc, event);
		else
			drm_crtc_send_vblank_event(crtc, event);
		spin_unlock_irq(&crtc->dev->event_lock);
	}
}

static struct drm_crtc_helper_funcs ltdc_crtc_helper_funcs = {
	.load_lut = ltdc_crtc_load_lut,
	.mode_set_nofb = ltdc_crtc_mode_set_nofb,
	.atomic_flush = ltdc_crtc_atomic_flush,
	.atomic_enable = ltdc_crtc_atomic_enable,
	.atomic_disable = ltdc_crtc_atomic_disable,
};

int ltdc_crtc_enable_vblank(struct drm_device *ddev, unsigned int pipe)
{
	struct ltdc_device *ldev = ddev->dev_private;

	DRM_DEBUG_DRIVER("\n");
	reg_set(ldev->regs, LTDC_IER, IER_LIE);

	return 0;
}

void ltdc_crtc_disable_vblank(struct drm_device *ddev, unsigned int pipe)
{
	struct ltdc_device *ldev = ddev->dev_private;

	DRM_DEBUG_DRIVER("\n");
	reg_clear(ldev->regs, LTDC_IER, IER_LIE);
}

static struct drm_crtc_funcs ltdc_crtc_funcs = {
	.destroy = drm_crtc_cleanup,
	.set_config = drm_atomic_helper_set_config,
	.page_flip = drm_atomic_helper_page_flip,
	.reset = drm_atomic_helper_crtc_reset,
	.atomic_duplicate_state = drm_atomic_helper_crtc_duplicate_state,
	.atomic_destroy_state = drm_atomic_helper_crtc_destroy_state,
};

/*
 * DRM_PLANE
 */

static int ltdc_plane_atomic_check(struct drm_plane *plane,
				   struct drm_plane_state *state)
{
	struct drm_framebuffer *fb = state->fb;
	u32 src_x, src_y, src_w, src_h;

	DRM_DEBUG_DRIVER("\n");

	if (!fb)
		return 0;

	/* convert src_ from 16:16 format */
	src_x = state->src_x >> 16;
	src_y = state->src_y >> 16;
	src_w = state->src_w >> 16;
	src_h = state->src_h >> 16;

	/* Reject scaling */
	if ((src_w != state->crtc_w) || (src_h != state->crtc_h)) {
		DRM_ERROR("Scaling is not supported");
		return -EINVAL;
	}

	return 0;
}

static void ltdc_plane_atomic_update(struct drm_plane *plane,
				     struct drm_plane_state *oldstate)
{
	struct ltdc_device *ldev = plane_to_ltdc(plane);
	struct drm_plane_state *state = plane->state;
	struct drm_framebuffer *fb = state->fb;
	u32 lofs = plane->index * LAY_OFS;
	u32 x0 = state->crtc_x;
	u32 x1 = state->crtc_x + state->crtc_w - 1;
	u32 y0 = state->crtc_y;
	u32 y1 = state->crtc_y + state->crtc_h - 1;
	u32 src_x, src_y, src_w, src_h;
	u32 val, pitch_in_bytes, line_length, paddr, ahbp, avbp, bpcr;
	enum ltdc_pix_fmt pf;

	if (!state->crtc || !fb) {
		DRM_DEBUG_DRIVER("fb or crtc NULL");
		return;
	}

	/* convert src_ from 16:16 format */
	src_x = state->src_x >> 16;
	src_y = state->src_y >> 16;
	src_w = state->src_w >> 16;
	src_h = state->src_h >> 16;

	DRM_DEBUG_DRIVER(
		"plane:%d fb:%d (%dx%d)@(%d,%d) -> (%dx%d)@(%d,%d)\n",
		plane->base.id, fb->base.id,
		src_w, src_h, src_x, src_y,
		state->crtc_w, state->crtc_h, state->crtc_x, state->crtc_y);

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
		val = 0; /* set by default ARGB 32 bits */
	}
	reg_update_bits(ldev->regs, LTDC_L1PFCR + lofs, LXPFCR_PF, val);

	/* Configures the color frame buffer pitch in bytes & line length */
	pitch_in_bytes = fb->pitches[0];
	line_length = drm_format_plane_cpp(fb->format->format, 0) *
		      (x1 - x0 + 1) + (ldev->caps.bus_width >> 3) - 1;
	val = ((pitch_in_bytes << 16) | line_length);
	reg_update_bits(ldev->regs, LTDC_L1CFBLR + lofs,
			LXCFBLR_CFBLL | LXCFBLR_CFBP, val);

	/* Specifies the constant alpha value */
	val = CONSTA_MAX;
	reg_update_bits(ldev->regs, LTDC_L1CACR + lofs,
			LXCACR_CONSTA, val);

	/* Specifies the blending factors */
	val = BF1_PAXCA | BF2_1PAXCA;
	reg_update_bits(ldev->regs, LTDC_L1BFCR + lofs,
			LXBFCR_BF2 | LXBFCR_BF1, val);

	/* Configures the frame buffer line number */
	val = y1 - y0 + 1;
	reg_update_bits(ldev->regs, LTDC_L1CFBLNR + lofs,
			LXCFBLNR_CFBLN, val);

	/* Sets the FB address */
	paddr = (u32)drm_fb_cma_get_gem_addr(fb, state, 0);

	DRM_DEBUG_DRIVER("fb: phys 0x%08x", paddr);
	reg_write(ldev->regs, LTDC_L1CFBAR + lofs, paddr);

	/* Enable layer and CLUT if needed */
	val = fb->format->format == DRM_FORMAT_C8 ? LXCR_CLUTEN : 0;
	val |= LXCR_LEN;
	reg_update_bits(ldev->regs, LTDC_L1CR + lofs,
			LXCR_LEN | LXCR_CLUTEN, val);

	mutex_lock(&ldev->err_lock);
	if (ldev->error_status & ISR_FUIF) {
		DRM_DEBUG_DRIVER("Fifo underrun\n");
		ldev->error_status &= ~ISR_FUIF;
	}
	if (ldev->error_status & ISR_TERRIF) {
		DRM_DEBUG_DRIVER("Transfer error\n");
		ldev->error_status &= ~ISR_TERRIF;
	}
	mutex_unlock(&ldev->err_lock);
}

static void ltdc_plane_atomic_disable(struct drm_plane *plane,
				      struct drm_plane_state *oldstate)
{
	struct ltdc_device *ldev = plane_to_ltdc(plane);
	u32 lofs = plane->index * LAY_OFS;

	/* disable layer */
	reg_clear(ldev->regs, LTDC_L1CR + lofs, LXCR_LEN);

	DRM_DEBUG_DRIVER("CRTC:%d plane:%d\n",
			 oldstate->crtc->base.id, plane->base.id);
}

static struct drm_plane_funcs ltdc_plane_funcs = {
	.update_plane = drm_atomic_helper_update_plane,
	.disable_plane = drm_atomic_helper_disable_plane,
	.destroy = drm_plane_cleanup,
	.set_property = drm_atomic_helper_plane_set_property,
	.reset = drm_atomic_helper_plane_reset,
	.atomic_duplicate_state = drm_atomic_helper_plane_duplicate_state,
	.atomic_destroy_state = drm_atomic_helper_plane_destroy_state,
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
	u32 formats[NB_PF];
	u32 drm_fmt;
	int ret;

	/* Get supported pixel formats */
	for (i = 0; i < NB_PF; i++) {
		drm_fmt = to_drm_pixelformat(ldev->caps.pix_fmt_hw[i]);
		if (!drm_fmt)
			continue;
		formats[nb_fmt++] = drm_fmt;
	}

	plane = devm_kzalloc(dev, sizeof(*plane), GFP_KERNEL);
	if (!plane)
		return 0;

	ret = drm_universal_plane_init(ddev, plane, possible_crtcs,
				       &ltdc_plane_funcs, formats, nb_fmt,
				       type, NULL);
	if (ret < 0)
		return 0;

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
	int res;

	primary = ltdc_plane_create(ddev, DRM_PLANE_TYPE_PRIMARY);
	if (!primary) {
		DRM_ERROR("Can not create primary plane\n");
		return -EINVAL;
	}

	res = drm_crtc_init_with_planes(ddev, crtc, primary, NULL,
					&ltdc_crtc_funcs, NULL);
	if (res) {
		DRM_ERROR("Can not initialize CRTC\n");
		goto cleanup;
	}

	drm_crtc_helper_add(crtc, &ltdc_crtc_helper_funcs);

	DRM_DEBUG_DRIVER("CRTC:%d created\n", crtc->base.id);

	/* Add planes. Note : the first layer is used by primary plane */
	for (i = 1; i < ldev->caps.nb_layers; i++) {
		overlay = ltdc_plane_create(ddev, DRM_PLANE_TYPE_OVERLAY);
		if (!overlay) {
			res = -ENOMEM;
			DRM_ERROR("Can not create overlay plane %d\n", i);
			goto cleanup;
		}
	}

	return 0;

cleanup:
	ltdc_plane_destroy_all(ddev);
	return res;
}

/*
 * DRM_ENCODER
 */

static void ltdc_rgb_encoder_enable(struct drm_encoder *encoder)
{
	struct ltdc_device *ldev = encoder_to_ltdc(encoder);

	DRM_DEBUG_DRIVER("\n");

	drm_panel_prepare(ldev->panel);
	drm_panel_enable(ldev->panel);
}

static void ltdc_rgb_encoder_disable(struct drm_encoder *encoder)
{
	struct ltdc_device *ldev = encoder_to_ltdc(encoder);

	DRM_DEBUG_DRIVER("\n");

	drm_panel_disable(ldev->panel);
	drm_panel_unprepare(ldev->panel);
}

static const struct drm_encoder_helper_funcs ltdc_rgb_encoder_helper_funcs = {
	.enable = ltdc_rgb_encoder_enable,
	.disable = ltdc_rgb_encoder_disable,
};

static const struct drm_encoder_funcs ltdc_rgb_encoder_funcs = {
	.destroy = drm_encoder_cleanup,
};

static struct drm_encoder *ltdc_rgb_encoder_create(struct drm_device *ddev)
{
	struct drm_encoder *encoder;

	encoder = devm_kzalloc(ddev->dev, sizeof(*encoder), GFP_KERNEL);
	if (!encoder)
		return NULL;

	encoder->possible_crtcs = CRTC_MASK;
	encoder->possible_clones = 0; /* No cloning support */

	drm_encoder_init(ddev, encoder, &ltdc_rgb_encoder_funcs,
			 DRM_MODE_ENCODER_DPI, NULL);

	drm_encoder_helper_add(encoder, &ltdc_rgb_encoder_helper_funcs);

	DRM_DEBUG_DRIVER("RGB encoder:%d created\n", encoder->base.id);

	return encoder;
}

/*
 * DRM_CONNECTOR
 */

static int ltdc_rgb_connector_get_modes(struct drm_connector *connector)
{
	struct drm_device *ddev = connector->dev;
	struct ltdc_device *ldev = ddev->dev_private;
	int ret = 0;

	DRM_DEBUG_DRIVER("\n");

	if (ldev->panel)
		ret = drm_panel_get_modes(ldev->panel);

	return ret < 0 ? 0 : ret;
}

static struct drm_connector_helper_funcs ltdc_rgb_connector_helper_funcs = {
	.get_modes = ltdc_rgb_connector_get_modes,
};

static enum drm_connector_status
ltdc_rgb_connector_detect(struct drm_connector *connector, bool force)
{
	struct ltdc_device *ldev = connector_to_ltdc(connector);

	return ldev->panel ? connector_status_connected :
	       connector_status_disconnected;
}

static void ltdc_rgb_connector_destroy(struct drm_connector *connector)
{
	DRM_DEBUG_DRIVER("\n");

	drm_connector_unregister(connector);
	drm_connector_cleanup(connector);
}

static const struct drm_connector_funcs ltdc_rgb_connector_funcs = {
	.dpms = drm_atomic_helper_connector_dpms,
	.fill_modes = drm_helper_probe_single_connector_modes,
	.detect = ltdc_rgb_connector_detect,
	.destroy = ltdc_rgb_connector_destroy,
	.reset = drm_atomic_helper_connector_reset,
	.atomic_duplicate_state = drm_atomic_helper_connector_duplicate_state,
	.atomic_destroy_state = drm_atomic_helper_connector_destroy_state,
};

struct drm_connector *ltdc_rgb_connector_create(struct drm_device *ddev)
{
	struct drm_connector *connector;
	int err;

	connector = devm_kzalloc(ddev->dev, sizeof(*connector), GFP_KERNEL);
	if (!connector) {
		DRM_ERROR("Failed to allocate connector\n");
		return NULL;
	}

	connector->polled = DRM_CONNECTOR_POLL_HPD;

	err = drm_connector_init(ddev, connector, &ltdc_rgb_connector_funcs,
				 DRM_MODE_CONNECTOR_DPI);
	if (err) {
		DRM_ERROR("Failed to initialize connector\n");
		return NULL;
	}

	drm_connector_helper_add(connector, &ltdc_rgb_connector_helper_funcs);

	DRM_DEBUG_DRIVER("RGB connector:%d created\n", connector->base.id);

	return connector;
}

static int ltdc_get_caps(struct drm_device *ddev)
{
	struct ltdc_device *ldev = ddev->dev_private;
	u32 bus_width_log2, lcr, gc2r;

	/* at least 1 layer must be managed */
	lcr = reg_read(ldev->regs, LTDC_LCR);

	ldev->caps.nb_layers = max_t(int, lcr, 1);

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
		break;
	case HWVER_20101:
		ldev->caps.reg_ofs = REG_OFS_4;
		ldev->caps.pix_fmt_hw = ltdc_pix_fmt_a1;
		break;
	default:
		return -ENODEV;
	}

	return 0;
}

static struct drm_panel *ltdc_get_panel(struct drm_device *ddev)
{
	struct device *dev = ddev->dev;
	struct device_node *np = dev->of_node;
	struct device_node *entity, *port = NULL;
	struct drm_panel *panel = NULL;

	DRM_DEBUG_DRIVER("\n");

	/*
	 * Parse ltdc node to get remote port and find RGB panel / HDMI slave
	 * If a dsi or a bridge (hdmi, lvds...) is connected to ltdc,
	 * a remote port & RGB panel will not be found.
	 */
	for_each_endpoint_of_node(np, entity) {
		if (!of_device_is_available(entity))
			continue;

		port = of_graph_get_remote_port_parent(entity);
		if (port) {
			panel = of_drm_find_panel(port);
			of_node_put(port);
			if (panel) {
				DRM_DEBUG_DRIVER("remote panel %s\n",
						 port->full_name);
			} else {
				DRM_DEBUG_DRIVER("panel missing\n");
				of_node_put(entity);
			}
		}
	}

	return panel;
}

int ltdc_load(struct drm_device *ddev)
{
	struct platform_device *pdev = to_platform_device(ddev->dev);
	struct ltdc_device *ldev = ddev->dev_private;
	struct device *dev = ddev->dev;
	struct device_node *np = dev->of_node;
	struct drm_encoder *encoder;
	struct drm_connector *connector = NULL;
	struct drm_crtc *crtc;
	struct reset_control *rstc;
	struct resource res;
	int irq, ret, i;

	DRM_DEBUG_DRIVER("\n");

	ldev->panel = ltdc_get_panel(ddev);
	if (!ldev->panel)
		return -EPROBE_DEFER;

	rstc = of_reset_control_get(np, NULL);

	mutex_init(&ldev->err_lock);

	ldev->pixel_clk = devm_clk_get(dev, "lcd");
	if (IS_ERR(ldev->pixel_clk)) {
		DRM_ERROR("Unable to get lcd clock\n");
		return -ENODEV;
	}

	if (clk_prepare_enable(ldev->pixel_clk)) {
		DRM_ERROR("Unable to prepare pixel clock\n");
		return -ENODEV;
	}

	if (of_address_to_resource(np, 0, &res)) {
		DRM_ERROR("Unable to get resource\n");
		ret = -ENODEV;
		goto err;
	}

	ldev->regs = devm_ioremap_resource(dev, &res);
	if (IS_ERR(ldev->regs)) {
		DRM_ERROR("Unable to get ltdc registers\n");
		ret = PTR_ERR(ldev->regs);
		goto err;
	}

	for (i = 0; i < MAX_IRQ; i++) {
		irq = platform_get_irq(pdev, i);
		if (irq < 0)
			continue;

		ret = devm_request_threaded_irq(dev, irq, ltdc_irq,
						ltdc_irq_thread, IRQF_ONESHOT,
						dev_name(dev), ddev);
		if (ret) {
			DRM_ERROR("Failed to register LTDC interrupt\n");
			goto err;
		}
	}

	if (!IS_ERR(rstc))
		reset_control_deassert(rstc);

	/* Disable interrupts */
	reg_clear(ldev->regs, LTDC_IER,
		  IER_LIE | IER_RRIE | IER_FUIE | IER_TERRIE);

	ret = ltdc_get_caps(ddev);
	if (ret) {
		DRM_ERROR("hardware identifier (0x%08x) not supported!\n",
			  ldev->caps.hw_version);
		goto err;
	}

	DRM_INFO("ltdc hw version 0x%08x - ready\n", ldev->caps.hw_version);

	if (ldev->panel) {
		encoder = ltdc_rgb_encoder_create(ddev);
		if (!encoder) {
			DRM_ERROR("Failed to create RGB encoder\n");
			ret = -EINVAL;
			goto err;
		}

		connector = ltdc_rgb_connector_create(ddev);
		if (!connector) {
			DRM_ERROR("Failed to create RGB connector\n");
			ret = -EINVAL;
			goto err;
		}

		ret = drm_mode_connector_attach_encoder(connector, encoder);
		if (ret) {
			DRM_ERROR("Failed to attach connector to encoder\n");
			goto err;
		}

		drm_panel_attach(ldev->panel, connector);
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

	/* Allow usage of vblank without having to call drm_irq_install */
	ddev->irq_enabled = 1;

	return 0;
err:
	if (ldev->panel)
		drm_panel_detach(ldev->panel);

	clk_disable_unprepare(ldev->pixel_clk);

	return ret;
}

void ltdc_unload(struct drm_device *ddev)
{
	struct ltdc_device *ldev = ddev->dev_private;

	DRM_DEBUG_DRIVER("\n");

	if (ldev->panel)
		drm_panel_detach(ldev->panel);

	clk_disable_unprepare(ldev->pixel_clk);
}

MODULE_AUTHOR("Philippe Cornu <philippe.cornu@st.com>");
MODULE_AUTHOR("Yannick Fertre <yannick.fertre@st.com>");
MODULE_AUTHOR("Fabien Dessenne <fabien.dessenne@st.com>");
MODULE_AUTHOR("Mickael Reulier <mickael.reulier@st.com>");
MODULE_DESCRIPTION("STMicroelectronics ST DRM LTDC driver");
MODULE_LICENSE("GPL v2");
