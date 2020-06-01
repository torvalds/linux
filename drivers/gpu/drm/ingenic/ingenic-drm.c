// SPDX-License-Identifier: GPL-2.0
//
// Ingenic JZ47xx KMS driver
//
// Copyright (C) 2019, Paul Cercueil <paul@crapouillou.net>

#include <linux/clk.h>
#include <linux/dma-mapping.h>
#include <linux/module.h>
#include <linux/of_device.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>

#include <drm/drm_atomic.h>
#include <drm/drm_atomic_helper.h>
#include <drm/drm_bridge.h>
#include <drm/drm_crtc.h>
#include <drm/drm_crtc_helper.h>
#include <drm/drm_drv.h>
#include <drm/drm_gem_cma_helper.h>
#include <drm/drm_fb_cma_helper.h>
#include <drm/drm_fb_helper.h>
#include <drm/drm_fourcc.h>
#include <drm/drm_gem_framebuffer_helper.h>
#include <drm/drm_irq.h>
#include <drm/drm_of.h>
#include <drm/drm_panel.h>
#include <drm/drm_plane.h>
#include <drm/drm_plane_helper.h>
#include <drm/drm_probe_helper.h>
#include <drm/drm_vblank.h>

#define JZ_REG_LCD_CFG				0x00
#define JZ_REG_LCD_VSYNC			0x04
#define JZ_REG_LCD_HSYNC			0x08
#define JZ_REG_LCD_VAT				0x0C
#define JZ_REG_LCD_DAH				0x10
#define JZ_REG_LCD_DAV				0x14
#define JZ_REG_LCD_PS				0x18
#define JZ_REG_LCD_CLS				0x1C
#define JZ_REG_LCD_SPL				0x20
#define JZ_REG_LCD_REV				0x24
#define JZ_REG_LCD_CTRL				0x30
#define JZ_REG_LCD_STATE			0x34
#define JZ_REG_LCD_IID				0x38
#define JZ_REG_LCD_DA0				0x40
#define JZ_REG_LCD_SA0				0x44
#define JZ_REG_LCD_FID0				0x48
#define JZ_REG_LCD_CMD0				0x4C
#define JZ_REG_LCD_DA1				0x50
#define JZ_REG_LCD_SA1				0x54
#define JZ_REG_LCD_FID1				0x58
#define JZ_REG_LCD_CMD1				0x5C

#define JZ_LCD_CFG_SLCD				BIT(31)
#define JZ_LCD_CFG_PS_DISABLE			BIT(23)
#define JZ_LCD_CFG_CLS_DISABLE			BIT(22)
#define JZ_LCD_CFG_SPL_DISABLE			BIT(21)
#define JZ_LCD_CFG_REV_DISABLE			BIT(20)
#define JZ_LCD_CFG_HSYNCM			BIT(19)
#define JZ_LCD_CFG_PCLKM			BIT(18)
#define JZ_LCD_CFG_INV				BIT(17)
#define JZ_LCD_CFG_SYNC_DIR			BIT(16)
#define JZ_LCD_CFG_PS_POLARITY			BIT(15)
#define JZ_LCD_CFG_CLS_POLARITY			BIT(14)
#define JZ_LCD_CFG_SPL_POLARITY			BIT(13)
#define JZ_LCD_CFG_REV_POLARITY			BIT(12)
#define JZ_LCD_CFG_HSYNC_ACTIVE_LOW		BIT(11)
#define JZ_LCD_CFG_PCLK_FALLING_EDGE		BIT(10)
#define JZ_LCD_CFG_DE_ACTIVE_LOW		BIT(9)
#define JZ_LCD_CFG_VSYNC_ACTIVE_LOW		BIT(8)
#define JZ_LCD_CFG_18_BIT			BIT(7)
#define JZ_LCD_CFG_PDW				(BIT(5) | BIT(4))

#define JZ_LCD_CFG_MODE_GENERIC_16BIT		0
#define JZ_LCD_CFG_MODE_GENERIC_18BIT		BIT(7)
#define JZ_LCD_CFG_MODE_GENERIC_24BIT		BIT(6)

#define JZ_LCD_CFG_MODE_SPECIAL_TFT_1		1
#define JZ_LCD_CFG_MODE_SPECIAL_TFT_2		2
#define JZ_LCD_CFG_MODE_SPECIAL_TFT_3		3

#define JZ_LCD_CFG_MODE_TV_OUT_P		4
#define JZ_LCD_CFG_MODE_TV_OUT_I		6

