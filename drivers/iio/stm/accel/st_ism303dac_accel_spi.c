// SPDX-License-Identifier: GPL-2.0-only
/*
 * STMicroelectronics ism303dac spi driver
 *
 * MEMS Software Solutions Team
 *
 * Copyright 2018 STMicroelectronics Inc.
 */

#include <linux/module.h>
#include <linux/slab.h>
#include <linux/spi/spi.h>
#include <linux/types.h>

#include "st_ism303dac_accel.h"

#define ST_SENSORS_SPI_READ			0x80

static int ism303dac_spi_read(struct ism303dac_data *cdata,
			      u8 reg_addr, int len, u8 *data, bool b_lock)
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

	if (b_lock)
		mutex_lock(&cdata->regs_lock);

	mutex_lock(&cdata->tb.buf_lock);
	cdata->tb.tx_buf[0] = reg_addr | ST_SENSORS_SPI_READ;

	err = spi_sync_transfer(to_spi_device(cdata->dev),
				xfers, ARRAY_SIZE(xfers));
	if (err)
		goto acc_spi_read_error;

	memcpy(data, cdata->tb.rx_buf, len*sizeof(u8));
	mutex_unlock(&cdata->tb.buf_lock);
	if (b_lock)
		mutex_unlock(&cdata->regs_lock);

	return len;

acc_spi_read_error:
	mutex_unlock(&cdata->tb.buf_lock);
	if (b_lock)
		mutex_unlock(&cdata->regs_lock);

	return err;
}

static int ism303dac_spi_write(struct ism303dac_data *cdata,
			       u8 reg_addr, int len, u8 *data, bool b_lock)
{
	int err;

	struct spi_transfer xfers = {
		.tx_buf = cdata->tb.tx_buf,
		.bits_per_word = 8,
		.len = len + 1,
	};

	if (len >= ISM303DAC_TX_MAX_LENGTH)
		return -ENOMEM;

	if (b_lock)
		mutex_lock(&cdata->regs_lock);

	mutex_lock(&cdata->tb.buf_lock);
	cdata->tb.tx_buf[0] = reg_addr;

	memcpy(&cdata->tb.tx_buf[1], data, len);

	err = spi_sync_transfer(to_spi_device(cdata->dev), &xfers, 1);
	mutex_unlock(&cdata->tb.buf_lock);
	if (b_lock)
		mutex_unlock(&cdata->regs_lock);

	return err;
}

static const struct ism303dac_transfer_function ism303dac_tf_spi = {
	.write = ism303dac_spi_write,
	.read = ism303dac_spi_read,
};

static int ism303dac_spi_probe(struct spi_device *spi)
{
	int err;
	struct ism303dac_data *cdata;

	cdata = kzalloc(sizeof(*cdata), GFP_KERNEL);
	if (!cdata)
		return -ENOMEM;

	cdata->dev = &spi->dev;
	cdata->name = spi->modalias;
	cdata->tf = &ism303dac_tf_spi;
	spi_set_drvdata(spi, cdata);

	err = ism303dac_common_probe(cdata, spi->irq);
	if (err < 0)
		goto free_data;

	return 0;

free_data:
	kfree(cdata);
	return err;
}

#if KERNEL_VERSION(5, 18, 0) <= LINUX_VERSION_CODE
static void ism303dac_spi_remove(struct spi_device *spi)
{
	struct ism303dac_data *cdata = spi_get_drvdata(spi);

	ism303dac_common_remove(cdata, spi->irq);
	dev_info(cdata->dev, "%s: removed\n", ISM303DAC_DEV_NAME);
	kfree(cdata);
}
#else /* LINUX_VERSION_CODE */
static int ism303dac_spi_remove(struct spi_device *spi)
{
	struct ism303dac_data *cdata = spi_get_drvdata(spi);

	ism303dac_common_remove(cdata, spi->irq);
	dev_info(cdata->dev, "%s: removed\n", ISM303DAC_DEV_NAME);
	kfree(cdata);

	return 0;
}
#endif /* LINUX_VERSION_CODE */

#ifdef CONFIG_PM
static int __maybe_unused ism303dac_suspend(struct device *dev)
{
	struct ism303dac_data *cdata = spi_get_drvdata(to_spi_device(dev));

	return ism303dac_common_suspend(cdata);
}

static int __maybe_unused ism303dac_resume(struct device *dev)
{
	struct ism303dac_data *cdata = spi_get_drvdata(to_spi_device(dev));

	return ism303dac_common_resume(cdata);
}

static const struct dev_pm_ops ism303dac_pm_ops = {
	SET_SYSTEM_SLEEP_PM_OPS(ism303dac_suspend, ism303dac_resume)
};

#define ISM303DAC_PM_OPS		(&ism303dac_pm_ops)
#else /* CONFIG_PM */
#define ISM303DAC_PM_OPS		NULL
#endif /* CONFIG_PM */

static const struct spi_device_id ism303dac_ids[] = {
	{ ISM303DAC_DEV_NAME, 0 },
	{}
};

MODULE_DEVICE_TABLE(spi, ism303dac_ids);

#ifdef CONFIG_OF
static const struct of_device_id ism303dac_id_table[] = {
	{.compatible = "st,ism303dac_accel",},
	{},
};

MODULE_DEVICE_TABLE(of, ism303dac_id_table);
#endif /* CONFIG_OF */

static struct spi_driver ism303dac_spi_driver = {
	.driver = {
		   .owner = THIS_MODULE,
		   .name = ISM303DAC_DEV_NAME,
		   .pm = ISM303DAC_PM_OPS,
#ifdef CONFIG_OF
		   .of_match_table = ism303dac_id_table,
#endif /* CONFIG_OF */
		   },
	.probe = ism303dac_spi_probe,
	.remove = ism303dac_spi_remove,
	.id_table = ism303dac_ids,
};

module_spi_driver(ism303dac_spi_driver);

MODULE_DESCRIPTION("STMicroelectronics ism303dac spi driver");
MODULE_AUTHOR("MEMS Software Solutions Team");
MODULE_LICENSE("GPL v2");
