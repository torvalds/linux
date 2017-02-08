/*
 * i.MX drm driver - Television Encoder (TVEv2)
 *
 * Copyright (C) 2013 Philipp Zabel, Pengutronix
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <linux/clk.h>
#include <linux/clk-provider.h>
#include <linux/component.h>
#include <linux/module.h>
#include <linux/i2c.h>
#include <linux/regmap.h>
#include <linux/regulator/consumer.h>
#include <linux/spinlock.h>
#include <linux/videodev2.h>
#include <drm/drmP.h>
#include <drm/drm_atomic_helper.h>
#include <drm/drm_fb_helper.h>
#include <drm/drm_crtc_helper.h>
#include <video/imx-ipu-v3.h>

#include "imx-drm.h"

#define TVE_COM_CONF_REG	0x00
#define TVE_TVDAC0_CONT_REG	0x28
#define TVE_TVDAC1_CONT_REG	0x2c
#define TVE_TVDAC2_CONT_REG	0x30
#define TVE_CD_CONT_REG		0x34
#define TVE_INT_CONT_REG	0x64
#define TVE_STAT_REG		0x68
#define TVE_TST_MODE_REG	0x6c
#define TVE_MV_CONT_REG		0xdc

/* TVE_COM_CONF_REG */
#define TVE_SYNC_CH_2_EN	BIT(22)
#define TVE_SYNC_CH_1_EN	BIT(21)
#define TVE_SYNC_CH_0_EN	BIT(20)
#define TVE_TV_OUT_MODE_MASK	(0x7 << 12)
#define TVE_TV_OUT_DISABLE	(0x0 << 12)
#define TVE_TV_OUT_CVBS_0	(0x1 << 12)
#define TVE_TV_OUT_CVBS_2	(0x2 << 12)
#define TVE_TV_OUT_CVBS_0_2	(0x3 << 12)
#define TVE_TV_OUT_SVIDEO_0_1	(0x4 << 12)
#define TVE_TV_OUT_SVIDEO_0_1_CVBS2_2	(0x5 << 12)
#define TVE_TV_OUT_YPBPR	(0x6 << 12)
#define TVE_TV_OUT_RGB		(0x7 << 12)
#define TVE_TV_STAND_MASK	(0xf << 8)
#define TVE_TV_STAND_HD_1080P30	(0xc << 8)
#define TVE_P2I_CONV_EN		BIT(7)
#define TVE_INP_VIDEO_FORM	BIT(6)
#define TVE_INP_YCBCR_422	(0x0 << 6)
#define TVE_INP_YCBCR_444	(0x1 << 6)
#define TVE_DATA_SOURCE_MASK	(0x3 << 4)
#define TVE_DATA_SOURCE_BUS1	(0x0 << 4)
#define TVE_DATA_SOURCE_BUS2	(0x1 << 4)
#define TVE_DATA_SOURCE_EXT	(0x2 << 4)
#define TVE_DATA_SOURCE_TESTGEN	(0x3 << 4)
#define TVE_IPU_CLK_EN_OFS	3
#define TVE_IPU_CLK_EN		BIT(3)
#define TVE_DAC_SAMP_RATE_OFS	1
#define TVE_DAC_SAMP_RATE_WIDTH	2
#define TVE_DAC_SAMP_RATE_MASK	(0x3 << 1)
#define TVE_DAC_FULL_RATE	(0x0 << 1)
#define TVE_DAC_DIV2_RATE	(0x1 << 1)
#define TVE_DAC_DIV4_RATE	(0x2 << 1)
#define TVE_EN			BIT(0)

/* TVE_TVDACx_CONT_REG */
#define TVE_TVDAC_GAIN_MASK	(0x3f << 0)

/* TVE_CD_CONT_REG */
#define TVE_CD_CH_2_SM_EN	BIT(22)
#define TVE_CD_CH_1_SM_EN	BIT(21)
#define TVE_CD_CH_0_SM_EN	BIT(20)
#define TVE_CD_CH_2_LM_EN	BIT(18)
#define TVE_CD_CH_1_LM_EN	BIT(17)
#define TVE_CD_CH_0_LM_EN	BIT(16)
#define TVE_CD_CH_2_REF_LVL	BIT(10)
#define TVE_CD_CH_1_REF_LVL	BIT(9)
#define TVE_CD_CH_0_REF_LVL	BIT(8)
#define TVE_CD_EN		BIT(0)

