/*
 * Copyright (C) Fuzhou Rockchip Electronics Co.Ltd
 * Author:
 *      Mark Yao <mark.yao@rock-chips.com>
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <drm/drmP.h>
#include <drm/drm_atomic_helper.h>
#include <drm/drm_crtc_helper.h>
#include <drm/drm_dp_helper.h>
#include <drm/drm_panel.h>
#include <drm/drm_of.h>

#include <linux/component.h>
#include <linux/clk.h>
#include <linux/mfd/syscon.h>
#include <linux/of_graph.h>
#include <linux/pm_runtime.h>
#include <linux/regmap.h>
#include <linux/reset.h>

#include <video/display_timing.h>

#include "rockchip_drm_drv.h"
#include "rockchip_drm_vop.h"
#include "rockchip_lvds.h"

#define DISPLAY_OUTPUT_RGB		0
#define DISPLAY_OUTPUT_LVDS		1
#define DISPLAY_OUTPUT_DUAL_LVDS	2

#define connector_to_lvds(c) \
		container_of(c, struct rockchip_lvds, connector)

#define encoder_to_lvds(c) \
		container_of(c, struct rockchip_lvds, encoder)
#define LVDS_CHIP(lvds)	((lvds)->soc_data->chip_type)

/*
 * @grf_offset: offset inside the grf regmap for setting the rockchip lvds
 */
struct rockchip_lvds_soc_data {
	int chip_type;
	int grf_soc_con5;
	int grf_soc_con6;
	int grf_soc_con7;
	int grf_soc_con15;
};

struct rockchip_lvds {
	void *base;
	struct device *dev;
	void __iomem *regs;
	void __iomem *regs_ctrl;
	struct regmap *grf;
	struct clk *pclk;
	struct clk *pclk_ctrl;
	const struct rockchip_lvds_soc_data *soc_data;

	int output;
	int format;

	struct drm_device *drm_dev;
	struct drm_panel *panel;
	struct drm_connector connector;
	struct drm_encoder encoder;

	struct mutex suspend_lock;
	int suspend;
	struct dev_pin_info *pins;
};

static inline void lvds_writel(struct rockchip_lvds *lvds, u32 offset, u32 val)
{
	writel_relaxed(val, lvds->regs + offset);
	if ((lvds->output == DISPLAY_OUTPUT_DUAL_LVDS) &&
	    (LVDS_CHIP(lvds) == RK3288_LVDS))
		writel_relaxed(val, lvds->regs + offset + 0x100);
}

static inline void lvds_msk_reg(struct rockchip_lvds *lvds, u32 offset,
				u32 msk, u32 val)
{
	u32 temp;

	temp = readl_relaxed(lvds->regs + offset) & (0xFF - (msk));
	writel_relaxed(temp | ((val) & (msk)), lvds->regs + offset);
}

static inline void lvds_dsi_writel(struct rockchip_lvds *lvds,
				   u32 offset, u32 val)
{
	writel_relaxed(val, lvds->regs_ctrl + offset);
}

static inline u32 lvds_phy_lockon(struct rockchip_lvds *lvds)
{
	u32 val = 0;

	val = readl_relaxed(lvds->regs_ctrl + 0x10);
	return (val & 0x01);
}

static inline int lvds_name_to_format(const char *s)
{
	if (!s)
		return -EINVAL;

	if (strncmp(s, "jeida", 6) == 0)
		return LVDS_FORMAT_JEIDA;
	else if (strncmp(s, "vesa", 5) == 0)
		return LVDS_FORMAT_VESA;

	return -EINVAL;
}

static inline int lvds_name_to_output(const char *s)
{
	if (!s)
		return -EINVAL;

	if (strncmp(s, "rgb", 3) == 0)
		return DISPLAY_OUTPUT_RGB;
	else if (strncmp(s, "lvds", 4) == 0)
		return DISPLAY_OUTPUT_LVDS;
	else if (strncmp(s, "duallvds", 8) == 0)
		return DISPLAY_OUTPUT_DUAL_LVDS;

	return -EINVAL;
}

