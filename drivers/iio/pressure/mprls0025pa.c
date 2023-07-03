// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * MPRLS0025PA - Honeywell MicroPressure pressure sensor series driver
 *
 * Copyright (c) Andreas Klinger <ak@it-klinger.de>
 *
 * Data sheet:
 *  https://prod-edam.honeywell.com/content/dam/honeywell-edam/sps/siot/en-us/
 *    products/sensors/pressure-sensors/board-mount-pressure-sensors/
 *    micropressure-mpr-series/documents/
 *    sps-siot-mpr-series-datasheet-32332628-ciid-172626.pdf
 *
 * 7-bit I2C default slave address: 0x18
 */

#include <linux/delay.h>
#include <linux/device.h>
#include <linux/i2c.h>
#include <linux/math64.h>
#include <linux/mod_devicetable.h>
#include <linux/module.h>
#include <linux/property.h>
#include <linux/units.h>

#include <linux/gpio/consumer.h>

#include <linux/iio/buffer.h>
#include <linux/iio/iio.h>
#include <linux/iio/trigger_consumer.h>
#include <linux/iio/triggered_buffer.h>

#include <linux/regulator/consumer.h>

#include <asm/unaligned.h>

/* bits in i2c status byte */
#define MPR_I2C_POWER	BIT(6)	/* device is powered */
#define MPR_I2C_BUSY	BIT(5)	/* device is busy */
#define MPR_I2C_MEMORY	BIT(2)	/* integrity test passed */
#define MPR_I2C_MATH	BIT(0)	/* internal math saturation */

/*
 * support _RAW sysfs interface:
 *
 * Calculation formula from the datasheet:
 * pressure = (press_cnt - outputmin) * scale + pmin
 * with:
 * * pressure	- measured pressure in Pascal
 * * press_cnt	- raw value read from sensor
 * * pmin	- minimum pressure range value of sensor (data->pmin)
 * * pmax	- maximum pressure range value of sensor (data->pmax)
 * * outputmin	- minimum numerical range raw value delivered by sensor
 *						(mpr_func_spec.output_min)
 * * outputmax	- maximum numerical range raw value delivered by sensor
 *						(mpr_func_spec.output_max)
 * * scale	- (pmax - pmin) / (outputmax - outputmin)
 *
 * formula of the userspace:
 * pressure = (raw + offset) * scale
 *
 * Values given to the userspace in sysfs interface:
 * * raw	- press_cnt
 * * offset	- (-1 * outputmin) - pmin / scale
 *                note: With all sensors from the datasheet pmin = 0
 *                which reduces the offset to (-1 * outputmin)
 */

/*
 * transfer function A: 10%   to 90%   of 2^24
 * transfer function B:  2.5% to 22.5% of 2^24
 * transfer function C: 20%   to 80%   of 2^24
 */
enum mpr_func_id {
	MPR_FUNCTION_A,
	MPR_FUNCTION_B,
	MPR_FUNCTION_C,
};

struct mpr_func_spec {
	u32			output_min;
	u32			output_max;
};

static const struct mpr_func_spec mpr_func_spec[] = {
	[MPR_FUNCTION_A] = {.output_min = 1677722, .output_max = 15099494},
	[MPR_FUNCTION_B] = {.output_min =  419430, .output_max =  3774874},
	[MPR_FUNCTION_C] = {.output_min = 3355443, .output_max = 13421773},
};

struct mpr_chan {
	s32			pres;		/* pressure value */
	s64			ts;		/* timestamp */
};

struct mpr_data {
	struct i2c_client	*client;
	struct mutex		lock;		/*
						 * access to device during read
						 */
	u32			pmin;		/* minimal pressure in pascal */
	u32			pmax;		/* maximal pressure in pascal */
	enum mpr_func_id	function;	/* transfer function */
	u32			outmin;		/*
						 * minimal numerical range raw
						 * value from sensor
						 */
	u32			outmax;		/*
						 * maximal numerical range raw
						 * value from sensor
						 */
	int                     scale;          /* int part of scale */
	int                     scale2;         /* nano part of scale */
	int                     offset;         /* int part of offset */
	int                     offset2;        /* nano part of offset */
	struct gpio_desc	*gpiod_reset;	/* reset */
	int			irq;		/*
						 * end of conversion irq;
						 * used to distinguish between
						 * irq mode and reading in a
						 * loop until data is ready
						 */
	struct completion	completion;	/* handshake from irq to read */
	struct mpr_chan		chan;		/*
						 * channel values for buffered
						 * mode
						 */
};

static const struct iio_chan_spec mpr_channels[] = {
	{
		.type = IIO_PRESSURE,
		.info_mask_separate = BIT(IIO_CHAN_INFO_RAW) |
					BIT(IIO_CHAN_INFO_SCALE) |
					BIT(IIO_CHAN_INFO_OFFSET),
		.scan_index = 0,
		.scan_type = {
			.sign = 's',
			.realbits = 32,
			.storagebits = 32,
			.endianness = IIO_CPU,
		},
	},
	IIO_CHAN_SOFT_TIMESTAMP(1),
};

