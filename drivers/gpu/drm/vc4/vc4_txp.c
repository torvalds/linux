// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright © 2018 Broadcom
 *
 * Authors:
 *	Eric Anholt <eric@anholt.net>
 *	Boris Brezillon <boris.brezillon@bootlin.com>
 */

#include <linux/clk.h>
#include <linux/component.h>
#include <linux/of_graph.h>
#include <linux/of_platform.h>
#include <linux/pm_runtime.h>

#include <drm/drm_atomic.h>
#include <drm/drm_atomic_helper.h>
#include <drm/drm_drv.h>
#include <drm/drm_edid.h>
#include <drm/drm_fb_dma_helper.h>
#include <drm/drm_fourcc.h>
#include <drm/drm_framebuffer.h>
#include <drm/drm_panel.h>
#include <drm/drm_probe_helper.h>
#include <drm/drm_vblank.h>
#include <drm/drm_writeback.h>

#include "vc4_drv.h"
#include "vc4_regs.h"

/* Base address of the output.  Raster formats must be 4-byte aligned,
 * T and LT must be 16-byte aligned or maybe utile-aligned (docs are
 * inconsistent, but probably utile).
 */
#define TXP_DST_PTR		0x00

/* Pitch in bytes for raster images, 16-byte aligned.  For tiled, it's
 * the width in tiles.
 */
#define TXP_DST_PITCH		0x04
/* For T-tiled imgaes, DST_PITCH should be the number of tiles wide,
 * shifted up.
 */
# define TXP_T_TILE_WIDTH_SHIFT		7
/* For LT-tiled images, DST_PITCH should be the number of utiles wide,
 * shifted up.
 */
# define TXP_LT_TILE_WIDTH_SHIFT	4

/* Pre-rotation width/height of the image.  Must match HVS config.
 *
 * If TFORMAT and 32-bit, limit is 1920 for 32-bit and 3840 to 16-bit
 * and width/height must be tile or utile-aligned as appropriate.  If
 * transposing (rotating), width is limited to 1920.
 *
 * Height is limited to various numbers between 4088 and 4095.  I'd
 * just use 4088 to be safe.
 */
#define TXP_DIM			0x08
# define TXP_HEIGHT_SHIFT		16
# define TXP_HEIGHT_MASK		GENMASK(31, 16)
# define TXP_WIDTH_SHIFT		0
# define TXP_WIDTH_MASK			GENMASK(15, 0)

#define TXP_DST_CTRL		0x0c
/* These bits are set to 0x54 */
#define TXP_PILOT_SHIFT			24
#define TXP_PILOT_MASK			GENMASK(31, 24)
/* Bits 22-23 are set to 0x01 */
#define TXP_VERSION_SHIFT		22
#define TXP_VERSION_MASK		GENMASK(23, 22)

/* Powers down the internal memory. */
# define TXP_POWERDOWN			BIT(21)

/* Enables storing the alpha component in 8888/4444, instead of
 * filling with ~ALPHA_INVERT.
 */
# define TXP_ALPHA_ENABLE		BIT(20)

/* 4 bits, each enables stores for a channel in each set of 4 bytes.
 * Set to 0xf for normal operation.
 */
# define TXP_BYTE_ENABLE_SHIFT		16
# define TXP_BYTE_ENABLE_MASK		GENMASK(19, 16)

/* Debug: Generate VSTART again at EOF. */
# define TXP_VSTART_AT_EOF		BIT(15)

/* Debug: Terminate the current frame immediately.  Stops AXI
 * writes.
 */
# define TXP_ABORT			BIT(14)

# define TXP_DITHER			BIT(13)

/* Inverts alpha if TXP_ALPHA_ENABLE, chooses fill value for
 * !TXP_ALPHA_ENABLE.
 */
# define TXP_ALPHA_INVERT		BIT(12)

/* Note: I've listed the channels here in high bit (in byte 3/2/1) to
 * low bit (in byte 0) order.
 */