static int rk3288_lvds_poweron(struct rockchip_lvds *lvds)
{
	if (lvds->output == DISPLAY_OUTPUT_RGB) {
		lvds_writel(lvds, RK3288_LVDS_CH0_REG0,
			    RK3288_LVDS_CH0_REG0_TTL_EN |
			    RK3288_LVDS_CH0_REG0_LANECK_EN |
			    RK3288_LVDS_CH0_REG0_LANE4_EN |
			    RK3288_LVDS_CH0_REG0_LANE3_EN |
			    RK3288_LVDS_CH0_REG0_LANE2_EN |
			    RK3288_LVDS_CH0_REG0_LANE1_EN |
			    RK3288_LVDS_CH0_REG0_LANE0_EN);
		lvds_writel(lvds, RK3288_LVDS_CH0_REG2,
			    RK3288_LVDS_PLL_FBDIV_REG2(0x46));

		lvds_writel(lvds, RK3288_LVDS_CH0_REG3,
			    RK3288_LVDS_PLL_FBDIV_REG3(0x46));
		lvds_writel(lvds, RK3288_LVDS_CH0_REG4,
			    RK3288_LVDS_CH0_REG4_LANECK_TTL_MODE |
			    RK3288_LVDS_CH0_REG4_LANE4_TTL_MODE |
			    RK3288_LVDS_CH0_REG4_LANE3_TTL_MODE |
			    RK3288_LVDS_CH0_REG4_LANE2_TTL_MODE |
			    RK3288_LVDS_CH0_REG4_LANE1_TTL_MODE |
			    RK3288_LVDS_CH0_REG4_LANE0_TTL_MODE);
		lvds_writel(lvds, RK3288_LVDS_CH0_REG5,
			    RK3288_LVDS_CH0_REG5_LANECK_TTL_DATA |
			    RK3288_LVDS_CH0_REG5_LANE4_TTL_DATA |
			    RK3288_LVDS_CH0_REG5_LANE3_TTL_DATA |
			    RK3288_LVDS_CH0_REG5_LANE2_TTL_DATA |
			    RK3288_LVDS_CH0_REG5_LANE1_TTL_DATA |
			    RK3288_LVDS_CH0_REG5_LANE0_TTL_DATA);
		lvds_writel(lvds, RK3288_LVDS_CH0_REGD,
			    RK3288_LVDS_PLL_PREDIV_REGD(0x0a));
		lvds_writel(lvds, RK3288_LVDS_CH0_REG20,
			    RK3288_LVDS_CH0_REG20_LSB);
	} else {
		lvds_writel(lvds, RK3288_LVDS_CH0_REG0,
			    RK3288_LVDS_CH0_REG0_LVDS_EN |
			    RK3288_LVDS_CH0_REG0_LANECK_EN |
			    RK3288_LVDS_CH0_REG0_LANE4_EN |
			    RK3288_LVDS_CH0_REG0_LANE3_EN |
			    RK3288_LVDS_CH0_REG0_LANE2_EN |
			    RK3288_LVDS_CH0_REG0_LANE1_EN |
			    RK3288_LVDS_CH0_REG0_LANE0_EN);
		lvds_writel(lvds, RK3288_LVDS_CH0_REG1,
			    RK3288_LVDS_CH0_REG1_LANECK_BIAS |
			    RK3288_LVDS_CH0_REG1_LANE4_BIAS |
			    RK3288_LVDS_CH0_REG1_LANE3_BIAS |
			    RK3288_LVDS_CH0_REG1_LANE2_BIAS |
			    RK3288_LVDS_CH0_REG1_LANE1_BIAS |
			    RK3288_LVDS_CH0_REG1_LANE0_BIAS);
		lvds_writel(lvds, RK3288_LVDS_CH0_REG2,
			    RK3288_LVDS_CH0_REG2_RESERVE_ON |
			    RK3288_LVDS_CH0_REG2_LANECK_LVDS_MODE |
			    RK3288_LVDS_CH0_REG2_LANE4_LVDS_MODE |
			    RK3288_LVDS_CH0_REG2_LANE3_LVDS_MODE |
			    RK3288_LVDS_CH0_REG2_LANE2_LVDS_MODE |
			    RK3288_LVDS_CH0_REG2_LANE1_LVDS_MODE |
			    RK3288_LVDS_CH0_REG2_LANE0_LVDS_MODE |
			    RK3288_LVDS_PLL_FBDIV_REG2(0x46));
		lvds_writel(lvds, RK3288_LVDS_CH0_REG3,
			    RK3288_LVDS_PLL_FBDIV_REG3(0x46));
		lvds_writel(lvds, RK3288_LVDS_CH0_REG4, 0x00);
		lvds_writel(lvds, RK3288_LVDS_CH0_REG5, 0x00);
		lvds_writel(lvds, RK3288_LVDS_CH0_REGD,
			    RK3288_LVDS_PLL_PREDIV_REGD(0x0a));
		lvds_writel(lvds, RK3288_LVDS_CH0_REG20,
			    RK3288_LVDS_CH0_REG20_LSB);
	}

	writel(RK3288_LVDS_CFG_REGC_PLL_ENABLE,
	       lvds->regs + RK3288_LVDS_CFG_REGC);
	writel(RK3288_LVDS_CFG_REG21_TX_ENABLE,
	       lvds->regs + RK3288_LVDS_CFG_REG21);

	return 0;
}