static void mpr_reset(struct mpr_data *data)
{
	if (data->gpiod_reset) {
		gpiod_set_value(data->gpiod_reset, 0);
		udelay(10);
		gpiod_set_value(data->gpiod_reset, 1);
	}
}

/**
 * mpr_read_pressure() - Read pressure value from sensor via I2C
 * @data: Pointer to private data struct.
 * @press: Output value read from sensor.
 *
 * Reading from the sensor by sending and receiving I2C telegrams.
 *
 * If there is an end of conversion (EOC) interrupt registered the function
 * waits for a maximum of one second for the interrupt.
 *
 * Context: The function can sleep and data->lock should be held when calling it
 * Return:
 * * 0		- OK, the pressure value could be read
 * * -ETIMEDOUT	- Timeout while waiting for the EOC interrupt or busy flag is
 *		  still set after nloops attempts of reading
 */
static int mpr_read_pressure(struct mpr_data *data, s32 *press)
{
	struct device *dev = &data->client->dev;
	int ret, i;
	u8 wdata[] = {0xAA, 0x00, 0x00};
	s32 status;
	int nloops = 10;
	u8 buf[4];

	reinit_completion(&data->completion);

	ret = i2c_master_send(data->client, wdata, sizeof(wdata));
	if (ret < 0) {
		dev_err(dev, "error while writing ret: %d\n", ret);
		return ret;
	}
	if (ret != sizeof(wdata)) {
		dev_err(dev, "received size doesn't fit - ret: %d / %u\n", ret,
							(u32)sizeof(wdata));
		return -EIO;
	}

	if (data->irq > 0) {
		ret = wait_for_completion_timeout(&data->completion, HZ);
		if (!ret) {
			dev_err(dev, "timeout while waiting for eoc irq\n");
			return -ETIMEDOUT;
		}
	} else {
		/* wait until status indicates data is ready */
		for (i = 0; i < nloops; i++) {
			/*
			 * datasheet only says to wait at least 5 ms for the
			 * data but leave the maximum response time open
			 * --> let's try it nloops (10) times which seems to be
			 *     quite long
			 */
			usleep_range(5000, 10000);
			status = i2c_smbus_read_byte(data->client);
			if (status < 0) {
				dev_err(dev,
					"error while reading, status: %d\n",
					status);
				return status;
			}
			if (!(status & MPR_I2C_BUSY))
				break;
		}
		if (i == nloops) {
			dev_err(dev, "timeout while reading\n");
			return -ETIMEDOUT;
		}
	}

	ret = i2c_master_recv(data->client, buf, sizeof(buf));
	if (ret < 0) {
		dev_err(dev, "error in i2c_master_recv ret: %d\n", ret);
		return ret;
	}
	if (ret != sizeof(buf)) {
		dev_err(dev, "received size doesn't fit - ret: %d / %u\n", ret,
							(u32)sizeof(buf));
		return -EIO;
	}

	if (buf[0] & MPR_I2C_BUSY) {
		/*
		 * it should never be the case that status still indicates
		 * business
		 */
		dev_err(dev, "data still not ready: %08x\n", buf[0]);
		return -ETIMEDOUT;
	}

	*press = get_unaligned_be24(&buf[1]);

	dev_dbg(dev, "received: %*ph cnt: %d\n", ret, buf, *press);

	return 0;
}

static irqreturn_t mpr_eoc_handler(int irq, void *p)
{
	struct mpr_data *data = p;

	complete(&data->completion);

	return IRQ_HANDLED;
}

static irqreturn_t mpr_trigger_handler(int irq, void *p)
{
	int ret;
	struct iio_poll_func *pf = p;
	struct iio_dev *indio_dev = pf->indio_dev;
	struct mpr_data *data = iio_priv(indio_dev);

	mutex_lock(&data->lock);
	ret = mpr_read_pressure(data, &data->chan.pres);
	if (ret < 0)
		goto err;

	iio_push_to_buffers_with_timestamp(indio_dev, &data->chan,
						iio_get_time_ns(indio_dev));

err:
	mutex_unlock(&data->lock);
	iio_trigger_notify_done(indio_dev->trig);

	return IRQ_HANDLED;
}

static int mpr_read_raw(struct iio_dev *indio_dev,
	struct iio_chan_spec const *chan, int *val, int *val2, long mask)
{
	int ret;
	s32 pressure;
	struct mpr_data *data = iio_priv(indio_dev);

	if (chan->type != IIO_PRESSURE)
		return -EINVAL;

	switch (mask) {
	case IIO_CHAN_INFO_RAW:
		mutex_lock(&data->lock);
		ret = mpr_read_pressure(data, &pressure);
		mutex_unlock(&data->lock);
		if (ret < 0)
			return ret;
		*val = pressure;
		return IIO_VAL_INT;
	case IIO_CHAN_INFO_SCALE:
		*val = data->scale;
		*val2 = data->scale2;
		return IIO_VAL_INT_PLUS_NANO;
	case IIO_CHAN_INFO_OFFSET:
		*val = data->offset;
		*val2 = data->offset2;
		return IIO_VAL_INT_PLUS_NANO;
	default:
		return -EINVAL;
	}
}