#define JZ_LCD_CFG_MODE_SINGLE_COLOR_STN	8
#define JZ_LCD_CFG_MODE_SINGLE_MONOCHROME_STN	9
#define JZ_LCD_CFG_MODE_DUAL_COLOR_STN		10
#define JZ_LCD_CFG_MODE_DUAL_MONOCHROME_STN	11

#define JZ_LCD_CFG_MODE_8BIT_SERIAL		12
#define JZ_LCD_CFG_MODE_LCM			13

#define JZ_LCD_VSYNC_VPS_OFFSET			16
#define JZ_LCD_VSYNC_VPE_OFFSET			0

#define JZ_LCD_HSYNC_HPS_OFFSET			16
#define JZ_LCD_HSYNC_HPE_OFFSET			0

#define JZ_LCD_VAT_HT_OFFSET			16
#define JZ_LCD_VAT_VT_OFFSET			0

#define JZ_LCD_DAH_HDS_OFFSET			16
#define JZ_LCD_DAH_HDE_OFFSET			0

#define JZ_LCD_DAV_VDS_OFFSET			16
#define JZ_LCD_DAV_VDE_OFFSET			0

#define JZ_LCD_CTRL_BURST_4			(0x0 << 28)
#define JZ_LCD_CTRL_BURST_8			(0x1 << 28)
#define JZ_LCD_CTRL_BURST_16			(0x2 << 28)
#define JZ_LCD_CTRL_RGB555			BIT(27)
#define JZ_LCD_CTRL_OFUP			BIT(26)
#define JZ_LCD_CTRL_FRC_GRAYSCALE_16		(0x0 << 24)
#define JZ_LCD_CTRL_FRC_GRAYSCALE_4		(0x1 << 24)
#define JZ_LCD_CTRL_FRC_GRAYSCALE_2		(0x2 << 24)
#define JZ_LCD_CTRL_PDD_MASK			(0xff << 16)
#define JZ_LCD_CTRL_EOF_IRQ			BIT(13)
#define JZ_LCD_CTRL_SOF_IRQ			BIT(12)
#define JZ_LCD_CTRL_OFU_IRQ			BIT(11)
#define JZ_LCD_CTRL_IFU0_IRQ			BIT(10)
#define JZ_LCD_CTRL_IFU1_IRQ			BIT(9)
#define JZ_LCD_CTRL_DD_IRQ			BIT(8)
#define JZ_LCD_CTRL_QDD_IRQ			BIT(7)
#define JZ_LCD_CTRL_REVERSE_ENDIAN		BIT(6)
#define JZ_LCD_CTRL_LSB_FISRT			BIT(5)
#define JZ_LCD_CTRL_DISABLE			BIT(4)
#define JZ_LCD_CTRL_ENABLE			BIT(3)
#define JZ_LCD_CTRL_BPP_1			0x0
#define JZ_LCD_CTRL_BPP_2			0x1
#define JZ_LCD_CTRL_BPP_4			0x2
#define JZ_LCD_CTRL_BPP_8			0x3
#define JZ_LCD_CTRL_BPP_15_16			0x4
#define JZ_LCD_CTRL_BPP_18_24			0x5
#define JZ_LCD_CTRL_BPP_MASK			(JZ_LCD_CTRL_RGB555 | (0x7 << 0))

#define JZ_LCD_CMD_SOF_IRQ			BIT(31)
#define JZ_LCD_CMD_EOF_IRQ			BIT(30)
#define JZ_LCD_CMD_ENABLE_PAL			BIT(28)

#define JZ_LCD_SYNC_MASK			0x3ff

#define JZ_LCD_STATE_EOF_IRQ			BIT(5)
#define JZ_LCD_STATE_SOF_IRQ			BIT(4)
#define JZ_LCD_STATE_DISABLED			BIT(0)

struct ingenic_dma_hwdesc {
	u32 next;
	u32 addr;
	u32 id;
	u32 cmd;
} __packed;

struct jz_soc_info {
	bool needs_dev_clk;
	unsigned int max_width, max_height;
};

struct ingenic_drm {
	struct drm_device drm;
	struct drm_plane primary;
	struct drm_crtc crtc;
	struct drm_encoder encoder;

	struct device *dev;
	struct regmap *map;
	struct clk *lcd_clk, *pix_clk;
	const struct jz_soc_info *soc_info;

	struct ingenic_dma_hwdesc *dma_hwdesc;
	dma_addr_t dma_hwdesc_phys;

