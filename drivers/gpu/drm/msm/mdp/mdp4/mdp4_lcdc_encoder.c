/*
 * Copyright (C) 2014 Red Hat
 * Author: Rob Clark <robdclark@gmail.com>
 * Author: Vinay Simha <vinaysimha@inforcecomputing.com>
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

#include "mdp4_kms.h"

#include "drm_crtc.h"
#include "drm_crtc_helper.h"

struct mdp4_lcdc_encoder {
	struct drm_encoder base;
	struct device_node *panel_node;
	struct drm_panel *panel;
	struct clk *lcdc_clk;
	unsigned long int pixclock;
	struct regulator *regs[3];
	bool enabled;
	uint32_t bsc;
};
#define to_mdp4_lcdc_encoder(x) container_of(x, struct mdp4_lcdc_encoder, base)

static struct mdp4_kms *get_kms(struct drm_encoder *encoder)
{
	struct msm_drm_private *priv = encoder->dev->dev_private;
	return to_mdp4_kms(to_mdp_kms(priv->kms));
}

#ifdef DOWNSTREAM_CONFIG_MSM_BUS_SCALING
#include <mach/board.h>
static void bs_init(struct mdp4_lcdc_encoder *mdp4_lcdc_encoder)
{
	struct drm_device *dev = mdp4_lcdc_encoder->base.dev;
	struct lcdc_platform_data *lcdc_pdata = mdp4_find_pdata("lvds.0");

	if (!lcdc_pdata) {
		dev_err(dev->dev, "could not find lvds pdata\n");
		return;
	}

	if (lcdc_pdata->bus_scale_table) {
		mdp4_lcdc_encoder->bsc = msm_bus_scale_register_client(
				lcdc_pdata->bus_scale_table);
		DBG("lvds : bus scale client: %08x", mdp4_lcdc_encoder->bsc);
	}
}

static void bs_fini(struct mdp4_lcdc_encoder *mdp4_lcdc_encoder)
{
	if (mdp4_lcdc_encoder->bsc) {
		msm_bus_scale_unregister_client(mdp4_lcdc_encoder->bsc);
		mdp4_lcdc_encoder->bsc = 0;
	}
}

static void bs_set(struct mdp4_lcdc_encoder *mdp4_lcdc_encoder, int idx)
{
	if (mdp4_lcdc_encoder->bsc) {
		DBG("set bus scaling: %d", idx);
		msm_bus_scale_client_update_request(mdp4_lcdc_encoder->bsc, idx);
	}
}
#else
static void bs_init(struct mdp4_lcdc_encoder *mdp4_lcdc_encoder) {}
static void bs_fini(struct mdp4_lcdc_encoder *mdp4_lcdc_encoder) {}
static void bs_set(struct mdp4_lcdc_encoder *mdp4_lcdc_encoder, int idx) {}
#endif

static void mdp4_lcdc_encoder_destroy(struct drm_encoder *encoder)
{
	struct mdp4_lcdc_encoder *mdp4_lcdc_encoder =
			to_mdp4_lcdc_encoder(encoder);
	bs_fini(mdp4_lcdc_encoder);
	drm_encoder_cleanup(encoder);
	kfree(mdp4_lcdc_encoder);
}

static const struct drm_encoder_funcs mdp4_lcdc_encoder_funcs = {
	.destroy = mdp4_lcdc_encoder_destroy,
};

/* this should probably be a helper: */
struct drm_connector *get_connector(struct drm_encoder *encoder)
{
	struct drm_device *dev = encoder->dev;
	struct drm_connector *connector;

	list_for_each_entry(connector, &dev->mode_config.connector_list, head)
		if (connector->encoder == encoder)
			return connector;

	return NULL;
}

