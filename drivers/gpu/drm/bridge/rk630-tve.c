// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2020 Rockchip Electronics Co. Ltd.
 *
 * Author: Algea Cao <algea.cao@rock-chips.com>
 */

#include <linux/delay.h>
#include <linux/i2c.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/mfd/rk630.h>
#include <linux/mfd/syscon.h>

#include <drm/drm_atomic_helper.h>
#include <drm/drm_crtc_helper.h>
#include <drm/drm_of.h>
#include <drm/drm_probe_helper.h>

#include "../rockchip/rockchip_drm_drv.h"

static const struct drm_display_mode rk630_tve_mode[2] = {
	{ DRM_MODE("720x576i", DRM_MODE_TYPE_DRIVER, 13500, 720, 753,
		   816, 864, 0, 576, 580, 586, 625, 0,
		   DRM_MODE_FLAG_NHSYNC | DRM_MODE_FLAG_NVSYNC |
		   DRM_MODE_FLAG_INTERLACE | DRM_MODE_FLAG_DBLCLK),
		   0, },
	{ DRM_MODE("720x480i", DRM_MODE_TYPE_DRIVER, 13500, 720, 753,
		   815, 858, 0, 480, 483, 489, 525, 0,
		   DRM_MODE_FLAG_PHSYNC | DRM_MODE_FLAG_PVSYNC |
		   DRM_MODE_FLAG_INTERLACE | DRM_MODE_FLAG_DBLCLK),
		   0, },
};

struct rk630_tve {
	struct device *dev;
	struct drm_connector connector;
	struct drm_bridge bridge;
	struct drm_encoder *encoder;

	struct regmap *grf;
	struct regmap *cru;
	struct regmap *tvemap;
	struct rk630 *parent;

	int mode;
	int is_4x;
	struct rockchip_drm_sub_dev sub_dev;
};

enum {
	CVBS_NTSC = 0,
	CVBS_PAL,
};

struct env_config {
	u32 offset;
	u32 value;
};

static struct env_config ntsc_bt656_config[] = {
	{ BT656_DECODER_CTRL, 0x00000001 },
	{ BT656_DECODER_CROP, 0x00000000 },
	{ BT656_DECODER_SIZE, 0x01e002d0 },
	{ BT656_DECODER_HTOTAL_HS_END, 0x035a003e },
	{ BT656_DECODER_VACT_ST_HACT_ST, 0x00150069 },
	{ BT656_DECODER_VTOTAL_VS_END, 0x020d0003 },
	{ BT656_DECODER_VS_ST_END_F1, 0x01060109 },
	{ BT656_DECODER_DBG_REG, 0x024002d0 },
};

static struct env_config ntsc_tve_config[] = {
	{ TVE_MODE_CTRL, 0x000af906 },
	{ TVE_HOR_TIMING1, 0x00c07a81 },
	{ TVE_HOR_TIMING2, 0x169810fc },
	{ TVE_HOR_TIMING3, 0x96b40000 },
	{ TVE_SUB_CAR_FRQ, 0x21f07bd7 },
	{ TVE_LUMA_FILTER1, 0x000a0ffa },
	{ TVE_LUMA_FILTER2, 0x0ff4001a },
	{ TVE_LUMA_FILTER3, 0x00110fd2 },
	{ TVE_LUMA_FILTER4, 0x0fe80051 },
	{ TVE_LUMA_FILTER5, 0x001a0f74 },
	{ TVE_LUMA_FILTER6, 0x0fe600ec },
	{ TVE_LUMA_FILTER7, 0x0ffa0e43 },
	{ TVE_LUMA_FILTER8, 0x08200527 },
	{ TVE_IMAGE_POSITION, 0x001500d6 },
	{ TVE_ROUTING, 0x10088880 },
	{ TVE_SYNC_ADJUST, 0x00000000 },
	{ TVE_STATUS, 0x00000000 },
	{ TVE_CTRL, 0x00000000 },
	{ TVE_INTR_STATUS, 0x00000000 },
	{ TVE_INTR_EN, 0x00000000 },
	{ TVE_INTR_CLR, 0x00000000 },
	{ TVE_COLOR_BUSRT_SAT, 0x0052543c },
	{ TVE_CHROMA_BANDWIDTH, 0x00000002 },
	{ TVE_BRIGHTNESS_CONTRAST, 0x00008300 },
	{ TVE_ID, 0x0a010000 },
	{ TVE_REVISION, 0x00010108 },
	{ TVE_CLAMP, 0x00000000 },
};

