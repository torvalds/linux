/*
 * Copyright (C) 2013 Red Hat
 * Author: Rob Clark <robdclark@gmail.com>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published by
 * the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <mach/clk.h>

#include "mdp4_kms.h"

#include "drm_crtc.h"
#include "drm_crtc_helper.h"


struct mdp4_dtv_encoder {
	struct drm_encoder base;
	struct clk *src_clk;
	struct clk *hdmi_clk;
	struct clk *mdp_clk;
	unsigned long int pixclock;
	bool enabled;
	uint32_t bsc;
};
#define to_mdp4_dtv_encoder(x) container_of(x, struct mdp4_dtv_encoder, base)

static struct mdp4_kms *get_kms(struct drm_encoder *encoder)
{
	struct msm_drm_private *priv = encoder->dev->dev_private;
	return to_mdp4_kms(priv->kms);
}

#ifdef CONFIG_MSM_BUS_SCALING
#include <mach/board.h>
/* not ironically named at all.. no, really.. */
static void bs_init(struct mdp4_dtv_encoder *mdp4_dtv_encoder)
{
	struct drm_device *dev = mdp4_dtv_encoder->base.dev;
	struct lcdc_platform_data *dtv_pdata = mdp4_find_pdata("dtv.0");

	if (!dtv_pdata) {
		dev_err(dev->dev, "could not find dtv pdata\n");
		return;
	}

	if (dtv_pdata->bus_scale_table) {
		mdp4_dtv_encoder->bsc = msm_bus_scale_register_client(
				dtv_pdata->bus_scale_table);
		DBG("bus scale client: %08x", mdp4_dtv_encoder->bsc);
		DBG("lcdc_power_save: %p", dtv_pdata->lcdc_power_save);
		if (dtv_pdata->lcdc_power_save)
			dtv_pdata->lcdc_power_save(1);
	}
}

static void bs_fini(struct mdp4_dtv_encoder *mdp4_dtv_encoder)
{
	if (mdp4_dtv_encoder->bsc) {
		msm_bus_scale_unregister_client(mdp4_dtv_encoder->bsc);
		mdp4_dtv_encoder->bsc = 0;
	}
}

static void bs_set(struct mdp4_dtv_encoder *mdp4_dtv_encoder, int idx)
{
	if (mdp4_dtv_encoder->bsc) {
		DBG("set bus scaling: %d", idx);
		msm_bus_scale_client_update_request(mdp4_dtv_encoder->bsc, idx);
	}
}
#else
static void bs_init(struct mdp4_dtv_encoder *mdp4_dtv_encoder) {}
static void bs_fini(struct mdp4_dtv_encoder *mdp4_dtv_encoder) {}
static void bs_set(struct mdp4_dtv_encoder *mdp4_dtv_encoder, int idx) {}
#endif

static void mdp4_dtv_encoder_destroy(struct drm_encoder *encoder)
{
	struct mdp4_dtv_encoder *mdp4_dtv_encoder = to_mdp4_dtv_encoder(encoder);
	bs_fini(mdp4_dtv_encoder);
	drm_encoder_cleanup(encoder);
	kfree(mdp4_dtv_encoder);
}

static const struct drm_encoder_funcs mdp4_dtv_encoder_funcs = {
	.destroy = mdp4_dtv_encoder_destroy,
};

static void mdp4_dtv_encoder_dpms(struct drm_encoder *encoder, int mode)
{
	struct drm_device *dev = encoder->dev;
	struct mdp4_dtv_encoder *mdp4_dtv_encoder = to_mdp4_dtv_encoder(encoder);
	struct mdp4_kms *mdp4_kms = get_kms(encoder);
	bool enabled = (mode == DRM_MODE_DPMS_ON);

	DBG("mode=%d", mode);

	if (enabled == mdp4_dtv_encoder->enabled)
		return;

	if (enabled) {
		unsigned long pc = mdp4_dtv_encoder->pixclock;
		int ret;

		bs_set(mdp4_dtv_encoder, 1);

		DBG("setting src_clk=%lu", pc);

		ret = clk_set_rate(mdp4_dtv_encoder->src_clk, pc);
		if (ret)
			dev_err(dev->dev, "failed to set src_clk to %lu: %d\n", pc, ret);
		clk_prepare_enable(mdp4_dtv_encoder->src_clk);
		ret = clk_prepare_enable(mdp4_dtv_encoder->hdmi_clk);
		if (ret)
			dev_err(dev->dev, "failed to enable hdmi_clk: %d\n", ret);
		ret = clk_prepare_enable(mdp4_dtv_encoder->mdp_clk);
		if (ret)
			dev_err(dev->dev, "failed to enabled mdp_clk: %d\n", ret);

		mdp4_write(mdp4_kms, REG_MDP4_DTV_ENABLE, 1);
	} else {
		mdp4_write(mdp4_kms, REG_MDP4_DTV_ENABLE, 0);

		/*
		 * Wait for a vsync so we know the ENABLE=0 latched before
		 * the (connector) source of the vsync's gets disabled,
		 * otherwise we end up in a funny state if we re-enable
		 * before the disable latches, which results that some of
		 * the settings changes for the new modeset (like new
		 * scanout buffer) don't latch properly..
		 */
		mdp4_irq_wait(mdp4_kms, MDP4_IRQ_EXTERNAL_VSYNC);

		clk_disable_unprepare(mdp4_dtv_encoder->src_clk);
		clk_disable_unprepare(mdp4_dtv_encoder->hdmi_clk);
		clk_disable_unprepare(mdp4_dtv_encoder->mdp_clk);

		bs_set(mdp4_dtv_encoder, 0);
	}

	mdp4_dtv_encoder->enabled = enabled;
}

