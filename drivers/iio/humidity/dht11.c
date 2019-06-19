// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * DHT11/DHT22 bit banging GPIO driver
 *
 * Copyright (c) Harald Geyer <harald@ccbib.org>
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
#include <linux/timekeeping.h>

#include <linux/iio/iio.h>

#define DRIVER_NAME	"dht11"

#define DHT11_DATA_VALID_TIME	2000000000  /* 2s in ns */

#define DHT11_EDGES_PREAMBLE 2
#define DHT11_BITS_PER_READ 40
/*
 * Note that when reading the sensor actually 84 edges are detected, but
 * since the last edge is not significant, we only store 83:
 */
#define DHT11_EDGES_PER_READ (2 * DHT11_BITS_PER_READ + \
			      DHT11_EDGES_PREAMBLE + 1)

/*
 * Data transmission timing:
 * Data bits are encoded as pulse length (high time) on the data line.
 * 0-bit: 22-30uS -- typically 26uS (AM2302)
 * 1-bit: 68-75uS -- typically 70uS (AM2302)
 * The acutal timings also depend on the properties of the cable, with
 * longer cables typically making pulses shorter.
 *
 * Our decoding depends on the time resolution of the system:
 * timeres > 34uS ... don't know what a 1-tick pulse is
 * 34uS > timeres > 30uS ... no problem (30kHz and 32kHz clocks)
 * 30uS > timeres > 23uS ... don't know what a 2-tick pulse is
 * timeres < 23uS ... no problem
 *
 * Luckily clocks in the 33-44kHz range are quite uncommon, so we can
 * support most systems if the threshold for decoding a pulse as 1-bit
 * is chosen carefully. If somebody really wants to support clocks around
 * 40kHz, where this driver is most unreliable, there are two options.
 * a) select an implementation using busy loop polling on those systems
 * b) use the checksum to do some probabilistic decoding
 */
#define DHT11_START_TRANSMISSION_MIN	18000  /* us */
#define DHT11_START_TRANSMISSION_MAX	20000  /* us */
#define DHT11_MIN_TIMERES	34000  /* ns */
#define DHT11_THRESHOLD		49000  /* ns */
#define DHT11_AMBIG_LOW		23000  /* ns */
#define DHT11_AMBIG_HIGH	30000  /* ns */

struct dht11 {
	struct device			*dev;

	int				gpio;
	int				irq;

	struct completion		completion;
	/* The iio sysfs interface doesn't prevent concurrent reads: */
	struct mutex			lock;

	s64				timestamp;
	int				temperature;
	int				humidity;

	/* num_edges: -1 means "no transmission in progress" */
	int				num_edges;
	struct {s64 ts; int value; }	edges[DHT11_EDGES_PER_READ];
};

#ifdef CONFIG_DYNAMIC_DEBUG
/*
 * dht11_edges_print: show the data as actually received by the
 *                    driver.
 */
static void dht11_edges_print(struct dht11 *dht11)
{
	int i;

	dev_dbg(dht11->dev, "%d edges detected:\n", dht11->num_edges);
	for (i = 1; i < dht11->num_edges; ++i) {
		dev_dbg(dht11->dev, "%d: %lld ns %s\n", i,
			dht11->edges[i].ts - dht11->edges[i - 1].ts,
			dht11->edges[i - 1].value ? "high" : "low");
	}
}
#endif /* CONFIG_DYNAMIC_DEBUG */

static unsigned char dht11_decode_byte(char *bits)
{
	unsigned char ret = 0;
	int i;

	for (i = 0; i < 8; ++i) {
		ret <<= 1;
		if (bits[i])
			++ret;
	}

	return ret;
}

static int dht11_decode(struct dht11 *dht11, int offset)
{
	int i, t;
	char bits[DHT11_BITS_PER_READ];
	unsigned char temp_int, temp_dec, hum_int, hum_dec, checksum;

	for (i = 0; i < DHT11_BITS_PER_READ; ++i) {
		t = dht11->edges[offset + 2 * i + 2].ts -
			dht11->edges[offset + 2 * i + 1].ts;
		if (!dht11->edges[offset + 2 * i + 1].value) {
			dev_dbg(dht11->dev,
				"lost synchronisation at edge %d\n",
				offset + 2 * i + 1);
			return -EIO;
		}
		bits[i] = t > DHT11_THRESHOLD;
	}

	hum_int = dht11_decode_byte(bits);
	hum_dec = dht11_decode_byte(&bits[8]);
	temp_int = dht11_decode_byte(&bits[16]);
	temp_dec = dht11_decode_byte(&bits[24]);
	checksum = dht11_decode_byte(&bits[32]);

	if (((hum_int + hum_dec + temp_int + temp_dec) & 0xff) != checksum) {
		dev_dbg(dht11->dev, "invalid checksum\n");
		return -EIO;
	}

	dht11->timestamp = ktime_get_boot_ns();
	if (hum_int < 4) {  /* DHT22: 100000 = (3*256+232)*100 */
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
		dht11->edges[dht11->num_edges].ts = ktime_get_boot_ns();
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
	int ret, timeres, offset;

	mutex_lock(&dht11->lock);
	if (dht11->timestamp + DHT11_DATA_VALID_TIME < ktime_get_boot_ns()) {
		timeres = ktime_get_resolution_ns();
		dev_dbg(dht11->dev, "current timeresolution: %dns\n", timeres);
		if (timeres > DHT11_MIN_TIMERES) {
			dev_err(dht11->dev, "timeresolution %dns too low\n",
				timeres);
			/* In theory a better clock could become available
			 * at some point ... and there is no error code
			 * that really fits better.
			 */
			ret = -EAGAIN;
			goto err;
		}
		if (timeres > DHT11_AMBIG_LOW && timeres < DHT11_AMBIG_HIGH)
			dev_warn(dht11->dev,
				 "timeresolution: %dns - decoding ambiguous\n",
				 timeres);

		reinit_completion(&dht11->completion);

		dht11->num_edges = 0;
		ret = gpio_direction_output(dht11->gpio, 0);
		if (ret)
			goto err;
		usleep_range(DHT11_START_TRANSMISSION_MIN,
			     DHT11_START_TRANSMISSION_MAX);
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

#ifdef CONFIG_DYNAMIC_DEBUG
		dht11_edges_print(dht11);
#endif

		if (ret == 0 && dht11->num_edges < DHT11_EDGES_PER_READ - 1) {
			dev_err(dht11->dev, "Only %d signal edges detected\n",
				dht11->num_edges);
			ret = -ETIMEDOUT;
		}
		if (ret < 0)
			goto err;

		offset = DHT11_EDGES_PREAMBLE +
				dht11->num_edges - DHT11_EDGES_PER_READ;
		for (; offset >= 0; --offset) {
			ret = dht11_decode(dht11, offset);
			if (!ret)
				break;
		}

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

	ret = of_get_gpio(node, 0);
	if (ret < 0)
		return ret;
	dht11->gpio = ret;
	ret = devm_gpio_request_one(dev, dht11->gpio, GPIOF_IN, pdev->name);
	if (ret)
		return ret;

	dht11->irq = gpio_to_irq(dht11->gpio);
	if (dht11->irq < 0) {
		dev_err(dev, "GPIO %d has no interrupt\n", dht11->gpio);
		return -EINVAL;
	}

	dht11->timestamp = ktime_get_boot_ns() - DHT11_DATA_VALID_TIME - 1;
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
