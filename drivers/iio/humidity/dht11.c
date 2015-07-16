/*
 * DHT11/DHT22 bit banging GPIO driver
 *
 * Copyright (c) Harald Geyer <harald@ccbib.org>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 */

#include <linux/err.h>
#include <linux/interrupt.h>
#include <linux/device.h>
#include <linux/kernel.h>
#include <linux/printk.h>
#include <linux/slab.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/sysfs.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/wait.h>
#include <linux/bitops.h>
#include <linux/completion.h>
#include <linux/mutex.h>
#include <linux/delay.h>
#include <linux/gpio.h>
#include <linux/of_gpio.h>

#include <linux/iio/iio.h>

#define DRIVER_NAME	"dht11"

#define DHT11_DATA_VALID_TIME	2000000000  /* 2s in ns */

#define DHT11_EDGES_PREAMBLE 2
#define DHT11_BITS_PER_READ 40
/*
 * Note that when reading the sensor actually 84 edges are detected, but
 * since the last edge is not significant, we only store 83:
 */
#define DHT11_EDGES_PER_READ (2*DHT11_BITS_PER_READ + DHT11_EDGES_PREAMBLE + 1)

/* Data transmission timing (nano seconds) */
#define DHT11_START_TRANSMISSION	18  /* ms */
#define DHT11_SENSOR_RESPONSE	80000
#define DHT11_START_BIT		50000
#define DHT11_DATA_BIT_LOW	27000
#define DHT11_DATA_BIT_HIGH	70000

struct dht11 {
	struct device			*dev;

	int				gpio;
	int				irq;

	struct completion		completion;
	struct mutex			lock;

	s64				timestamp;
	int				temperature;
	int				humidity;

	/* num_edges: -1 means "no transmission in progress" */
	int				num_edges;
	struct {s64 ts; int value; }	edges[DHT11_EDGES_PER_READ];
};

static unsigned char dht11_decode_byte(int *timing, int threshold)
{
	unsigned char ret = 0;
	int i;

	for (i = 0; i < 8; ++i) {
		ret <<= 1;
		if (timing[i] >= threshold)
			++ret;
	}

	return ret;
}

static int dht11_decode(struct dht11 *dht11, int offset)
{
	int i, t, timing[DHT11_BITS_PER_READ], threshold,
		timeres = DHT11_SENSOR_RESPONSE;
	unsigned char temp_int, temp_dec, hum_int, hum_dec, checksum;

	/* Calculate timestamp resolution */
	for (i = 1; i < dht11->num_edges; ++i) {
		t = dht11->edges[i].ts - dht11->edges[i-1].ts;
		if (t > 0 && t < timeres)
			timeres = t;
	}
	if (2*timeres > DHT11_DATA_BIT_HIGH) {
		pr_err("dht11: timeresolution %d too bad for decoding\n",
			timeres);
		return -EIO;
	}
	threshold = DHT11_DATA_BIT_HIGH / timeres;
	if (DHT11_DATA_BIT_LOW/timeres + 1 >= threshold)
		pr_err("dht11: WARNING: decoding ambiguous\n");

	/* scale down with timeres and check validity */
	for (i = 0; i < DHT11_BITS_PER_READ; ++i) {
		t = dht11->edges[offset + 2*i + 2].ts -
			dht11->edges[offset + 2*i + 1].ts;
		if (!dht11->edges[offset + 2*i + 1].value)
			return -EIO;  /* lost synchronisation */
		timing[i] = t / timeres;
	}

	hum_int = dht11_decode_byte(timing, threshold);
	hum_dec = dht11_decode_byte(&timing[8], threshold);
	temp_int = dht11_decode_byte(&timing[16], threshold);
	temp_dec = dht11_decode_byte(&timing[24], threshold);
	checksum = dht11_decode_byte(&timing[32], threshold);

	if (((hum_int + hum_dec + temp_int + temp_dec) & 0xff) != checksum)
		return -EIO;

	dht11->timestamp = iio_get_time_ns();
	if (hum_int < 20) {  /* DHT22 */
		dht11->temperature = (((temp_int & 0x7f) << 8) + temp_dec) *
					((temp_int & 0x80) ? -100 : 100);
		dht11->humidity = ((hum_int << 8) + hum_dec) * 100;
	} else if (temp_dec == 0 && hum_dec == 0) {  /* DHT11 */
		dht11->temperature = temp_int * 1000;
		dht11->humidity = hum_int * 1000;
	} else {
		dev_err(dht11->dev,
			"Don't know how to decode data: %d %d %d %d\n",
			hum_int, hum_dec, temp_int, temp_dec);
		return -EIO;
	}

	return 0;
}

/*
 * IRQ handler called on GPIO edges
 */