# define TXP_FORMAT_SHIFT		8
# define TXP_FORMAT_MASK		GENMASK(11, 8)
# define TXP_FORMAT_ABGR4444		0
# define TXP_FORMAT_ARGB4444		1
# define TXP_FORMAT_BGRA4444		2
# define TXP_FORMAT_RGBA4444		3
# define TXP_FORMAT_BGR565		6
# define TXP_FORMAT_RGB565		7
/* 888s are non-rotated, raster-only */
# define TXP_FORMAT_BGR888		8
# define TXP_FORMAT_RGB888		9
# define TXP_FORMAT_ABGR8888		12
# define TXP_FORMAT_ARGB8888		13
# define TXP_FORMAT_BGRA8888		14
# define TXP_FORMAT_RGBA8888		15

/* If TFORMAT is set, generates LT instead of T format. */
# define TXP_LINEAR_UTILE		BIT(7)

/* Rotate output by 90 degrees. */
# define TXP_TRANSPOSE			BIT(6)

/* Generate a tiled format for V3D. */
# define TXP_TFORMAT			BIT(5)

/* Generates some undefined test mode output. */
# define TXP_TEST_MODE			BIT(4)

/* Request odd field from HVS. */
# define TXP_FIELD			BIT(3)

/* Raise interrupt when idle. */
# define TXP_EI				BIT(2)

/* Set when generating a frame, clears when idle. */
# define TXP_BUSY			BIT(1)

/* Starts a frame.  Self-clearing. */
# define TXP_GO				BIT(0)

/* Number of lines received and committed to memory. */
#define TXP_PROGRESS		0x10

#define TXP_READ(offset) readl(txp->regs + (offset))
#define TXP_WRITE(offset, val) writel(val, txp->regs + (offset))

struct vc4_txp {
	struct vc4_crtc	base;

	struct platform_device *pdev;

	struct drm_writeback_connector connector;

	void __iomem *regs;
};

static inline struct vc4_txp *encoder_to_vc4_txp(struct drm_encoder *encoder)
{
	return container_of(encoder, struct vc4_txp, connector.encoder);
}

static inline struct vc4_txp *connector_to_vc4_txp(struct drm_connector *conn)
{
	return container_of(conn, struct vc4_txp, connector.base);
}

static const struct debugfs_reg32 txp_regs[] = {
	VC4_REG32(TXP_DST_PTR),
	VC4_REG32(TXP_DST_PITCH),
	VC4_REG32(TXP_DIM),
	VC4_REG32(TXP_DST_CTRL),
	VC4_REG32(TXP_PROGRESS),
};

static int vc4_txp_connector_get_modes(struct drm_connector *connector)
{
	struct drm_device *dev = connector->dev;

	return drm_add_modes_noedid(connector, dev->mode_config.max_width,
				    dev->mode_config.max_height);
}

static enum drm_mode_status
vc4_txp_connector_mode_valid(struct drm_connector *connector,
			     struct drm_display_mode *mode)
{
	struct drm_device *dev = connector->dev;
	struct drm_mode_config *mode_config = &dev->mode_config;
	int w = mode->hdisplay, h = mode->vdisplay;

	if (w < mode_config->min_width || w > mode_config->max_width)
		return MODE_BAD_HVALUE;

	if (h < mode_config->min_height || h > mode_config->max_height)
		return MODE_BAD_VVALUE;

	return MODE_OK;
}

static const u32 drm_fmts[] = {
	DRM_FORMAT_RGB888,
	DRM_FORMAT_BGR888,
	DRM_FORMAT_XRGB8888,
	DRM_FORMAT_XBGR8888,
	DRM_FORMAT_ARGB8888,
	DRM_FORMAT_ABGR8888,
	DRM_FORMAT_RGBX8888,
	DRM_FORMAT_BGRX8888,
	DRM_FORMAT_RGBA8888,
	DRM_FORMAT_BGRA8888,
};

static const u32 txp_fmts[] = {
	TXP_FORMAT_RGB888,
	TXP_FORMAT_BGR888,
	TXP_FORMAT_ARGB8888,
	TXP_FORMAT_ABGR8888,
	TXP_FORMAT_ARGB8888,
	TXP_FORMAT_ABGR8888,
	TXP_FORMAT_RGBA8888,
	TXP_FORMAT_BGRA8888,
	TXP_FORMAT_RGBA8888,
	TXP_FORMAT_BGRA8888,
};

