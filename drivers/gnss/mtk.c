// SPDX-License-Identifier: GPL-2.0
/*
 * Mediatek GNSS receiver driver
 *
 * Copyright (C) 2018 Johan Hovold <johan@kernel.org>
 */

#include <linux/errno.h>
#include <linux/gnss.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/regulator/consumer.h>
#include <linux/serdev.h>

#include "serial.h"

struct mtk_data {
	struct regulator *vbackup;
	struct regulator *vcc;
};

static int mtk_set_active(struct gnss_serial *gserial)
{
	struct mtk_data *data = gnss_serial_get_drvdata(gserial);
	int ret;

	ret = regulator_enable(data->vcc);
	if (ret)
		return ret;

	return 0;
}

static int mtk_set_standby(struct gnss_serial *gserial)
{
	struct mtk_data *data = gnss_serial_get_drvdata(gserial);
	int ret;

	ret = regulator_disable(data->vcc);
	if (ret)
		return ret;

	return 0;
}

static int mtk_set_power(struct gnss_serial *gserial,
			 enum gnss_serial_pm_state state)
{
	switch (state) {
	case GNSS_SERIAL_ACTIVE:
		return mtk_set_active(gserial);
	case GNSS_SERIAL_OFF:
	case GNSS_SERIAL_STANDBY:
		return mtk_set_standby(gserial);
	}

	return -EINVAL;
}

static const struct gnss_serial_ops mtk_gserial_ops = {
	.set_power = mtk_set_power,
};

static int mtk_probe(struct serdev_device *serdev)
{
	struct gnss_serial *gserial;
	struct mtk_data *data;
	int ret;

	gserial = gnss_serial_allocate(serdev, sizeof(*data));
	if (IS_ERR(gserial)) {
		ret = PTR_ERR(gserial);
		return ret;
	}

	gserial->ops = &mtk_gserial_ops;

	gserial->gdev->type = GNSS_TYPE_MTK;

	data = gnss_serial_get_drvdata(gserial);

	data->vcc = devm_regulator_get(&serdev->dev, "vcc");
	if (IS_ERR(data->vcc)) {
		ret = PTR_ERR(data->vcc);
		goto err_free_gserial;
	}

	data->vbackup = devm_regulator_get_optional(&serdev->dev, "vbackup");
	if (IS_ERR(data->vbackup)) {
		ret = PTR_ERR(data->vbackup);
		if (ret == -ENODEV)
			data->vbackup = NULL;
		else
			goto err_free_gserial;
	}

	if (data->vbackup) {
		ret = regulator_enable(data->vbackup);
		if (ret)
			goto err_free_gserial;
	}

	ret = gnss_serial_register(gserial);
	if (ret)
		goto err_disable_vbackup;

	return 0;

err_disable_vbackup:
	if (data->vbackup)
		regulator_disable(data->vbackup);
err_free_gserial:
	gnss_serial_free(gserial);

	return ret;
}

static void mtk_remove(struct serdev_device *serdev)
{
	struct gnss_serial *gserial = serdev_device_get_drvdata(serdev);
	struct mtk_data *data = gnss_serial_get_drvdata(gserial);

	gnss_serial_deregister(gserial);
	if (data->vbackup)
		regulator_disable(data->vbackup);
	gnss_serial_free(gserial);
};

#ifdef CONFIG_OF
static const struct of_device_id mtk_of_match[] = {
	{ .compatible = "globaltop,pa6h" },
	{},
};
MODULE_DEVICE_TABLE(of, mtk_of_match);
#endif

static struct serdev_device_driver mtk_driver = {
	.driver	= {
		.name		= "gnss-mtk",
		.of_match_table	= of_match_ptr(mtk_of_match),
		.pm		= &gnss_serial_pm_ops,
	},
	.probe	= mtk_probe,
	.remove	= mtk_remove,
};
module_serdev_device_driver(mtk_driver);

MODULE_AUTHOR("Loys Ollivier <lollivier@baylibre.com>");
MODULE_DESCRIPTION("Mediatek GNSS receiver driver");
MODULE_LICENSE("GPL v2");