static int rk3368_lvds_poweron(struct rockchip_lvds *lvds)
{
	u32 delay_times = 20;

	if (lvds->output == DISPLAY_OUTPUT_LVDS) {
		/* set VOCM 900 mv and V-DIFF 350 mv */
		lvds_msk_reg(lvds, MIPIPHY_REGE4, m_VOCM | m_DIFF_V,
			     v_VOCM(0) | v_DIFF_V(2));
		/* power up lvds pll and ldo */
		lvds_msk_reg(lvds, MIPIPHY_REG1,
			     m_SYNC_RST | m_LDO_PWR_DOWN | m_PLL_PWR_DOWN,
			     v_SYNC_RST(0) | v_LDO_PWR_DOWN(0) |
			     v_PLL_PWR_DOWN(0));
		/* enable lvds lane and power on pll */
		lvds_writel(lvds, MIPIPHY_REGEB,
			    v_LANE0_EN(1) | v_LANE1_EN(1) | v_LANE2_EN(1) |
			    v_LANE3_EN(1) | v_LANECLK_EN(1) | v_PLL_PWR_OFF(0));

		/* enable lvds */
		lvds_msk_reg(lvds, MIPIPHY_REGE3,
			     m_MIPI_EN | m_LVDS_EN | m_TTL_EN,
			     v_MIPI_EN(0) | v_LVDS_EN(1) | v_TTL_EN(0));
	} else {
		lvds_msk_reg(lvds, MIPIPHY_REGE3,
			     m_MIPI_EN | m_LVDS_EN | m_TTL_EN,
			     v_MIPI_EN(0) | v_LVDS_EN(0) | v_TTL_EN(1));
	}
	/* delay for waitting pll lock on */
	while (delay_times--) {
		if (lvds_phy_lockon(lvds))
			break;
		udelay(100);
	}

	if (delay_times <= 0)
		dev_err(lvds->dev, "wait phy lockon failed, please check hardware\n");

	return 0;
}

static void rk3368_output_ttl(struct rockchip_lvds *lvds)
{
	u32 val = 0;

	/* enable lane */
	lvds_writel(lvds, MIPIPHY_REG0, 0x7f);
	val = v_LANE0_EN(1) | v_LANE1_EN(1) | v_LANE2_EN(1) | v_LANE3_EN(1) |
		v_LANECLK_EN(1) | v_PLL_PWR_OFF(1);
	lvds_writel(lvds, MIPIPHY_REGEB, val);

	/* set ttl mode and reset phy config */
	val = v_LVDS_MODE_EN(0) | v_TTL_MODE_EN(1) | v_MIPI_MODE_EN(0) |
		v_MSB_SEL(1) | v_DIG_INTER_RST(1);
	lvds_writel(lvds, MIPIPHY_REGE0, val);

	rk3368_lvds_poweron(lvds);
}

static void rk3368_output_lvds(struct rockchip_lvds *lvds)
{
	/* digital internal disable */
	lvds_msk_reg(lvds, MIPIPHY_REGE1, m_DIG_INTER_EN, v_DIG_INTER_EN(0));

	/* set pll prediv and fbdiv */
	lvds_writel(lvds, MIPIPHY_REG3, v_PREDIV(2) | v_FBDIV_MSB(0));
	lvds_writel(lvds, MIPIPHY_REG4, v_FBDIV_LSB(28));

	lvds_writel(lvds, MIPIPHY_REGE8, 0xfc);

	/* set lvds mode and reset phy config */
	lvds_msk_reg(lvds, MIPIPHY_REGE0,
		     m_MSB_SEL | m_DIG_INTER_RST,
		     v_MSB_SEL(1) | v_DIG_INTER_RST(1));

	rk3368_lvds_poweron(lvds);
	lvds_msk_reg(lvds, MIPIPHY_REGE1, m_DIG_INTER_EN, v_DIG_INTER_EN(1));
}

static int rk3368_lvds_output(struct rockchip_lvds *lvds)
{
	if (lvds->output == DISPLAY_OUTPUT_RGB)
		rk3368_output_ttl(lvds);
	else
		rk3368_output_lvds(lvds);
	return 0;
}

