/* SPDX-License-Identifier: GPL-2.0 */
#include <linux/module.h>
#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/err.h>
#include <linux/hdmi.h>
#include <linux/mutex.h>
#include <linux/mfd/syscon.h>
#include <linux/nvmem-consumer.h>
#include <linux/of_device.h>
#include <linux/pm_runtime.h>
#include <linux/regmap.h>

#include <drm/drm_atomic_helper.h>
#include <drm/drm_crtc_helper.h>
#include <drm/drm_of.h>
#include <drm/drm_probe_helper.h>

#include <uapi/linux/videodev2.h>

#include "rockchip_drm_drv.h"
#include "rockchip_drm_tve.h"
#include "rockchip_drm_vop.h"

#define RK322X_VDAC_STANDARD 0x15

static const struct drm_display_mode cvbs_mode[] = {
	{ DRM_MODE("720x576i", DRM_MODE_TYPE_DRIVER, 13500, 720, 753,
		   816, 864, 0, 576, 580, 586, 625, 0,
		   DRM_MODE_FLAG_PHSYNC | DRM_MODE_FLAG_PVSYNC |
		   DRM_MODE_FLAG_INTERLACE | DRM_MODE_FLAG_DBLCLK),
		   0, },

	{ DRM_MODE("720x480i", DRM_MODE_TYPE_DRIVER, 13500, 720, 753,
		   815, 858, 0, 480, 480, 486, 525, 0,
		   DRM_MODE_FLAG_PHSYNC | DRM_MODE_FLAG_PVSYNC |
		   DRM_MODE_FLAG_INTERLACE | DRM_MODE_FLAG_DBLCLK),
		   0, },
};

#define tve_writel(offset, v)		writel_relaxed(v, tve->regbase + (offset))
#define tve_readl(offset)		readl_relaxed(tve->regbase + (offset))

#define tve_dac_writel(offset, v)	writel_relaxed(v, tve->vdacbase + (offset))
#define tve_dac_readl(offset)		readl_relaxed(tve->vdacbase + (offset))

#define tve_dac_grf_writel(offset, v)	regmap_write(tve->dac_grf, offset, v)
#define tve_dac_grf_readl(offset, v)	regmap_read(tve->dac_grf, offset, v)

#define connector_to_tve(x)		container_of(x, struct rockchip_tve, connector)
#define encoder_to_tve(x)		container_of(x, struct rockchip_tve, encoder)

struct rockchip_tve_data {
	int input_format;
	int soc_type;
};

static int
rockchip_tve_get_modes(struct drm_connector *connector)
{
	int count;
	struct rockchip_tve *tve = connector_to_tve(connector);

	for (count = 0; count < ARRAY_SIZE(cvbs_mode); count++) {
		struct drm_display_mode *mode_ptr;

		mode_ptr = drm_mode_duplicate(connector->dev,
					      &cvbs_mode[count]);
		if (tve->preferred_mode == count)
			mode_ptr->type |= DRM_MODE_TYPE_PREFERRED;
		drm_mode_probed_add(connector, mode_ptr);
	}

	return count;
}

static enum drm_mode_status
rockchip_tve_mode_valid(struct drm_connector *connector,
			struct drm_display_mode *mode)
{
	return MODE_OK;
}

static struct drm_encoder *rockchip_tve_best_encoder(struct drm_connector
						     *connector)
{
	struct rockchip_tve *tve = connector_to_tve(connector);

	return &tve->encoder;
}

static void rockchip_encoder_destroy(struct drm_encoder *encoder)
{
	drm_encoder_cleanup(encoder);
}

static enum drm_connector_status
rockchip_tve_connector_detect(struct drm_connector *connector, bool force)
{
	return connector_status_connected;
}

static void rockchip_tve_connector_destroy(struct drm_connector *connector)
{
	drm_connector_cleanup(connector);
}