static void vc4_txp_armed(struct drm_crtc_state *state)
{
	struct vc4_crtc_state *vc4_state = to_vc4_crtc_state(state);

	vc4_state->txp_armed = true;
}

static int vc4_txp_connector_atomic_check(struct drm_connector *conn,
					  struct drm_atomic_state *state)
{
	struct drm_connector_state *conn_state;
	struct drm_crtc_state *crtc_state;
	struct drm_framebuffer *fb;
	int i;

	conn_state = drm_atomic_get_new_connector_state(state, conn);
	if (!conn_state->writeback_job)
		return 0;

	crtc_state = drm_atomic_get_new_crtc_state(state, conn_state->crtc);

	fb = conn_state->writeback_job->fb;
	if (fb->width != crtc_state->mode.hdisplay ||
	    fb->height != crtc_state->mode.vdisplay) {
		DRM_DEBUG_KMS("Invalid framebuffer size %ux%u\n",
			      fb->width, fb->height);
		return -EINVAL;
	}

	for (i = 0; i < ARRAY_SIZE(drm_fmts); i++) {
		if (fb->format->format == drm_fmts[i])
			break;
	}

	if (i == ARRAY_SIZE(drm_fmts))
		return -EINVAL;

	/* Pitch must be aligned on 16 bytes. */
	if (fb->pitches[0] & GENMASK(3, 0))
		return -EINVAL;

	vc4_txp_armed(crtc_state);

	return 0;
}

static void vc4_txp_connector_atomic_commit(struct drm_connector *conn,
					struct drm_atomic_state *state)
{
	struct drm_device *drm = conn->dev;
	struct drm_connector_state *conn_state = drm_atomic_get_new_connector_state(state,
										    conn);
	struct vc4_txp *txp = connector_to_vc4_txp(conn);
	struct drm_gem_dma_object *gem;
	struct drm_display_mode *mode;
	struct drm_framebuffer *fb;
	u32 ctrl;
	int idx;
	int i;

	if (WARN_ON(!conn_state->writeback_job))
		return;

	mode = &conn_state->crtc->state->adjusted_mode;
	fb = conn_state->writeback_job->fb;

	for (i = 0; i < ARRAY_SIZE(drm_fmts); i++) {
		if (fb->format->format == drm_fmts[i])
			break;
	}

	if (WARN_ON(i == ARRAY_SIZE(drm_fmts)))
		return;

	ctrl = TXP_GO | TXP_EI |
	       VC4_SET_FIELD(0xf, TXP_BYTE_ENABLE) |
	       VC4_SET_FIELD(txp_fmts[i], TXP_FORMAT);

	if (fb->format->has_alpha)
		ctrl |= TXP_ALPHA_ENABLE;
	else
		/*
		 * If TXP_ALPHA_ENABLE isn't set and TXP_ALPHA_INVERT is, the
		 * hardware will force the output padding to be 0xff.
		 */
		ctrl |= TXP_ALPHA_INVERT;

	if (!drm_dev_enter(drm, &idx))
		return;

	gem = drm_fb_dma_get_gem_obj(fb, 0);
	TXP_WRITE(TXP_DST_PTR, gem->dma_addr + fb->offsets[0]);
	TXP_WRITE(TXP_DST_PITCH, fb->pitches[0]);
	TXP_WRITE(TXP_DIM,
		  VC4_SET_FIELD(mode->hdisplay, TXP_WIDTH) |
		  VC4_SET_FIELD(mode->vdisplay, TXP_HEIGHT));

	TXP_WRITE(TXP_DST_CTRL, ctrl);

	drm_writeback_queue_job(&txp->connector, conn_state);

	drm_dev_exit(idx);
}

static const struct drm_connector_helper_funcs vc4_txp_connector_helper_funcs = {
	.get_modes = vc4_txp_connector_get_modes,
	.mode_valid = vc4_txp_connector_mode_valid,
	.atomic_check = vc4_txp_connector_atomic_check,
	.atomic_commit = vc4_txp_connector_atomic_commit,
};

static enum drm_connector_status
vc4_txp_connector_detect(struct drm_connector *connector, bool force)
{
	return connector_status_connected;
}

