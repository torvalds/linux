// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2015, The Linux Foundation. All rights reserved.
 */

#include <linux/debugfs.h>
#include <linux/err.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_platform.h>
#include <linux/platform_device.h>
#include <linux/pm.h>
#include <linux/slab.h>
#include <linux/thermal.h>
#include "tsens.h"

static int tsens_get_temp(void *data, int *temp)
{
	struct tsens_sensor *s = data;
	struct tsens_priv *priv = s->priv;

	return priv->ops->get_temp(s, temp);
}

static int tsens_get_trend(void *data, int trip, enum thermal_trend *trend)
{
	struct tsens_sensor *s = data;
	struct tsens_priv *priv = s->priv;

	if (priv->ops->get_trend)
		return priv->ops->get_trend(s, trend);

	return -ENOTSUPP;
}

static int  __maybe_unused tsens_suspend(struct device *dev)
{
	struct tsens_priv *priv = dev_get_drvdata(dev);

	if (priv->ops && priv->ops->suspend)
		return priv->ops->suspend(priv);

	return 0;
}

static int __maybe_unused tsens_resume(struct device *dev)
{
	struct tsens_priv *priv = dev_get_drvdata(dev);

	if (priv->ops && priv->ops->resume)
		return priv->ops->resume(priv);

	return 0;
}

static SIMPLE_DEV_PM_OPS(tsens_pm_ops, tsens_suspend, tsens_resume);

static const struct of_device_id tsens_table[] = {
	{
		.compatible = "qcom,msm8916-tsens",
		.data = &data_8916,
	}, {
		.compatible = "qcom,msm8974-tsens",
		.data = &data_8974,
	}, {
		.compatible = "qcom,msm8976-tsens",
		.data = &data_8976,
	}, {
		.compatible = "qcom,msm8996-tsens",
		.data = &data_8996,
	}, {
		.compatible = "qcom,tsens-v1",
		.data = &data_tsens_v1,
	}, {
		.compatible = "qcom,tsens-v2",
		.data = &data_tsens_v2,
	},
	{}
};
MODULE_DEVICE_TABLE(of, tsens_table);

static const struct thermal_zone_of_device_ops tsens_of_ops = {
	.get_temp = tsens_get_temp,
	.get_trend = tsens_get_trend,
	.set_trips = tsens_set_trips,
};

static int tsens_register_irq(struct tsens_priv *priv, char *irqname,
			      irq_handler_t thread_fn)
{
	struct platform_device *pdev;
	int ret, irq;

	pdev = of_find_device_by_node(priv->dev->of_node);
	if (!pdev)
		return -ENODEV;

	irq = platform_get_irq_byname(pdev, irqname);
	if (irq < 0) {
		ret = irq;
		/* For old DTs with no IRQ defined */
		if (irq == -ENXIO)
			ret = 0;
	} else {
		ret = devm_request_threaded_irq(&pdev->dev, irq,
						NULL, thread_fn,
						IRQF_ONESHOT,
						dev_name(&pdev->dev), priv);
		if (ret)
			dev_err(&pdev->dev, "%s: failed to get irq\n",
				__func__);
		else
			enable_irq_wake(irq);
	}

	put_device(&pdev->dev);
	return ret;
}

static int tsens_register(struct tsens_priv *priv)
{
	int i, ret;
	struct thermal_zone_device *tzd;

	for (i = 0;  i < priv->num_sensors; i++) {
		priv->sensor[i].priv = priv;
		tzd = devm_thermal_zone_of_sensor_register(priv->dev, priv->sensor[i].hw_id,
							   &priv->sensor[i],
							   &tsens_of_ops);
		if (IS_ERR(tzd))
			continue;
		priv->sensor[i].tzd = tzd;
		if (priv->ops->enable)
			priv->ops->enable(priv, i);
	}

	ret = tsens_register_irq(priv, "uplow", tsens_irq_thread);
	if (ret < 0)
		return ret;

	if (priv->feat->crit_int)
		ret = tsens_register_irq(priv, "critical",
					 tsens_critical_irq_thread);

	return ret;
}

static int tsens_probe(struct platform_device *pdev)
{
	int ret, i;
	struct device *dev;
	struct device_node *np;
	struct tsens_priv *priv;
	const struct tsens_plat_data *data;
	const struct of_device_id *id;
	u32 num_sensors;

	if (pdev->dev.of_node)
		dev = &pdev->dev;
	else
		dev = pdev->dev.parent;

	np = dev->of_node;

	id = of_match_node(tsens_table, np);
	if (id)
		data = id->data;
	else
		data = &data_8960;

	num_sensors = data->num_sensors;

	if (np)
		of_property_read_u32(np, "#qcom,sensors", &num_sensors);

	if (num_sensors <= 0) {
		dev_err(dev, "%s: invalid number of sensors\n", __func__);
		return -EINVAL;
	}

	priv = devm_kzalloc(dev,
			     struct_size(priv, sensor, num_sensors),
			     GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	priv->dev = dev;
	priv->num_sensors = num_sensors;
	priv->ops = data->ops;
	for (i = 0;  i < priv->num_sensors; i++) {
		if (data->hw_ids)
			priv->sensor[i].hw_id = data->hw_ids[i];
		else
			priv->sensor[i].hw_id = i;
	}
	priv->feat = data->feat;
	priv->fields = data->fields;

	platform_set_drvdata(pdev, priv);

	if (!priv->ops || !priv->ops->init || !priv->ops->get_temp)
		return -EINVAL;

	ret = priv->ops->init(priv);
	if (ret < 0) {
		dev_err(dev, "%s: init failed\n", __func__);
		return ret;
	}

	if (priv->ops->calibrate) {
		ret = priv->ops->calibrate(priv);
		if (ret < 0) {
			if (ret != -EPROBE_DEFER)
				dev_err(dev, "%s: calibration failed\n", __func__);
			return ret;
		}
	}

	return tsens_register(priv);
}

static int tsens_remove(struct platform_device *pdev)
{
	struct tsens_priv *priv = platform_get_drvdata(pdev);

	debugfs_remove_recursive(priv->debug_root);
	tsens_disable_irq(priv);
	if (priv->ops->disable)
		priv->ops->disable(priv);

	return 0;
}

static struct platform_driver tsens_driver = {
	.probe = tsens_probe,
	.remove = tsens_remove,
	.driver = {
		.name = "qcom-tsens",
		.pm	= &tsens_pm_ops,
		.of_match_table = tsens_table,
	},
};
module_platform_driver(tsens_driver);

MODULE_LICENSE("GPL v2");
MODULE_DESCRIPTION("QCOM Temperature Sensor driver");
MODULE_ALIAS("platform:qcom-tsens");