	bool panel_is_sharp;
};

static const u32 ingenic_drm_primary_formats[] = {
	DRM_FORMAT_XRGB1555,
	DRM_FORMAT_RGB565,
	DRM_FORMAT_XRGB8888,
};

static bool ingenic_drm_writeable_reg(struct device *dev, unsigned int reg)
{
	switch (reg) {
	case JZ_REG_LCD_IID:
	case JZ_REG_LCD_SA0:
	case JZ_REG_LCD_FID0:
	case JZ_REG_LCD_CMD0:
	case JZ_REG_LCD_SA1:
	case JZ_REG_LCD_FID1:
	case JZ_REG_LCD_CMD1:
		return false;
	default:
		return true;
	}
}

static const struct regmap_config ingenic_drm_regmap_config = {
	.reg_bits = 32,
	.val_bits = 32,
	.reg_stride = 4,

	.max_register = JZ_REG_LCD_CMD1,
	.writeable_reg = ingenic_drm_writeable_reg,
};

static inline struct ingenic_drm *drm_device_get_priv(struct drm_device *drm)
{
	return container_of(drm, struct ingenic_drm, drm);
}

static inline struct ingenic_drm *drm_crtc_get_priv(struct drm_crtc *crtc)
{
	return container_of(crtc, struct ingenic_drm, crtc);
}

static inline struct ingenic_drm *
drm_encoder_get_priv(struct drm_encoder *encoder)
{
	return container_of(encoder, struct ingenic_drm, encoder);
}

static inline struct ingenic_drm *drm_plane_get_priv(struct drm_plane *plane)
{
	return container_of(plane, struct ingenic_drm, primary);
}

static void ingenic_drm_crtc_atomic_enable(struct drm_crtc *crtc,
					   struct drm_crtc_state *state)
{
	struct ingenic_drm *priv = drm_crtc_get_priv(crtc);

	regmap_write(priv->map, JZ_REG_LCD_STATE, 0);

	regmap_update_bits(priv->map, JZ_REG_LCD_CTRL,
			   JZ_LCD_CTRL_ENABLE | JZ_LCD_CTRL_DISABLE,
			   JZ_LCD_CTRL_ENABLE);

	drm_crtc_vblank_on(crtc);
}

static void ingenic_drm_crtc_atomic_disable(struct drm_crtc *crtc,
					    struct drm_crtc_state *state)
{
	struct ingenic_drm *priv = drm_crtc_get_priv(crtc);
	unsigned int var;

	drm_crtc_vblank_off(crtc);

	regmap_update_bits(priv->map, JZ_REG_LCD_CTRL,
			   JZ_LCD_CTRL_DISABLE, JZ_LCD_CTRL_DISABLE);

	regmap_read_poll_timeout(priv->map, JZ_REG_LCD_STATE, var,
				 var & JZ_LCD_STATE_DISABLED,
				 1000, 0);
}

static void ingenic_drm_crtc_update_timings(struct ingenic_drm *priv,
					    struct drm_display_mode *mode)
{
	unsigned int vpe, vds, vde, vt, hpe, hds, hde, ht;

	vpe = mode->vsync_end - mode->vsync_start;
	vds = mode->vtotal - mode->vsync_start;
	vde = vds + mode->vdisplay;
	vt = vde + mode->vsync_start - mode->vdisplay;

	hpe = mode->hsync_end - mode->hsync_start;
	hds = mode->htotal - mode->hsync_start;
	hde = hds + mode->hdisplay;
	ht = hde + mode->hsync_start - mode->hdisplay;

	regmap_write(priv->map, JZ_REG_LCD_VSYNC,
		     0 << JZ_LCD_VSYNC_VPS_OFFSET |
		     vpe << JZ_LCD_VSYNC_VPE_OFFSET);

	regmap_write(priv->map, JZ_REG_LCD_HSYNC,
		     0 << JZ_LCD_HSYNC_HPS_OFFSET |
		     hpe << JZ_LCD_HSYNC_HPE_OFFSET);

	regmap_write(priv->map, JZ_REG_LCD_VAT,
		     ht << JZ_LCD_VAT_HT_OFFSET |
		     vt << JZ_LCD_VAT_VT_OFFSET);

	regmap_write(priv->map, JZ_REG_LCD_DAH,
		     hds << JZ_LCD_DAH_HDS_OFFSET |
		     hde << JZ_LCD_DAH_HDE_OFFSET);
	regmap_write(priv->map, JZ_REG_LCD_DAV,
		     vds << JZ_LCD_DAV_VDS_OFFSET |
		     vde << JZ_LCD_DAV_VDE_OFFSET);