static int rockchip_lvds_poweron(struct rockchip_lvds *lvds)
{
	int ret;

	if (lvds->pclk) {
		ret = clk_enable(lvds->pclk);
		if (ret < 0) {
			dev_err(lvds->dev, "failed to enable lvds pclk %d\n", ret);
			return ret;
		}
	}
	if (lvds->pclk_ctrl) {
		ret = clk_enable(lvds->pclk_ctrl);
		if (ret < 0) {
			dev_err(lvds->dev, "failed to enable lvds pclk_ctrl %d\n", ret);
			return ret;
		}
	}

	ret = pm_runtime_get_sync(lvds->dev);
	if (ret < 0) {
		dev_err(lvds->dev, "failed to get pm runtime: %d\n", ret);
		return ret;
	}
	if (LVDS_CHIP(lvds) == RK3288_LVDS)
		rk3288_lvds_poweron(lvds);
	else if (LVDS_CHIP(lvds) == RK3368_LVDS)
		rk3368_lvds_output(lvds);

	return 0;
}

static void rockchip_lvds_poweroff(struct rockchip_lvds *lvds)
{
	int ret;
	u32 val;

	if (LVDS_CHIP(lvds) == RK3288_LVDS) {
		ret = regmap_write(lvds->grf,
				   lvds->soc_data->grf_soc_con7, 0xffff8000);
		if (ret != 0)
			dev_err(lvds->dev, "Could not write to GRF: %d\n", ret);

		writel(RK3288_LVDS_CFG_REG21_TX_DISABLE,
		       lvds->regs + RK3288_LVDS_CFG_REG21);
		writel(RK3288_LVDS_CFG_REGC_PLL_DISABLE,
		       lvds->regs + RK3288_LVDS_CFG_REGC);

		pm_runtime_put(lvds->dev);
		if (lvds->pclk)
			clk_disable(lvds->pclk);
	} else if (LVDS_CHIP(lvds) == RK3368_LVDS) {
		val = v_RK3368_LVDSMODE_EN(0) | v_RK3368_MIPIPHY_TTL_EN(0);
		ret = regmap_write(lvds->grf, lvds->soc_data->grf_soc_con7, val);
		if (ret != 0) {
			dev_err(lvds->dev, "Could not write to GRF: %d\n", ret);
			return;
		}

		/* disable lvds lane and power off pll */
		lvds_writel(lvds, MIPIPHY_REGEB,
			    v_LANE0_EN(0) | v_LANE1_EN(0) | v_LANE2_EN(0) |
			    v_LANE3_EN(0) | v_LANECLK_EN(0) | v_PLL_PWR_OFF(1));

		/* power down lvds pll and bandgap */
		lvds_msk_reg(lvds, MIPIPHY_REG1,
			     m_SYNC_RST | m_LDO_PWR_DOWN | m_PLL_PWR_DOWN,
			     v_SYNC_RST(1) | v_LDO_PWR_DOWN(1) | v_PLL_PWR_DOWN(1));

		/* disable lvds */
		lvds_msk_reg(lvds, MIPIPHY_REGE3, m_LVDS_EN | m_TTL_EN,
			     v_LVDS_EN(0) | v_TTL_EN(0));
		pm_runtime_put(lvds->dev);
		if (lvds->pclk)
			clk_disable(lvds->pclk);
		if (lvds->pclk_ctrl)
			clk_disable(lvds->pclk_ctrl);
	}
}

static enum drm_connector_status
rockchip_lvds_connector_detect(struct drm_connector *connector, bool force)
{
	return connector_status_connected;
}

static void rockchip_lvds_connector_destroy(struct drm_connector *connector)
{
	drm_connector_cleanup(connector);
}

static struct drm_connector_funcs rockchip_lvds_connector_funcs = {
	.dpms = drm_atomic_helper_connector_dpms,
	.detect = rockchip_lvds_connector_detect,
	.fill_modes = drm_helper_probe_single_connector_modes,
	.destroy = rockchip_lvds_connector_destroy,
	.reset = drm_atomic_helper_connector_reset,
	.atomic_duplicate_state = drm_atomic_helper_connector_duplicate_state,
	.atomic_destroy_state = drm_atomic_helper_connector_destroy_state,
};

static int rockchip_lvds_connector_get_modes(struct drm_connector *connector)
{
	struct rockchip_lvds *lvds = connector_to_lvds(connector);
	struct drm_panel *panel = lvds->panel;

	return panel->funcs->get_modes(panel);
}

static struct drm_encoder *
rockchip_lvds_connector_best_encoder(struct drm_connector *connector)
{
	struct rockchip_lvds *lvds = connector_to_lvds(connector);

	return &lvds->encoder;
}

static enum drm_mode_status rockchip_lvds_connector_mode_valid(
		struct drm_connector *connector,
		struct drm_display_mode *mode)
{
	return MODE_OK;
}

static
struct drm_connector_helper_funcs rockchip_lvds_connector_helper_funcs = {
	.get_modes = rockchip_lvds_connector_get_modes,
	.mode_valid = rockchip_lvds_connector_mode_valid,
	.best_encoder = rockchip_lvds_connector_best_encoder,
};