static void setup_phy(struct drm_encoder *encoder)
{
	struct drm_device *dev = encoder->dev;
	struct drm_connector *connector = get_connector(encoder);
	struct mdp4_kms *mdp4_kms = get_kms(encoder);
	uint32_t lvds_intf = 0, lvds_phy_cfg0 = 0;
	int bpp, nchan, swap;

	if (!connector)
		return;

	bpp = 3 * connector->display_info.bpc;

	if (!bpp)
		bpp = 18;

	/* TODO, these should come from panel somehow: */
	nchan = 1;
	swap = 0;

	switch (bpp) {
	case 24:
		mdp4_write(mdp4_kms, REG_MDP4_LCDC_LVDS_MUX_CTL_3_TO_0(0),
				MDP4_LCDC_LVDS_MUX_CTL_3_TO_0_BIT0(0x08) |
				MDP4_LCDC_LVDS_MUX_CTL_3_TO_0_BIT1(0x05) |
				MDP4_LCDC_LVDS_MUX_CTL_3_TO_0_BIT2(0x04) |
				MDP4_LCDC_LVDS_MUX_CTL_3_TO_0_BIT3(0x03));
		mdp4_write(mdp4_kms, REG_MDP4_LCDC_LVDS_MUX_CTL_6_TO_4(0),
				MDP4_LCDC_LVDS_MUX_CTL_6_TO_4_BIT4(0x02) |
				MDP4_LCDC_LVDS_MUX_CTL_6_TO_4_BIT5(0x01) |
				MDP4_LCDC_LVDS_MUX_CTL_6_TO_4_BIT6(0x00));
		mdp4_write(mdp4_kms, REG_MDP4_LCDC_LVDS_MUX_CTL_3_TO_0(1),
				MDP4_LCDC_LVDS_MUX_CTL_3_TO_0_BIT0(0x11) |
				MDP4_LCDC_LVDS_MUX_CTL_3_TO_0_BIT1(0x10) |
				MDP4_LCDC_LVDS_MUX_CTL_3_TO_0_BIT2(0x0d) |
				MDP4_LCDC_LVDS_MUX_CTL_3_TO_0_BIT3(0x0c));
		mdp4_write(mdp4_kms, REG_MDP4_LCDC_LVDS_MUX_CTL_6_TO_4(1),
				MDP4_LCDC_LVDS_MUX_CTL_6_TO_4_BIT4(0x0b) |
				MDP4_LCDC_LVDS_MUX_CTL_6_TO_4_BIT5(0x0a) |
				MDP4_LCDC_LVDS_MUX_CTL_6_TO_4_BIT6(0x09));
		mdp4_write(mdp4_kms, REG_MDP4_LCDC_LVDS_MUX_CTL_3_TO_0(2),
				MDP4_LCDC_LVDS_MUX_CTL_3_TO_0_BIT0(0x1a) |
				MDP4_LCDC_LVDS_MUX_CTL_3_TO_0_BIT1(0x19) |
				MDP4_LCDC_LVDS_MUX_CTL_3_TO_0_BIT2(0x18) |
				MDP4_LCDC_LVDS_MUX_CTL_3_TO_0_BIT3(0x15));
		mdp4_write(mdp4_kms, REG_MDP4_LCDC_LVDS_MUX_CTL_6_TO_4(2),
				MDP4_LCDC_LVDS_MUX_CTL_6_TO_4_BIT4(0x14) |
				MDP4_LCDC_LVDS_MUX_CTL_6_TO_4_BIT5(0x13) |
				MDP4_LCDC_LVDS_MUX_CTL_6_TO_4_BIT6(0x12));
		mdp4_write(mdp4_kms, REG_MDP4_LCDC_LVDS_MUX_CTL_3_TO_0(3),
				MDP4_LCDC_LVDS_MUX_CTL_3_TO_0_BIT0(0x1b) |
				MDP4_LCDC_LVDS_MUX_CTL_3_TO_0_BIT1(0x17) |
				MDP4_LCDC_LVDS_MUX_CTL_3_TO_0_BIT2(0x16) |
				MDP4_LCDC_LVDS_MUX_CTL_3_TO_0_BIT3(0x0f));
		mdp4_write(mdp4_kms, REG_MDP4_LCDC_LVDS_MUX_CTL_6_TO_4(3),
				MDP4_LCDC_LVDS_MUX_CTL_6_TO_4_BIT4(0x0e) |
				MDP4_LCDC_LVDS_MUX_CTL_6_TO_4_BIT5(0x07) |
				MDP4_LCDC_LVDS_MUX_CTL_6_TO_4_BIT6(0x06));
		if (nchan == 2) {
			lvds_intf |= MDP4_LCDC_LVDS_INTF_CTL_CH2_DATA_LANE3_EN |
					MDP4_LCDC_LVDS_INTF_CTL_CH2_DATA_LANE2_EN |
					MDP4_LCDC_LVDS_INTF_CTL_CH2_DATA_LANE1_EN |
					MDP4_LCDC_LVDS_INTF_CTL_CH2_DATA_LANE0_EN |
					MDP4_LCDC_LVDS_INTF_CTL_CH1_DATA_LANE3_EN |
					MDP4_LCDC_LVDS_INTF_CTL_CH1_DATA_LANE2_EN |
					MDP4_LCDC_LVDS_INTF_CTL_CH1_DATA_LANE1_EN |
					MDP4_LCDC_LVDS_INTF_CTL_CH1_DATA_LANE0_EN;
		} else {
			lvds_intf |= MDP4_LCDC_LVDS_INTF_CTL_CH1_DATA_LANE3_EN |
					MDP4_LCDC_LVDS_INTF_CTL_CH1_DATA_LANE2_EN |
					MDP4_LCDC_LVDS_INTF_CTL_CH1_DATA_LANE1_EN |
					MDP4_LCDC_LVDS_INTF_CTL_CH1_DATA_LANE0_EN;
		}
		break;

	case 18:
		mdp4_write(mdp4_kms, REG_MDP4_LCDC_LVDS_MUX_CTL_3_TO_0(0),
				MDP4_LCDC_LVDS_MUX_CTL_3_TO_0_BIT0(0x0a) |
				MDP4_LCDC_LVDS_MUX_CTL_3_TO_0_BIT1(0x07) |
				MDP4_LCDC_LVDS_MUX_CTL_3_TO_0_BIT2(0x06) |
				MDP4_LCDC_LVDS_MUX_CTL_3_TO_0_BIT3(0x05));
		mdp4_write(mdp4_kms, REG_MDP4_LCDC_LVDS_MUX_CTL_6_TO_4(0),
				MDP4_LCDC_LVDS_MUX_CTL_6_TO_4_BIT4(0x04) |
				MDP4_LCDC_LVDS_MUX_CTL_6_TO_4_BIT5(0x03) |
				MDP4_LCDC_LVDS_MUX_CTL_6_TO_4_BIT6(0x02));
		mdp4_write(mdp4_kms, REG_MDP4_LCDC_LVDS_MUX_CTL_3_TO_0(1),
				MDP4_LCDC_LVDS_MUX_CTL_3_TO_0_BIT0(0x13) |
				MDP4_LCDC_LVDS_MUX_CTL_3_TO_0_BIT1(0x12) |
				MDP4_LCDC_LVDS_MUX_CTL_3_TO_0_BIT2(0x0f) |
				MDP4_LCDC_LVDS_MUX_CTL_3_TO_0_BIT3(0x0e));
		mdp4_write(mdp4_kms, REG_MDP4_LCDC_LVDS_MUX_CTL_6_TO_4(1),
				MDP4_LCDC_LVDS_MUX_CTL_6_TO_4_BIT4(0x0d) |
				MDP4_LCDC_LVDS_MUX_CTL_6_TO_4_BIT5(0x0c) |
				MDP4_LCDC_LVDS_MUX_CTL_6_TO_4_BIT6(0x0b));
		mdp4_write(mdp4_kms, REG_MDP4_LCDC_LVDS_MUX_CTL_3_TO_0(2),
				MDP4_LCDC_LVDS_MUX_CTL_3_TO_0_BIT0(0x1a) |
				MDP4_LCDC_LVDS_MUX_CTL_3_TO_0_BIT1(0x19) |
				MDP4_LCDC_LVDS_MUX_CTL_3_TO_0_BIT2(0x18) |
				MDP4_LCDC_LVDS_MUX_CTL_3_TO_0_BIT3(0x17));
		mdp4_write(mdp4_kms, REG_MDP4_LCDC_LVDS_MUX_CTL_6_TO_4(2),
				MDP4_LCDC_LVDS_MUX_CTL_6_TO_4_BIT4(0x16) |
				MDP4_LCDC_LVDS_MUX_CTL_6_TO_4_BIT5(0x15) |
				MDP4_LCDC_LVDS_MUX_CTL_6_TO_4_BIT6(0x14));
		if (nchan == 2) {
			lvds_intf |= MDP4_LCDC_LVDS_INTF_CTL_CH2_DATA_LANE2_EN |
					MDP4_LCDC_LVDS_INTF_CTL_CH2_DATA_LANE1_EN |
					MDP4_LCDC_LVDS_INTF_CTL_CH2_DATA_LANE0_EN |
					MDP4_LCDC_LVDS_INTF_CTL_CH1_DATA_LANE2_EN |
					MDP4_LCDC_LVDS_INTF_CTL_CH1_DATA_LANE1_EN |
					MDP4_LCDC_LVDS_INTF_CTL_CH1_DATA_LANE0_EN;
		} else {
			lvds_intf |= MDP4_LCDC_LVDS_INTF_CTL_CH1_DATA_LANE2_EN |
					MDP4_LCDC_LVDS_INTF_CTL_CH1_DATA_LANE1_EN |
					MDP4_LCDC_LVDS_INTF_CTL_CH1_DATA_LANE0_EN;
		}
		lvds_intf |= MDP4_LCDC_LVDS_INTF_CTL_RGB_OUT;
		break;

	default:
		dev_err(dev->dev, "unknown bpp: %d\n", bpp);
		return;
	}

	switch (nchan) {
	case 1:
		lvds_phy_cfg0 = MDP4_LVDS_PHY_CFG0_CHANNEL0;
		lvds_intf |= MDP4_LCDC_LVDS_INTF_CTL_CH1_CLK_LANE_EN |
				MDP4_LCDC_LVDS_INTF_CTL_MODE_SEL;
		break;
	case 2:
		lvds_phy_cfg0 = MDP4_LVDS_PHY_CFG0_CHANNEL0 |
				MDP4_LVDS_PHY_CFG0_CHANNEL1;
		lvds_intf |= MDP4_LCDC_LVDS_INTF_CTL_CH2_CLK_LANE_EN |
				MDP4_LCDC_LVDS_INTF_CTL_CH1_CLK_LANE_EN;
		break;
	default:
		dev_err(dev->dev, "unknown # of channels: %d\n", nchan);
		return;
	}

	if (swap)
		lvds_intf |= MDP4_LCDC_LVDS_INTF_CTL_CH_SWAP;

	lvds_intf |= MDP4_LCDC_LVDS_INTF_CTL_ENABLE;

	mdp4_write(mdp4_kms, REG_MDP4_LVDS_PHY_CFG0, lvds_phy_cfg0);
	mdp4_write(mdp4_kms, REG_MDP4_LCDC_LVDS_INTF_CTL, lvds_intf);
	mdp4_write(mdp4_kms, REG_MDP4_LVDS_PHY_CFG2, 0x30);

	mb();
	udelay(1);
	lvds_phy_cfg0 |= MDP4_LVDS_PHY_CFG0_SERIALIZATION_ENBLE;
	mdp4_write(mdp4_kms, REG_MDP4_LVDS_PHY_CFG0, lvds_phy_cfg0);
}

