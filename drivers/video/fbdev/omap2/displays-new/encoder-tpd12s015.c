/*
 * TPD12S015 HDMI ESD protection & level shifter chip driver
 *
 * Copyright (C) 2013 Texas Instruments
 * Author: Tomi Valkeinen <tomi.valkeinen@ti.com>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published by
 * the Free Software Foundation.
 */

#include <linux/completion.h>
#include <linux/delay.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/gpio.h>
#include <linux/platform_device.h>
#include <linux/of_gpio.h>

#include <video/omapdss.h>
#include <video/omap-panel-data.h>

struct panel_drv_data {
	struct omap_dss_device dssdev;
	struct omap_dss_device *in;

	int ct_cp_hpd_gpio;
	int ls_oe_gpio;
	int hpd_gpio;

	struct omap_video_timings timings;

	struct completion hpd_completion;
};

#define to_panel_data(x) container_of(x, struct panel_drv_data, dssdev)

static irqreturn_t tpd_hpd_irq_handler(int irq, void *data)
{
	struct panel_drv_data *ddata = data;
	bool hpd;

	hpd = gpio_get_value_cansleep(ddata->hpd_gpio);

	dev_dbg(ddata->dssdev.dev, "hpd %d\n", hpd);

	if (gpio_is_valid(ddata->ls_oe_gpio)) {
		if (hpd)
			gpio_set_value_cansleep(ddata->ls_oe_gpio, 1);
		else
			gpio_set_value_cansleep(ddata->ls_oe_gpio, 0);
	}

	complete_all(&ddata->hpd_completion);

	return IRQ_HANDLED;
}

static int tpd_connect(struct omap_dss_device *dssdev,
		struct omap_dss_device *dst)
{
	struct panel_drv_data *ddata = to_panel_data(dssdev);
	struct omap_dss_device *in = ddata->in;
	int r;

	r = in->ops.hdmi->connect(in, dssdev);
	if (r)
		return r;

	dst->src = dssdev;
	dssdev->dst = dst;

	reinit_completion(&ddata->hpd_completion);

	gpio_set_value_cansleep(ddata->ct_cp_hpd_gpio, 1);
	/* DC-DC converter needs at max 300us to get to 90% of 5V */
	udelay(300);

	/*
	 * If there's a cable connected, wait for the hpd irq to trigger,
	 * which turns on the level shifters.
	 */
	if (gpio_get_value_cansleep(ddata->hpd_gpio)) {
		unsigned long to;
		to = wait_for_completion_timeout(&ddata->hpd_completion,
				msecs_to_jiffies(250));
		WARN_ON_ONCE(to == 0);
	}

	return 0;
}

static void tpd_disconnect(struct omap_dss_device *dssdev,
		struct omap_dss_device *dst)
{
	struct panel_drv_data *ddata = to_panel_data(dssdev);
	struct omap_dss_device *in = ddata->in;

	WARN_ON(dst != dssdev->dst);

	if (dst != dssdev->dst)
		return;

	gpio_set_value_cansleep(ddata->ct_cp_hpd_gpio, 0);

	dst->src = NULL;
	dssdev->dst = NULL;

	in->ops.hdmi->disconnect(in, &ddata->dssdev);
}

static int tpd_enable(struct omap_dss_device *dssdev)
{
	struct panel_drv_data *ddata = to_panel_data(dssdev);
	struct omap_dss_device *in = ddata->in;
	int r;

	if (dssdev->state == OMAP_DSS_DISPLAY_ACTIVE)
		return 0;

	in->ops.hdmi->set_timings(in, &ddata->timings);

	r = in->ops.hdmi->enable(in);
	if (r)
		return r;

	dssdev->state = OMAP_DSS_DISPLAY_ACTIVE;

	return r;
}

static void tpd_disable(struct omap_dss_device *dssdev)
{
	struct panel_drv_data *ddata = to_panel_data(dssdev);
	struct omap_dss_device *in = ddata->in;

	if (dssdev->state != OMAP_DSS_DISPLAY_ACTIVE)
		return;

	in->ops.hdmi->disable(in);

	dssdev->state = OMAP_DSS_DISPLAY_DISABLED;
}

static void tpd_set_timings(struct omap_dss_device *dssdev,
		struct omap_video_timings *timings)
{
	struct panel_drv_data *ddata = to_panel_data(dssdev);
	struct omap_dss_device *in = ddata->in;

	ddata->timings = *timings;
	dssdev->panel.timings = *timings;

	in->ops.hdmi->set_timings(in, timings);
}

static void tpd_get_timings(struct omap_dss_device *dssdev,
		struct omap_video_timings *timings)
{
	struct panel_drv_data *ddata = to_panel_data(dssdev);

