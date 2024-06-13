// SPDX-License-Identifier: GPL-2.0
// Copyright (c) 2018 Crane Merchandising Systems. All rights reserved.
// Copyright (C) 2018 Oleh Kravchenko <oleg@kaa.org.ua>

#include <linux/delay.h>
#include <linux/leds.h>
#include <linux/module.h>
#include <linux/of_device.h>
#include <linux/spi/spi.h>
#include <linux/workqueue.h>

/*
 *  CR0014114 SPI protocol descrtiption:
 *  +----+-----------------------------------+----+
 *  | CMD|             BRIGHTNESS            |CRC |
 *  +----+-----------------------------------+----+
 *  |    | LED0| LED1| LED2| LED3| LED4| LED5|    |
 *  |    +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+    |
 *  |    |R|G|B|R|G|B|R|G|B|R|G|B|R|G|B|R|G|B|    |
 *  | 1  +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+ 1  |
 *  |    |1|1|1|1|1|1|1|1|1|1|1|1|1|1|1|1|1|1|    |
 *  |    +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+    |
 *  |    |               18                  |    |
 *  +----+-----------------------------------+----+
 *  |                    20                       |
 *  +---------------------------------------------+
 *
 *  PS: Boards can be connected to the chain:
 *      SPI -> board0 -> board1 -> board2 ..
 */

/* CR0014114 SPI commands */
#define CR_SET_BRIGHTNESS	0x80
#define CR_INIT_REENUMERATE	0x81
#define CR_NEXT_REENUMERATE	0x82

/* CR0014114 default settings */
#define CR_MAX_BRIGHTNESS	GENMASK(6, 0)
#define CR_FW_DELAY_MSEC	10
#define CR_RECOUNT_DELAY	(HZ * 3600)

#define CR_DEV_NAME		"cr0014114"

struct cr0014114_led {
	struct cr0014114	*priv;
	struct led_classdev	ldev;
	u8			brightness;
};

struct cr0014114 {
	bool			do_recount;
	size_t			count;
	struct delayed_work	work;
	struct device		*dev;
	struct mutex		lock;
	struct spi_device	*spi;
	u8			*buf;
	unsigned long		delay;
	struct cr0014114_led	leds[];
};

static void cr0014114_calc_crc(u8 *buf, const size_t len)
{
	size_t	i;
	u8	crc;

	for (i = 1, crc = 1; i < len - 1; i++)
		crc += buf[i];
	crc |= BIT(7);

	/* special case when CRC matches the SPI commands */
	if (crc == CR_SET_BRIGHTNESS ||
	    crc == CR_INIT_REENUMERATE ||
	    crc == CR_NEXT_REENUMERATE)
		crc = 0xfe;

	buf[len - 1] = crc;
}

static int cr0014114_recount(struct cr0014114 *priv)
{
	int	ret;
	size_t	i;
	u8	cmd;

	dev_dbg(priv->dev, "LEDs recount is started\n");

	cmd = CR_INIT_REENUMERATE;
	ret = spi_write(priv->spi, &cmd, sizeof(cmd));
	if (ret)
		goto err;

	cmd = CR_NEXT_REENUMERATE;
	for (i = 0; i < priv->count; i++) {
		msleep(CR_FW_DELAY_MSEC);

		ret = spi_write(priv->spi, &cmd, sizeof(cmd));
		if (ret)
			goto err;
	}

err:
	dev_dbg(priv->dev, "LEDs recount is finished\n");

	if (ret)
		dev_err(priv->dev, "with error %d", ret);

	return ret;
}

static int cr0014114_sync(struct cr0014114 *priv)
{
	int		ret;
	size_t		i;
	unsigned long	udelay, now = jiffies;

	/* to avoid SPI mistiming with firmware we should wait some time */
	if (time_after(priv->delay, now)) {
		udelay = jiffies_to_usecs(priv->delay - now);
		usleep_range(udelay, udelay + 1);
	}

	if (unlikely(priv->do_recount)) {
		ret = cr0014114_recount(priv);
		if (ret)
			goto err;

		priv->do_recount = false;
		msleep(CR_FW_DELAY_MSEC);
	}

	priv->buf[0] = CR_SET_BRIGHTNESS;
	for (i = 0; i < priv->count; i++)
		priv->buf[i + 1] = priv->leds[i].brightness;
	cr0014114_calc_crc(priv->buf, priv->count + 2);
	ret = spi_write(priv->spi, priv->buf, priv->count + 2);

err:
	priv->delay = jiffies + msecs_to_jiffies(CR_FW_DELAY_MSEC);

	return ret;
}