	if (priv->panel_is_sharp) {
		regmap_write(priv->map, JZ_REG_LCD_PS, hde << 16 | (hde + 1));
		regmap_write(priv->map, JZ_REG_LCD_CLS, hde << 16 | (hde + 1));
		regmap_write(priv->map, JZ_REG_LCD_SPL, hpe << 16 | (hpe + 1));
		regmap_write(priv->map, JZ_REG_LCD_REV, mode->htotal << 16);
	}
}

static void ingenic_drm_crtc_update_ctrl(struct ingenic_drm *priv,
					 const struct drm_format_info *finfo)
{
	unsigned int ctrl = JZ_LCD_CTRL_OFUP | JZ_LCD_CTRL_BURST_16;

	switch (finfo->format) {
	case DRM_FORMAT_XRGB1555:
		ctrl |= JZ_LCD_CTRL_RGB555;
		/* fall-through */
	case DRM_FORMAT_RGB565:
		ctrl |= JZ_LCD_CTRL_BPP_15_16;
		break;
	case DRM_FORMAT_XRGB8888:
		ctrl |= JZ_LCD_CTRL_BPP_18_24;
		break;
	}

	regmap_update_bits(priv->map, JZ_REG_LCD_CTRL,
			   JZ_LCD_CTRL_OFUP | JZ_LCD_CTRL_BURST_16 |
			   JZ_LCD_CTRL_BPP_MASK, ctrl);
}

static int ingenic_drm_crtc_atomic_check(struct drm_crtc *crtc,
					 struct drm_crtc_state *state)
{
	struct ingenic_drm *priv = drm_crtc_get_priv(crtc);
	long rate;

	if (!drm_atomic_crtc_needs_modeset(state))
		return 0;

	if (state->mode.hdisplay > priv->soc_info->max_width ||
	    state->mode.vdisplay > priv->soc_info->max_height)
		return -EINVAL;

	rate = clk_round_rate(priv->pix_clk,
			      state->adjusted_mode.clock * 1000);
	if (rate < 0)
		return rate;

	return 0;
}

static void ingenic_drm_crtc_atomic_flush(struct drm_crtc *crtc,
					  struct drm_crtc_state *oldstate)
{
	struct ingenic_drm *priv = drm_crtc_get_priv(crtc);
	struct drm_crtc_state *state = crtc->state;
	struct drm_pending_vblank_event *event = state->event;
	struct drm_framebuffer *drm_fb = crtc->primary->state->fb;
	const struct drm_format_info *finfo;

	if (drm_atomic_crtc_needs_modeset(state)) {
		finfo = drm_format_info(drm_fb->format->format);

		ingenic_drm_crtc_update_timings(priv, &state->mode);
		ingenic_drm_crtc_update_ctrl(priv, finfo);

		clk_set_rate(priv->pix_clk, state->adjusted_mode.clock * 1000);

		regmap_write(priv->map, JZ_REG_LCD_DA0, priv->dma_hwdesc->next);
	}

	if (event) {
		state->event = NULL;

		spin_lock_irq(&crtc->dev->event_lock);
		if (drm_crtc_vblank_get(crtc) == 0)
			drm_crtc_arm_vblank_event(crtc, event);
		else
			drm_crtc_send_vblank_event(crtc, event);
		spin_unlock_irq(&crtc->dev->event_lock);
	}
}

static void ingenic_drm_plane_atomic_update(struct drm_plane *plane,
					    struct drm_plane_state *oldstate)
{
	struct ingenic_drm *priv = drm_plane_get_priv(plane);
	struct drm_plane_state *state = plane->state;
	unsigned int width, height, cpp;
	dma_addr_t addr;

	if (state && state->fb) {
		addr = drm_fb_cma_get_gem_addr(state->fb, state, 0);
		width = state->src_w >> 16;
		height = state->src_h >> 16;
		cpp = state->fb->format->cpp[plane->index];

		priv->dma_hwdesc->addr = addr;
		priv->dma_hwdesc->cmd = width * height * cpp / 4;
		priv->dma_hwdesc->cmd |= JZ_LCD_CMD_EOF_IRQ;
	}
}