static void rockchip_lvds_encoder_dpms(struct drm_encoder *encoder, int mode)
{
	struct rockchip_lvds *lvds = encoder_to_lvds(encoder);
	int ret;

	mutex_lock(&lvds->suspend_lock);

	switch (mode) {
	case DRM_MODE_DPMS_ON:
		if (!lvds->suspend)
			goto out;

		drm_panel_prepare(lvds->panel);
		ret = rockchip_lvds_poweron(lvds);
		if (ret < 0) {
			drm_panel_unprepare(lvds->panel);
			goto out;
		}
		drm_panel_enable(lvds->panel);

		lvds->suspend = false;
		break;
	case DRM_MODE_DPMS_STANDBY:
	case DRM_MODE_DPMS_SUSPEND:
	case DRM_MODE_DPMS_OFF:
		if (lvds->suspend)
			goto out;

		drm_panel_disable(lvds->panel);
		rockchip_lvds_poweroff(lvds);
		drm_panel_unprepare(lvds->panel);

		lvds->suspend = true;
		break;
	default:
		break;
	}

out:
	mutex_unlock(&lvds->suspend_lock);
}

static bool
rockchip_lvds_encoder_mode_fixup(struct drm_encoder *encoder,
				const struct drm_display_mode *mode,
				struct drm_display_mode *adjusted_mode)
{
	return true;
}

static void rockchip_lvds_encoder_mode_set(struct drm_encoder *encoder,
					  struct drm_display_mode *mode,
					  struct drm_display_mode *adjusted)
{
	struct rockchip_lvds *lvds = encoder_to_lvds(encoder);
	u32 h_bp = mode->htotal - mode->hsync_start;
	u8 pin_hsync = (mode->flags & DRM_MODE_FLAG_PHSYNC) ? 1 : 0;
	u8 pin_dclk = (mode->flags & DRM_MODE_FLAG_PCSYNC) ? 1 : 0;
	u32 val;
	int ret;

	if (LVDS_CHIP(lvds) == RK3288_LVDS) {
		val = lvds->format;
		if (lvds->output == DISPLAY_OUTPUT_DUAL_LVDS)
			val |= LVDS_DUAL | LVDS_CH0_EN | LVDS_CH1_EN;
		else if (lvds->output == DISPLAY_OUTPUT_LVDS)
			val |= LVDS_CH0_EN;
		else if (lvds->output == DISPLAY_OUTPUT_RGB)
			val |= LVDS_TTL_EN | LVDS_CH0_EN | LVDS_CH1_EN;

		if (h_bp & 0x01)
			val |= LVDS_START_PHASE_RST_1;

		val |= (pin_dclk << 8) | (pin_hsync << 9);
		val |= (0xffff << 16);
		ret = regmap_write(lvds->grf, lvds->soc_data->grf_soc_con7, val);
		if (ret != 0) {
			dev_err(lvds->dev, "Could not write to GRF: %d\n", ret);
			return;
		}
	} else if (LVDS_CHIP(lvds) == RK3368_LVDS) {
		if (lvds->output == DISPLAY_OUTPUT_RGB) {
			/* iomux to lcdc */
			if (lvds->pins && !IS_ERR(lvds->pins->default_state))
				pinctrl_select_state(lvds->pins->p,
						     lvds->pins->default_state);

			lvds_dsi_writel(lvds, 0x0, 0x4);/*set clock lane enable*/
			/* enable lvds mode */
			val = v_RK3368_LVDSMODE_EN(0) |
				v_RK3368_MIPIPHY_TTL_EN(1) |
				v_RK3368_MIPIPHY_LANE0_EN(1) |
				v_RK3368_MIPIDPI_FORCEX_EN(1);
			ret = regmap_write(lvds->grf,
					   lvds->soc_data->grf_soc_con7, val);
			if (ret != 0) {
				dev_err(lvds->dev,
					"Could not write to GRF: %d\n", ret);
				return;
			}
			val = v_RK3368_FORCE_JETAG(0);
			ret = regmap_write(lvds->grf,
					   lvds->soc_data->grf_soc_con15, val);
			if (ret != 0) {
				dev_err(lvds->dev,
					"Could not write to GRF: %d\n", ret);
				return;
			}
		} else if (lvds->output == DISPLAY_OUTPUT_LVDS) {
			/* enable lvds mode */
			val = v_RK3368_LVDSMODE_EN(1) | v_RK3368_MIPIPHY_TTL_EN(0);
			/* config lvds_format */
			val |= v_RK3368_LVDS_OUTPUT_FORMAT(lvds->format);
			/* LSB receive mode */
			val |= v_RK3368_LVDS_MSBSEL(LVDS_MSB_D7);
			val |= v_RK3368_MIPIPHY_LANE0_EN(1) |
			       v_RK3368_MIPIDPI_FORCEX_EN(1);
			ret = regmap_write(lvds->grf,
					   lvds->soc_data->grf_soc_con7, val);
			if (ret != 0) {
				dev_err(lvds->dev,
					"Could not write to GRF: %d\n", ret);
				return;
			}
		}
	}
}

