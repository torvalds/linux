/*
 * STMicroelectronics pressures driver
 *
 * Copyright 2013 STMicroelectronics Inc.
 *
 * Denis Ciocca <denis.ciocca@st.com>
 *
 * Licensed under the GPL-2.
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/spi/spi.h>
#include <linux/iio/iio.h>

#include <linux/iio/common/st_sensors.h>
#include <linux/iio/common/st_sensors_spi.h>
#include "st_pressure.h"

#ifdef CONFIG_OF
/*
 * For new single-chip sensors use <device_name> as compatible string.
 * For old single-chip devices keep <device_name>-press to maintain
 * compatibility
 */
static const struct of_device_id st_press_of_match[] = {
	{
		.compatible = "st,lps001wp-press",
		.data = LPS001WP_PRESS_DEV_NAME,
	},
	{
		.compatible = "st,lps25h-press",
		.data = LPS25H_PRESS_DEV_NAME,
	},
	{
		.compatible = "st,lps331ap-press",
		.data = LPS331AP_PRESS_DEV_NAME,
	},
	{
		.compatible = "st,lps22hb-press",
		.data = LPS22HB_PRESS_DEV_NAME,
	},
	{},
};
MODULE_DEVICE_TABLE(of, st_press_of_match);
#else
#define st_press_of_match	NULL
#endif

static int st_press_spi_probe(struct spi_device *spi)
{
	struct iio_dev *indio_dev;
	struct st_sensor_data *press_data;
	int err;

	indio_dev = devm_iio_device_alloc(&spi->dev, sizeof(*press_data));
	if (indio_dev == NULL)
		return -ENOMEM;

	press_data = iio_priv(indio_dev);

	st_sensors_of_name_probe(&spi->dev, st_press_of_match,
				 spi->modalias, sizeof(spi->modalias));
	st_sensors_spi_configure(indio_dev, spi, press_data);

	err = st_press_common_probe(indio_dev);
	if (err < 0)
		return err;

	return 0;
}

static int st_press_spi_remove(struct spi_device *spi)
{
	st_press_common_remove(spi_get_drvdata(spi));

	return 0;
}

static const struct spi_device_id st_press_id_table[] = {
	{ LPS001WP_PRESS_DEV_NAME },
	{ LPS25H_PRESS_DEV_NAME },
	{ LPS331AP_PRESS_DEV_NAME },
	{ LPS22HB_PRESS_DEV_NAME },
	{},
};
MODULE_DEVICE_TABLE(spi, st_press_id_table);

static struct spi_driver st_press_driver = {
	.driver = {
		.name = "st-press-spi",
		.of_match_table = of_match_ptr(st_press_of_match),
	},
	.probe = st_press_spi_probe,
	.remove = st_press_spi_remove,
	.id_table = st_press_id_table,
};
module_spi_driver(st_press_driver);

MODULE_AUTHOR("Denis Ciocca <denis.ciocca@st.com>");
MODULE_DESCRIPTION("STMicroelectronics pressures spi driver");
MODULE_LICENSE("GPL v2");