static void cr0014114_recount_work(struct work_struct *work)
{
	int			ret;
	struct cr0014114	*priv = container_of(work,
						     struct cr0014114,
						     work.work);

	mutex_lock(&priv->lock);
	priv->do_recount = true;
	ret = cr0014114_sync(priv);
	mutex_unlock(&priv->lock);

	if (ret)
		dev_warn(priv->dev, "sync of LEDs failed %d\n", ret);

	schedule_delayed_work(&priv->work, CR_RECOUNT_DELAY);
}

static int cr0014114_set_sync(struct led_classdev *ldev,
			      enum led_brightness brightness)
{
	int			ret;
	struct cr0014114_led    *led = container_of(ldev,
						    struct cr0014114_led,
						    ldev);

	dev_dbg(led->priv->dev, "Set brightness to %d\n", brightness);

	mutex_lock(&led->priv->lock);
	led->brightness = (u8)brightness;
	ret = cr0014114_sync(led->priv);
	mutex_unlock(&led->priv->lock);

	return ret;
}

static int cr0014114_probe_dt(struct cr0014114 *priv)
{
	size_t			i = 0;
	struct cr0014114_led	*led;
	struct fwnode_handle	*child;
	struct led_init_data	init_data = {};
	int			ret;

	device_for_each_child_node(priv->dev, child) {
		led = &priv->leds[i];

		led->priv			  = priv;
		led->ldev.max_brightness	  = CR_MAX_BRIGHTNESS;
		led->ldev.brightness_set_blocking = cr0014114_set_sync;

		init_data.fwnode = child;
		init_data.devicename = CR_DEV_NAME;
		init_data.default_label = ":";

		ret = devm_led_classdev_register_ext(priv->dev, &led->ldev,
						     &init_data);
		if (ret) {
			dev_err(priv->dev,
				"failed to register LED device, err %d", ret);
			fwnode_handle_put(child);
			return ret;
		}

		i++;
	}

	return 0;
}

static int cr0014114_probe(struct spi_device *spi)
{
	struct cr0014114	*priv;
	size_t			count;
	int			ret;

	count = device_get_child_node_count(&spi->dev);
	if (!count) {
		dev_err(&spi->dev, "LEDs are not defined in device tree!");
		return -ENODEV;
	}

	priv = devm_kzalloc(&spi->dev, struct_size(priv, leds, count),
			    GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	priv->buf = devm_kzalloc(&spi->dev, count + 2, GFP_KERNEL);
	if (!priv->buf)
		return -ENOMEM;

	mutex_init(&priv->lock);
	INIT_DELAYED_WORK(&priv->work, cr0014114_recount_work);
	priv->count	= count;
	priv->dev	= &spi->dev;
	priv->spi	= spi;
	priv->delay	= jiffies -
			  msecs_to_jiffies(CR_FW_DELAY_MSEC);

	priv->do_recount = true;
	ret = cr0014114_sync(priv);
	if (ret) {
		dev_err(priv->dev, "first recount failed %d\n", ret);
		return ret;
	}

	priv->do_recount = true;
	ret = cr0014114_sync(priv);
	if (ret) {
		dev_err(priv->dev, "second recount failed %d\n", ret);
		return ret;
	}

	ret = cr0014114_probe_dt(priv);
	if (ret)
		return ret;

	/* setup recount work to workaround buggy firmware */
	schedule_delayed_work(&priv->work, CR_RECOUNT_DELAY);

	spi_set_drvdata(spi, priv);

	return 0;
}

static void cr0014114_remove(struct spi_device *spi)
{
	struct cr0014114 *priv = spi_get_drvdata(spi);

	cancel_delayed_work_sync(&priv->work);
	mutex_destroy(&priv->lock);
}

static const struct of_device_id cr0014114_dt_ids[] = {
	{ .compatible = "crane,cr0014114", },
	{},
};

MODULE_DEVICE_TABLE(of, cr0014114_dt_ids);

static struct spi_driver cr0014114_driver = {
	.probe		= cr0014114_probe,
	.remove		= cr0014114_remove,
	.driver = {
		.name		= KBUILD_MODNAME,
		.of_match_table	= cr0014114_dt_ids,
	},
};

module_spi_driver(cr0014114_driver);

MODULE_AUTHOR("Oleh Kravchenko <oleg@kaa.org.ua>");
MODULE_DESCRIPTION("cr0014114 LED driver");
MODULE_LICENSE("GPL v2");
MODULE_ALIAS("spi:cr0014114");