static int rockchip_lvds_set_vop_source(struct rockchip_lvds *lvds,
					struct drm_encoder *encoder)
{
	u32 val;
	int ret;

	ret = drm_of_encoder_active_endpoint_id(lvds->dev->of_node, encoder);
	if (ret < 0)
		return ret;

	if (LVDS_CHIP(lvds) == RK3288_LVDS) {
		if (ret)
			val = RK3288_LVDS_SOC_CON6_SEL_VOP_LIT |
			      (RK3288_LVDS_SOC_CON6_SEL_VOP_LIT << 16);
		else
			val = RK3288_LVDS_SOC_CON6_SEL_VOP_LIT << 16;

		ret = regmap_write(lvds->grf, lvds->soc_data->grf_soc_con6, val);
		if (ret < 0)
			return ret;
	}
	return 0;
}

static int
rockchip_lvds_encoder_atomic_check(struct drm_encoder *encoder,
				   struct drm_crtc_state *crtc_state,
				   struct drm_connector_state *conn_state)
{
	struct rockchip_crtc_state *s = to_rockchip_crtc_state(crtc_state);
	struct drm_connector *connector = conn_state->connector;
	struct drm_display_info *info = &connector->display_info;

	s->output_mode = ROCKCHIP_OUT_MODE_P888;
	s->output_type = DRM_MODE_CONNECTOR_LVDS;
	if (info->num_bus_formats)
		s->bus_format = info->bus_formats[0];

	return 0;
}

static void rockchip_lvds_encoder_commit(struct drm_encoder *encoder)
{
	struct rockchip_lvds *lvds = encoder_to_lvds(encoder);

	rockchip_lvds_encoder_dpms(encoder, DRM_MODE_DPMS_ON);
	rockchip_lvds_set_vop_source(lvds, encoder);
}

static void rockchip_lvds_encoder_disable(struct drm_encoder *encoder)
{
	rockchip_lvds_encoder_dpms(encoder, DRM_MODE_DPMS_OFF);
}

static struct drm_encoder_helper_funcs rockchip_lvds_encoder_helper_funcs = {
	.dpms = rockchip_lvds_encoder_dpms,
	.mode_fixup = rockchip_lvds_encoder_mode_fixup,
	.mode_set = rockchip_lvds_encoder_mode_set,
	.commit = rockchip_lvds_encoder_commit,
	.disable = rockchip_lvds_encoder_disable,
	.atomic_check = rockchip_lvds_encoder_atomic_check,
};

static void rockchip_lvds_encoder_destroy(struct drm_encoder *encoder)
{
	drm_encoder_cleanup(encoder);
}

static struct drm_encoder_funcs rockchip_lvds_encoder_funcs = {
	.destroy = rockchip_lvds_encoder_destroy,
};

static struct rockchip_lvds_soc_data rk3288_lvds_data = {
	.chip_type = RK3288_LVDS,
	.grf_soc_con6 = 0x025c,
	.grf_soc_con7 = 0x0260,
};

static struct rockchip_lvds_soc_data rk33xx_lvds_data = {
	.chip_type = RK3368_LVDS,
	.grf_soc_con5  = 0x0414,
	.grf_soc_con6  = 0x0418,
	.grf_soc_con7  = 0x041c,
	.grf_soc_con15 = 0x043c,
};

static const struct of_device_id rockchip_lvds_dt_ids[] = {
	{
		.compatible = "rockchip,rk3288-lvds",
		.data = &rk3288_lvds_data
	},
	{
		.compatible = "rockchip,rk33xx-lvds",
		.data = &rk33xx_lvds_data
	},
	{}
};
MODULE_DEVICE_TABLE(of, rockchip_lvds_dt_ids);