static void mdp4_lcdc_encoder_mode_set(struct drm_encoder *encoder,
		struct drm_display_mode *mode,
		struct drm_display_mode *adjusted_mode)
{
	struct mdp4_lcdc_encoder *mdp4_lcdc_encoder =
			to_mdp4_lcdc_encoder(encoder);
	struct mdp4_kms *mdp4_kms = get_kms(encoder);
	uint32_t lcdc_hsync_skew, vsync_period, vsync_len, ctrl_pol;
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

	mdp4_lcdc_encoder->pixclock = mode->clock * 1000;

	DBG("pixclock=%lu", mdp4_lcdc_encoder->pixclock);

	ctrl_pol = 0;
	if (mode->flags & DRM_MODE_FLAG_NHSYNC)
		ctrl_pol |= MDP4_LCDC_CTRL_POLARITY_HSYNC_LOW;
	if (mode->flags & DRM_MODE_FLAG_NVSYNC)
		ctrl_pol |= MDP4_LCDC_CTRL_POLARITY_VSYNC_LOW;
	/* probably need to get DATA_EN polarity from panel.. */

	lcdc_hsync_skew = 0;  /* get this from panel? */

	hsync_start_x = (mode->htotal - mode->hsync_start);
	hsync_end_x = mode->htotal - (mode->hsync_start - mode->hdisplay) - 1;

