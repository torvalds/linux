#include <linux/module.h>
#include <linux/delay.h>
#include <linux/err.h>
#include <linux/hdmi.h>
#include <linux/mutex.h>
#include <linux/of_device.h>
#include <linux/pm_runtime.h>

#include <drm/drmP.h>
#include <drm/drm_atomic_helper.h>
#include <drm/drm_crtc_helper.h>
#include <drm/drm_of.h>

#include "rockchip_drm_drv.h"
#include "rockchip_drm_tve.h"
#include "rockchip_drm_vop.h"

static const struct drm_display_mode cvbs_mode[] = {
	{ DRM_MODE("720x576i", DRM_MODE_TYPE_DRIVER |
		   DRM_MODE_TYPE_PREFERRED, 13500, 720, 732,
		   795, 864, 0, 576, 580, 586, 625, 0,
		   DRM_MODE_FLAG_PHSYNC | DRM_MODE_FLAG_PVSYNC |
		   DRM_MODE_FLAG_INTERLACE | DRM_MODE_FLAG_DBLCLK),
		   .vrefresh = 50, 0, },
	{ DRM_MODE("720x480i", DRM_MODE_TYPE_DRIVER, 13500, 720, 739,
		   801, 858, 0, 480, 488, 494, 525, 0,
		   DRM_MODE_FLAG_PHSYNC | DRM_MODE_FLAG_PVSYNC |
		   DRM_MODE_FLAG_INTERLACE | DRM_MODE_FLAG_DBLCLK),
		   .vrefresh = 60, 0, },
};

#define tve_writel(offset, v)	writel_relaxed(v, tve->regbase + (offset))
#define tve_readl(offset)	readl_relaxed(tve->regbase + (offset))

#define tve_dac_writel(offset, v)   writel_relaxed(v, tve->vdacbase + (offset))
#define tve_dac_readl(offset)	readl_relaxed(tve->vdacbase + (offset))

#define connector_to_tve(x) container_of(x, struct rockchip_tve, connector)
#define encoder_to_tve(x) container_of(x, struct rockchip_tve, encoder)