/* TVE_INT_CONT_REG */
#define TVE_FRAME_END_IEN	BIT(13)
#define TVE_CD_MON_END_IEN	BIT(2)
#define TVE_CD_SM_IEN		BIT(1)
#define TVE_CD_LM_IEN		BIT(0)

/* TVE_TST_MODE_REG */
#define TVE_TVDAC_TEST_MODE_MASK	(0x7 << 0)

#define IMX_TVE_DAC_VOLTAGE	2750000

enum {
	TVE_MODE_TVOUT,
	TVE_MODE_VGA,
};

struct imx_tve {
	struct drm_connector connector;
	struct drm_encoder encoder;
	struct device *dev;
	spinlock_t lock;	/* register lock */
	bool enabled;
	int mode;
	int di_hsync_pin;
	int di_vsync_pin;

	struct regmap *regmap;
	struct regulator *dac_reg;
	struct i2c_adapter *ddc;
	struct clk *clk;
	struct clk *di_sel_clk;
	struct clk_hw clk_hw_di;
	struct clk *di_clk;
};

static inline struct imx_tve *con_to_tve(struct drm_connector *c)
{
	return container_of(c, struct imx_tve, connector);
}

static inline struct imx_tve *enc_to_tve(struct drm_encoder *e)
{
	return container_of(e, struct imx_tve, encoder);
}

static void tve_lock(void *__tve)
__acquires(&tve->lock)
{
	struct imx_tve *tve = __tve;

	spin_lock(&tve->lock);
}

static void tve_unlock(void *__tve)
__releases(&tve->lock)
{
	struct imx_tve *tve = __tve;

	spin_unlock(&tve->lock);
}

static void tve_enable(struct imx_tve *tve)
{
	int ret;

	if (!tve->enabled) {
		tve->enabled = true;
		clk_prepare_enable(tve->clk);
		ret = regmap_update_bits(tve->regmap, TVE_COM_CONF_REG,
					 TVE_EN, TVE_EN);
	}

	/* clear interrupt status register */
	regmap_write(tve->regmap, TVE_STAT_REG, 0xffffffff);

	/* cable detection irq disabled in VGA mode, enabled in TVOUT mode */
	if (tve->mode == TVE_MODE_VGA)
		regmap_write(tve->regmap, TVE_INT_CONT_REG, 0);
	else
		regmap_write(tve->regmap, TVE_INT_CONT_REG,
			     TVE_CD_SM_IEN |
			     TVE_CD_LM_IEN |
			     TVE_CD_MON_END_IEN);
}

static void tve_disable(struct imx_tve *tve)
{
	int ret;

	if (tve->enabled) {
		tve->enabled = false;
		ret = regmap_update_bits(tve->regmap, TVE_COM_CONF_REG,
					 TVE_EN, 0);
		clk_disable_unprepare(tve->clk);
	}
}

static int tve_setup_tvout(struct imx_tve *tve)
{
	return -ENOTSUPP;
}

static int tve_setup_vga(struct imx_tve *tve)
{
	unsigned int mask;
	unsigned int val;
	int ret;

	/* set gain to (1 + 10/128) to provide 0.7V peak-to-peak amplitude */
	ret = regmap_update_bits(tve->regmap, TVE_TVDAC0_CONT_REG,
				 TVE_TVDAC_GAIN_MASK, 0x0a);
	if (ret)
		return ret;

	ret = regmap_update_bits(tve->regmap, TVE_TVDAC1_CONT_REG,
				 TVE_TVDAC_GAIN_MASK, 0x0a);
	if (ret)
		return ret;

	ret = regmap_update_bits(tve->regmap, TVE_TVDAC2_CONT_REG,
				 TVE_TVDAC_GAIN_MASK, 0x0a);
	if (ret)
		return ret;

	/* set configuration register */
	mask = TVE_DATA_SOURCE_MASK | TVE_INP_VIDEO_FORM;
	val  = TVE_DATA_SOURCE_BUS2 | TVE_INP_YCBCR_444;
	mask |= TVE_TV_STAND_MASK       | TVE_P2I_CONV_EN;
	val  |= TVE_TV_STAND_HD_1080P30 | 0;
	mask |= TVE_TV_OUT_MODE_MASK | TVE_SYNC_CH_0_EN;
	val  |= TVE_TV_OUT_RGB       | TVE_SYNC_CH_0_EN;
	ret = regmap_update_bits(tve->regmap, TVE_COM_CONF_REG, mask, val);
	if (ret)
		return ret;

	/* set test mode (as documented) */
	return regmap_update_bits(tve->regmap, TVE_TST_MODE_REG,
				 TVE_TVDAC_TEST_MODE_MASK, 1);
}