static int rockchip_lvds_bind(struct device *dev, struct device *master,
			     void *data)
{
	struct rockchip_lvds *lvds = dev_get_drvdata(dev);
	struct drm_device *drm_dev = data;
	struct drm_encoder *encoder;
	struct drm_connector *connector;
	struct device_node *panel_node, *port, *endpoint;
	int ret, i;
	const char *name;
	lvds->drm_dev = drm_dev;

	port = of_graph_get_port_by_id(dev->of_node, 1);
	if (port) {
		endpoint = of_get_child_by_name(port, "endpoint");
		of_node_put(port);
		if (!endpoint) {
			dev_err(dev, "no output endpoint found\n");
			return -EINVAL;
		}
		panel_node = of_graph_get_remote_port_parent(endpoint);
		of_node_put(endpoint);
		if (!panel_node) {
			dev_err(dev, "no output node found\n");
			return -EINVAL;
		}
		lvds->panel = of_drm_find_panel(panel_node);
		if (!lvds->panel) {
			DRM_ERROR("failed to find panel\n");
			of_node_put(panel_node);
			return -EPROBE_DEFER;
		}

		if (of_property_read_string(panel_node, "rockchip,output", &name))
			/* default set it as output rgb */
			lvds->output = DISPLAY_OUTPUT_RGB;
		else
			lvds->output = lvds_name_to_output(name);

		if (lvds->output < 0) {
			dev_err(dev, "invalid output type [%s]\n", name);
			return lvds->output;
		}

		if (of_property_read_string(panel_node, "rockchip,data-mapping",
					    &name))
			/* default set it as format jeida */
			lvds->format = LVDS_FORMAT_JEIDA;
		else
			lvds->format = lvds_name_to_format(name);

		if (lvds->format < 0) {
			dev_err(dev, "invalid data-mapping format [%s]\n", name);
			return lvds->format;
		}

		if (of_property_read_u32(panel_node, "rockchip,data-width", &i)) {
			lvds->format |= LVDS_24BIT;
		} else {
			if (i == 24) {
				lvds->format |= LVDS_24BIT;
			} else if (i == 18) {
				lvds->format |= LVDS_18BIT;
			} else {
				dev_err(dev,
					"rockchip-lvds unsupport data-width[%d]\n", i);
				return -EINVAL;
			}
		}
		of_node_put(panel_node);
	}

	encoder = &lvds->encoder;
	encoder->possible_crtcs = drm_of_find_possible_crtcs(drm_dev,
							     dev->of_node);

	ret = drm_encoder_init(drm_dev, encoder, &rockchip_lvds_encoder_funcs,
			       DRM_MODE_ENCODER_LVDS, NULL);
	if (ret < 0) {
		DRM_ERROR("failed to initialize encoder with drm\n");
		return ret;
	}

	drm_encoder_helper_add(encoder, &rockchip_lvds_encoder_helper_funcs);

	connector = &lvds->connector;
	connector->dpms = DRM_MODE_DPMS_OFF;

	ret = drm_connector_init(drm_dev, connector,
				 &rockchip_lvds_connector_funcs,
				 DRM_MODE_CONNECTOR_LVDS);
	if (ret < 0) {
		DRM_ERROR("failed to initialize connector with drm\n");
		goto err_free_encoder;
	}

	drm_connector_helper_add(connector,
				 &rockchip_lvds_connector_helper_funcs);

	ret = drm_mode_connector_attach_encoder(connector, encoder);
	if (ret < 0) {
		DRM_ERROR("failed to attach connector and encoder\n");
		goto err_free_connector;
	}

	ret = drm_panel_attach(lvds->panel, connector);
	if (ret < 0) {
		DRM_ERROR("failed to attach connector and encoder\n");
		goto err_free_connector;
	}
	lvds->connector.port = dev->of_node;
	pm_runtime_enable(dev);

	return 0;

err_free_connector:
	drm_connector_cleanup(connector);
err_free_encoder:
	drm_encoder_cleanup(encoder);
	return ret;
}

static void rockchip_lvds_unbind(struct device *dev, struct device *master,
				void *data)
{
	struct rockchip_lvds *lvds = dev_get_drvdata(dev);

	rockchip_lvds_encoder_dpms(&lvds->encoder, DRM_MODE_DPMS_OFF);

	drm_panel_detach(lvds->panel);

	drm_connector_cleanup(&lvds->connector);
	drm_encoder_cleanup(&lvds->encoder);

	pm_runtime_disable(dev);
}

static const struct component_ops rockchip_lvds_component_ops = {
	.bind = rockchip_lvds_bind,
	.unbind = rockchip_lvds_unbind,
};

