/*
 * Copyright (c) 2015, The Linux Foundation. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 and
 * only version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include "mdp5_kms.h"

#include "drm_crtc.h"
#include "drm_crtc_helper.h"

struct mdp5_cmd_encoder {
	struct drm_encoder base;
	struct mdp5_interface intf;
	bool enabled;
	uint32_t bsc;
};
#define to_mdp5_cmd_encoder(x) container_of(x, struct mdp5_cmd_encoder, base)

static struct mdp5_kms *get_kms(struct drm_encoder *encoder)
{
	struct msm_drm_private *priv = encoder->dev->dev_private;
	return to_mdp5_kms(to_mdp_kms(priv->kms));
}

#ifdef DOWNSTREAM_CONFIG_MSM_BUS_SCALING
#include <mach/board.h>
#include <linux/msm-bus.h>
#include <linux/msm-bus-board.h>
#define MDP_BUS_VECTOR_ENTRY(ab_val, ib_val)		\
	{						\
		.src = MSM_BUS_MASTER_MDP_PORT0,	\
		.dst = MSM_BUS_SLAVE_EBI_CH0,		\
		.ab = (ab_val),				\
		.ib = (ib_val),				\
	}

static struct msm_bus_vectors mdp_bus_vectors[] = {
	MDP_BUS_VECTOR_ENTRY(0, 0),
	MDP_BUS_VECTOR_ENTRY(2000000000, 2000000000),
};
static struct msm_bus_paths mdp_bus_usecases[] = { {
		.num_paths = 1,
		.vectors = &mdp_bus_vectors[0],
}, {
		.num_paths = 1,
		.vectors = &mdp_bus_vectors[1],
} };
static struct msm_bus_scale_pdata mdp_bus_scale_table = {
	.usecase = mdp_bus_usecases,
	.num_usecases = ARRAY_SIZE(mdp_bus_usecases),
	.name = "mdss_mdp",
};

static void bs_init(struct mdp5_cmd_encoder *mdp5_cmd_enc)
{
	mdp5_cmd_enc->bsc = msm_bus_scale_register_client(
			&mdp_bus_scale_table);
	DBG("bus scale client: %08x", mdp5_cmd_enc->bsc);
}

static void bs_fini(struct mdp5_cmd_encoder *mdp5_cmd_enc)
{
	if (mdp5_cmd_enc->bsc) {
		msm_bus_scale_unregister_client(mdp5_cmd_enc->bsc);
		mdp5_cmd_enc->bsc = 0;
	}
}

static void bs_set(struct mdp5_cmd_encoder *mdp5_cmd_enc, int idx)
{
	if (mdp5_cmd_enc->bsc) {
		DBG("set bus scaling: %d", idx);
		/* HACK: scaling down, and then immediately back up
		 * seems to leave things broken (underflow).. so
		 * never disable:
		 */
		idx = 1;
		msm_bus_scale_client_update_request(mdp5_cmd_enc->bsc, idx);
	}
}
#else
static void bs_init(struct mdp5_cmd_encoder *mdp5_cmd_enc) {}
static void bs_fini(struct mdp5_cmd_encoder *mdp5_cmd_enc) {}
static void bs_set(struct mdp5_cmd_encoder *mdp5_cmd_enc, int idx) {}
#endif

#define VSYNC_CLK_RATE 19200000
static int pingpong_tearcheck_setup(struct drm_encoder *encoder,
					struct drm_display_mode *mode)
{
	struct mdp5_kms *mdp5_kms = get_kms(encoder);
	struct device *dev = encoder->dev->dev;
	u32 total_lines_x100, vclks_line, cfg;
	long vsync_clk_speed;
	int pp_id = GET_PING_PONG_ID(mdp5_crtc_get_lm(encoder->crtc));

	if (IS_ERR_OR_NULL(mdp5_kms->vsync_clk)) {
		dev_err(dev, "vsync_clk is not initialized\n");
		return -EINVAL;
	}

	total_lines_x100 = mode->vtotal * mode->vrefresh;
	if (!total_lines_x100) {
		dev_err(dev, "%s: vtotal(%d) or vrefresh(%d) is 0\n",
				__func__, mode->vtotal, mode->vrefresh);
		return -EINVAL;
	}

	vsync_clk_speed = clk_round_rate(mdp5_kms->vsync_clk, VSYNC_CLK_RATE);
	if (vsync_clk_speed <= 0) {
		dev_err(dev, "vsync_clk round rate failed %ld\n",
							vsync_clk_speed);
		return -EINVAL;
	}
	vclks_line = vsync_clk_speed * 100 / total_lines_x100;