static struct env_config pal_bt656_config[] = {
	{ BT656_DECODER_CTRL, 0x00000001 },
	{ BT656_DECODER_CROP, 0x00000000 },
	{ BT656_DECODER_SIZE, 0x024002d0 },
	{ BT656_DECODER_HTOTAL_HS_END, 0x0360003f },
	{ BT656_DECODER_VACT_ST_HACT_ST, 0x0016006f },
	{ BT656_DECODER_VTOTAL_VS_END, 0x02710003 },
	{ BT656_DECODER_VS_ST_END_F1, 0x0138013b },
	{ BT656_DECODER_DBG_REG, 0x024002d0 },
};

static struct env_config pal_tve_config[] = {
	{ TVE_MODE_CTRL, 0x010ab906 },
	{ TVE_HOR_TIMING1, 0x00c28381 },
	{ TVE_HOR_TIMING2, 0x267d111d },
	{ TVE_HOR_TIMING3, 0x76c00880 },
	{ TVE_SUB_CAR_FRQ, 0x2a098acb },
	{ TVE_LUMA_FILTER1, 0x000a0ffa },
	{ TVE_LUMA_FILTER2, 0x0ff4001a },
	{ TVE_LUMA_FILTER3, 0x00110fd2 },
	{ TVE_LUMA_FILTER4, 0x0fe80051 },
	{ TVE_LUMA_FILTER5, 0x001a0f74 },
	{ TVE_LUMA_FILTER6, 0x0fe600ec },
	{ TVE_LUMA_FILTER7, 0x0ffa0e43 },
	{ TVE_LUMA_FILTER8, 0x08200527 },
	{ TVE_IMAGE_POSITION, 0x001500f6 },
	{ TVE_ROUTING, 0x1000088a },
	{ TVE_SYNC_ADJUST, 0x00000000 },
	{ TVE_STATUS, 0x000000b0 },
	{ TVE_CTRL, 0x00000000 },
	{ TVE_INTR_STATUS, 0x00000000 },
	{ TVE_INTR_EN, 0x00000000 },
	{ TVE_INTR_CLR, 0x00000000 },
	{ TVE_COLOR_BUSRT_SAT, 0x002e553c },
	{ TVE_CHROMA_BANDWIDTH, 0x00000022 },
	{ TVE_BRIGHTNESS_CONTRAST, 0x00008900 },
	{ TVE_ID, 0x0a010000 },
	{ TVE_REVISION, 0x00010108 },
	{ TVE_CLAMP, 0x00000000 },
};

static const struct regmap_range rk630_tve_readable_ranges[] = {
	regmap_reg_range(BT656_DECODER_CTRL, BT656_DECODER_DBG_REG),
	regmap_reg_range(TVE_MODE_CTRL, TVE_ROUTING),
	regmap_reg_range(TVE_SYNC_ADJUST, TVE_STATUS),
	regmap_reg_range(TVE_CTRL, TVE_COLOR_BUSRT_SAT),
	regmap_reg_range(TVE_CHROMA_BANDWIDTH, TVE_BRIGHTNESS_CONTRAST),
	regmap_reg_range(TVE_ID, TVE_CLAMP),
};

static const struct regmap_access_table rk630_tve_readable_table = {
	.yes_ranges = rk630_tve_readable_ranges,
	.n_yes_ranges = ARRAY_SIZE(rk630_tve_readable_ranges),
};

const struct regmap_config rk630_tve_regmap_config = {
	.name = "tve",
	.reg_bits = 32,
	.val_bits = 32,
	.reg_stride = 4,
	.max_register = TVE_MAX_REGISTER,
	.reg_format_endian = REGMAP_ENDIAN_NATIVE,
	.val_format_endian = REGMAP_ENDIAN_NATIVE,
	.rd_table = &rk630_tve_readable_table,
};
EXPORT_SYMBOL_GPL(rk630_tve_regmap_config);