	vsync_period = mode->vtotal * mode->htotal;
	vsync_len = (mode->vsync_end - mode->vsync_start) * mode->htotal;
	display_v_start = (mode->vtotal - mode->vsync_start) * mode->htotal + lcdc_hsync_skew;
	display_v_end = vsync_period - ((mode->vsync_start - mode->vdisplay) * mode->htotal) + lcdc_hsync_skew - 1;

	mdp4_write(mdp4_kms, REG_MDP4_LCDC_HSYNC_CTRL,
			MDP4_LCDC_HSYNC_CTRL_PULSEW(mode->hsync_end - mode->hsync_start) |
			MDP4_LCDC_HSYNC_CTRL_PERIOD(mode->htotal));
	mdp4_write(mdp4_kms, REG_MDP4_LCDC_VSYNC_PERIOD, vsync_period);
	mdp4_write(mdp4_kms, REG_MDP4_LCDC_VSYNC_LEN, vsync_len);
	mdp4_write(mdp4_kms, REG_MDP4_LCDC_DISPLAY_HCTRL,
			MDP4_LCDC_DISPLAY_HCTRL_START(hsync_start_x) |
			MDP4_LCDC_DISPLAY_HCTRL_END(hsync_end_x));
	mdp4_write(mdp4_kms, REG_MDP4_LCDC_DISPLAY_VSTART, display_v_start);
	mdp4_write(mdp4_kms, REG_MDP4_LCDC_DISPLAY_VEND, display_v_end);
	mdp4_write(mdp4_kms, REG_MDP4_LCDC_BORDER_CLR, 0);
	mdp4_write(mdp4_kms, REG_MDP4_LCDC_UNDERFLOW_CLR,
			MDP4_LCDC_UNDERFLOW_CLR_ENABLE_RECOVERY |
			MDP4_LCDC_UNDERFLOW_CLR_COLOR(0xff));
	mdp4_write(mdp4_kms, REG_MDP4_LCDC_HSYNC_SKEW, lcdc_hsync_skew);
	mdp4_write(mdp4_kms, REG_MDP4_LCDC_CTRL_POLARITY, ctrl_pol);
	mdp4_write(mdp4_kms, REG_MDP4_LCDC_ACTIVE_HCTL,
			MDP4_LCDC_ACTIVE_HCTL_START(0) |
			MDP4_LCDC_ACTIVE_HCTL_END(0));
	mdp4_write(mdp4_kms, REG_MDP4_LCDC_ACTIVE_VSTART, 0);
	mdp4_write(mdp4_kms, REG_MDP4_LCDC_ACTIVE_VEND, 0);
}