static int imx_tve_connector_get_modes(struct drm_connector *connector)
{
	struct imx_tve *tve = con_to_tve(connector);
	struct edid *edid;
	int ret = 0;

	if (!tve->ddc)
		return 0;

	edid = drm_get_edid(connector, tve->ddc);
	if (edid) {
		drm_mode_connector_update_edid_property(connector, edid);
		ret = drm_add_edid_modes(connector, edid);
		kfree(edid);
	}

	return ret;
}

static int imx_tve_connector_mode_valid(struct drm_connector *connector,
					struct drm_display_mode *mode)
{
	struct imx_tve *tve = con_to_tve(connector);
	unsigned long rate;

	/* pixel clock with 2x oversampling */
	rate = clk_round_rate(tve->clk, 2000UL * mode->clock) / 2000;
	if (rate == mode->clock)
		return MODE_OK;

	/* pixel clock without oversampling */
	rate = clk_round_rate(tve->clk, 1000UL * mode->clock) / 1000;
	if (rate == mode->clock)
		return MODE_OK;

	dev_warn(tve->dev, "ignoring mode %dx%d\n",
		 mode->hdisplay, mode->vdisplay);

	return MODE_BAD;
}

static struct drm_encoder *imx_tve_connector_best_encoder(
		struct drm_connector *connector)
{
	struct imx_tve *tve = con_to_tve(connector);

	return &tve->encoder;
}

static void imx_tve_encoder_mode_set(struct drm_encoder *encoder,
				     struct drm_display_mode *orig_mode,
				     struct drm_display_mode *mode)
{
	struct imx_tve *tve = enc_to_tve(encoder);
	unsigned long rounded_rate;
	unsigned long rate;
	int div = 1;
	int ret;

	/*
	 * FIXME
	 * we should try 4k * mode->clock first,
	 * and enable 4x oversampling for lower resolutions
	 */
	rate = 2000UL * mode->clock;
	clk_set_rate(tve->clk, rate);
	rounded_rate = clk_get_rate(tve->clk);
	if (rounded_rate >= rate)
		div = 2;
	clk_set_rate(tve->di_clk, rounded_rate / div);

	ret = clk_set_parent(tve->di_sel_clk, tve->di_clk);
	if (ret < 0) {
		dev_err(tve->dev, "failed to set di_sel parent to tve_di: %d\n",
			ret);
	}

	regmap_update_bits(tve->regmap, TVE_COM_CONF_REG,
			   TVE_IPU_CLK_EN, TVE_IPU_CLK_EN);

	if (tve->mode == TVE_MODE_VGA)
		ret = tve_setup_vga(tve);
	else
		ret = tve_setup_tvout(tve);
	if (ret)
		dev_err(tve->dev, "failed to set configuration: %d\n", ret);
}

static void imx_tve_encoder_enable(struct drm_encoder *encoder)
{
	struct imx_tve *tve = enc_to_tve(encoder);

	tve_enable(tve);
}

static void imx_tve_encoder_disable(struct drm_encoder *encoder)
{
	struct imx_tve *tve = enc_to_tve(encoder);

	tve_disable(tve);
}

static int imx_tve_atomic_check(struct drm_encoder *encoder,
				struct drm_crtc_state *crtc_state,
				struct drm_connector_state *conn_state)
{
	struct imx_crtc_state *imx_crtc_state = to_imx_crtc_state(crtc_state);
	struct imx_tve *tve = enc_to_tve(encoder);

