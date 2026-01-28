// SPDX-License-Identifier: GPL-2.0-only
/*
 * rfd77402.c - Support for RF Digital RFD77402 Time-of-Flight (distance) sensor
 *
 * Copyright 2017 Peter Meerwald-Stadler <pmeerw@pmeerw.net>
 *
 * 7-bit I2C slave address 0x4c
 *
 * https://media.digikey.com/pdf/Data%20Sheets/RF%20Digital%20PDFs/RFD77402.pdf
 */

#include <linux/bits.h>
#include <linux/completion.h>
#include <linux/delay.h>
#include <linux/dev_printk.h>
#include <linux/errno.h>
#include <linux/i2c.h>
#include <linux/interrupt.h>
#include <linux/iopoll.h>
#include <linux/jiffies.h>
#include <linux/module.h>
#include <linux/types.h>

#include <linux/iio/iio.h>

#define RFD77402_DRV_NAME "rfd77402"

#define RFD77402_ICSR		0x00 /* Interrupt Control Status Register */
#define RFD77402_ICSR_CLR_CFG	BIT(0)
#define RFD77402_ICSR_CLR_TYPE	BIT(1)
#define RFD77402_ICSR_INT_MODE	BIT(2)
#define RFD77402_ICSR_INT_POL	BIT(3)
#define RFD77402_ICSR_RESULT	BIT(4)
#define RFD77402_ICSR_M2H_MSG	BIT(5)
#define RFD77402_ICSR_H2M_MSG	BIT(6)
#define RFD77402_ICSR_RESET	BIT(7)

#define RFD77402_IER		0x02
#define RFD77402_IER_RESULT	BIT(0)
#define RFD77402_IER_M2H_MSG	BIT(1)
#define RFD77402_IER_H2M_MSG	BIT(2)
#define RFD77402_IER_RESET	BIT(3)

#define RFD77402_CMD_R		0x04
#define RFD77402_CMD_SINGLE	0x01
#define RFD77402_CMD_STANDBY	0x10
#define RFD77402_CMD_MCPU_OFF	0x11
#define RFD77402_CMD_MCPU_ON	0x12
#define RFD77402_CMD_RESET	BIT(6)
#define RFD77402_CMD_VALID	BIT(7)

#define RFD77402_STATUS_R	0x06
#define RFD77402_STATUS_PM_MASK	GENMASK(4, 0)
#define RFD77402_STATUS_STANDBY	0x00
#define RFD77402_STATUS_MCPU_OFF	0x10
#define RFD77402_STATUS_MCPU_ON	0x18

#define RFD77402_RESULT_R	0x08
#define RFD77402_RESULT_DIST_MASK	GENMASK(12, 2)
#define RFD77402_RESULT_ERR_MASK	GENMASK(14, 13)
#define RFD77402_RESULT_VALID	BIT(15)

#define RFD77402_PMU_CFG	0x14
#define RFD77402_PMU_MCPU_INIT	BIT(9)

#define RFD77402_I2C_INIT_CFG	0x1c
#define RFD77402_I2C_ADDR_INCR	BIT(0)
#define RFD77402_I2C_DATA_INCR	BIT(2)
#define RFD77402_I2C_HOST_DEBUG	BIT(5)
#define RFD77402_I2C_MCPU_DEBUG	BIT(6)

#define RFD77402_CMD_CFGR_A	0x0c
#define RFD77402_CMD_CFGR_B	0x0e
#define RFD77402_HFCFG_0	0x20
#define RFD77402_HFCFG_1	0x22
#define RFD77402_HFCFG_2	0x24
#define RFD77402_HFCFG_3	0x26

#define RFD77402_MOD_CHIP_ID	0x28