static void mdp4_lcdc_encoder_disable(struct drm_encoder *encoder)
{
	struct drm_device *dev = encoder->dev;
	struct mdp4_lcdc_encoder *mdp4_lcdc_encoder =
			to_mdp4_lcdc_encoder(encoder);
	struct mdp4_kms *mdp4_kms = get_kms(encoder);
	struct drm_panel *panel;
	int i, ret;

	if (WARN_ON(!mdp4_lcdc_encoder->enabled))
		return;

	mdp4_write(mdp4_kms, REG_MDP4_LCDC_ENABLE, 0);

	panel = of_drm_find_panel(mdp4_lcdc_encoder->panel_node);
	if (panel) {
		drm_panel_disable(panel);
		drm_panel_unprepare(panel);
	}

	/*
	 * Wait for a vsync so we know the ENABLE=0 latched before
	 * the (connector) source of the vsync's gets disabled,
	 * otherwise we end up in a funny state if we re-enable
	 * before the disable latches, which results that some of
	 * the settings changes for the new modeset (like new
	 * scanout buffer) don't latch properly..
	 */
	mdp_irq_wait(&mdp4_kms->base, MDP4_IRQ_PRIMARY_VSYNC);

	clk_disable_unprepare(mdp4_lcdc_encoder->lcdc_clk);

	for (i = 0; i < ARRAY_SIZE(mdp4_lcdc_encoder->regs); i++) {
		ret = regulator_disable(mdp4_lcdc_encoder->regs[i]);
		if (ret)
			dev_err(dev->dev, "failed to disable regulator: %d\n", ret);
	}

	bs_set(mdp4_lcdc_encoder, 0);

	mdp4_lcdc_encoder->enabled = false;
}