	imx_crtc_state->bus_format = MEDIA_BUS_FMT_GBR888_1X24;
	imx_crtc_state->di_hsync_pin = tve->di_hsync_pin;
	imx_crtc_state->di_vsync_pin = tve->di_vsync_pin;

	return 0;
}

static const struct drm_connector_funcs imx_tve_connector_funcs = {
	.dpms = drm_atomic_helper_connector_dpms,
	.fill_modes = drm_helper_probe_single_connector_modes,
	.destroy = imx_drm_connector_destroy,
	.reset = drm_atomic_helper_connector_reset,
	.atomic_duplicate_state = drm_atomic_helper_connector_duplicate_state,
	.atomic_destroy_state = drm_atomic_helper_connector_destroy_state,
};

static const struct drm_connector_helper_funcs imx_tve_connector_helper_funcs = {
	.get_modes = imx_tve_connector_get_modes,
	.best_encoder = imx_tve_connector_best_encoder,
	.mode_valid = imx_tve_connector_mode_valid,
};

static const struct drm_encoder_funcs imx_tve_encoder_funcs = {
	.destroy = imx_drm_encoder_destroy,
};

static const struct drm_encoder_helper_funcs imx_tve_encoder_helper_funcs = {
	.mode_set = imx_tve_encoder_mode_set,
	.enable = imx_tve_encoder_enable,
	.disable = imx_tve_encoder_disable,
	.atomic_check = imx_tve_atomic_check,
};

static irqreturn_t imx_tve_irq_handler(int irq, void *data)
{
	struct imx_tve *tve = data;
	unsigned int val;

	regmap_read(tve->regmap, TVE_STAT_REG, &val);

	/* clear interrupt status register */
	regmap_write(tve->regmap, TVE_STAT_REG, 0xffffffff);

	return IRQ_HANDLED;
}

static unsigned long clk_tve_di_recalc_rate(struct clk_hw *hw,
					    unsigned long parent_rate)
{
	struct imx_tve *tve = container_of(hw, struct imx_tve, clk_hw_di);
	unsigned int val;
	int ret;

	ret = regmap_read(tve->regmap, TVE_COM_CONF_REG, &val);
	if (ret < 0)
		return 0;

	switch (val & TVE_DAC_SAMP_RATE_MASK) {
	case TVE_DAC_DIV4_RATE:
		return parent_rate / 4;
	case TVE_DAC_DIV2_RATE:
		return parent_rate / 2;
	case TVE_DAC_FULL_RATE:
	default:
		return parent_rate;
	}

	return 0;
}

static long clk_tve_di_round_rate(struct clk_hw *hw, unsigned long rate,
				  unsigned long *prate)
{
	unsigned long div;

	div = *prate / rate;
	if (div >= 4)
		return *prate / 4;
	else if (div >= 2)
		return *prate / 2;
	return *prate;
}

static int clk_tve_di_set_rate(struct clk_hw *hw, unsigned long rate,
			       unsigned long parent_rate)
{
	struct imx_tve *tve = container_of(hw, struct imx_tve, clk_hw_di);
	unsigned long div;
	u32 val;
	int ret;

	div = parent_rate / rate;
	if (div >= 4)
		val = TVE_DAC_DIV4_RATE;
	else if (div >= 2)
		val = TVE_DAC_DIV2_RATE;
	else
		val = TVE_DAC_FULL_RATE;

	ret = regmap_update_bits(tve->regmap, TVE_COM_CONF_REG,
				 TVE_DAC_SAMP_RATE_MASK, val);

	if (ret < 0) {
		dev_err(tve->dev, "failed to set divider: %d\n", ret);
		return ret;
	}

	return 0;
}

static struct clk_ops clk_tve_di_ops = {
	.round_rate = clk_tve_di_round_rate,
	.set_rate = clk_tve_di_set_rate,
	.recalc_rate = clk_tve_di_recalc_rate,
};