static void tve_set_mode(struct rockchip_tve *tve)
{
	int mode = tve->tv_format;

	dev_dbg(tve->dev, "tve set mode:%d\n", mode);
	if (tve->input_format == INPUT_FORMAT_RGB)
		tve_writel(TV_CTRL, v_CVBS_MODE(mode) | v_CLK_UPSTREAM_EN(2) |
			   v_TIMING_EN(2) | v_LUMA_FILTER_GAIN(0) |
			   v_LUMA_FILTER_UPSAMPLE(1) | v_CSC_PATH(0));
	else
		tve_writel(TV_CTRL, v_CVBS_MODE(mode) | v_CLK_UPSTREAM_EN(2) |
			   v_TIMING_EN(2) | v_LUMA_FILTER_GAIN(0) |
			   v_LUMA_FILTER_UPSAMPLE(1) | v_CSC_PATH(3));

	tve_writel(TV_LUMA_FILTER0, tve->lumafilter0);
	tve_writel(TV_LUMA_FILTER1, tve->lumafilter1);
	tve_writel(TV_LUMA_FILTER2, tve->lumafilter2);

	if (mode == TVOUT_CVBS_NTSC) {
		dev_dbg(tve->dev, "NTSC MODE\n");
		tve_writel(TV_ROUTING, v_DAC_SENSE_EN(0) | v_Y_IRE_7_5(1) |
			v_Y_AGC_PULSE_ON(0) | v_Y_VIDEO_ON(1) |
			v_YPP_MODE(1) | v_Y_SYNC_ON(1) | v_PIC_MODE(mode));
		tve_writel(TV_BW_CTRL, v_CHROMA_BW(BP_FILTER_NTSC) |
			v_COLOR_DIFF_BW(COLOR_DIFF_FILTER_BW_1_3));
		tve_writel(TV_SATURATION, 0x0042543C);
		if (tve->test_mode)
			tve_writel(TV_BRIGHTNESS_CONTRAST, 0x00008300);
		else
			tve_writel(TV_BRIGHTNESS_CONTRAST, 0x00007900);

		tve_writel(TV_FREQ_SC,	0x21F07BD7);
		tve_writel(TV_SYNC_TIMING, 0x00C07a81);
		tve_writel(TV_ADJ_TIMING, 0x96B40000 | 0x70);
		tve_writel(TV_ACT_ST,	0x001500D6);
		tve_writel(TV_ACT_TIMING, 0x069800FC | (1 << 12) | (1 << 28));

	} else if (mode == TVOUT_CVBS_PAL) {
		dev_dbg(tve->dev, "PAL MODE\n");
		tve_writel(TV_ROUTING, v_DAC_SENSE_EN(0) | v_Y_IRE_7_5(0) |
			v_Y_AGC_PULSE_ON(0) | v_Y_VIDEO_ON(1) |
			v_YPP_MODE(1) | v_Y_SYNC_ON(1) | v_PIC_MODE(mode));
		tve_writel(TV_BW_CTRL, v_CHROMA_BW(BP_FILTER_PAL) |
			v_COLOR_DIFF_BW(COLOR_DIFF_FILTER_BW_1_3));

		tve_writel(TV_SATURATION, tve->saturation);
		tve_writel(TV_BRIGHTNESS_CONTRAST, tve->brightcontrast);

		tve_writel(TV_FREQ_SC,	0x2A098ACB);
		tve_writel(TV_SYNC_TIMING, 0x00C28381);
		tve_writel(TV_ADJ_TIMING, (0xc << 28) | 0x06c00800 | 0x80);
		tve_writel(TV_ACT_ST,	0x001500F6);
		tve_writel(TV_ACT_TIMING, 0x0694011D | (1 << 12) | (2 << 28));

		tve_writel(TV_ADJ_TIMING, tve->adjtiming);
		tve_writel(TV_ACT_TIMING, 0x0694011D |
			   (1 << 12) | (2 << 28));
	}
}

static void dac_init(struct rockchip_tve *tve)
{
	tve_dac_writel(VDAC_VDAC1, v_CUR_REG(tve->dac1level) |
				   m_DR_PWR_DOWN | m_BG_PWR_DOWN);
	tve_dac_writel(VDAC_VDAC2, v_CUR_CTR(tve->daclevel));
	tve_dac_writel(VDAC_VDAC3, v_CAB_EN(0));
}