static const struct drm_connector_funcs vc4_txp_connector_funcs = {
	.detect = vc4_txp_connector_detect,
	.fill_modes = drm_helper_probe_single_connector_modes,
	.destroy = drm_connector_cleanup,
	.reset = drm_atomic_helper_connector_reset,
	.atomic_duplicate_state = drm_atomic_helper_connector_duplicate_state,
	.atomic_destroy_state = drm_atomic_helper_connector_destroy_state,
};

static void vc4_txp_encoder_disable(struct drm_encoder *encoder)
{
	struct drm_device *drm = encoder->dev;
	struct vc4_txp *txp = encoder_to_vc4_txp(encoder);
	int idx;

	if (!drm_dev_enter(drm, &idx))
		return;

	if (TXP_READ(TXP_DST_CTRL) & TXP_BUSY) {
		unsigned long timeout = jiffies + msecs_to_jiffies(1000);

		TXP_WRITE(TXP_DST_CTRL, TXP_ABORT);

		while (TXP_READ(TXP_DST_CTRL) & TXP_BUSY &&
		       time_before(jiffies, timeout))
			;

		WARN_ON(TXP_READ(TXP_DST_CTRL) & TXP_BUSY);
	}

	TXP_WRITE(TXP_DST_CTRL, TXP_POWERDOWN);

	drm_dev_exit(idx);
}

static const struct drm_encoder_helper_funcs vc4_txp_encoder_helper_funcs = {
	.disable = vc4_txp_encoder_disable,
};

static int vc4_txp_enable_vblank(struct drm_crtc *crtc)
{
	return 0;
}

static void vc4_txp_disable_vblank(struct drm_crtc *crtc) {}

static const struct drm_crtc_funcs vc4_txp_crtc_funcs = {
	.set_config		= drm_atomic_helper_set_config,
	.page_flip		= vc4_page_flip,
	.reset			= vc4_crtc_reset,
	.atomic_duplicate_state	= vc4_crtc_duplicate_state,
	.atomic_destroy_state	= vc4_crtc_destroy_state,
	.enable_vblank		= vc4_txp_enable_vblank,
	.disable_vblank		= vc4_txp_disable_vblank,
	.late_register		= vc4_crtc_late_register,
};

static int vc4_txp_atomic_check(struct drm_crtc *crtc,
				struct drm_atomic_state *state)
{
	struct drm_crtc_state *crtc_state = drm_atomic_get_new_crtc_state(state,
									  crtc);
	int ret;

	ret = vc4_hvs_atomic_check(crtc, state);
	if (ret)
		return ret;

	crtc_state->no_vblank = true;

	return 0;
}

static void vc4_txp_atomic_enable(struct drm_crtc *crtc,
				  struct drm_atomic_state *state)
{
	drm_crtc_vblank_on(crtc);
	vc4_hvs_atomic_enable(crtc, state);
}

static void vc4_txp_atomic_disable(struct drm_crtc *crtc,
				   struct drm_atomic_state *state)
{
	struct drm_device *dev = crtc->dev;

	/* Disable vblank irq handling before crtc is disabled. */
	drm_crtc_vblank_off(crtc);

	vc4_hvs_atomic_disable(crtc, state);

	/*
	 * Make sure we issue a vblank event after disabling the CRTC if
	 * someone was waiting it.
	 */
	if (crtc->state->event) {
		unsigned long flags;

		spin_lock_irqsave(&dev->event_lock, flags);
		drm_crtc_send_vblank_event(crtc, crtc->state->event);
		crtc->state->event = NULL;
		spin_unlock_irqrestore(&dev->event_lock, flags);
	}
}

static const struct drm_crtc_helper_funcs vc4_txp_crtc_helper_funcs = {
	.atomic_check	= vc4_txp_atomic_check,
	.atomic_begin	= vc4_hvs_atomic_begin,
	.atomic_flush	= vc4_hvs_atomic_flush,
	.atomic_enable	= vc4_txp_atomic_enable,
	.atomic_disable	= vc4_txp_atomic_disable,
};