static void ingenic_drm_encoder_atomic_mode_set(struct drm_encoder *encoder,
						struct drm_crtc_state *crtc_state,
						struct drm_connector_state *conn_state)
{
	struct ingenic_drm *priv = drm_encoder_get_priv(encoder);
	struct drm_display_mode *mode = &crtc_state->adjusted_mode;
	struct drm_connector *conn = conn_state->connector;
	struct drm_display_info *info = &conn->display_info;
	unsigned int cfg;

	priv->panel_is_sharp = info->bus_flags & DRM_BUS_FLAG_SHARP_SIGNALS;

	if (priv->panel_is_sharp) {
		cfg = JZ_LCD_CFG_MODE_SPECIAL_TFT_1 | JZ_LCD_CFG_REV_POLARITY;
	} else {
		cfg = JZ_LCD_CFG_PS_DISABLE | JZ_LCD_CFG_CLS_DISABLE
		    | JZ_LCD_CFG_SPL_DISABLE | JZ_LCD_CFG_REV_DISABLE;
	}

	if (mode->flags & DRM_MODE_FLAG_NHSYNC)
		cfg |= JZ_LCD_CFG_HSYNC_ACTIVE_LOW;
	if (mode->flags & DRM_MODE_FLAG_NVSYNC)
		cfg |= JZ_LCD_CFG_VSYNC_ACTIVE_LOW;
	if (info->bus_flags & DRM_BUS_FLAG_DE_LOW)
		cfg |= JZ_LCD_CFG_DE_ACTIVE_LOW;
	if (info->bus_flags & DRM_BUS_FLAG_PIXDATA_NEGEDGE)
		cfg |= JZ_LCD_CFG_PCLK_FALLING_EDGE;

	if (!priv->panel_is_sharp) {
		if (conn->connector_type == DRM_MODE_CONNECTOR_TV) {
			if (mode->flags & DRM_MODE_FLAG_INTERLACE)
				cfg |= JZ_LCD_CFG_MODE_TV_OUT_I;
			else
				cfg |= JZ_LCD_CFG_MODE_TV_OUT_P;
		} else {
			switch (*info->bus_formats) {
			case MEDIA_BUS_FMT_RGB565_1X16:
				cfg |= JZ_LCD_CFG_MODE_GENERIC_16BIT;
				break;
			case MEDIA_BUS_FMT_RGB666_1X18:
				cfg |= JZ_LCD_CFG_MODE_GENERIC_18BIT;
				break;
			case MEDIA_BUS_FMT_RGB888_1X24:
				cfg |= JZ_LCD_CFG_MODE_GENERIC_24BIT;
				break;
			case MEDIA_BUS_FMT_RGB888_3X8:
				cfg |= JZ_LCD_CFG_MODE_8BIT_SERIAL;
				break;
			default:
				break;
			}
		}
	}

	regmap_write(priv->map, JZ_REG_LCD_CFG, cfg);
}

static int ingenic_drm_encoder_atomic_check(struct drm_encoder *encoder,
					    struct drm_crtc_state *crtc_state,
					    struct drm_connector_state *conn_state)
{
	struct drm_display_info *info = &conn_state->connector->display_info;

	if (info->num_bus_formats != 1)
		return -EINVAL;

	if (conn_state->connector->connector_type == DRM_MODE_CONNECTOR_TV)
		return 0;

	switch (*info->bus_formats) {
	case MEDIA_BUS_FMT_RGB565_1X16:
	case MEDIA_BUS_FMT_RGB666_1X18:
	case MEDIA_BUS_FMT_RGB888_1X24:
	case MEDIA_BUS_FMT_RGB888_3X8:
		return 0;
	default:
		return -EINVAL;
	}
}

static irqreturn_t ingenic_drm_irq_handler(int irq, void *arg)
{
	struct ingenic_drm *priv = drm_device_get_priv(arg);
	unsigned int state;

	regmap_read(priv->map, JZ_REG_LCD_STATE, &state);

	regmap_update_bits(priv->map, JZ_REG_LCD_STATE,
			   JZ_LCD_STATE_EOF_IRQ, 0);

	if (state & JZ_LCD_STATE_EOF_IRQ)
		drm_crtc_handle_vblank(&priv->crtc);

	return IRQ_HANDLED;
}

static void ingenic_drm_release(struct drm_device *drm)
{
	struct ingenic_drm *priv = drm_device_get_priv(drm);

	drm_mode_config_cleanup(drm);
	drm_dev_fini(drm);
	kfree(priv);
}