static void dac_enable(struct rockchip_tve *tve, bool enable)
{
	u32 mask = 0;
	u32 val = 0;
	u32 grfreg = 0;

	if (enable) {
		dev_dbg(tve->dev, "dac enable\n");

		mask = m_VBG_EN | m_DAC_EN | m_DAC_GAIN;
		if (tve->soc_type == SOC_RK3036) {
			val = m_VBG_EN | m_DAC_EN | v_DAC_GAIN(tve->daclevel);
			grfreg = RK3036_GRF_SOC_CON3;
		} else if (tve->soc_type == SOC_RK312X) {
			val = m_VBG_EN | m_DAC_EN | v_DAC_GAIN(tve->daclevel);
			grfreg = RK312X_GRF_TVE_CON;
		} else if (tve->soc_type == SOC_RK322X || tve->soc_type == SOC_RK3328) {
			val = v_CUR_REG(tve->dac1level) | v_DR_PWR_DOWN(0) | v_BG_PWR_DOWN(0);
		}
	} else {
		dev_dbg(tve->dev, "dac disable\n");

		mask = m_VBG_EN | m_DAC_EN;
		if (tve->soc_type == SOC_RK312X)
			grfreg = RK312X_GRF_TVE_CON;
		else if (tve->soc_type == SOC_RK3036)
			grfreg = RK3036_GRF_SOC_CON3;
		else if (tve->soc_type == SOC_RK322X || tve->soc_type == SOC_RK3328)
			val = v_CUR_REG(tve->dac1level) | m_DR_PWR_DOWN | m_BG_PWR_DOWN;
	}

	if (grfreg)
		tve_dac_grf_writel(grfreg, (mask << 16) | val);
	else if (tve->vdacbase)
		tve_dac_writel(VDAC_VDAC1, val);
}

static int cvbs_set_disable(struct rockchip_tve *tve)
{
	int ret = 0;

	dev_dbg(tve->dev, "%s\n", __func__);
	if (!tve->enable)
		return 0;

	ret = pm_runtime_put(tve->dev);
	if (ret < 0) {
		dev_err(tve->dev, "failed to put pm runtime: %d\n", ret);
		return ret;
	}
	dac_enable(tve, false);
	tve->enable = 0;

	return 0;
}

static int cvbs_set_enable(struct rockchip_tve *tve)
{
	int ret = 0;

	dev_dbg(tve->dev, "%s\n", __func__);
	if (tve->enable)
		return 0;

	ret = pm_runtime_get_sync(tve->dev);
	if (ret < 0) {
		dev_err(tve->dev, "failed to get pm runtime: %d\n", ret);
		return ret;
	}
	dac_enable(tve, true);
	tve_set_mode(tve);
	tve->enable = 1;

	return 0;
}

static void rockchip_tve_encoder_enable(struct drm_encoder *encoder)
{
	struct rockchip_tve *tve = encoder_to_tve(encoder);

	mutex_lock(&tve->suspend_lock);

	dev_dbg(tve->dev, "tve encoder enable\n");
	cvbs_set_enable(tve);

	mutex_unlock(&tve->suspend_lock);
}

static void rockchip_tve_encoder_disable(struct drm_encoder *encoder)
{
	struct rockchip_tve *tve = encoder_to_tve(encoder);

	mutex_lock(&tve->suspend_lock);

	dev_dbg(tve->dev, "tve encoder enable\n");
	cvbs_set_disable(tve);

	mutex_unlock(&tve->suspend_lock);
}

static void rockchip_tve_encoder_mode_set(struct drm_encoder *encoder,
					  struct drm_display_mode *mode,
				struct drm_display_mode *adjusted_mode)
{
	struct rockchip_tve *tve = encoder_to_tve(encoder);

	dev_dbg(tve->dev, "encoder mode set:%s\n", adjusted_mode->name);

	if (adjusted_mode->vdisplay == 576)
		tve->tv_format = TVOUT_CVBS_PAL;
	else
		tve->tv_format = TVOUT_CVBS_NTSC;

	if (tve->enable) {
		dac_enable(tve, false);
		msleep(200);

		tve_set_mode(tve);
		dac_enable(tve, true);
	}
}

static bool
rockchip_tve_encoder_mode_fixup(struct drm_encoder *encoder,
				const struct drm_display_mode *mode,
				struct drm_display_mode *adjusted_mode)
{
	return true;
}

static int
rockchip_tve_encoder_atomic_check(struct drm_encoder *encoder,
				  struct drm_crtc_state *crtc_state,
				  struct drm_connector_state *conn_state)
{
	struct rockchip_crtc_state *s = to_rockchip_crtc_state(crtc_state);
	struct drm_connector *connector = conn_state->connector;
	struct drm_display_info *info = &connector->display_info;

	s->output_mode = ROCKCHIP_OUT_MODE_P888;
	s->output_type = DRM_MODE_CONNECTOR_TV;
	if (info->num_bus_formats)
		s->bus_format = info->bus_formats[0];
	else
		s->bus_format = MEDIA_BUS_FMT_YUV8_1X24;

	s->color_space = V4L2_COLORSPACE_SMPTE170M;
	s->tv_state = &conn_state->tv;

	return 0;
}