static void mdp4_lcdc_encoder_enable(struct drm_encoder *encoder)
{
	struct drm_device *dev = encoder->dev;
	struct mdp4_lcdc_encoder *mdp4_lcdc_encoder =
			to_mdp4_lcdc_encoder(encoder);
	unsigned long pc = mdp4_lcdc_encoder->pixclock;
	struct mdp4_kms *mdp4_kms = get_kms(encoder);
	struct drm_panel *panel;
	int i, ret;

	if (WARN_ON(mdp4_lcdc_encoder->enabled))
		return;

	/* TODO: hard-coded for 18bpp: */
	mdp4_crtc_set_config(encoder->crtc,
			MDP4_DMA_CONFIG_R_BPC(BPC6) |
			MDP4_DMA_CONFIG_G_BPC(BPC6) |
			MDP4_DMA_CONFIG_B_BPC(BPC6) |
			MDP4_DMA_CONFIG_PACK_ALIGN_MSB |
			MDP4_DMA_CONFIG_PACK(0x21) |
			MDP4_DMA_CONFIG_DEFLKR_EN |
			MDP4_DMA_CONFIG_DITHER_EN);
	mdp4_crtc_set_intf(encoder->crtc, INTF_LCDC_DTV, 0);

	bs_set(mdp4_lcdc_encoder, 1);

	for (i = 0; i < ARRAY_SIZE(mdp4_lcdc_encoder->regs); i++) {
		ret = regulator_enable(mdp4_lcdc_encoder->regs[i]);
		if (ret)
			dev_err(dev->dev, "failed to enable regulator: %d\n", ret);
	}

	DBG("setting lcdc_clk=%lu", pc);
	ret = clk_set_rate(mdp4_lcdc_encoder->lcdc_clk, pc);
	if (ret)
		dev_err(dev->dev, "failed to configure lcdc_clk: %d\n", ret);
	ret = clk_prepare_enable(mdp4_lcdc_encoder->lcdc_clk);
	if (ret)
		dev_err(dev->dev, "failed to enable lcdc_clk: %d\n", ret);

	panel = of_drm_find_panel(mdp4_lcdc_encoder->panel_node);
	if (panel) {
		drm_panel_prepare(panel);
		drm_panel_enable(panel);
	}

	setup_phy(encoder);

	mdp4_write(mdp4_kms, REG_MDP4_LCDC_ENABLE, 1);

	mdp4_lcdc_encoder->enabled = true;
}