	cfg = MDP5_PP_SYNC_CONFIG_VSYNC_COUNTER_EN
		| MDP5_PP_SYNC_CONFIG_VSYNC_IN_EN;
	cfg |= MDP5_PP_SYNC_CONFIG_VSYNC_COUNT(vclks_line);

	mdp5_write(mdp5_kms, REG_MDP5_PP_SYNC_CONFIG_VSYNC(pp_id), cfg);
	mdp5_write(mdp5_kms,
		REG_MDP5_PP_SYNC_CONFIG_HEIGHT(pp_id), 0xfff0);
	mdp5_write(mdp5_kms,
		REG_MDP5_PP_VSYNC_INIT_VAL(pp_id), mode->vdisplay);
	mdp5_write(mdp5_kms, REG_MDP5_PP_RD_PTR_IRQ(pp_id), mode->vdisplay + 1);
	mdp5_write(mdp5_kms, REG_MDP5_PP_START_POS(pp_id), mode->vdisplay);
	mdp5_write(mdp5_kms, REG_MDP5_PP_SYNC_THRESH(pp_id),
			MDP5_PP_SYNC_THRESH_START(4) |
			MDP5_PP_SYNC_THRESH_CONTINUE(4));

	return 0;
}

static int pingpong_tearcheck_enable(struct drm_encoder *encoder)
{
	struct mdp5_kms *mdp5_kms = get_kms(encoder);
	int pp_id = GET_PING_PONG_ID(mdp5_crtc_get_lm(encoder->crtc));
	int ret;

	ret = clk_set_rate(mdp5_kms->vsync_clk,
		clk_round_rate(mdp5_kms->vsync_clk, VSYNC_CLK_RATE));
	if (ret) {
		dev_err(encoder->dev->dev,
			"vsync_clk clk_set_rate failed, %d\n", ret);
		return ret;
	}
	ret = clk_prepare_enable(mdp5_kms->vsync_clk);
	if (ret) {
		dev_err(encoder->dev->dev,
			"vsync_clk clk_prepare_enable failed, %d\n", ret);
		return ret;
	}

	mdp5_write(mdp5_kms, REG_MDP5_PP_TEAR_CHECK_EN(pp_id), 1);

	return 0;
}

static void pingpong_tearcheck_disable(struct drm_encoder *encoder)
{
	struct mdp5_kms *mdp5_kms = get_kms(encoder);
	int pp_id = GET_PING_PONG_ID(mdp5_crtc_get_lm(encoder->crtc));

	mdp5_write(mdp5_kms, REG_MDP5_PP_TEAR_CHECK_EN(pp_id), 0);
	clk_disable_unprepare(mdp5_kms->vsync_clk);
}

static void mdp5_cmd_encoder_destroy(struct drm_encoder *encoder)
{
	struct mdp5_cmd_encoder *mdp5_cmd_enc = to_mdp5_cmd_encoder(encoder);
	bs_fini(mdp5_cmd_enc);
	drm_encoder_cleanup(encoder);
	kfree(mdp5_cmd_enc);
}

static const struct drm_encoder_funcs mdp5_cmd_encoder_funcs = {
	.destroy = mdp5_cmd_encoder_destroy,
};

static bool mdp5_cmd_encoder_mode_fixup(struct drm_encoder *encoder,
		const struct drm_display_mode *mode,
		struct drm_display_mode *adjusted_mode)
{
	return true;
}

static void mdp5_cmd_encoder_mode_set(struct drm_encoder *encoder,
		struct drm_display_mode *mode,
		struct drm_display_mode *adjusted_mode)
{
	struct mdp5_cmd_encoder *mdp5_cmd_enc = to_mdp5_cmd_encoder(encoder);

	mode = adjusted_mode;

	DBG("set mode: %d:\"%s\" %d %d %d %d %d %d %d %d %d %d 0x%x 0x%x",
			mode->base.id, mode->name,
			mode->vrefresh, mode->clock,
			mode->hdisplay, mode->hsync_start,
			mode->hsync_end, mode->htotal,
			mode->vdisplay, mode->vsync_start,
			mode->vsync_end, mode->vtotal,
			mode->type, mode->flags);
	pingpong_tearcheck_setup(encoder, mode);
	mdp5_crtc_set_intf(encoder->crtc, &mdp5_cmd_enc->intf);
}