static const struct drm_connector_helper_funcs
rockchip_tve_connector_helper_funcs = {
	.mode_valid = rockchip_tve_mode_valid,
	.get_modes = rockchip_tve_get_modes,
	.best_encoder = rockchip_tve_best_encoder,
};

static const struct drm_encoder_funcs rockchip_tve_encoder_funcs = {
	.destroy = rockchip_encoder_destroy,
};

static const struct drm_connector_funcs rockchip_tve_connector_funcs = {
	.detect = rockchip_tve_connector_detect,
	.fill_modes = drm_helper_probe_single_connector_modes,
	.destroy = rockchip_tve_connector_destroy,
	.reset = drm_atomic_helper_connector_reset,
	.atomic_duplicate_state = drm_atomic_helper_connector_duplicate_state,
	.atomic_destroy_state = drm_atomic_helper_connector_destroy_state,
};

static const struct drm_encoder_helper_funcs
rockchip_tve_encoder_helper_funcs = {
	.mode_fixup = rockchip_tve_encoder_mode_fixup,
	.mode_set = rockchip_tve_encoder_mode_set,
	.enable = rockchip_tve_encoder_enable,
	.disable = rockchip_tve_encoder_disable,
	.atomic_check = rockchip_tve_encoder_atomic_check,
};

static int tve_parse_dt(struct device_node *np,
			struct rockchip_tve *tve)
{
	int ret, val;
	u32 getdac = 0;
	size_t len;
	struct nvmem_cell *cell;
	unsigned char *efuse_buf;

	ret = of_property_read_u32(np, "rockchip,tvemode", &val);
	if (ret < 0) {
		tve->preferred_mode = 0;
	} else if (val > 1) {
		dev_err(tve->dev, "tve mode value invalid\n");
		return -EINVAL;
	}
	tve->preferred_mode = val;

	ret = of_property_read_u32(np, "rockchip,saturation", &val);
	if (val == 0 || ret < 0)
		return -EINVAL;
	tve->saturation = val;

	ret = of_property_read_u32(np, "rockchip,brightcontrast", &val);
	if (val == 0 || ret < 0)
		return -EINVAL;
	tve->brightcontrast = val;

	ret = of_property_read_u32(np, "rockchip,adjtiming", &val);
	if (val == 0 || ret < 0)
		return -EINVAL;
	tve->adjtiming = val;

	ret = of_property_read_u32(np, "rockchip,lumafilter0", &val);
	if (val == 0 || ret < 0)
		return -EINVAL;
	tve->lumafilter0 = val;

	ret = of_property_read_u32(np, "rockchip,lumafilter1", &val);
	if (val == 0 || ret < 0)
		return -EINVAL;
	tve->lumafilter1 = val;

	ret = of_property_read_u32(np, "rockchip,lumafilter2", &val);
	if (val == 0 || ret < 0)
		return -EINVAL;
	tve->lumafilter2 = val;

	ret = of_property_read_u32(np, "rockchip,daclevel", &val);
	if (val == 0 || ret < 0) {
		return -EINVAL;
	} else {
		tve->daclevel = val;
		if (tve->soc_type == SOC_RK322X || tve->soc_type == SOC_RK3328) {
			cell = nvmem_cell_get(tve->dev, "tve_dac_adj");
			if (IS_ERR(cell)) {
				dev_dbg(tve->dev, "failed to get id cell: %ld\n", PTR_ERR(cell));
			} else {
				efuse_buf = nvmem_cell_read(cell, &len);
				nvmem_cell_put(cell);
				if (IS_ERR(efuse_buf))
					return PTR_ERR(efuse_buf);
				if (len == 1)
					getdac = efuse_buf[0];
				kfree(efuse_buf);

				if (getdac > 0) {
					tve->daclevel = getdac + 5 + val - RK322X_VDAC_STANDARD;
					if (tve->daclevel > 0x3f) {
						dev_err(tve->dev, "rk322x daclevel error!\n");
						tve->daclevel = val;
					}
				}
			}
		}
	}

	if (tve->soc_type == SOC_RK322X || tve->soc_type == SOC_RK3328) {
		ret = of_property_read_u32(np, "rockchip,dac1level", &val);
		if ((val == 0) || (ret < 0))
			return -EINVAL;
		tve->dac1level = val;
	}


	return 0;
}

