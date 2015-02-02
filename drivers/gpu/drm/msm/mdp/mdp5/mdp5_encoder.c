/*
 * Copyright (c) 2014, The Linux Foundation. All rights reserved.
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

#include "mdp5_kms.h"

#include "drm_crtc.h"
#include "drm_crtc_helper.h"

struct mdp5_encoder {
	struct drm_encoder base;
	int intf;
	enum mdp5_intf intf_id;
	spinlock_t intf_lock;	/* protect REG_MDP5_INTF_* registers */
	bool enabled;
	uint32_t bsc;
};
#define to_mdp5_encoder(x) container_of(x, struct mdp5_encoder, base)

static struct mdp5_kms *get_kms(struct drm_encoder *encoder)
{
	struct msm_drm_private *priv = encoder->dev->dev_private;
	return to_mdp5_kms(to_mdp_kms(priv->kms));
}

#ifdef CONFIG_MSM_BUS_SCALING
#include <mach/board.h>
#include <mach/msm_bus.h>
#include <mach/msm_bus_board.h>
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

static void bs_init(struct mdp5_encoder *mdp5_encoder)
{
	mdp5_encoder->bsc = msm_bus_scale_register_client(
			&mdp_bus_scale_table);
	DBG("bus scale client: %08x", mdp5_encoder->bsc);
}

static void bs_fini(struct mdp5_encoder *mdp5_encoder)
{
	if (mdp5_encoder->bsc) {
		msm_bus_scale_unregister_client(mdp5_encoder->bsc);
		mdp5_encoder->bsc = 0;
	}
}

static void bs_set(struct mdp5_encoder *mdp5_encoder, int idx)
{
	if (mdp5_encoder->bsc) {
		DBG("set bus scaling: %d", idx);
		/* HACK: scaling down, and then immediately back up
		 * seems to leave things broken (underflow).. so
		 * never disable:
		 */
		idx = 1;
		msm_bus_scale_client_update_request(mdp5_encoder->bsc, idx);
	}
}
#else
static void bs_init(struct mdp5_encoder *mdp5_encoder) {}
static void bs_fini(struct mdp5_encoder *mdp5_encoder) {}
static void bs_set(struct mdp5_encoder *mdp5_encoder, int idx) {}
#endif

static void mdp5_encoder_destroy(struct drm_encoder *encoder)
{
	struct mdp5_encoder *mdp5_encoder = to_mdp5_encoder(encoder);
	bs_fini(mdp5_encoder);
	drm_encoder_cleanup(encoder);
	kfree(mdp5_encoder);
}

static const struct drm_encoder_funcs mdp5_encoder_funcs = {
	.destroy = mdp5_encoder_destroy,
};

static bool mdp5_encoder_mode_fixup(struct drm_encoder *encoder,
		const struct drm_display_mode *mode,
		struct drm_display_mode *adjusted_mode)
{
	return true;
}

static void mdp5_encoder_mode_set(struct drm_encoder *encoder,
		struct drm_display_mode *mode,
		struct drm_display_mode *adjusted_mode)
{
	struct mdp5_encoder *mdp5_encoder = to_mdp5_encoder(encoder);
	struct mdp5_kms *mdp5_kms = get_kms(encoder);
	struct drm_device *dev = encoder->dev;
	struct drm_connector *connector;
	int intf = mdp5_encoder->intf;
	uint32_t dtv_hsync_skew, vsync_period, vsync_len, ctrl_pol;
	uint32_t display_v_start, display_v_end;
	uint32_t hsync_start_x, hsync_end_x;
	uint32_t format = 0x2100;
	unsigned long flags;

	mode = adjusted_mode;

	DBG("set mode: %d:\"%s\" %d %d %d %d %d %d %d %d %d %d 0x%x 0x%x",
			mode->base.id, mode->name,
			mode->vrefresh, mode->clock,
			mode->hdisplay, mode->hsync_start,
			mode->hsync_end, mode->htotal,
			mode->vdisplay, mode->vsync_start,
			mode->vsync_end, mode->vtotal,
			mode->type, mode->flags);