static int ingenic_drm_enable_vblank(struct drm_crtc *crtc)
{
	struct ingenic_drm *priv = drm_crtc_get_priv(crtc);

	regmap_update_bits(priv->map, JZ_REG_LCD_CTRL,
			   JZ_LCD_CTRL_EOF_IRQ, JZ_LCD_CTRL_EOF_IRQ);

	return 0;
}

static void ingenic_drm_disable_vblank(struct drm_crtc *crtc)
{
	struct ingenic_drm *priv = drm_crtc_get_priv(crtc);

	regmap_update_bits(priv->map, JZ_REG_LCD_CTRL, JZ_LCD_CTRL_EOF_IRQ, 0);
}

DEFINE_DRM_GEM_CMA_FOPS(ingenic_drm_fops);

static struct drm_driver ingenic_drm_driver_data = {
	.driver_features	= DRIVER_MODESET | DRIVER_GEM | DRIVER_ATOMIC,
	.name			= "ingenic-drm",
	.desc			= "DRM module for Ingenic SoCs",
	.date			= "20190422",
	.major			= 1,
	.minor			= 0,
	.patchlevel		= 0,

	.fops			= &ingenic_drm_fops,

	.dumb_create		= drm_gem_cma_dumb_create,
	.gem_free_object_unlocked = drm_gem_cma_free_object,
	.gem_vm_ops		= &drm_gem_cma_vm_ops,

	.prime_handle_to_fd	= drm_gem_prime_handle_to_fd,
	.prime_fd_to_handle	= drm_gem_prime_fd_to_handle,
	.gem_prime_get_sg_table	= drm_gem_cma_prime_get_sg_table,
	.gem_prime_import_sg_table = drm_gem_cma_prime_import_sg_table,
	.gem_prime_vmap		= drm_gem_cma_prime_vmap,
	.gem_prime_vunmap	= drm_gem_cma_prime_vunmap,
	.gem_prime_mmap		= drm_gem_cma_prime_mmap,

	.irq_handler		= ingenic_drm_irq_handler,
	.release		= ingenic_drm_release,
};

static const struct drm_plane_funcs ingenic_drm_primary_plane_funcs = {
	.update_plane		= drm_atomic_helper_update_plane,
	.disable_plane		= drm_atomic_helper_disable_plane,
	.reset			= drm_atomic_helper_plane_reset,
	.destroy		= drm_plane_cleanup,

	.atomic_duplicate_state	= drm_atomic_helper_plane_duplicate_state,
	.atomic_destroy_state	= drm_atomic_helper_plane_destroy_state,
};

static const struct drm_crtc_funcs ingenic_drm_crtc_funcs = {
	.set_config		= drm_atomic_helper_set_config,
	.page_flip		= drm_atomic_helper_page_flip,
	.reset			= drm_atomic_helper_crtc_reset,
	.destroy		= drm_crtc_cleanup,

	.atomic_duplicate_state	= drm_atomic_helper_crtc_duplicate_state,
	.atomic_destroy_state	= drm_atomic_helper_crtc_destroy_state,

	.enable_vblank		= ingenic_drm_enable_vblank,
	.disable_vblank		= ingenic_drm_disable_vblank,

	.gamma_set		= drm_atomic_helper_legacy_gamma_set,
};

static const struct drm_plane_helper_funcs ingenic_drm_plane_helper_funcs = {
	.atomic_update		= ingenic_drm_plane_atomic_update,
	.prepare_fb		= drm_gem_fb_prepare_fb,
};

static const struct drm_crtc_helper_funcs ingenic_drm_crtc_helper_funcs = {
	.atomic_enable		= ingenic_drm_crtc_atomic_enable,
	.atomic_disable		= ingenic_drm_crtc_atomic_disable,
	.atomic_flush		= ingenic_drm_crtc_atomic_flush,
	.atomic_check		= ingenic_drm_crtc_atomic_check,
};

static const struct drm_encoder_helper_funcs ingenic_drm_encoder_helper_funcs = {
	.atomic_mode_set	= ingenic_drm_encoder_atomic_mode_set,
	.atomic_check		= ingenic_drm_encoder_atomic_check,
};

static const struct drm_mode_config_funcs ingenic_drm_mode_config_funcs = {
	.fb_create		= drm_gem_fb_create,
	.output_poll_changed	= drm_fb_helper_output_poll_changed,
	.atomic_check		= drm_atomic_helper_check,
	.atomic_commit		= drm_atomic_helper_commit,
};