static irqreturn_t dht11_handle_irq(int irq, void *data)
{
	struct iio_dev *iio = data;
	struct dht11 *dht11 = iio_priv(iio);

	/* TODO: Consider making the handler safe for IRQ sharing */
	if (dht11->num_edges < DHT11_EDGES_PER_READ && dht11->num_edges >= 0) {
		dht11->edges[dht11->num_edges].ts = iio_get_time_ns();
		dht11->edges[dht11->num_edges++].value =
						gpio_get_value(dht11->gpio);

		if (dht11->num_edges >= DHT11_EDGES_PER_READ)
			complete(&dht11->completion);
	}

	return IRQ_HANDLED;
}

static int dht11_read_raw(struct iio_dev *iio_dev,
			const struct iio_chan_spec *chan,
			int *val, int *val2, long m)
{
	struct dht11 *dht11 = iio_priv(iio_dev);
	int ret;

	mutex_lock(&dht11->lock);
	if (dht11->timestamp + DHT11_DATA_VALID_TIME < iio_get_time_ns()) {
		reinit_completion(&dht11->completion);

		dht11->num_edges = 0;
		ret = gpio_direction_output(dht11->gpio, 0);
		if (ret)
			goto err;
		msleep(DHT11_START_TRANSMISSION);
		ret = gpio_direction_input(dht11->gpio);
		if (ret)
			goto err;

		ret = request_irq(dht11->irq, dht11_handle_irq,
				  IRQF_TRIGGER_RISING | IRQF_TRIGGER_FALLING,
				  iio_dev->name, iio_dev);
		if (ret)
			goto err;

		ret = wait_for_completion_killable_timeout(&dht11->completion,
								 HZ);

		free_irq(dht11->irq, iio_dev);

		if (ret == 0 && dht11->num_edges < DHT11_EDGES_PER_READ - 1) {
			dev_err(&iio_dev->dev,
					"Only %d signal edges detected\n",
					dht11->num_edges);
			ret = -ETIMEDOUT;
		}
		if (ret < 0)
			goto err;

		ret = dht11_decode(dht11,
				dht11->num_edges == DHT11_EDGES_PER_READ ?
					DHT11_EDGES_PREAMBLE :
					DHT11_EDGES_PREAMBLE - 2);
		if (ret)
			goto err;
	}

	ret = IIO_VAL_INT;
	if (chan->type == IIO_TEMP)
		*val = dht11->temperature;
	else if (chan->type == IIO_HUMIDITYRELATIVE)
		*val = dht11->humidity;
	else
		ret = -EINVAL;
err:
	dht11->num_edges = -1;
	mutex_unlock(&dht11->lock);
	return ret;
}

static const struct iio_info dht11_iio_info = {
	.driver_module		= THIS_MODULE,
	.read_raw		= dht11_read_raw,
};

static const struct iio_chan_spec dht11_chan_spec[] = {
	{ .type = IIO_TEMP,
		.info_mask_separate = BIT(IIO_CHAN_INFO_PROCESSED), },
	{ .type = IIO_HUMIDITYRELATIVE,
		.info_mask_separate = BIT(IIO_CHAN_INFO_PROCESSED), }
};

static const struct of_device_id dht11_dt_ids[] = {
	{ .compatible = "dht11", },
	{ }
};
MODULE_DEVICE_TABLE(of, dht11_dt_ids);

static int dht11_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct device_node *node = dev->of_node;
	struct dht11 *dht11;
	struct iio_dev *iio;
	int ret;

	iio = devm_iio_device_alloc(dev, sizeof(*dht11));
	if (!iio) {
		dev_err(dev, "Failed to allocate IIO device\n");
		return -ENOMEM;
	}

	dht11 = iio_priv(iio);
	dht11->dev = dev;

	dht11->gpio = ret = of_get_gpio(node, 0);
	if (ret < 0)
		return ret;
	ret = devm_gpio_request_one(dev, dht11->gpio, GPIOF_IN, pdev->name);
	if (ret)
		return ret;

	dht11->irq = gpio_to_irq(dht11->gpio);
	if (dht11->irq < 0) {
		dev_err(dev, "GPIO %d has no interrupt\n", dht11->gpio);
		return -EINVAL;
	}

	dht11->timestamp = iio_get_time_ns() - DHT11_DATA_VALID_TIME - 1;
	dht11->num_edges = -1;

	platform_set_drvdata(pdev, iio);

	init_completion(&dht11->completion);
	mutex_init(&dht11->lock);
	iio->name = pdev->name;
	iio->dev.parent = &pdev->dev;
	iio->info = &dht11_iio_info;
	iio->modes = INDIO_DIRECT_MODE;
	iio->channels = dht11_chan_spec;
	iio->num_channels = ARRAY_SIZE(dht11_chan_spec);

	return devm_iio_device_register(dev, iio);
}

static struct platform_driver dht11_driver = {
	.driver = {
		.name	= DRIVER_NAME,
		.of_match_table = dht11_dt_ids,
	},
	.probe  = dht11_probe,
};

module_platform_driver(dht11_driver);

MODULE_AUTHOR("Harald Geyer <harald@ccbib.org>");
MODULE_DESCRIPTION("DHT11 humidity/temperature sensor driver");
MODULE_LICENSE("GPL v2");