	ctrl_pol = 0;
	if (mode->flags & DRM_MODE_FLAG_NHSYNC)
		ctrl_pol |= MDP5_INTF_POLARITY_CTL_HSYNC_LOW;
	if (mode->flags & DRM_MODE_FLAG_NVSYNC)
		ctrl_pol |= MDP5_INTF_POLARITY_CTL_VSYNC_LOW;
	/* probably need to get DATA_EN polarity from panel.. */

	dtv_hsync_skew = 0;  /* get this from panel? */

	/* Get color format from panel, default is 8bpc */
	list_for_each_entry(connector, &dev->mode_config.connector_list, head) {
		if (connector->encoder == encoder) {
			switch (connector->display_info.bpc) {
			case 4:
				format |= 0;
				break;
			case 5:
				format |= 0x15;
				break;
			case 6:
				format |= 0x2A;
				break;
			case 8:
			default:
				format |= 0x3F;
				break;
			}
			break;
		}
	}

	hsync_start_x = (mode->htotal - mode->hsync_start);
	hsync_end_x = mode->htotal - (mode->hsync_start - mode->hdisplay) - 1;

	vsync_period = mode->vtotal * mode->htotal;
	vsync_len = (mode->vsync_end - mode->vsync_start) * mode->htotal;
	display_v_start = (mode->vtotal - mode->vsync_start) * mode->htotal + dtv_hsync_skew;
	display_v_end = vsync_period - ((mode->vsync_start - mode->vdisplay) * mode->htotal) + dtv_hsync_skew - 1;

	/*
	 * For edp only:
	 * DISPLAY_V_START = (VBP * HCYCLE) + HBP
	 * DISPLAY_V_END = (VBP + VACTIVE) * HCYCLE - 1 - HFP
	 */
	if (mdp5_encoder->intf_id == INTF_eDP) {
		display_v_start += mode->htotal - mode->hsync_start;
		display_v_end -= mode->hsync_start - mode->hdisplay;
	}

	spin_lock_irqsave(&mdp5_encoder->intf_lock, flags);

	mdp5_write(mdp5_kms, REG_MDP5_INTF_HSYNC_CTL(intf),
			MDP5_INTF_HSYNC_CTL_PULSEW(mode->hsync_end - mode->hsync_start) |
			MDP5_INTF_HSYNC_CTL_PERIOD(mode->htotal));
	mdp5_write(mdp5_kms, REG_MDP5_INTF_VSYNC_PERIOD_F0(intf), vsync_period);
	mdp5_write(mdp5_kms, REG_MDP5_INTF_VSYNC_LEN_F0(intf), vsync_len);
	mdp5_write(mdp5_kms, REG_MDP5_INTF_DISPLAY_HCTL(intf),
			MDP5_INTF_DISPLAY_HCTL_START(hsync_start_x) |
			MDP5_INTF_DISPLAY_HCTL_END(hsync_end_x));
	mdp5_write(mdp5_kms, REG_MDP5_INTF_DISPLAY_VSTART_F0(intf), display_v_start);
	mdp5_write(mdp5_kms, REG_MDP5_INTF_DISPLAY_VEND_F0(intf), display_v_end);
	mdp5_write(mdp5_kms, REG_MDP5_INTF_BORDER_COLOR(intf), 0);
	mdp5_write(mdp5_kms, REG_MDP5_INTF_UNDERFLOW_COLOR(intf), 0xff);
	mdp5_write(mdp5_kms, REG_MDP5_INTF_HSYNC_SKEW(intf), dtv_hsync_skew);
	mdp5_write(mdp5_kms, REG_MDP5_INTF_POLARITY_CTL(intf), ctrl_pol);
	mdp5_write(mdp5_kms, REG_MDP5_INTF_ACTIVE_HCTL(intf),
			MDP5_INTF_ACTIVE_HCTL_START(0) |
			MDP5_INTF_ACTIVE_HCTL_END(0));
	mdp5_write(mdp5_kms, REG_MDP5_INTF_ACTIVE_VSTART_F0(intf), 0);
	mdp5_write(mdp5_kms, REG_MDP5_INTF_ACTIVE_VEND_F0(intf), 0);
	mdp5_write(mdp5_kms, REG_MDP5_INTF_PANEL_FORMAT(intf), format);
	mdp5_write(mdp5_kms, REG_MDP5_INTF_FRAME_LINE_COUNT_EN(intf), 0x3);  /* frame+line? */