static const struct drm_encoder_helper_funcs mdp4_lcdc_encoder_helper_funcs = {
	.mode_set = mdp4_lcdc_encoder_mode_set,
	.disable = mdp4_lcdc_encoder_disable,
	.enable = mdp4_lcdc_encoder_enable,
};

long mdp4_lcdc_round_pixclk(struct drm_encoder *encoder, unsigned long rate)
{
	struct mdp4_lcdc_encoder *mdp4_lcdc_encoder =
			to_mdp4_lcdc_encoder(encoder);
	return clk_round_rate(mdp4_lcdc_encoder->lcdc_clk, rate);
}

/* initialize encoder */
struct drm_encoder *mdp4_lcdc_encoder_init(struct drm_device *dev,
		struct device_node *panel_node)
{
	struct drm_encoder *encoder = NULL;
	struct mdp4_lcdc_encoder *mdp4_lcdc_encoder;
	struct regulator *reg;
	int ret;

	mdp4_lcdc_encoder = kzalloc(sizeof(*mdp4_lcdc_encoder), GFP_KERNEL);
	if (!mdp4_lcdc_encoder) {
		ret = -ENOMEM;
		goto fail;
	}

	mdp4_lcdc_encoder->panel_node = panel_node;

	encoder = &mdp4_lcdc_encoder->base;

	drm_encoder_init(dev, encoder, &mdp4_lcdc_encoder_funcs,
			 DRM_MODE_ENCODER_LVDS, NULL);
	drm_encoder_helper_add(encoder, &mdp4_lcdc_encoder_helper_funcs);

	/* TODO: do we need different pll in other cases? */
	mdp4_lcdc_encoder->lcdc_clk = mpd4_lvds_pll_init(dev);
	if (IS_ERR(mdp4_lcdc_encoder->lcdc_clk)) {
		dev_err(dev->dev, "failed to get lvds_clk\n");
		ret = PTR_ERR(mdp4_lcdc_encoder->lcdc_clk);
		goto fail;
	}

	/* TODO: different regulators in other cases? */
	reg = devm_regulator_get(dev->dev, "lvds-vccs-3p3v");
	if (IS_ERR(reg)) {
		ret = PTR_ERR(reg);
		dev_err(dev->dev, "failed to get lvds-vccs-3p3v: %d\n", ret);
		goto fail;
	}
	mdp4_lcdc_encoder->regs[0] = reg;

	reg = devm_regulator_get(dev->dev, "lvds-pll-vdda");
	if (IS_ERR(reg)) {
		ret = PTR_ERR(reg);
		dev_err(dev->dev, "failed to get lvds-pll-vdda: %d\n", ret);
		goto fail;
	}
	mdp4_lcdc_encoder->regs[1] = reg;

	reg = devm_regulator_get(dev->dev, "lvds-vdda");
	if (IS_ERR(reg)) {
		ret = PTR_ERR(reg);
		dev_err(dev->dev, "failed to get lvds-vdda: %d\n", ret);
		goto fail;
	}
	mdp4_lcdc_encoder->regs[2] = reg;

	bs_init(mdp4_lcdc_encoder);

	return encoder;

fail:
	if (encoder)
		mdp4_lcdc_encoder_destroy(encoder);

	return ERR_PTR(ret);
}