static int tve_clk_init(struct imx_tve *tve, void __iomem *base)
{
	const char *tve_di_parent[1];
	struct clk_init_data init = {
		.name = "tve_di",
		.ops = &clk_tve_di_ops,
		.num_parents = 1,
		.flags = 0,
	};

	tve_di_parent[0] = __clk_get_name(tve->clk);
	init.parent_names = (const char **)&tve_di_parent;

	tve->clk_hw_di.init = &init;
	tve->di_clk = clk_register(tve->dev, &tve->clk_hw_di);
	if (IS_ERR(tve->di_clk)) {
		dev_err(tve->dev, "failed to register TVE output clock: %ld\n",
			PTR_ERR(tve->di_clk));
		return PTR_ERR(tve->di_clk);
	}

	return 0;
}

static int imx_tve_register(struct drm_device *drm, struct imx_tve *tve)
{
	int encoder_type;
	int ret;

	encoder_type = tve->mode == TVE_MODE_VGA ?
				DRM_MODE_ENCODER_DAC : DRM_MODE_ENCODER_TVDAC;

	ret = imx_drm_encoder_parse_of(drm, &tve->encoder, tve->dev->of_node);
	if (ret)
		return ret;

	drm_encoder_helper_add(&tve->encoder, &imx_tve_encoder_helper_funcs);
	drm_encoder_init(drm, &tve->encoder, &imx_tve_encoder_funcs,
			 encoder_type, NULL);

	drm_connector_helper_add(&tve->connector,
			&imx_tve_connector_helper_funcs);
	drm_connector_init(drm, &tve->connector, &imx_tve_connector_funcs,
			   DRM_MODE_CONNECTOR_VGA);

	drm_mode_connector_attach_encoder(&tve->connector, &tve->encoder);

	return 0;
}

static bool imx_tve_readable_reg(struct device *dev, unsigned int reg)
{
	return (reg % 4 == 0) && (reg <= 0xdc);
}

static struct regmap_config tve_regmap_config = {
	.reg_bits = 32,
	.val_bits = 32,
	.reg_stride = 4,

	.readable_reg = imx_tve_readable_reg,

	.lock = tve_lock,
	.unlock = tve_unlock,

	.max_register = 0xdc,
};

static const char * const imx_tve_modes[] = {
	[TVE_MODE_TVOUT]  = "tvout",
	[TVE_MODE_VGA] = "vga",
};

static const int of_get_tve_mode(struct device_node *np)
{
	const char *bm;
	int ret, i;

	ret = of_property_read_string(np, "fsl,tve-mode", &bm);
	if (ret < 0)
		return ret;

	for (i = 0; i < ARRAY_SIZE(imx_tve_modes); i++)
		if (!strcasecmp(bm, imx_tve_modes[i]))
			return i;

	return -EINVAL;
}