	spin_unlock_irqrestore(&mdp5_encoder->intf_lock, flags);
}

static void mdp5_encoder_disable(struct drm_encoder *encoder)
{
	struct mdp5_encoder *mdp5_encoder = to_mdp5_encoder(encoder);
	struct mdp5_kms *mdp5_kms = get_kms(encoder);
	int intf = mdp5_encoder->intf;
	unsigned long flags;

	if (WARN_ON(!mdp5_encoder->enabled))
		return;

	spin_lock_irqsave(&mdp5_encoder->intf_lock, flags);
	mdp5_write(mdp5_kms, REG_MDP5_INTF_TIMING_ENGINE_EN(intf), 0);
	spin_unlock_irqrestore(&mdp5_encoder->intf_lock, flags);

	/*
	 * Wait for a vsync so we know the ENABLE=0 latched before
	 * the (connector) source of the vsync's gets disabled,
	 * otherwise we end up in a funny state if we re-enable
	 * before the disable latches, which results that some of
	 * the settings changes for the new modeset (like new
	 * scanout buffer) don't latch properly..
	 */
	mdp_irq_wait(&mdp5_kms->base, intf2vblank(intf));

	bs_set(mdp5_encoder, 0);

	mdp5_encoder->enabled = false;
}

static void mdp5_encoder_enable(struct drm_encoder *encoder)
{
	struct mdp5_encoder *mdp5_encoder = to_mdp5_encoder(encoder);
	struct mdp5_kms *mdp5_kms = get_kms(encoder);
	int intf = mdp5_encoder->intf;
	unsigned long flags;

	if (WARN_ON(mdp5_encoder->enabled))
		return;

	mdp5_crtc_set_intf(encoder->crtc, mdp5_encoder->intf,
			mdp5_encoder->intf_id);

	bs_set(mdp5_encoder, 1);
	spin_lock_irqsave(&mdp5_encoder->intf_lock, flags);
	mdp5_write(mdp5_kms, REG_MDP5_INTF_TIMING_ENGINE_EN(intf), 1);
	spin_unlock_irqrestore(&mdp5_encoder->intf_lock, flags);

	mdp5_encoder->enabled = false;
}

static const struct drm_encoder_helper_funcs mdp5_encoder_helper_funcs = {
	.mode_fixup = mdp5_encoder_mode_fixup,
	.mode_set = mdp5_encoder_mode_set,
	.prepare = mdp5_encoder_disable,
	.commit = mdp5_encoder_enable,
};

/* initialize encoder */
struct drm_encoder *mdp5_encoder_init(struct drm_device *dev, int intf,
		enum mdp5_intf intf_id)
{
	struct drm_encoder *encoder = NULL;
	struct mdp5_encoder *mdp5_encoder;
	int ret;

	mdp5_encoder = kzalloc(sizeof(*mdp5_encoder), GFP_KERNEL);
	if (!mdp5_encoder) {
		ret = -ENOMEM;
		goto fail;
	}

	mdp5_encoder->intf = intf;
	mdp5_encoder->intf_id = intf_id;
	encoder = &mdp5_encoder->base;

	spin_lock_init(&mdp5_encoder->intf_lock);

	drm_encoder_init(dev, encoder, &mdp5_encoder_funcs,
			 DRM_MODE_ENCODER_TMDS);
	drm_encoder_helper_add(encoder, &mdp5_encoder_helper_funcs);

	bs_init(mdp5_encoder);

	return encoder;

fail:
	if (encoder)
		mdp5_encoder_destroy(encoder);

	return ERR_PTR(ret);
}