	*timings = ddata->timings;
}

static int tpd_check_timings(struct omap_dss_device *dssdev,
		struct omap_video_timings *timings)
{
	struct panel_drv_data *ddata = to_panel_data(dssdev);
	struct omap_dss_device *in = ddata->in;
	int r;

	r = in->ops.hdmi->check_timings(in, timings);

	return r;
}

static int tpd_read_edid(struct omap_dss_device *dssdev,
		u8 *edid, int len)
{
	struct panel_drv_data *ddata = to_panel_data(dssdev);
	struct omap_dss_device *in = ddata->in;

	if (!gpio_get_value_cansleep(ddata->hpd_gpio))
		return -ENODEV;

	return in->ops.hdmi->read_edid(in, edid, len);
}

static bool tpd_detect(struct omap_dss_device *dssdev)
{
	struct panel_drv_data *ddata = to_panel_data(dssdev);

	return gpio_get_value_cansleep(ddata->hpd_gpio);
}

static int tpd_audio_enable(struct omap_dss_device *dssdev)
{
	struct panel_drv_data *ddata = to_panel_data(dssdev);
	struct omap_dss_device *in = ddata->in;

	return in->ops.hdmi->audio_enable(in);
}

static void tpd_audio_disable(struct omap_dss_device *dssdev)
{
	struct panel_drv_data *ddata = to_panel_data(dssdev);
	struct omap_dss_device *in = ddata->in;

	in->ops.hdmi->audio_disable(in);
}

static int tpd_audio_start(struct omap_dss_device *dssdev)
{
	struct panel_drv_data *ddata = to_panel_data(dssdev);
	struct omap_dss_device *in = ddata->in;

	return in->ops.hdmi->audio_start(in);
}

static void tpd_audio_stop(struct omap_dss_device *dssdev)
{
	struct panel_drv_data *ddata = to_panel_data(dssdev);
	struct omap_dss_device *in = ddata->in;

	in->ops.hdmi->audio_stop(in);
}

static bool tpd_audio_supported(struct omap_dss_device *dssdev)
{
	struct panel_drv_data *ddata = to_panel_data(dssdev);
	struct omap_dss_device *in = ddata->in;

	return in->ops.hdmi->audio_supported(in);
}

static int tpd_audio_config(struct omap_dss_device *dssdev,
		struct omap_dss_audio *audio)
{
	struct panel_drv_data *ddata = to_panel_data(dssdev);
	struct omap_dss_device *in = ddata->in;

	return in->ops.hdmi->audio_config(in, audio);
}

static const struct omapdss_hdmi_ops tpd_hdmi_ops = {
	.connect		= tpd_connect,
	.disconnect		= tpd_disconnect,

	.enable			= tpd_enable,
	.disable		= tpd_disable,

	.check_timings		= tpd_check_timings,
	.set_timings		= tpd_set_timings,
	.get_timings		= tpd_get_timings,

	.read_edid		= tpd_read_edid,
	.detect			= tpd_detect,

	.audio_enable		= tpd_audio_enable,
	.audio_disable		= tpd_audio_disable,
	.audio_start		= tpd_audio_start,
	.audio_stop		= tpd_audio_stop,
	.audio_supported	= tpd_audio_supported,
	.audio_config		= tpd_audio_config,
};

static int tpd_probe_pdata(struct platform_device *pdev)
{
	struct panel_drv_data *ddata = platform_get_drvdata(pdev);
	struct encoder_tpd12s015_platform_data *pdata;
	struct omap_dss_device *dssdev, *in;

	pdata = dev_get_platdata(&pdev->dev);

	ddata->ct_cp_hpd_gpio = pdata->ct_cp_hpd_gpio;
	ddata->ls_oe_gpio = pdata->ls_oe_gpio;
	ddata->hpd_gpio = pdata->hpd_gpio;

	in = omap_dss_find_output(pdata->source);
	if (in == NULL) {
		dev_err(&pdev->dev, "Failed to find video source\n");
		return -ENODEV;
	}

	ddata->in = in;

	dssdev = &ddata->dssdev;
	dssdev->name = pdata->name;

	return 0;
}