static int imx_tve_bind(struct device *dev, struct device *master, void *data)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct drm_device *drm = data;
	struct device_node *np = dev->of_node;
	struct device_node *ddc_node;
	struct imx_tve *tve;
	struct resource *res;
	void __iomem *base;
	unsigned int val;
	int irq;
	int ret;

	tve = devm_kzalloc(dev, sizeof(*tve), GFP_KERNEL);
	if (!tve)
		return -ENOMEM;

	tve->dev = dev;
	spin_lock_init(&tve->lock);

	ddc_node = of_parse_phandle(np, "ddc-i2c-bus", 0);
	if (ddc_node) {
		tve->ddc = of_find_i2c_adapter_by_node(ddc_node);
		of_node_put(ddc_node);
	}

	tve->mode = of_get_tve_mode(np);
	if (tve->mode != TVE_MODE_VGA) {
		dev_err(dev, "only VGA mode supported, currently\n");
		return -EINVAL;
	}

	if (tve->mode == TVE_MODE_VGA) {
		ret = of_property_read_u32(np, "fsl,hsync-pin",
					   &tve->di_hsync_pin);

		if (ret < 0) {
			dev_err(dev, "failed to get hsync pin\n");
			return ret;
		}

		ret = of_property_read_u32(np, "fsl,vsync-pin",
					   &tve->di_vsync_pin);

		if (ret < 0) {
			dev_err(dev, "failed to get vsync pin\n");
			return ret;
		}
	}

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	base = devm_ioremap_resource(dev, res);
	if (IS_ERR(base))
		return PTR_ERR(base);

	tve_regmap_config.lock_arg = tve;
	tve->regmap = devm_regmap_init_mmio_clk(dev, "tve", base,
						&tve_regmap_config);
	if (IS_ERR(tve->regmap)) {
		dev_err(dev, "failed to init regmap: %ld\n",
			PTR_ERR(tve->regmap));
		return PTR_ERR(tve->regmap);
	}

	irq = platform_get_irq(pdev, 0);
	if (irq < 0) {
		dev_err(dev, "failed to get irq\n");
		return irq;
	}

	ret = devm_request_threaded_irq(dev, irq, NULL,
					imx_tve_irq_handler, IRQF_ONESHOT,
					"imx-tve", tve);
	if (ret < 0) {
		dev_err(dev, "failed to request irq: %d\n", ret);
		return ret;
	}

	tve->dac_reg = devm_regulator_get(dev, "dac");
	if (!IS_ERR(tve->dac_reg)) {
		if (regulator_get_voltage(tve->dac_reg) != IMX_TVE_DAC_VOLTAGE)
			dev_warn(dev, "dac voltage is not %d uV\n", IMX_TVE_DAC_VOLTAGE);
		ret = regulator_enable(tve->dac_reg);
		if (ret)
			return ret;
	}

	tve->clk = devm_clk_get(dev, "tve");
	if (IS_ERR(tve->clk)) {
		dev_err(dev, "failed to get high speed tve clock: %ld\n",
			PTR_ERR(tve->clk));
		return PTR_ERR(tve->clk);
	}

	/* this is the IPU DI clock input selector, can be parented to tve_di */
	tve->di_sel_clk = devm_clk_get(dev, "di_sel");
	if (IS_ERR(tve->di_sel_clk)) {
		dev_err(dev, "failed to get ipu di mux clock: %ld\n",
			PTR_ERR(tve->di_sel_clk));
		return PTR_ERR(tve->di_sel_clk);
	}

	ret = tve_clk_init(tve, base);
	if (ret < 0)
		return ret;

	ret = regmap_read(tve->regmap, TVE_COM_CONF_REG, &val);
	if (ret < 0) {
		dev_err(dev, "failed to read configuration register: %d\n",
			ret);
		return ret;
	}
	if (val != 0x00100000) {
		dev_err(dev, "configuration register default value indicates this is not a TVEv2\n");
		return -ENODEV;
	}

	/* disable cable detection for VGA mode */
	ret = regmap_write(tve->regmap, TVE_CD_CONT_REG, 0);
	if (ret)
		return ret;

	ret = imx_tve_register(drm, tve);
	if (ret)
		return ret;

	dev_set_drvdata(dev, tve);

	return 0;
}

static void imx_tve_unbind(struct device *dev, struct device *master,
	void *data)
{
	struct imx_tve *tve = dev_get_drvdata(dev);

	if (!IS_ERR(tve->dac_reg))
		regulator_disable(tve->dac_reg);
}

static const struct component_ops imx_tve_ops = {
	.bind	= imx_tve_bind,
	.unbind	= imx_tve_unbind,
};

static int imx_tve_probe(struct platform_device *pdev)
{
	return component_add(&pdev->dev, &imx_tve_ops);
}

static int imx_tve_remove(struct platform_device *pdev)
{
	component_del(&pdev->dev, &imx_tve_ops);
	return 0;
}

static const struct of_device_id imx_tve_dt_ids[] = {
	{ .compatible = "fsl,imx53-tve", },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, imx_tve_dt_ids);

static struct platform_driver imx_tve_driver = {
	.probe		= imx_tve_probe,
	.remove		= imx_tve_remove,
	.driver		= {
		.of_match_table = imx_tve_dt_ids,
		.name	= "imx-tve",
	},
};

module_platform_driver(imx_tve_driver);

MODULE_DESCRIPTION("i.MX Television Encoder driver");
MODULE_AUTHOR("Philipp Zabel, Pengutronix");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:imx-tve");
