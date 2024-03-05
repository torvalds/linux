// SPDX-License-Identifier: GPL-2.0-only
/*
 * STMicroelectronics lis2hh12 driver
 *
 * MEMS Software Solutions Team
 *
 * Copyright 2016 STMicroelectronics Inc.
 */

#include <linux/module.h>
#include <linux/slab.h>
#include <linux/spi/spi.h>
#include <linux/types.h>

#include "st_lis2hh12.h"

#define ST_SENSORS_SPI_READ			0x80

static int lis2hh12_spi_read(struct lis2hh12_data *cdata,
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

	mutex_lock(&cdata->tb.buf_lock);

	cdata->tb.tx_buf[0] = reg_addr | ST_SENSORS_SPI_READ;

	err = spi_sync_transfer(to_spi_device(cdata->dev),
						xfers, ARRAY_SIZE(xfers));
	if (err)
		goto acc_spi_read_error;

	memcpy(data, cdata->tb.rx_buf, len*sizeof(u8));

	mutex_unlock(&cdata->tb.buf_lock);

	return len;

acc_spi_read_error:
	mutex_unlock(&cdata->tb.buf_lock);

	return err;
}

static int lis2hh12_spi_write(struct lis2hh12_data *cdata,
				u8 reg_addr, int len, u8 *data)
{
	int err;

	struct spi_transfer xfers = {
		.tx_buf = cdata->tb.tx_buf,
		.bits_per_word = 8,
		.len = len + 1,
	};

	if (len >= LIS2HH12_TX_MAX_LENGTH)
		return -ENOMEM;

	mutex_lock(&cdata->tb.buf_lock);

	cdata->tb.tx_buf[0] = reg_addr;

	memcpy(&cdata->tb.tx_buf[1], data, len);

	err = spi_sync_transfer(to_spi_device(cdata->dev), &xfers, 1);

	mutex_unlock(&cdata->tb.buf_lock);

	return err;
}

static const struct lis2hh12_transfer_function lis2hh12_tf_spi = {
	.write = lis2hh12_spi_write,
	.read = lis2hh12_spi_read,
};

static int lis2hh12_spi_probe(struct spi_device *spi)
{
	int err;
	struct lis2hh12_data *cdata;

	cdata = kmalloc(sizeof(*cdata), GFP_KERNEL);
	if (!cdata)
		return -ENOMEM;

	cdata->dev = &spi->dev;
	cdata->name = spi->modalias;
	cdata->tf = &lis2hh12_tf_spi;
	spi_set_drvdata(spi, cdata);

	err = lis2hh12_common_probe(cdata, spi->irq);
	if (err < 0)
		goto free_data;

	return 0;

free_data:
	kfree(cdata);
	return err;
}

#if KERNEL_VERSION(5, 18, 0) <= LINUX_VERSION_CODE
static void lis2hh12_spi_remove(struct spi_device *spi)
{
	struct lis2hh12_data *cdata = spi_get_drvdata(spi);

	lis2hh12_common_remove(cdata, spi->irq);
	dev_info(cdata->dev, "%s: removed\n", LIS2HH12_DEV_NAME);
	kfree(cdata);
}
#else /* LINUX_VERSION_CODE */
static int lis2hh12_spi_remove(struct spi_device *spi)
{
	struct lis2hh12_data *cdata = spi_get_drvdata(spi);

	lis2hh12_common_remove(cdata, spi->irq);
	dev_info(cdata->dev, "%s: removed\n", LIS2HH12_DEV_NAME);
	kfree(cdata);

	return 0;
}
#endif /* LINUX_VERSION_CODE */

#ifdef CONFIG_PM
static int __maybe_unused lis2hh12_suspend(struct device *dev)
{
	struct lis2hh12_data *cdata = spi_get_drvdata(to_spi_device(dev));

	return lis2hh12_common_suspend(cdata);
}

static int __maybe_unused lis2hh12_resume(struct device *dev)
{
	struct lis2hh12_data *cdata = spi_get_drvdata(to_spi_device(dev));

	return lis2hh12_common_resume(cdata);
}

static const struct dev_pm_ops lis2hh12_pm_ops = {
	SET_SYSTEM_SLEEP_PM_OPS(lis2hh12_suspend, lis2hh12_resume)
};

#define LIS2HH12_PM_OPS		(&lis2hh12_pm_ops)
#else /* CONFIG_PM */
#define LIS2HH12_PM_OPS		NULL
#endif /* CONFIG_PM */

static const struct spi_device_id lis2hh12_ids[] = {
	{"lis2hh12", 0},
	{}
};

MODULE_DEVICE_TABLE(spi, lis2hh12_ids);

#ifdef CONFIG_OF
static const struct of_device_id lis2hh12_id_table[] = {
	{ .compatible = "st,lis2hh12"},
	{},
};

MODULE_DEVICE_TABLE(of, lis2hh12_id_table);
#endif /* CONFIG_OF */

static struct spi_driver lis2hh12_spi_driver = {
	.driver = {
		   .owner = THIS_MODULE,
		   .name = LIS2HH12_DEV_NAME,
		   .pm = LIS2HH12_PM_OPS,
#ifdef CONFIG_OF
		   .of_match_table = lis2hh12_id_table,
#endif /* CONFIG_OF */
		   },
	.probe = lis2hh12_spi_probe,
	.remove = lis2hh12_spi_remove,
	.id_table = lis2hh12_ids,
};

module_spi_driver(lis2hh12_spi_driver);

MODULE_DESCRIPTION("STMicroelectronics lis2hh12 spi driver");
MODULE_AUTHOR("MEMS Software Solutions Team");
MODULE_LICENSE("GPL v2");