static void mdp5_cmd_encoder_disable(struct drm_encoder *encoder)
{
	struct mdp5_cmd_encoder *mdp5_cmd_enc = to_mdp5_cmd_encoder(encoder);
	struct mdp5_ctl *ctl = mdp5_crtc_get_ctl(encoder->crtc);
	struct mdp5_interface *intf = &mdp5_cmd_enc->intf;

	if (WARN_ON(!mdp5_cmd_enc->enabled))
		return;

	pingpong_tearcheck_disable(encoder);

	mdp5_ctl_set_encoder_state(ctl, false);
	mdp5_ctl_commit(ctl, mdp_ctl_flush_mask_encoder(intf));

	bs_set(mdp5_cmd_enc, 0);

	mdp5_cmd_enc->enabled = false;
}

static void mdp5_cmd_encoder_enable(struct drm_encoder *encoder)
{
	struct mdp5_cmd_encoder *mdp5_cmd_enc = to_mdp5_cmd_encoder(encoder);
	struct mdp5_ctl *ctl = mdp5_crtc_get_ctl(encoder->crtc);
	struct mdp5_interface *intf = &mdp5_cmd_enc->intf;

	if (WARN_ON(mdp5_cmd_enc->enabled))
		return;

	bs_set(mdp5_cmd_enc, 1);
	if (pingpong_tearcheck_enable(encoder))
		return;

	mdp5_ctl_commit(ctl, mdp_ctl_flush_mask_encoder(intf));

	mdp5_ctl_set_encoder_state(ctl, true);

	mdp5_cmd_enc->enabled = true;
}

static const struct drm_encoder_helper_funcs mdp5_cmd_encoder_helper_funcs = {
	.mode_fixup = mdp5_cmd_encoder_mode_fixup,
	.mode_set = mdp5_cmd_encoder_mode_set,
	.disable = mdp5_cmd_encoder_disable,
	.enable = mdp5_cmd_encoder_enable,
};

int mdp5_cmd_encoder_set_split_display(struct drm_encoder *encoder,
					struct drm_encoder *slave_encoder)
{
	struct mdp5_cmd_encoder *mdp5_cmd_enc = to_mdp5_cmd_encoder(encoder);
	struct mdp5_kms *mdp5_kms;
	int intf_num;
	u32 data = 0;

	if (!encoder || !slave_encoder)
		return -EINVAL;

	mdp5_kms = get_kms(encoder);
	intf_num = mdp5_cmd_enc->intf.num;

	/* Switch slave encoder's trigger MUX, to use the master's
	 * start signal for the slave encoder
	 */
	if (intf_num == 1)
		data |= MDP5_MDP_SPLIT_DPL_UPPER_INTF2_SW_TRG_MUX;
	else if (intf_num == 2)
		data |= MDP5_MDP_SPLIT_DPL_UPPER_INTF1_SW_TRG_MUX;
	else
		return -EINVAL;

	/* Smart Panel, Sync mode */
	data |= MDP5_MDP_SPLIT_DPL_UPPER_SMART_PANEL;

	/* Make sure clocks are on when connectors calling this function. */
	mdp5_enable(mdp5_kms);
	mdp5_write(mdp5_kms, REG_MDP5_MDP_SPLIT_DPL_UPPER(0), data);

	mdp5_write(mdp5_kms, REG_MDP5_MDP_SPLIT_DPL_LOWER(0),
			MDP5_MDP_SPLIT_DPL_LOWER_SMART_PANEL);
	mdp5_write(mdp5_kms, REG_MDP5_MDP_SPLIT_DPL_EN(0), 1);
	mdp5_disable(mdp5_kms);

	return 0;
}

/* initialize command mode encoder */
struct drm_encoder *mdp5_cmd_encoder_init(struct drm_device *dev,
				struct mdp5_interface *intf)
{
	struct drm_encoder *encoder = NULL;
	struct mdp5_cmd_encoder *mdp5_cmd_enc;
	int ret;

	if (WARN_ON((intf->type != INTF_DSI) &&
		(intf->mode != MDP5_INTF_DSI_MODE_COMMAND))) {
		ret = -EINVAL;
		goto fail;
	}

	mdp5_cmd_enc = kzalloc(sizeof(*mdp5_cmd_enc), GFP_KERNEL);
	if (!mdp5_cmd_enc) {
		ret = -ENOMEM;
		goto fail;
	}

	memcpy(&mdp5_cmd_enc->intf, intf, sizeof(mdp5_cmd_enc->intf));
	encoder = &mdp5_cmd_enc->base;

	drm_encoder_init(dev, encoder, &mdp5_cmd_encoder_funcs,
			DRM_MODE_ENCODER_DSI);

	drm_encoder_helper_add(encoder, &mdp5_cmd_encoder_helper_funcs);

	bs_init(mdp5_cmd_enc);

	return encoder;

fail:
	if (encoder)
		mdp5_cmd_encoder_destroy(encoder);

	return ERR_PTR(ret);
}