static bool mdp4_dtv_encoder_mode_fixup(struct drm_encoder *encoder,
		const struct drm_display_mode *mode,
		struct drm_display_mode *adjusted_mode)
{
	return true;
}

static void mdp4_dtv_encoder_mode_set(struct drm_encoder *encoder,
		struct drm_display_mode *mode,
		struct drm_display_mode *adjusted_mode)
{
	struct mdp4_dtv_encoder *mdp4_dtv_encoder = to_mdp4_dtv_encoder(encoder);
	struct mdp4_kms *mdp4_kms = get_kms(encoder);
	uint32_t dtv_hsync_skew, vsync_period, vsync_len, ctrl_pol;
	uint32_t display_v_start, display_v_end;
	uint32_t hsync_start_x, hsync_end_x;

	mode = adjusted_mode;

	DBG("set mode: %d:\"%s\" %d %d %d %d %d %d %d %d %d %d 0x%x 0x%x",
			mode->base.id, mode->name,
			mode->vrefresh, mode->clock,
			mode->hdisplay, mode->hsync_start,
			mode->hsync_end, mode->htotal,
			mode->vdisplay, mode->vsync_start,
			mode->vsync_end, mode->vtotal,
			mode->type, mode->flags);

	mdp4_dtv_encoder->pixclock = mode->clock * 1000;

	DBG("pixclock=%lu", mdp4_dtv_encoder->pixclock);

	ctrl_pol = 0;
	if (mode->flags & DRM_MODE_FLAG_NHSYNC)
		ctrl_pol |= MDP4_DTV_CTRL_POLARITY_HSYNC_LOW;
	if (mode->flags & DRM_MODE_FLAG_NVSYNC)
		ctrl_pol |= MDP4_DTV_CTRL_POLARITY_VSYNC_LOW;
	/* probably need to get DATA_EN polarity from panel.. */

	dtv_hsync_skew = 0;  /* get this from panel? */

	hsync_start_x = (mode->htotal - mode->hsync_start);
	hsync_end_x = mode->htotal - (mode->hsync_start - mode->hdisplay) - 1;

	vsync_period = mode->vtotal * mode->htotal;
	vsync_len = (mode->vsync_end - mode->vsync_start) * mode->htotal;
	display_v_start = (mode->vtotal - mode->vsync_start) * mode->htotal + dtv_hsync_skew;
	display_v_end = vsync_period - ((mode->vsync_start - mode->vdisplay) * mode->htotal) + dtv_hsync_skew - 1;

	mdp4_write(mdp4_kms, REG_MDP4_DTV_HSYNC_CTRL,
			MDP4_DTV_HSYNC_CTRL_PULSEW(mode->hsync_end - mode->hsync_start) |
			MDP4_DTV_HSYNC_CTRL_PERIOD(mode->htotal));
	mdp4_write(mdp4_kms, REG_MDP4_DTV_VSYNC_PERIOD, vsync_period);
	mdp4_write(mdp4_kms, REG_MDP4_DTV_VSYNC_LEN, vsync_len);
	mdp4_write(mdp4_kms, REG_MDP4_DTV_DISPLAY_HCTRL,
			MDP4_DTV_DISPLAY_HCTRL_START(hsync_start_x) |
			MDP4_DTV_DISPLAY_HCTRL_END(hsync_end_x));
	mdp4_write(mdp4_kms, REG_MDP4_DTV_DISPLAY_VSTART, display_v_start);
	mdp4_write(mdp4_kms, REG_MDP4_DTV_DISPLAY_VEND, display_v_end);
	mdp4_write(mdp4_kms, REG_MDP4_DTV_BORDER_CLR, 0);
	mdp4_write(mdp4_kms, REG_MDP4_DTV_UNDERFLOW_CLR,
			MDP4_DTV_UNDERFLOW_CLR_ENABLE_RECOVERY |
			MDP4_DTV_UNDERFLOW_CLR_COLOR(0xff));
	mdp4_write(mdp4_kms, REG_MDP4_DTV_HSYNC_SKEW, dtv_hsync_skew);
	mdp4_write(mdp4_kms, REG_MDP4_DTV_CTRL_POLARITY, ctrl_pol);
	mdp4_write(mdp4_kms, REG_MDP4_DTV_ACTIVE_HCTL,
			MDP4_DTV_ACTIVE_HCTL_START(0) |
			MDP4_DTV_ACTIVE_HCTL_END(0));
	mdp4_write(mdp4_kms, REG_MDP4_DTV_ACTIVE_VSTART, 0);
	mdp4_write(mdp4_kms, REG_MDP4_DTV_ACTIVE_VEND, 0);
}