static struct rk630_tve *bridge_to_tve(struct drm_bridge *bridge)
{
	return container_of(bridge, struct rk630_tve, bridge);
}

static struct rk630_tve *connector_to_tve(struct drm_connector *connector)
{
	return container_of(connector, struct rk630_tve, connector);
}

static int rk630_tve_write_block(struct rk630_tve *tve,
				 struct env_config *config, int len)
{
	int i, ret = 0;

	for (i = 0; i < len; i++) {
		ret = regmap_write(tve->tvemap, config[i].offset,
				   config[i].value);
		if (ret)
			break;
	}

	return ret;
}

static int rk630_tve_cfg_set(struct rk630_tve *tve)
{
	int ret;
	struct env_config *bt656_cfg, *tve_cfg;

	switch (tve->mode) {
	case CVBS_PAL:
		dev_dbg(tve->dev, "rk630 PAL\n");
		bt656_cfg = pal_bt656_config;
		tve_cfg = pal_tve_config;
		break;
	case CVBS_NTSC:
		dev_dbg(tve->dev, "rk630 NTSC\n");
		bt656_cfg = ntsc_bt656_config;
		tve_cfg = ntsc_tve_config;
		break;
	default:
		dev_dbg(tve->dev, "mode select err\n");
		return -EINVAL;
	}

	ret = rk630_tve_write_block(tve, bt656_cfg, 8);
	if (ret) {
		dev_err(tve->dev, "rk630 bt656 write err!\n");
		return ret;
	}

	if (tve->mode == CVBS_PAL)
		regmap_update_bits(tve->grf, PLUMAGE_GRF_SOC_CON0,
				   SW_TVE_DCLK_POL_MASK |
				   SW_TVE_DCLK_EN_MASK |
				   SW_DCLK_UPSAMPLE_EN_MASK |
				   SW_TVE_MODE_MASK | SW_TVE_EN_MASK,
				   SW_TVE_DCLK_POL(0) | SW_TVE_DCLK_EN(1) |
				   SW_DCLK_UPSAMPLE_EN(tve->is_4x) |
				   SW_TVE_MODE(1) | SW_TVE_EN(1));
	else
		regmap_update_bits(tve->grf, PLUMAGE_GRF_SOC_CON0,
				   SW_TVE_DCLK_POL_MASK |
				   SW_TVE_DCLK_EN_MASK |
				   SW_DCLK_UPSAMPLE_EN_MASK |
				   SW_TVE_MODE_MASK | SW_TVE_EN_MASK,
				   SW_TVE_DCLK_POL(0) | SW_TVE_DCLK_EN(1) |
				   SW_DCLK_UPSAMPLE_EN(tve->is_4x) |
				   SW_TVE_MODE(0) | SW_TVE_EN(1));

	ret = rk630_tve_write_block(tve, tve_cfg, 27);
	if (ret < 0) {
		dev_err(tve->dev, "rk630 tve write err\n");
		return ret;
	}

	return ret;
}

static int rk630_tve_disable(struct rk630_tve *tve)
{
	regmap_update_bits(tve->grf, PLUMAGE_GRF_SOC_CON3, VDAC_ENDAC0_MASK,
			   VDAC_ENDAC0(0));

	return 0;
}