static int
rockchip_tve_get_modes(struct drm_connector *connector)
{
	int count;

	for (count = 0; count < ARRAY_SIZE(cvbs_mode); count++) {
		struct drm_display_mode *mode_ptr;

		mode_ptr = drm_mode_duplicate(connector->dev, &cvbs_mode[count]);
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
	if (tve->inputformat == INPUT_FORMAT_RGB)
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
	u32 val;

	if (enable) {
		dev_dbg(tve->dev, "dac enable\n");
		val = 0x70;
	} else {
		dev_dbg(tve->dev, "dac disable\n");
		val = v_CUR_REG(0x7) | m_DR_PWR_DOWN | m_BG_PWR_DOWN;
	}

	if (tve->vdacbase)
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
	.dpms = drm_atomic_helper_connector_dpms,
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
	int ret;
	u32 val;

	ret = of_property_read_u32(np, "rockchip,saturation", &val);
	if ((val == 0) || (ret < 0))
		return -EINVAL;
	tve->saturation = val;

	ret = of_property_read_u32(np, "rockchip,brightcontrast", &val);
	if ((val == 0) || (ret < 0))
		return -EINVAL;
	tve->brightcontrast = val;

	ret = of_property_read_u32(np, "rockchip,adjtiming", &val);
	if ((val == 0) || (ret < 0))
		return -EINVAL;
	tve->adjtiming = val;

	ret = of_property_read_u32(np, "rockchip,lumafilter0", &val);
	if ((val == 0) || (ret < 0))
		return -EINVAL;
	tve->lumafilter0 = val;

	ret = of_property_read_u32(np, "rockchip,lumafilter1", &val);
	if ((val == 0) || (ret < 0))
		return -EINVAL;
	tve->lumafilter1 = val;

	ret = of_property_read_u32(np, "rockchip,lumafilter2", &val);
	if ((val == 0) || (ret < 0))
		return -EINVAL;
	tve->lumafilter2 = val;

	ret = of_property_read_u32(np, "rockchip,daclevel", &val);
	if ((val == 0) || (ret < 0))
		return -EINVAL;
	tve->daclevel = val;

	ret = of_property_read_u32(np, "rockchip,dac1level", &val);
	if ((val == 0) || (ret < 0))
		return -EINVAL;
	tve->dac1level = val;

	return 0;
}

static const struct of_device_id rockchip_tve_dt_ids[] = {
	{
		.compatible = "rockchip,rk3328-tve",
	},
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

	if (!strcmp(match->compatible, "rockchip,rk3328-tve")) {
		tve->inputformat = INPUT_FORMAT_YUV;
	} else {
		dev_err(tve->dev, "It is not a valid tv encoder! ");
		return -ENOMEM;
	}

	ret = tve_parse_dt(np, tve);
	if (ret) {
		dev_err(tve->dev, "TVE parse dts error!");
		return -EINVAL;
	}

	tve->enable = 0;
	platform_set_drvdata(pdev, tve);
	tve->dev = &pdev->dev;
	tve->drm_dev = drm_dev;
	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	tve->reg_phy_base = res->start;
	tve->len = resource_size(res);
	tve->regbase = devm_ioremap(tve->dev, res->start, tve->len);
	if (IS_ERR(tve->regbase)) {
		dev_err(tve->dev,
			"rk3328 tv encoder device map registers failed!");
		return PTR_ERR(tve->regbase);
	}

	res = platform_get_resource(pdev, IORESOURCE_MEM, 1);
	tve->len = resource_size(res);
	tve->vdacbase = devm_ioremap(tve->dev, res->start, tve->len);
	if (IS_ERR(tve->vdacbase)) {
		dev_err(tve->dev,
			"rk3328 tv encoder device dac map registers failed!");
		return PTR_ERR(tve->vdacbase);
	}

	dac_init(tve);

	mutex_init(&tve->suspend_lock);

	tve->tv_format = TVOUT_CVBS_PAL;
	encoder = &tve->encoder;
	encoder->possible_crtcs = drm_of_find_possible_crtcs(drm_dev,
							     dev->of_node);
	dev_dbg(tve->dev, "possible_crtc:%d\n", encoder->possible_crtcs);

	ret = drm_encoder_init(drm_dev, encoder, &rockchip_tve_encoder_funcs,
			       DRM_MODE_ENCODER_TVDAC, NULL);
	if (ret < 0) {
		dev_err(tve->dev, "failed to initialize encoder with drm\n");
		return ret;
	}

	drm_encoder_helper_add(encoder, &rockchip_tve_encoder_helper_funcs);

	connector = &tve->connector;
	connector->dpms = DRM_MODE_DPMS_OFF;

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

	ret = drm_mode_connector_attach_encoder(connector, encoder);
	if (ret < 0) {
		dev_dbg(tve->dev, "failed to attach connector and encoder\n");
		goto err_free_connector;
	}

	pm_runtime_enable(dev);
	dev_dbg(tve->dev, "%s tv encoder probe ok\n", match->compatible);

	return 0;

err_free_connector:
	drm_connector_cleanup(connector);
err_free_encoder:
	drm_encoder_cleanup(encoder);
	return ret;
}

static void rockchip_tve_unbind(struct device *dev, struct device *master,
				void *data)
{
	struct rockchip_tve *tve = dev_get_drvdata(dev);

	rockchip_tve_encoder_disable(&tve->encoder);

	drm_connector_cleanup(&tve->connector);
	drm_encoder_cleanup(&tve->encoder);

	pm_runtime_disable(dev);
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

static int rockchip_tve_remove(struct platform_device *pdev)
{
	component_del(&pdev->dev, &rockchip_tve_component_ops);

	return 0;
}

struct platform_driver rockchip_tve_driver = {
	.probe = rockchip_tve_probe,
	.remove = rockchip_tve_remove,
	.driver = {
		   .name = "rockchip-tve",
		   .of_match_table = of_match_ptr(rockchip_tve_dt_ids),
	},
};
module_platform_driver(rockchip_tve_driver);

MODULE_AUTHOR("Algea Cao <Algea.cao@rock-chips.com>");
MODULE_DESCRIPTION("ROCKCHIP TVE Driver");
MODULE_LICENSE("GPL v2");
