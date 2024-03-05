// SPDX-License-Identifier: GPL-2.0-only
/*
 * STMicroelectronics st_mag40 driver
 *
 * MEMS Software Solutions Team
 *
 * Copyright 2016 STMicroelectronics Inc.
 */

#include <linux/module.h>
#include <linux/slab.h>
#include <linux/spi/spi.h>
#include <linux/types.h>

#include "st_mag40_core.h"

#define ST_SENSORS_SPI_READ	0x80

static int st_mag40_spi_read(struct st_mag40_data *cdata,
			     u8 reg_addr, int len, u8 *data)
{
	int err;

	struct spi_transfer xfers[] = {
		{
			.tx_buf = cdata->tb.tx_buf,
			.bits_per_word = 8,
			.len = 1,
		},
		{
			.rx_buf = cdata->tb.rx_buf,
			.bits_per_word = 8,
			.len = len,
		}
	};

	cdata->tb.tx_buf[0] = reg_addr | ST_SENSORS_SPI_READ;

	err = spi_sync_transfer(to_spi_device(cdata->dev),
						xfers, ARRAY_SIZE(xfers));
	if (err)
		return err;

	memcpy(data, cdata->tb.rx_buf, len*sizeof(u8));

	return len;
}

static int st_mag40_spi_write(struct st_mag40_data *cdata,
			      u8 reg_addr, int len, u8 *data)
{
	struct spi_transfer xfers = {
		.tx_buf = cdata->tb.tx_buf,
		.bits_per_word = 8,
		.len = len + 1,
	};

	if (len >= ST_MAG40_RX_MAX_LENGTH)
		return -ENOMEM;

	cdata->tb.tx_buf[0] = reg_addr;

	memcpy(&cdata->tb.tx_buf[1], data, len);

	return spi_sync_transfer(to_spi_device(cdata->dev), &xfers, 1);
}

static const struct st_mag40_transfer_function st_mag40_tf_spi = {
	.write = st_mag40_spi_write,
	.read = st_mag40_spi_read,
};

static int st_mag40_spi_probe(struct spi_device *spi)
{
	struct st_mag40_data *cdata;
	struct iio_dev *iio_dev;

	iio_dev = devm_iio_device_alloc(&spi->dev, sizeof(*cdata));
	if (!iio_dev)
		return -ENOMEM;

	spi_set_drvdata(spi, iio_dev);
	iio_dev->dev.parent = &spi->dev;
	iio_dev->name = spi->modalias;

	cdata = iio_priv(iio_dev);
	cdata->dev = &spi->dev;
	cdata->name = spi->modalias;
	cdata->tf = &st_mag40_tf_spi;
	cdata->irq = spi->irq;

	return st_mag40_common_probe(iio_dev);
}

#if KERNEL_VERSION(5, 18, 0) <= LINUX_VERSION_CODE
static void st_mag40_spi_remove(struct spi_device *spi)
{
	struct iio_dev *iio_dev = spi_get_drvdata(spi);

	st_mag40_common_remove(iio_dev);
}
#else /* LINUX_VERSION_CODE */
static int st_mag40_spi_remove(struct spi_device *spi)
{
	struct iio_dev *iio_dev = spi_get_drvdata(spi);

	st_mag40_common_remove(iio_dev);

	return 0;
}
#endif /* LINUX_VERSION_CODE */

#ifdef CONFIG_PM
static int __maybe_unused st_mag40_spi_suspend(struct device *dev)
{
	struct iio_dev *iio_dev = dev_get_drvdata(dev);
	struct st_mag40_data *cdata = iio_priv(iio_dev);

	return st_mag40_common_suspend(cdata);
}

static int __maybe_unused st_mag40_spi_resume(struct device *dev)
{
	struct iio_dev *iio_dev = dev_get_drvdata(dev);
	struct st_mag40_data *cdata = iio_priv(iio_dev);

	return st_mag40_common_resume(cdata);
}

static const struct dev_pm_ops st_mag40_spi_pm_ops = {
	SET_SYSTEM_SLEEP_PM_OPS(st_mag40_spi_suspend, st_mag40_spi_resume)
};
#define ST_MAG40_PM_OPS		(&st_mag40_spi_pm_ops)
#else /* CONFIG_PM */
#define ST_MAG40_PM_OPS		NULL
#endif /* CONFIG_PM */

static const struct spi_device_id st_mag40_ids[] = {
	{ LSM303AH_DEV_NAME, 0 },
	{ LSM303AGR_DEV_NAME, 0 },
	{ LIS2MDL_DEV_NAME, 0 },
	{ ISM303DAC_DEV_NAME, 0 },
	{ IIS2MDC_DEV_NAME, 0 },
	{}
};

MODULE_DEVICE_TABLE(spi, st_mag40_ids);

#ifdef CONFIG_OF
static const struct of_device_id st_mag40_id_table[] = {
	{
		.compatible = "st,lsm303ah_magn",
		.data = LSM303AH_DEV_NAME,
	},
	{
		.compatible = "st,lsm303agr_magn",
		.data = LSM303AGR_DEV_NAME,
	},
	{
		.compatible = "st,lis2mdl_magn",
		.data = LSM303AGR_DEV_NAME,
	},
	{
		.compatible = "st,ism303dac_magn",
		.data = ISM303DAC_DEV_NAME,
	},
	{
		.compatible = "st,iis2mdc_magn",
		.data = IIS2MDC_DEV_NAME,
	},
	{},
};

MODULE_DEVICE_TABLE(of, st_mag40_id_table);
#endif /* CONFIG_OF */

static struct spi_driver st_mag40_spi_driver = {
	.driver = {
		   .owner = THIS_MODULE,
		   .name = ST_MAG40_DEV_NAME,
		   .pm = ST_MAG40_PM_OPS,
#ifdef CONFIG_OF
		   .of_match_table = st_mag40_id_table,
#endif /* CONFIG_OF */
		   },
	.probe = st_mag40_spi_probe,
	.remove = st_mag40_spi_remove,
	.id_table = st_mag40_ids,
};
module_spi_driver(st_mag40_spi_driver);

MODULE_DESCRIPTION("STMicroelectronics st_mag40 spi driver");
MODULE_AUTHOR("MEMS Software Solutions Team");
MODULE_LICENSE("GPL v2");