static void check_uboot_logo(struct rockchip_tve *tve)
{
	int lumafilter0, lumafilter1, lumafilter2, vdac;

	if (tve->soc_type == SOC_RK322X || tve->soc_type == SOC_RK3328) {
		vdac = tve_dac_readl(VDAC_VDAC1);
		/* Whether the dac power has been turned down. */
		if (vdac & m_DR_PWR_DOWN) {
			tve->connector.dpms = DRM_MODE_DPMS_OFF;
			return;
		}
	}

	lumafilter0 = tve_readl(TV_LUMA_FILTER0);
	lumafilter1 = tve_readl(TV_LUMA_FILTER1);
	lumafilter2 = tve_readl(TV_LUMA_FILTER2);

	/*
	 * The default lumafilter value is 0. If lumafilter value
	 * is equal to the dts value, uboot logo is enabled.
	 */
	if (lumafilter0 == tve->lumafilter0 &&
	    lumafilter1 == tve->lumafilter1 &&
	    lumafilter2 == tve->lumafilter2) {
		tve->connector.dpms = DRM_MODE_DPMS_ON;
		return;
	}

	if (tve->soc_type == SOC_RK322X || tve->soc_type == SOC_RK3328)
		dac_init(tve);

	tve->connector.dpms = DRM_MODE_DPMS_OFF;
}

static const struct rockchip_tve_data rk3036_tve = {
	.soc_type = SOC_RK3036,
	.input_format = INPUT_FORMAT_RGB,
};

static const struct rockchip_tve_data rk312x_tve = {
	.soc_type = SOC_RK312X,
	.input_format = INPUT_FORMAT_RGB,
};

static const struct rockchip_tve_data rk322x_tve = {
	.soc_type = SOC_RK322X,
	.input_format = INPUT_FORMAT_YUV,
};

static const struct rockchip_tve_data rk3328_tve = {
	.soc_type = SOC_RK3328,
	.input_format = INPUT_FORMAT_YUV,
};

static const struct of_device_id rockchip_tve_dt_ids[] = {
	{ .compatible = "rockchip,rk3036-tve", .data = &rk3036_tve },
	{ .compatible = "rockchip,rk312x-tve", .data = &rk312x_tve },
	{ .compatible = "rockchip,rk322x-tve", .data = &rk322x_tve },
	{ .compatible = "rockchip,rk3328-tve", .data = &rk3328_tve },
	{}
};

MODULE_DEVICE_TABLE(of, rockchip_tve_dt_ids);