static const struct drm_encoder_funcs ingenic_drm_encoder_funcs = {
	.destroy		= drm_encoder_cleanup,
};

static void ingenic_drm_free_dma_hwdesc(void *d)
{
	struct ingenic_drm *priv = d;

	dma_free_coherent(priv->dev, sizeof(*priv->dma_hwdesc),
			  priv->dma_hwdesc, priv->dma_hwdesc_phys);
}

static int ingenic_drm_probe(struct platform_device *pdev)
{
	const struct jz_soc_info *soc_info;
	struct device *dev = &pdev->dev;
	struct ingenic_drm *priv;
	struct clk *parent_clk;
	struct drm_bridge *bridge;
	struct drm_panel *panel;
	struct drm_device *drm;
	void __iomem *base;
	long parent_rate;
	int ret, irq;

	soc_info = of_device_get_match_data(dev);
	if (!soc_info) {
		dev_err(dev, "Missing platform data\n");
		return -EINVAL;
	}

	priv = kzalloc(sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	priv->soc_info = soc_info;
	priv->dev = dev;
	drm = &priv->drm;
	drm->dev_private = priv;

	platform_set_drvdata(pdev, priv);

	ret = devm_drm_dev_init(dev, drm, &ingenic_drm_driver_data);
	if (ret) {
		kfree(priv);
		return ret;
	}

	drm_mode_config_init(drm);
	drm->mode_config.min_width = 0;
	drm->mode_config.min_height = 0;
	drm->mode_config.max_width = soc_info->max_width;
	drm->mode_config.max_height = 4095;
	drm->mode_config.funcs = &ingenic_drm_mode_config_funcs;

	base = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(base)) {
		dev_err(dev, "Failed to get memory resource");
		return PTR_ERR(base);
	}

	priv->map = devm_regmap_init_mmio(dev, base,
					  &ingenic_drm_regmap_config);
	if (IS_ERR(priv->map)) {
		dev_err(dev, "Failed to create regmap");
		return PTR_ERR(priv->map);
	}

	irq = platform_get_irq(pdev, 0);
	if (irq < 0) {
		dev_err(dev, "Failed to get platform irq");
		return irq;
	}

	if (soc_info->needs_dev_clk) {
		priv->lcd_clk = devm_clk_get(dev, "lcd");
		if (IS_ERR(priv->lcd_clk)) {
			dev_err(dev, "Failed to get lcd clock");
			return PTR_ERR(priv->lcd_clk);
		}
	}

	priv->pix_clk = devm_clk_get(dev, "lcd_pclk");
	if (IS_ERR(priv->pix_clk)) {
		dev_err(dev, "Failed to get pixel clock");
		return PTR_ERR(priv->pix_clk);
	}

	ret = drm_of_find_panel_or_bridge(dev->of_node, 0, 0, &panel, &bridge);
	if (ret) {
		if (ret != -EPROBE_DEFER)
			dev_err(dev, "Failed to get panel handle");
		return ret;
	}

	if (panel)
		bridge = devm_drm_panel_bridge_add_typed(dev, panel,
							 DRM_MODE_CONNECTOR_DPI);

	priv->dma_hwdesc = dma_alloc_coherent(dev, sizeof(*priv->dma_hwdesc),
					      &priv->dma_hwdesc_phys,
					      GFP_KERNEL);
	if (!priv->dma_hwdesc)
		return -ENOMEM;

	ret = devm_add_action_or_reset(dev, ingenic_drm_free_dma_hwdesc, priv);
	if (ret)
		return ret;

	priv->dma_hwdesc->next = priv->dma_hwdesc_phys;
	priv->dma_hwdesc->id = 0xdeafbead;

	drm_plane_helper_add(&priv->primary, &ingenic_drm_plane_helper_funcs);

	ret = drm_universal_plane_init(drm, &priv->primary,
				       0, &ingenic_drm_primary_plane_funcs,
				       ingenic_drm_primary_formats,
				       ARRAY_SIZE(ingenic_drm_primary_formats),
				       NULL, DRM_PLANE_TYPE_PRIMARY, NULL);
	if (ret) {
		dev_err(dev, "Failed to register primary plane: %i", ret);
		return ret;
	}

	drm_crtc_helper_add(&priv->crtc, &ingenic_drm_crtc_helper_funcs);

	ret = drm_crtc_init_with_planes(drm, &priv->crtc, &priv->primary,
					NULL, &ingenic_drm_crtc_funcs, NULL);
	if (ret) {
		dev_err(dev, "Failed to init CRTC: %i", ret);
		return ret;
	}

	priv->encoder.possible_crtcs = 1;

	drm_encoder_helper_add(&priv->encoder,
			       &ingenic_drm_encoder_helper_funcs);

	ret = drm_encoder_init(drm, &priv->encoder, &ingenic_drm_encoder_funcs,
			       DRM_MODE_ENCODER_DPI, NULL);
	if (ret) {
		dev_err(dev, "Failed to init encoder: %i", ret);
		return ret;
	}

	ret = drm_bridge_attach(&priv->encoder, bridge, NULL, 0);
	if (ret) {
		dev_err(dev, "Unable to attach bridge");
		return ret;
	}

	ret = drm_irq_install(drm, irq);
	if (ret) {
		dev_err(dev, "Unable to install IRQ handler");
		return ret;
	}

	ret = drm_vblank_init(drm, 1);
	if (ret) {
		dev_err(dev, "Failed calling drm_vblank_init()");
		return ret;
	}

	drm_mode_config_reset(drm);

	ret = clk_prepare_enable(priv->pix_clk);
	if (ret) {
		dev_err(dev, "Unable to start pixel clock");
		return ret;
	}

	if (priv->lcd_clk) {
		parent_clk = clk_get_parent(priv->lcd_clk);
		parent_rate = clk_get_rate(parent_clk);

		/* LCD Device clock must be 3x the pixel clock for STN panels,
		 * or 1.5x the pixel clock for TFT panels. To avoid having to
		 * check for the LCD device clock everytime we do a mode change,
		 * we set the LCD device clock to the highest rate possible.
		 */
		ret = clk_set_rate(priv->lcd_clk, parent_rate);
		if (ret) {
			dev_err(dev, "Unable to set LCD clock rate");
			goto err_pixclk_disable;
		}

		ret = clk_prepare_enable(priv->lcd_clk);
		if (ret) {
			dev_err(dev, "Unable to start lcd clock");
			goto err_pixclk_disable;
		}
	}

	ret = drm_dev_register(drm, 0);
	if (ret) {
		dev_err(dev, "Failed to register DRM driver");
		goto err_devclk_disable;
	}

	ret = drm_fbdev_generic_setup(drm, 32);
	if (ret)
		dev_warn(dev, "Unable to start fbdev emulation: %i", ret);

	return 0;

err_devclk_disable:
	if (priv->lcd_clk)
		clk_disable_unprepare(priv->lcd_clk);
err_pixclk_disable:
	clk_disable_unprepare(priv->pix_clk);
	return ret;
}