static int rk630_tve_enable(struct rk630_tve *tve)
{
	int ret, i;
	u32 val = 0;

	dev_dbg(tve->dev, "%s\n", __func__);

	/* config bt656 input gpio*/
	regmap_write(tve->grf, PLUMAGE_GRF_GPIO0A_IOMUX, 0x55555555);

	regmap_update_bits(tve->grf, PLUMAGE_GRF_GPIO0B_IOMUX, GPIO0B0_SEL_MASK,
			   GPIO0B0_SEL(1));

	regmap_update_bits(tve->grf, PLUMAGE_GRF_SOC_CON3, VDAC_ENDAC0_MASK,
			   VDAC_ENDAC0(0));

	ret = rk630_tve_cfg_set(tve);
	if (ret)
		return ret;

	/*config clk*/
	if (!tve->is_4x) {
		regmap_update_bits(tve->cru, CRU_MODE_CON, CLK_SPLL_MODE_MASK,
				   CLK_SPLL_MODE(2));
	} else {
		regmap_update_bits(tve->cru, CRU_SPLL_CON1, PLLPD0_MASK,
				   PLLPD0(1));

		regmap_update_bits(tve->cru, CRU_MODE_CON, CLK_SPLL_MODE_MASK,
				   CLK_SPLL_MODE(1));

		regmap_update_bits(tve->cru, CRU_SPLL_CON1, PLLPD0_MASK,
				   PLLPD0(0));

		for (i = 0; i < 10; i++) {
			usleep_range(1000, 2000);
			regmap_read(tve->cru, CRU_SPLL_CON1, &val);
			if (val & PLL_LOCK) {
				dev_dbg(tve->dev, "rk630 pll locked\n");
				break;
			}
		}
		if (!(val & PLL_LOCK)) {
			dev_err(tve->dev, "rk630 pll unlock\n");
			return -EINVAL;
		}
	}

	/* enable vdac */
	regmap_update_bits(tve->grf, PLUMAGE_GRF_SOC_CON3,
			   VDAC_ENVBG_MASK | VDAC_ENDAC0_MASK,
			   VDAC_ENVBG(1) | VDAC_ENDAC0(1));

	return 0;
}

static enum drm_mode_status
rk630_tve_mode_valid(struct drm_connector *connector,
		  struct drm_display_mode *mode)
{
	return MODE_OK;
}

static int
rk630_tve_get_modes(struct drm_connector *connector)
{
	int count;
	u32 bus_format = MEDIA_BUS_FMT_UYVY8_2X8;
	struct rk630_tve *tve = connector_to_tve(connector);

	for (count = 0; count < ARRAY_SIZE(rk630_tve_mode); count++) {
		struct drm_display_mode *mode_ptr;

		mode_ptr = drm_mode_duplicate(connector->dev,
					      &rk630_tve_mode[count]);
		if (!mode_ptr) {
			dev_err(tve->dev, "mode duplicate failed\n");
			return -ENOMEM;
		}

		if (!count)
			mode_ptr->type |= DRM_MODE_TYPE_PREFERRED;
		drm_mode_probed_add(connector, mode_ptr);
	}
	drm_display_info_set_bus_formats(&connector->display_info,
					 &bus_format, 1);

	return count;
}

static enum drm_connector_status
rk630_tve_connector_detect(struct drm_connector *connector,
			bool force)
{
	return connector_status_connected;
}

static struct drm_encoder *rk630_tve_best_encoder(struct drm_connector *connector)
{
	struct rk630_tve *tve = connector_to_tve(connector);

	return tve->encoder;
}

static
const struct drm_connector_helper_funcs rk630_tve_connector_helper_funcs = {
	.get_modes = rk630_tve_get_modes,
	.mode_valid = rk630_tve_mode_valid,
	.best_encoder = rk630_tve_best_encoder,
};

static const struct drm_connector_funcs rk630_tve_connector_funcs = {
	.fill_modes = drm_helper_probe_single_connector_modes,
	.detect = rk630_tve_connector_detect,
	.destroy = drm_connector_cleanup,
	.reset = drm_atomic_helper_connector_reset,
	.atomic_duplicate_state = drm_atomic_helper_connector_duplicate_state,
	.atomic_destroy_state = drm_atomic_helper_connector_destroy_state,
};

static void
rk630_tve_bridge_mode_set(struct drm_bridge *bridge,
			  const struct drm_display_mode *mode,
			  const struct drm_display_mode *adjusted_mode)
{
	struct rk630_tve *tve;

	tve = bridge_to_tve(bridge);

	if (adjusted_mode->vdisplay == 576)
		tve->mode = CVBS_PAL;
	else
		tve->mode = CVBS_NTSC;
}

static void rk630_tve_bridge_enable(struct drm_bridge *bridge)
{
	int ret;
	struct rk630_tve *tve = bridge_to_tve(bridge);

	dev_dbg(tve->dev, "%s\n",  __func__);
	ret = rk630_tve_enable(tve);
	if (ret)
		dev_err(tve->dev, "rk630 enable failed\n");
}