static int rockchip_tve_bind(struct device *dev, struct device *master,
			     void *data)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct drm_device *drm_dev = data;
	struct device_node *np = dev->of_node;
	const struct of_device_id *match;
	const struct rockchip_tve_data *tve_data;
	struct rockchip_tve *tve;
	struct resource *res;
	struct drm_encoder *encoder;
	struct drm_connector *connector;
	int ret;

	tve = devm_kzalloc(dev, sizeof(*tve), GFP_KERNEL);
	if (!tve)
		return -ENOMEM;

	match = of_match_node(rockchip_tve_dt_ids, np);
	if (!match) {
		dev_err(tve->dev, "tve can't match node\n");
		return -EINVAL;
	}

	tve->dev = &pdev->dev;
	tve_data = of_device_get_match_data(dev);
	if (tve_data) {
		tve->soc_type = tve_data->soc_type;
		tve->input_format = tve_data->input_format;
	}

	ret = tve_parse_dt(np, tve);
	if (ret) {
		dev_err(tve->dev, "TVE parse dts error!");
		return -EINVAL;
	}

	tve->enable = 0;
	tve->drm_dev = drm_dev;
	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	tve->reg_phy_base = res->start;
	tve->len = resource_size(res);
	tve->regbase = devm_ioremap(tve->dev, res->start, tve->len);
	if (IS_ERR(tve->regbase)) {
		dev_err(tve->dev,
			"tv encoder device map registers failed!");
		return PTR_ERR(tve->regbase);
	}

	if (tve->soc_type == SOC_RK322X || tve->soc_type == SOC_RK3328) {
		res = platform_get_resource(pdev, IORESOURCE_MEM, 1);
		tve->len = resource_size(res);
		tve->vdacbase = devm_ioremap(tve->dev, res->start, tve->len);
		if (IS_ERR(tve->vdacbase)) {
			dev_err(tve->dev, "tv encoder device dac map registers failed!");
			return PTR_ERR(tve->vdacbase);
		}
	}

	if (tve->soc_type == SOC_RK3036) {
		tve->aclk = devm_clk_get(tve->dev, "aclk");
		if (IS_ERR(tve->aclk)) {
			dev_err(tve->dev, "Unable to get tve aclk\n");
			return PTR_ERR(tve->aclk);
		}

		ret = clk_prepare_enable(tve->aclk);
		if (ret) {
			dev_err(tve->dev, "Cannot enable tve aclk: %d\n", ret);
			return ret;
		}
	}

	tve->dac_grf = syscon_regmap_lookup_by_phandle(dev->of_node, "rockchip,grf");

	mutex_init(&tve->suspend_lock);
	check_uboot_logo(tve);
	tve->tv_format = TVOUT_CVBS_PAL;
	encoder = &tve->encoder;
	encoder->possible_crtcs = rockchip_drm_of_find_possible_crtcs(drm_dev,
								      dev->of_node);
	dev_dbg(tve->dev, "possible_crtc:%d\n", encoder->possible_crtcs);

	ret = drm_encoder_init(drm_dev, encoder, &rockchip_tve_encoder_funcs,
			       DRM_MODE_ENCODER_TVDAC, NULL);
	if (ret < 0) {
		dev_err(tve->dev, "failed to initialize encoder with drm\n");
		goto err_disable_aclk;
	}

	drm_encoder_helper_add(encoder, &rockchip_tve_encoder_helper_funcs);

	connector = &tve->connector;
	connector->interlace_allowed = 1;
	ret = drm_connector_init(drm_dev, connector,
				 &rockchip_tve_connector_funcs,
				 DRM_MODE_CONNECTOR_TV);
	if (ret < 0) {
		dev_dbg(tve->dev, "failed to initialize connector with drm\n");
		goto err_free_encoder;
	}

	drm_connector_helper_add(connector,
				 &rockchip_tve_connector_helper_funcs);

	ret = drm_connector_attach_encoder(connector, encoder);
	if (ret < 0) {
		dev_dbg(tve->dev, "failed to attach connector and encoder\n");
		goto err_free_connector;
	}
	tve->sub_dev.connector = &tve->connector;
	tve->sub_dev.of_node = tve->dev->of_node;
	rockchip_drm_register_sub_dev(&tve->sub_dev);

	pm_runtime_enable(dev);
	dev_set_drvdata(dev, tve);
	dev_dbg(tve->dev, "%s tv encoder probe ok\n", match->compatible);

	return 0;

err_free_connector:
	drm_connector_cleanup(connector);
err_free_encoder:
	drm_encoder_cleanup(encoder);
err_disable_aclk:
	if (tve->soc_type == SOC_RK3036)
		clk_disable_unprepare(tve->aclk);

	return ret;
}

static void rockchip_tve_unbind(struct device *dev, struct device *master,
				void *data)
{
	struct rockchip_tve *tve = dev_get_drvdata(dev);

	rockchip_drm_unregister_sub_dev(&tve->sub_dev);
	rockchip_tve_encoder_disable(&tve->encoder);

	drm_connector_cleanup(&tve->connector);
	drm_encoder_cleanup(&tve->encoder);

	pm_runtime_disable(dev);
	dev_set_drvdata(dev, NULL);
}

static const struct component_ops rockchip_tve_component_ops = {
	.bind = rockchip_tve_bind,
	.unbind = rockchip_tve_unbind,
};

static int rockchip_tve_probe(struct platform_device *pdev)
{
	component_add(&pdev->dev, &rockchip_tve_component_ops);

	return 0;
}

static void rockchip_tve_shutdown(struct platform_device *pdev)
{
	struct rockchip_tve *tve = dev_get_drvdata(&pdev->dev);

	if (!tve)
		return;

	mutex_lock(&tve->suspend_lock);

	dev_dbg(tve->dev, "tve shutdown\n");
	cvbs_set_disable(tve);

	mutex_unlock(&tve->suspend_lock);
}

static int rockchip_tve_remove(struct platform_device *pdev)
{
	component_del(&pdev->dev, &rockchip_tve_component_ops);

	return 0;
}

struct platform_driver rockchip_tve_driver = {
	.probe = rockchip_tve_probe,
	.remove = rockchip_tve_remove,
	.shutdown = rockchip_tve_shutdown,
	.driver = {
		   .name = "rockchip-tve",
		   .of_match_table = of_match_ptr(rockchip_tve_dt_ids),
	},
};

MODULE_AUTHOR("Algea Cao <Algea.cao@rock-chips.com>");
MODULE_DESCRIPTION("ROCKCHIP TVE Driver");
MODULE_LICENSE("GPL v2");