static irqreturn_t vc4_txp_interrupt(int irq, void *data)
{
	struct vc4_txp *txp = data;
	struct vc4_crtc *vc4_crtc = &txp->base;

	/*
	 * We don't need to protect the register access using
	 * drm_dev_enter() there because the interrupt handler lifetime
	 * is tied to the device itself, and not to the DRM device.
	 *
	 * So when the device will be gone, one of the first thing we
	 * will be doing will be to unregister the interrupt handler,
	 * and then unregister the DRM device. drm_dev_enter() would
	 * thus always succeed if we are here.
	 */
	TXP_WRITE(TXP_DST_CTRL, TXP_READ(TXP_DST_CTRL) & ~TXP_EI);
	vc4_crtc_handle_vblank(vc4_crtc);
	drm_writeback_signal_completion(&txp->connector, 0);

	return IRQ_HANDLED;
}

static const struct vc4_crtc_data vc4_txp_crtc_data = {
	.debugfs_name = "txp_regs",
	.hvs_available_channels = BIT(2),
	.hvs_output = 2,
};

static int vc4_txp_bind(struct device *dev, struct device *master, void *data)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct drm_device *drm = dev_get_drvdata(master);
	struct vc4_crtc *vc4_crtc;
	struct vc4_txp *txp;
	struct drm_crtc *crtc;
	struct drm_encoder *encoder;
	int ret, irq;

	irq = platform_get_irq(pdev, 0);
	if (irq < 0)
		return irq;

	txp = drmm_kzalloc(drm, sizeof(*txp), GFP_KERNEL);
	if (!txp)
		return -ENOMEM;
	vc4_crtc = &txp->base;
	crtc = &vc4_crtc->base;

	vc4_crtc->pdev = pdev;
	vc4_crtc->data = &vc4_txp_crtc_data;
	vc4_crtc->feeds_txp = true;

	txp->pdev = pdev;

	txp->regs = vc4_ioremap_regs(pdev, 0);
	if (IS_ERR(txp->regs))
		return PTR_ERR(txp->regs);
	vc4_crtc->regset.base = txp->regs;
	vc4_crtc->regset.regs = txp_regs;
	vc4_crtc->regset.nregs = ARRAY_SIZE(txp_regs);

	drm_connector_helper_add(&txp->connector.base,
				 &vc4_txp_connector_helper_funcs);
	ret = drm_writeback_connector_init(drm, &txp->connector,
					   &vc4_txp_connector_funcs,
					   &vc4_txp_encoder_helper_funcs,
					   drm_fmts, ARRAY_SIZE(drm_fmts),
					   0);
	if (ret)
		return ret;

	ret = vc4_crtc_init(drm, vc4_crtc,
			    &vc4_txp_crtc_funcs, &vc4_txp_crtc_helper_funcs);
	if (ret)
		return ret;

	encoder = &txp->connector.encoder;
	encoder->possible_crtcs = drm_crtc_mask(crtc);

	ret = devm_request_irq(dev, irq, vc4_txp_interrupt, 0,
			       dev_name(dev), txp);
	if (ret)
		return ret;

	dev_set_drvdata(dev, txp);

	return 0;
}

static void vc4_txp_unbind(struct device *dev, struct device *master,
			   void *data)
{
	struct vc4_txp *txp = dev_get_drvdata(dev);

	drm_connector_cleanup(&txp->connector.base);
}

static const struct component_ops vc4_txp_ops = {
	.bind   = vc4_txp_bind,
	.unbind = vc4_txp_unbind,
};

static int vc4_txp_probe(struct platform_device *pdev)
{
	return component_add(&pdev->dev, &vc4_txp_ops);
}

static int vc4_txp_remove(struct platform_device *pdev)
{
	component_del(&pdev->dev, &vc4_txp_ops);
	return 0;
}

static const struct of_device_id vc4_txp_dt_match[] = {
	{ .compatible = "brcm,bcm2835-txp" },
	{ /* sentinel */ },
};

struct platform_driver vc4_txp_driver = {
	.probe = vc4_txp_probe,
	.remove = vc4_txp_remove,
	.driver = {
		.name = "vc4_txp",
		.of_match_table = vc4_txp_dt_match,
	},
};