static void rk630_tve_bridge_disable(struct drm_bridge *bridge)
{
	struct rk630_tve *tve = bridge_to_tve(bridge);

	dev_dbg(tve->dev, "%s\n",  __func__);
	rk630_tve_disable(tve);
}

static int rk630_tve_bridge_attach(struct drm_bridge *bridge,
				   enum drm_bridge_attach_flags flags)
{
	struct rk630_tve *tve = bridge_to_tve(bridge);
	int ret;

	if (!bridge->encoder) {
		dev_err(tve->dev, "Parent encoder object not found\n");
		return -ENODEV;
	}

	tve->encoder = bridge->encoder;
	ret = drm_connector_init(bridge->dev, &tve->connector,
				 &rk630_tve_connector_funcs,
				 DRM_MODE_CONNECTOR_TV);
	if (ret) {
		dev_err(tve->dev, "Failed to initialize connector\n");
		return ret;
	}

	drm_connector_helper_add(&tve->connector,
				 &rk630_tve_connector_helper_funcs);
	ret = drm_connector_attach_encoder(&tve->connector,
					   bridge->encoder);

	if (ret)
		dev_err(tve->dev, "rk630 attach failed ret:%d", ret);
	tve->sub_dev.connector = &tve->connector;
	tve->sub_dev.of_node = tve->dev->of_node;
	rockchip_drm_register_sub_dev(&tve->sub_dev);
	tve->connector.interlace_allowed = 1;

	return ret;
}

static void rk1000_bridge_detach(struct drm_bridge *bridge)
{
	struct rk630_tve *tve = bridge_to_tve(bridge);

	rockchip_drm_unregister_sub_dev(&tve->sub_dev);
}

static struct drm_bridge_funcs rk630_tve_bridge_funcs = {
	.enable = rk630_tve_bridge_enable,
	.disable = rk630_tve_bridge_disable,
	.mode_set = rk630_tve_bridge_mode_set,
	.attach = rk630_tve_bridge_attach,
	.detach = rk1000_bridge_detach,
};

static int rk630_tve_probe(struct platform_device *pdev)
{
	struct rk630 *rk630 = dev_get_drvdata(pdev->dev.parent);
	struct rk630_tve *tve;
	struct device *dev = &pdev->dev;

	if (!of_device_is_available(dev->of_node))
		return -ENODEV;

	tve = devm_kzalloc(dev, sizeof(*tve), GFP_KERNEL);
	if (!tve)
		return -ENOMEM;

	tve->dev = dev;
	tve->parent = rk630;
	platform_set_drvdata(pdev, tve);

	tve->grf = rk630->grf;
	tve->cru = rk630->cru;
	tve->tvemap = rk630->tve;
	if (!tve->grf | !tve->cru | !tve->tvemap)
		return -ENODEV;

	tve->mode = CVBS_PAL;

	tve->bridge.funcs = &rk630_tve_bridge_funcs;
	tve->bridge.of_node = tve->dev->of_node;

	drm_bridge_add(&tve->bridge);

	dev_dbg(tve->dev, "rk630 probe tve ok\n");

	return 0;
}

static int rk630_tve_remove(struct platform_device *pdev)
{
	struct rk630_tve *tve = platform_get_drvdata(pdev);

	drm_bridge_remove(&tve->bridge);

	return 0;
}

static const struct of_device_id rk630_tve_dt_ids[] = {
	{ .compatible = "rockchip,rk630-tve" },
	{ }
};

MODULE_DEVICE_TABLE(of, rk630_tve_dt_ids);

static struct platform_driver rk630_tve_driver = {
	.driver = {
		.name = "rk630-tve",
		.of_match_table = of_match_ptr(rk630_tve_dt_ids),
	},
	.probe = rk630_tve_probe,
	.remove = rk630_tve_remove,
};
module_platform_driver(rk630_tve_driver);

MODULE_AUTHOR("Algea Cao <Algea.cao@rock-chips.com>");
MODULE_DESCRIPTION("ROCKCHIP rk630 TVE Driver");
MODULE_LICENSE("GPL v2");