static void mdp4_dtv_encoder_prepare(struct drm_encoder *encoder)
{
	mdp4_dtv_encoder_dpms(encoder, DRM_MODE_DPMS_OFF);
}

static void mdp4_dtv_encoder_commit(struct drm_encoder *encoder)
{
	mdp4_crtc_set_config(encoder->crtc,
			MDP4_DMA_CONFIG_R_BPC(BPC8) |
			MDP4_DMA_CONFIG_G_BPC(BPC8) |
			MDP4_DMA_CONFIG_B_BPC(BPC8) |
			MDP4_DMA_CONFIG_PACK(0x21));
	mdp4_crtc_set_intf(encoder->crtc, INTF_LCDC_DTV);
	mdp4_dtv_encoder_dpms(encoder, DRM_MODE_DPMS_ON);
}

static const struct drm_encoder_helper_funcs mdp4_dtv_encoder_helper_funcs = {
	.dpms = mdp4_dtv_encoder_dpms,
	.mode_fixup = mdp4_dtv_encoder_mode_fixup,
	.mode_set = mdp4_dtv_encoder_mode_set,
	.prepare = mdp4_dtv_encoder_prepare,
	.commit = mdp4_dtv_encoder_commit,
};

long mdp4_dtv_round_pixclk(struct drm_encoder *encoder, unsigned long rate)
{
	struct mdp4_dtv_encoder *mdp4_dtv_encoder = to_mdp4_dtv_encoder(encoder);
	return clk_round_rate(mdp4_dtv_encoder->src_clk, rate);
}

/* initialize encoder */
struct drm_encoder *mdp4_dtv_encoder_init(struct drm_device *dev)
{
	struct drm_encoder *encoder = NULL;
	struct mdp4_dtv_encoder *mdp4_dtv_encoder;
	int ret;

	mdp4_dtv_encoder = kzalloc(sizeof(*mdp4_dtv_encoder), GFP_KERNEL);
	if (!mdp4_dtv_encoder) {
		ret = -ENOMEM;
		goto fail;
	}

	encoder = &mdp4_dtv_encoder->base;

	drm_encoder_init(dev, encoder, &mdp4_dtv_encoder_funcs,
			 DRM_MODE_ENCODER_TMDS);
	drm_encoder_helper_add(encoder, &mdp4_dtv_encoder_helper_funcs);

	mdp4_dtv_encoder->src_clk = devm_clk_get(dev->dev, "src_clk");
	if (IS_ERR(mdp4_dtv_encoder->src_clk)) {
		dev_err(dev->dev, "failed to get src_clk\n");
		ret = PTR_ERR(mdp4_dtv_encoder->src_clk);
		goto fail;
	}

	mdp4_dtv_encoder->hdmi_clk = devm_clk_get(dev->dev, "hdmi_clk");
	if (IS_ERR(mdp4_dtv_encoder->hdmi_clk)) {
		dev_err(dev->dev, "failed to get hdmi_clk\n");
		ret = PTR_ERR(mdp4_dtv_encoder->hdmi_clk);
		goto fail;
	}

	mdp4_dtv_encoder->mdp_clk = devm_clk_get(dev->dev, "mdp_clk");
	if (IS_ERR(mdp4_dtv_encoder->mdp_clk)) {
		dev_err(dev->dev, "failed to get mdp_clk\n");
		ret = PTR_ERR(mdp4_dtv_encoder->mdp_clk);
		goto fail;
	}

	bs_init(mdp4_dtv_encoder);

	return encoder;

fail:
	if (encoder)
		mdp4_dtv_encoder_destroy(encoder);

	return ERR_PTR(ret);
}