static int rockchip_lvds_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct rockchip_lvds *lvds;
	const struct of_device_id *match;
	struct resource *res;
	int ret;

	if (!dev->of_node)
		return -ENODEV;

	lvds = devm_kzalloc(&pdev->dev, sizeof(*lvds), GFP_KERNEL);
	if (!lvds)
		return -ENOMEM;

	lvds->dev = dev;
	lvds->suspend = true;
	match = of_match_node(rockchip_lvds_dt_ids, dev->of_node);
	lvds->soc_data = match->data;

	if (LVDS_CHIP(lvds) == RK3288_LVDS) {
		res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
		lvds->regs = devm_ioremap_resource(&pdev->dev, res);
		if (IS_ERR(lvds->regs))
			return PTR_ERR(lvds->regs);
	} else if (LVDS_CHIP(lvds) == RK3368_LVDS) {
		/* lvds regs on MIPIPHY_REG */
		res = platform_get_resource_byname(pdev, IORESOURCE_MEM,
						   "mipi_lvds_phy");
		lvds->regs = devm_ioremap_resource(&pdev->dev, res);
		if (IS_ERR(lvds->regs)) {
			dev_err(&pdev->dev, "ioremap lvds phy reg failed\n");
			return PTR_ERR(lvds->regs);
		}

		/* pll lock on status reg that is MIPICTRL Register */
		res = platform_get_resource_byname(pdev, IORESOURCE_MEM,
						   "mipi_lvds_ctl");
		lvds->regs_ctrl = devm_ioremap_resource(&pdev->dev, res);
		if (IS_ERR(lvds->regs_ctrl)) {
			dev_err(&pdev->dev, "ioremap lvds ctl reg failed\n");
			return PTR_ERR(lvds->regs_ctrl);
		}
		/* mipi ctrl clk for read lvds phy lock state */
		lvds->pclk_ctrl = devm_clk_get(&pdev->dev, "pclk_lvds_ctl");
		if (IS_ERR(lvds->pclk_ctrl)) {
			dev_err(dev, "could not get pclk_ctrl\n");
			lvds->pclk_ctrl = NULL;
		}
		lvds->pins = devm_kzalloc(lvds->dev, sizeof(*lvds->pins),
					  GFP_KERNEL);
		if (!lvds->pins)
			return -ENOMEM;

		lvds->pins->p = devm_pinctrl_get(lvds->dev);
		if (IS_ERR(lvds->pins->p)) {
			dev_info(lvds->dev, "no pinctrl handle\n");
			devm_kfree(lvds->dev, lvds->pins);
			lvds->pins = NULL;
		} else {
			lvds->pins->default_state =
				pinctrl_lookup_state(lvds->pins->p, "lcdc");
			if (IS_ERR(lvds->pins->default_state)) {
				dev_info(lvds->dev, "no default pinctrl state\n");
				devm_kfree(lvds->dev, lvds->pins);
				lvds->pins = NULL;
			}
		}
	}
	lvds->pclk = devm_clk_get(&pdev->dev, "pclk_lvds");
	if (IS_ERR(lvds->pclk)) {
		dev_err(dev, "could not get pclk_lvds\n");
		return PTR_ERR(lvds->pclk);
	}
	lvds->grf = syscon_regmap_lookup_by_phandle(dev->of_node,
						    "rockchip,grf");
	if (IS_ERR(lvds->grf)) {
		dev_err(dev, "missing rockchip,grf property\n");
		return PTR_ERR(lvds->grf);
	}

	dev_set_drvdata(dev, lvds);
	mutex_init(&lvds->suspend_lock);

	if (lvds->pclk) {
		ret = clk_prepare(lvds->pclk);
		if (ret < 0) {
			dev_err(dev, "failed to prepare pclk_lvds\n");
			return ret;
		}
	}
	if (lvds->pclk_ctrl) {
		ret = clk_prepare(lvds->pclk_ctrl);
		if (ret < 0) {
			dev_err(dev, "failed to prepare pclk_ctrl lvds\n");
			return ret;
		}
	}
	ret = component_add(&pdev->dev, &rockchip_lvds_component_ops);
	if (ret < 0) {
		if (lvds->pclk)
			clk_unprepare(lvds->pclk);
		if (lvds->pclk_ctrl)
			clk_unprepare(lvds->pclk_ctrl);
	}

	return ret;
}

static int rockchip_lvds_remove(struct platform_device *pdev)
{
	struct rockchip_lvds *lvds = dev_get_drvdata(&pdev->dev);

	component_del(&pdev->dev, &rockchip_lvds_component_ops);
	if (lvds->pclk)
		clk_unprepare(lvds->pclk);
	if (lvds->pclk_ctrl)
		clk_unprepare(lvds->pclk_ctrl);
	return 0;
}

struct platform_driver rockchip_lvds_driver = {
	.probe = rockchip_lvds_probe,
	.remove = rockchip_lvds_remove,
	.driver = {
		   .name = "rockchip-lvds",
		   .of_match_table = of_match_ptr(rockchip_lvds_dt_ids),
	},
};
module_platform_driver(rockchip_lvds_driver);

MODULE_AUTHOR("Mark Yao <mark.yao@rock-chips.com>");
MODULE_AUTHOR("Heiko Stuebner <heiko@sntech.de>");
MODULE_DESCRIPTION("ROCKCHIP LVDS Driver");
MODULE_LICENSE("GPL v2");