/* magic configuration values from datasheet */
static const struct {
	u8 reg;
	u16 val;
} rf77402_tof_config[] = {
	{RFD77402_CMD_CFGR_A,	0xe100},
	{RFD77402_CMD_CFGR_B,	0x10ff},
	{RFD77402_HFCFG_0,	0x07d0},
	{RFD77402_HFCFG_1,	0x5008},
	{RFD77402_HFCFG_2,	0xa041},
	{RFD77402_HFCFG_3,	0x45d4},
};

/**
 * struct rfd77402_data - device-specific data for the RFD77402 sensor
 * @client: I2C client handle
 * @lock: mutex to serialize sensor reads
 * @completion: completion used for interrupt-driven measurements
 * @irq_en: indicates whether interrupt mode is enabled
 */
struct rfd77402_data {
	struct i2c_client *client;
	struct mutex lock;
	struct completion completion;
	bool irq_en;
};

static const struct iio_chan_spec rfd77402_channels[] = {
	{
		.type = IIO_DISTANCE,
		.info_mask_separate = BIT(IIO_CHAN_INFO_RAW) |
				      BIT(IIO_CHAN_INFO_SCALE),
	},
};

static irqreturn_t rfd77402_interrupt_handler(int irq, void *pdata)
{
	struct rfd77402_data *data = pdata;
	int ret;

	ret = i2c_smbus_read_byte_data(data->client, RFD77402_ICSR);
	if (ret < 0)
		return IRQ_NONE;

	/* Check if the interrupt is from our device */
	if (!(ret & RFD77402_ICSR_RESULT))
		return IRQ_NONE;

	/* Signal completion of measurement */
	complete(&data->completion);
	return IRQ_HANDLED;
}

static int rfd77402_wait_for_irq(struct rfd77402_data *data)
{
	int ret;

	/*
	 * According to RFD77402 Datasheet v1.8,
	 * Section 3.1.1 "Single Measure" (Figure: Single Measure Flow Chart),
	 * the suggested timeout for single measure is 100 ms.
	 */
	ret = wait_for_completion_timeout(&data->completion,
					  msecs_to_jiffies(100));
	if (ret == 0)
		return -ETIMEDOUT;

	return 0;
}

static int rfd77402_set_state(struct i2c_client *client, u8 state, u16 check)
{
	int ret;

	ret = i2c_smbus_write_byte_data(client, RFD77402_CMD_R,
					state | RFD77402_CMD_VALID);
	if (ret < 0)
		return ret;

	usleep_range(10000, 20000);

	ret = i2c_smbus_read_word_data(client, RFD77402_STATUS_R);
	if (ret < 0)
		return ret;
	if ((ret & RFD77402_STATUS_PM_MASK) != check)
		return -ENODEV;

	return 0;
}

static int rfd77402_wait_for_result(struct rfd77402_data *data)
{
	struct i2c_client *client = data->client;
	int val, ret;

	if (data->irq_en) {
		reinit_completion(&data->completion);
		return rfd77402_wait_for_irq(data);
	}

	/*
	 * As per RFD77402 datasheet section '3.1.1 Single Measure', the
	 * suggested timeout value for single measure is 100ms.
	 */
	ret = read_poll_timeout(i2c_smbus_read_byte_data, val,
				 (val < 0) || (val & RFD77402_ICSR_RESULT),
				 10 * USEC_PER_MSEC,
				 10 * 10 * USEC_PER_MSEC,
				 false,
				 client, RFD77402_ICSR);
	if (val < 0)
		return val;

	return ret;
}