static int tpd_probe_of(struct platform_device *pdev)
{
	struct panel_drv_data *ddata = platform_get_drvdata(pdev);
	struct device_node *node = pdev->dev.of_node;
	struct omap_dss_device *in;
	int gpio;

	/* CT CP HPD GPIO */
	gpio = of_get_gpio(node, 0);
	if (!gpio_is_valid(gpio)) {
		dev_err(&pdev->dev, "failed to parse CT CP HPD gpio\n");
		return gpio;
	}
	ddata->ct_cp_hpd_gpio = gpio;

	/* LS OE GPIO */
	gpio = of_get_gpio(node, 1);
	if (gpio_is_valid(gpio) || gpio == -ENOENT) {
		ddata->ls_oe_gpio = gpio;
	} else {
		dev_err(&pdev->dev, "failed to parse LS OE gpio\n");
		return gpio;
	}

	/* HPD GPIO */
	gpio = of_get_gpio(node, 2);
	if (!gpio_is_valid(gpio)) {
		dev_err(&pdev->dev, "failed to parse HPD gpio\n");
		return gpio;
	}
	ddata->hpd_gpio = gpio;

	in = omapdss_of_find_source_for_first_ep(node);
	if (IS_ERR(in)) {
		dev_err(&pdev->dev, "failed to find video source\n");
		return PTR_ERR(in);
	}

	ddata->in = in;

	return 0;
}

static int tpd_probe(struct platform_device *pdev)
{
	struct omap_dss_device *in, *dssdev;
	struct panel_drv_data *ddata;
	int r;

	ddata = devm_kzalloc(&pdev->dev, sizeof(*ddata), GFP_KERNEL);
	if (!ddata)
		return -ENOMEM;

	platform_set_drvdata(pdev, ddata);

	init_completion(&ddata->hpd_completion);

	if (dev_get_platdata(&pdev->dev)) {
		r = tpd_probe_pdata(pdev);
		if (r)
			return r;
	} else if (pdev->dev.of_node) {
		r = tpd_probe_of(pdev);
		if (r)
			return r;
	} else {
		return -ENODEV;
	}

	r = devm_gpio_request_one(&pdev->dev, ddata->ct_cp_hpd_gpio,
			GPIOF_OUT_INIT_LOW, "hdmi_ct_cp_hpd");
	if (r)
		goto err_gpio;

	if (gpio_is_valid(ddata->ls_oe_gpio)) {
		r = devm_gpio_request_one(&pdev->dev, ddata->ls_oe_gpio,
				GPIOF_OUT_INIT_LOW, "hdmi_ls_oe");
		if (r)
			goto err_gpio;
	}

	r = devm_gpio_request_one(&pdev->dev, ddata->hpd_gpio,
			GPIOF_DIR_IN, "hdmi_hpd");
	if (r)
		goto err_gpio;

	r = devm_request_threaded_irq(&pdev->dev, gpio_to_irq(ddata->hpd_gpio),
				 NULL, tpd_hpd_irq_handler,
				 IRQF_TRIGGER_RISING | IRQF_TRIGGER_FALLING |
				 IRQF_ONESHOT, "hpd", ddata);
	if (r)
		goto err_irq;

	dssdev = &ddata->dssdev;
	dssdev->ops.hdmi = &tpd_hdmi_ops;
	dssdev->dev = &pdev->dev;
	dssdev->type = OMAP_DISPLAY_TYPE_HDMI;
	dssdev->output_type = OMAP_DISPLAY_TYPE_HDMI;
	dssdev->owner = THIS_MODULE;

	in = ddata->in;

	r = omapdss_register_output(dssdev);
	if (r) {
		dev_err(&pdev->dev, "Failed to register output\n");
		goto err_reg;
	}

	return 0;
err_reg:
err_irq:
err_gpio:
	omap_dss_put_device(ddata->in);
	return r;
}

static int __exit tpd_remove(struct platform_device *pdev)
{
	struct panel_drv_data *ddata = platform_get_drvdata(pdev);
	struct omap_dss_device *dssdev = &ddata->dssdev;
	struct omap_dss_device *in = ddata->in;

	omapdss_unregister_output(&ddata->dssdev);

	WARN_ON(omapdss_device_is_enabled(dssdev));
	if (omapdss_device_is_enabled(dssdev))
		tpd_disable(dssdev);

	WARN_ON(omapdss_device_is_connected(dssdev));
	if (omapdss_device_is_connected(dssdev))
		tpd_disconnect(dssdev, dssdev->dst);

	omap_dss_put_device(in);

	return 0;
}

static const struct of_device_id tpd_of_match[] = {
	{ .compatible = "omapdss,ti,tpd12s015", },
	{},
};

MODULE_DEVICE_TABLE(of, tpd_of_match);

static struct platform_driver tpd_driver = {
	.probe	= tpd_probe,
	.remove	= __exit_p(tpd_remove),
	.driver	= {
		.name	= "tpd12s015",
		.owner	= THIS_MODULE,
		.of_match_table = tpd_of_match,
	},
};

module_platform_driver(tpd_driver);

MODULE_AUTHOR("Tomi Valkeinen <tomi.valkeinen@ti.com>");
MODULE_DESCRIPTION("TPD12S015 driver");
MODULE_LICENSE("GPL");