static const struct iio_info mpr_info = {
	.read_raw = &mpr_read_raw,
};

static int mpr_probe(struct i2c_client *client)
{
	int ret;
	struct mpr_data *data;
	struct iio_dev *indio_dev;
	struct device *dev = &client->dev;
	s64 scale, offset;

	if (!i2c_check_functionality(client->adapter, I2C_FUNC_SMBUS_READ_BYTE))
		return dev_err_probe(dev, -EOPNOTSUPP,
					"I2C functionality not supported\n");

	indio_dev = devm_iio_device_alloc(dev, sizeof(*data));
	if (!indio_dev)
		return dev_err_probe(dev, -ENOMEM, "couldn't get iio_dev\n");

	data = iio_priv(indio_dev);
	data->client = client;
	data->irq = client->irq;

	mutex_init(&data->lock);
	init_completion(&data->completion);

	indio_dev->name = "mprls0025pa";
	indio_dev->info = &mpr_info;
	indio_dev->channels = mpr_channels;
	indio_dev->num_channels = ARRAY_SIZE(mpr_channels);
	indio_dev->modes = INDIO_DIRECT_MODE;

	ret = devm_regulator_get_enable(dev, "vdd");
	if (ret)
		return dev_err_probe(dev, ret,
				"can't get and enable vdd supply\n");

	if (dev_fwnode(dev)) {
		ret = device_property_read_u32(dev, "honeywell,pmin-pascal",
								&data->pmin);
		if (ret)
			return dev_err_probe(dev, ret,
				"honeywell,pmin-pascal could not be read\n");
		ret = device_property_read_u32(dev, "honeywell,pmax-pascal",
								&data->pmax);
		if (ret)
			return dev_err_probe(dev, ret,
				"honeywell,pmax-pascal could not be read\n");
		ret = device_property_read_u32(dev,
				"honeywell,transfer-function", &data->function);
		if (ret)
			return dev_err_probe(dev, ret,
				"honeywell,transfer-function could not be read\n");
		if (data->function > MPR_FUNCTION_C)
			return dev_err_probe(dev, -EINVAL,
				"honeywell,transfer-function %d invalid\n",
								data->function);
	} else {
		/* when loaded as i2c device we need to use default values */
		dev_notice(dev, "firmware node not found; using defaults\n");
		data->pmin = 0;
		data->pmax = 172369; /* 25 psi */
		data->function = MPR_FUNCTION_A;
	}

	data->outmin = mpr_func_spec[data->function].output_min;
	data->outmax = mpr_func_spec[data->function].output_max;

	/* use 64 bit calculation for preserving a reasonable precision */
	scale = div_s64(((s64)(data->pmax - data->pmin)) * NANO,
						data->outmax - data->outmin);
	data->scale = div_s64_rem(scale, NANO, &data->scale2);
	/*
	 * multiply with NANO before dividing by scale and later divide by NANO
	 * again.
	 */
	offset = ((-1LL) * (s64)data->outmin) * NANO -
			div_s64(div_s64((s64)data->pmin * NANO, scale), NANO);
	data->offset = div_s64_rem(offset, NANO, &data->offset2);

	if (data->irq > 0) {
		ret = devm_request_irq(dev, data->irq, mpr_eoc_handler,
				IRQF_TRIGGER_RISING, client->name, data);
		if (ret)
			return dev_err_probe(dev, ret,
				"request irq %d failed\n", data->irq);
	}

	data->gpiod_reset = devm_gpiod_get_optional(dev, "reset",
							GPIOD_OUT_HIGH);
	if (IS_ERR(data->gpiod_reset))
		return dev_err_probe(dev, PTR_ERR(data->gpiod_reset),
						"request reset-gpio failed\n");

	mpr_reset(data);

	ret = devm_iio_triggered_buffer_setup(dev, indio_dev, NULL,
						mpr_trigger_handler, NULL);
	if (ret)
		return dev_err_probe(dev, ret,
					"iio triggered buffer setup failed\n");

	ret = devm_iio_device_register(dev, indio_dev);
	if (ret)
		return dev_err_probe(dev, ret,
					"unable to register iio device\n");

	return 0;
}

static const struct of_device_id mpr_matches[] = {
	{ .compatible = "honeywell,mprls0025pa" },
	{ }
};
MODULE_DEVICE_TABLE(of, mpr_matches);

static const struct i2c_device_id mpr_id[] = {
	{ "mprls0025pa" },
	{ }
};
MODULE_DEVICE_TABLE(i2c, mpr_id);

static struct i2c_driver mpr_driver = {
	.probe		= mpr_probe,
	.id_table	= mpr_id,
	.driver		= {
		.name		= "mprls0025pa",
		.of_match_table = mpr_matches,
	},
};
module_i2c_driver(mpr_driver);

MODULE_AUTHOR("Andreas Klinger <ak@it-klinger.de>");
MODULE_DESCRIPTION("Honeywell MPRLS0025PA I2C driver");
MODULE_LICENSE("GPL");