static int rfd77402_measure(struct rfd77402_data *data)
{
	struct i2c_client *client = data->client;
	int ret;

	ret = rfd77402_set_state(client, RFD77402_CMD_MCPU_ON,
				 RFD77402_STATUS_MCPU_ON);
	if (ret < 0)
		return ret;

	ret = i2c_smbus_write_byte_data(client, RFD77402_CMD_R,
					RFD77402_CMD_SINGLE |
					RFD77402_CMD_VALID);
	if (ret < 0)
		goto err;

	ret = rfd77402_wait_for_result(data);
	if (ret < 0)
		goto err;

	ret = i2c_smbus_read_word_data(client, RFD77402_RESULT_R);
	if (ret < 0)
		goto err;

	if ((ret & RFD77402_RESULT_ERR_MASK) ||
	    !(ret & RFD77402_RESULT_VALID)) {
		ret = -EIO;
		goto err;
	}

	return (ret & RFD77402_RESULT_DIST_MASK) >> 2;

err:
	rfd77402_set_state(client, RFD77402_CMD_MCPU_OFF,
			   RFD77402_STATUS_MCPU_OFF);
	return ret;
}

static int rfd77402_read_raw(struct iio_dev *indio_dev,
			     struct iio_chan_spec const *chan,
			     int *val, int *val2, long mask)
{
	struct rfd77402_data *data = iio_priv(indio_dev);
	int ret;

	switch (mask) {
	case IIO_CHAN_INFO_RAW:
		mutex_lock(&data->lock);
		ret = rfd77402_measure(data);
		mutex_unlock(&data->lock);
		if (ret < 0)
			return ret;
		*val = ret;
		return IIO_VAL_INT;
	case IIO_CHAN_INFO_SCALE:
		/* 1 LSB is 1 mm */
		*val = 0;
		*val2 = 1000;
		return IIO_VAL_INT_PLUS_MICRO;
	default:
		return -EINVAL;
	}
}

static const struct iio_info rfd77402_info = {
	.read_raw = rfd77402_read_raw,
};

static int rfd77402_config_irq(struct i2c_client *client, u8 csr, u8 ier)
{
	int ret;

	ret = i2c_smbus_write_byte_data(client, RFD77402_ICSR, csr);
	if (ret)
		return ret;

	return i2c_smbus_write_byte_data(client, RFD77402_IER, ier);
}

static int rfd77402_init(struct rfd77402_data *data)
{
	struct i2c_client *client = data->client;
	int ret, i;

	ret = rfd77402_set_state(client, RFD77402_CMD_STANDBY,
				 RFD77402_STATUS_STANDBY);
	if (ret < 0)
		return ret;

	if (data->irq_en) {
		/*
		 * Enable interrupt mode:
		 * - Configure ICSR for auto-clear on read and
		 *   push-pull output
		 * - Enable "result ready" interrupt in IER
		 */
		ret = rfd77402_config_irq(client,
					  RFD77402_ICSR_CLR_CFG |
					  RFD77402_ICSR_INT_MODE,
					  RFD77402_IER_RESULT);
	} else {
		/*
		 * Disable all interrupts:
		 * - Clear ICSR configuration
		 * - Disable all interrupts in IER
		 */
		ret = rfd77402_config_irq(client, 0, 0);
	}
	if (ret)
		return ret;

	/* I2C configuration */
	ret = i2c_smbus_write_word_data(client, RFD77402_I2C_INIT_CFG,
					RFD77402_I2C_ADDR_INCR |
					RFD77402_I2C_DATA_INCR |
					RFD77402_I2C_HOST_DEBUG	|
					RFD77402_I2C_MCPU_DEBUG);
	if (ret < 0)
		return ret;

	/* set initialization */
	ret = i2c_smbus_write_word_data(client, RFD77402_PMU_CFG, 0x0500);
	if (ret < 0)
		return ret;

	ret = rfd77402_set_state(client, RFD77402_CMD_MCPU_OFF,
				 RFD77402_STATUS_MCPU_OFF);
	if (ret < 0)
		return ret;

	/* set initialization */
	ret = i2c_smbus_write_word_data(client, RFD77402_PMU_CFG, 0x0600);
	if (ret < 0)
		return ret;

	ret = rfd77402_set_state(client, RFD77402_CMD_MCPU_ON,
				 RFD77402_STATUS_MCPU_ON);
	if (ret < 0)
		return ret;

	for (i = 0; i < ARRAY_SIZE(rf77402_tof_config); i++) {
		ret = i2c_smbus_write_word_data(client,
						rf77402_tof_config[i].reg,
						rf77402_tof_config[i].val);
		if (ret < 0)
			return ret;
	}

	ret = rfd77402_set_state(client, RFD77402_CMD_STANDBY,
				 RFD77402_STATUS_STANDBY);

	return ret;
}