static int ingenic_drm_remove(struct platform_device *pdev)
{
	struct ingenic_drm *priv = platform_get_drvdata(pdev);

	if (priv->lcd_clk)
		clk_disable_unprepare(priv->lcd_clk);
	clk_disable_unprepare(priv->pix_clk);

	drm_dev_unregister(&priv->drm);
	drm_atomic_helper_shutdown(&priv->drm);

	return 0;
}

static const struct jz_soc_info jz4740_soc_info = {
	.needs_dev_clk = true,
	.max_width = 800,
	.max_height = 600,
};

static const struct jz_soc_info jz4725b_soc_info = {
	.needs_dev_clk = false,
	.max_width = 800,
	.max_height = 600,
};

static const struct jz_soc_info jz4770_soc_info = {
	.needs_dev_clk = false,
	.max_width = 1280,
	.max_height = 720,
};

static const struct of_device_id ingenic_drm_of_match[] = {
	{ .compatible = "ingenic,jz4740-lcd", .data = &jz4740_soc_info },
	{ .compatible = "ingenic,jz4725b-lcd", .data = &jz4725b_soc_info },
	{ .compatible = "ingenic,jz4770-lcd", .data = &jz4770_soc_info },
	{ /* sentinel */ },
};
MODULE_DEVICE_TABLE(of, ingenic_drm_of_match);

static struct platform_driver ingenic_drm_driver = {
	.driver = {
		.name = "ingenic-drm",
		.of_match_table = of_match_ptr(ingenic_drm_of_match),
	},
	.probe = ingenic_drm_probe,
	.remove = ingenic_drm_remove,
};
module_platform_driver(ingenic_drm_driver);

MODULE_AUTHOR("Paul Cercueil <paul@crapouillou.net>");
MODULE_DESCRIPTION("DRM driver for the Ingenic SoCs\n");
MODULE_LICENSE("GPL v2");