static int rfd77402_powerdown(struct i2c_client *client)
{
	return rfd77402_set_state(client, RFD77402_CMD_STANDBY,
				  RFD77402_STATUS_STANDBY);
}

static void rfd77402_disable(void *client)
{
	rfd77402_powerdown(client);
}

static int rfd77402_probe(struct i2c_client *client)
{
	struct rfd77402_data *data;
	struct iio_dev *indio_dev;
	int ret;

	ret = i2c_smbus_read_word_data(client, RFD77402_MOD_CHIP_ID);
	if (ret < 0)
		return ret;
	if (ret != 0xad01 && ret != 0xad02) /* known chip ids */
		return -ENODEV;

	indio_dev = devm_iio_device_alloc(&client->dev, sizeof(*data));
	if (!indio_dev)
		return -ENOMEM;

	data = iio_priv(indio_dev);
	data->client = client;

	ret = devm_mutex_init(&client->dev, &data->lock);
	if (ret)
		return ret;

	init_completion(&data->completion);

	if (client->irq > 0) {
		ret = devm_request_threaded_irq(&client->dev, client->irq,
						NULL, rfd77402_interrupt_handler,
						IRQF_ONESHOT,
						"rfd77402", data);
		if (ret)
			return ret;

		data->irq_en = true;
		dev_dbg(&client->dev, "Using interrupt mode\n");
	} else {
		dev_dbg(&client->dev, "Using polling mode\n");
	}

	indio_dev->info = &rfd77402_info;
	indio_dev->channels = rfd77402_channels;
	indio_dev->num_channels = ARRAY_SIZE(rfd77402_channels);
	indio_dev->name = RFD77402_DRV_NAME;
	indio_dev->modes = INDIO_DIRECT_MODE;

	ret = rfd77402_init(data);
	if (ret < 0)
		return ret;

	ret = devm_add_action_or_reset(&client->dev, rfd77402_disable, client);
	if (ret)
		return ret;

	return devm_iio_device_register(&client->dev, indio_dev);
}

static int rfd77402_suspend(struct device *dev)
{
	return rfd77402_powerdown(to_i2c_client(dev));
}

static int rfd77402_resume(struct device *dev)
{
	struct iio_dev *indio_dev = dev_get_drvdata(dev);
	struct rfd77402_data *data = iio_priv(indio_dev);

	return rfd77402_init(data);
}

static DEFINE_SIMPLE_DEV_PM_OPS(rfd77402_pm_ops, rfd77402_suspend,
				rfd77402_resume);

static const struct i2c_device_id rfd77402_id[] = {
	{ "rfd77402" },
	{ }
};
MODULE_DEVICE_TABLE(i2c, rfd77402_id);

static const struct of_device_id rfd77402_of_match[] = {
	{ .compatible = "rfdigital,rfd77402" },
	{ }
};
MODULE_DEVICE_TABLE(of, rfd77402_of_match);

static struct i2c_driver rfd77402_driver = {
	.driver = {
		.name   = RFD77402_DRV_NAME,
		.pm     = pm_sleep_ptr(&rfd77402_pm_ops),
		.of_match_table = rfd77402_of_match,
	},
	.probe = rfd77402_probe,
	.id_table = rfd77402_id,
};

module_i2c_driver(rfd77402_driver);

MODULE_AUTHOR("Peter Meerwald-Stadler <pmeerw@pmeerw.net>");
MODULE_DESCRIPTION("RFD77402 Time-of-Flight sensor driver");
MODULE_LICENSE("GPL");
