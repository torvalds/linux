/*
 * LCD panel driver for TPO TD043MTEA1
 *
 * Author: Gražvydas Ignotas <notasas@gmail.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include <linux/module.h>
#include <linux/delay.h>
#include <linux/spi/spi.h>
#include <linux/regulator/consumer.h>
#include <linux/gpio.h>
#include <linux/err.h>
#include <linux/slab.h>

#include <video/omapdss.h>

#define TPO_R02_MODE(x)		((x) & 7)
#define TPO_R02_MODE_800x480	7
#define TPO_R02_NCLK_RISING	BIT(3)
#define TPO_R02_HSYNC_HIGH	BIT(4)
#define TPO_R02_VSYNC_HIGH	BIT(5)

#define TPO_R03_NSTANDBY	BIT(0)
#define TPO_R03_EN_CP_CLK	BIT(1)
#define TPO_R03_EN_VGL_PUMP	BIT(2)
#define TPO_R03_EN_PWM		BIT(3)
#define TPO_R03_DRIVING_CAP_100	BIT(4)
#define TPO_R03_EN_PRE_CHARGE	BIT(6)
#define TPO_R03_SOFTWARE_CTL	BIT(7)

#define TPO_R04_NFLIP_H		BIT(0)
#define TPO_R04_NFLIP_V		BIT(1)
#define TPO_R04_CP_CLK_FREQ_1H	BIT(2)
#define TPO_R04_VGL_FREQ_1H	BIT(4)

#define TPO_R03_VAL_NORMAL (TPO_R03_NSTANDBY | TPO_R03_EN_CP_CLK | \
			TPO_R03_EN_VGL_PUMP |  TPO_R03_EN_PWM | \
			TPO_R03_DRIVING_CAP_100 | TPO_R03_EN_PRE_CHARGE | \
			TPO_R03_SOFTWARE_CTL)

#define TPO_R03_VAL_STANDBY (TPO_R03_DRIVING_CAP_100 | \
			TPO_R03_EN_PRE_CHARGE | TPO_R03_SOFTWARE_CTL)

static const u16 tpo_td043_def_gamma[12] = {
	106, 200, 289, 375, 460, 543, 625, 705, 785, 864, 942, 1020
};

struct tpo_td043_device {
	struct spi_device *spi;
	struct regulator *vcc_reg;
	u16 gamma[12];
	u32 mode;
	u32 hmirror:1;
	u32 vmirror:1;
};

static int tpo_td043_write(struct spi_device *spi, u8 addr, u8 data)
{
	struct spi_message	m;
	struct spi_transfer	xfer;
	u16			w;
	int			r;

	spi_message_init(&m);

	memset(&xfer, 0, sizeof(xfer));

	w = ((u16)addr << 10) | (1 << 8) | data;
	xfer.tx_buf = &w;
	xfer.bits_per_word = 16;
	xfer.len = 2;
	spi_message_add_tail(&xfer, &m);

	r = spi_sync(spi, &m);
	if (r < 0)
		dev_warn(&spi->dev, "failed to write to LCD reg (%d)\n", r);
	return r;
}

static void tpo_td043_write_gamma(struct spi_device *spi, u16 gamma[12])
{
	u8 i, val;

	/* gamma bits [9:8] */
	for (val = i = 0; i < 4; i++)
		val |= (gamma[i] & 0x300) >> ((i + 1) * 2);
	tpo_td043_write(spi, 0x11, val);

	for (val = i = 0; i < 4; i++)
		val |= (gamma[i+4] & 0x300) >> ((i + 1) * 2);
	tpo_td043_write(spi, 0x12, val);

	for (val = i = 0; i < 4; i++)
		val |= (gamma[i+8] & 0x300) >> ((i + 1) * 2);
	tpo_td043_write(spi, 0x13, val);

	/* gamma bits [7:0] */
	for (val = i = 0; i < 12; i++)
		tpo_td043_write(spi, 0x14 + i, gamma[i] & 0xff);
}

static int tpo_td043_write_mirror(struct spi_device *spi, bool h, bool v)
{
	u8 reg4 = TPO_R04_NFLIP_H | TPO_R04_NFLIP_V | \
		TPO_R04_CP_CLK_FREQ_1H | TPO_R04_VGL_FREQ_1H;
	if (h)
		reg4 &= ~TPO_R04_NFLIP_H;
	if (v)
		reg4 &= ~TPO_R04_NFLIP_V;

	return tpo_td043_write(spi, 4, reg4);
}

static int tpo_td043_set_hmirror(struct omap_dss_device *dssdev, bool enable)
{
	struct tpo_td043_device *tpo_td043 = dev_get_drvdata(&dssdev->dev);

	tpo_td043->hmirror = enable;
	return tpo_td043_write_mirror(tpo_td043->spi, tpo_td043->hmirror,
			tpo_td043->vmirror);
}

static bool tpo_td043_get_hmirror(struct omap_dss_device *dssdev)
{
	struct tpo_td043_device *tpo_td043 = dev_get_drvdata(&dssdev->dev);

	return tpo_td043->hmirror;
}

static ssize_t tpo_td043_vmirror_show(struct device *dev,
	struct device_attribute *attr, char *buf)
{
	struct tpo_td043_device *tpo_td043 = dev_get_drvdata(dev);

	return snprintf(buf, PAGE_SIZE, "%d\n", tpo_td043->vmirror);
}

static ssize_t tpo_td043_vmirror_store(struct device *dev,
	struct device_attribute *attr, const char *buf, size_t count)
{
	struct tpo_td043_device *tpo_td043 = dev_get_drvdata(dev);
	int val;
	int ret;

	ret = kstrtoint(buf, 0, &val);
	if (ret < 0)
		return ret;

	val = !!val;

	ret = tpo_td043_write_mirror(tpo_td043->spi, tpo_td043->hmirror, val);
	if (ret < 0)
		return ret;

	tpo_td043->vmirror = val;

	return count;
}

static ssize_t tpo_td043_mode_show(struct device *dev,
	struct device_attribute *attr, char *buf)
{
	struct tpo_td043_device *tpo_td043 = dev_get_drvdata(dev);

	return snprintf(buf, PAGE_SIZE, "%d\n", tpo_td043->mode);
}

static ssize_t tpo_td043_mode_store(struct device *dev,
	struct device_attribute *attr, const char *buf, size_t count)
{
	struct tpo_td043_device *tpo_td043 = dev_get_drvdata(dev);
	long val;
	int ret;

	ret = kstrtol(buf, 0, &val);
	if (ret != 0 || val & ~7)
		return -EINVAL;

	tpo_td043->mode = val;

	val |= TPO_R02_NCLK_RISING;
	tpo_td043_write(tpo_td043->spi, 2, val);

	return count;
}

static ssize_t tpo_td043_gamma_show(struct device *dev,
	struct device_attribute *attr, char *buf)
{
	struct tpo_td043_device *tpo_td043 = dev_get_drvdata(dev);
	ssize_t len = 0;
	int ret;
	int i;

	for (i = 0; i < ARRAY_SIZE(tpo_td043->gamma); i++) {
		ret = snprintf(buf + len, PAGE_SIZE - len, "%u ",
				tpo_td043->gamma[i]);
		if (ret < 0)
			return ret;
		len += ret;
	}
	buf[len - 1] = '\n';

	return len;
}

static ssize_t tpo_td043_gamma_store(struct device *dev,
	struct device_attribute *attr, const char *buf, size_t count)
{
	struct tpo_td043_device *tpo_td043 = dev_get_drvdata(dev);
	unsigned int g[12];
	int ret;
	int i;

	ret = sscanf(buf, "%u %u %u %u %u %u %u %u %u %u %u %u",
			&g[0], &g[1], &g[2], &g[3], &g[4], &g[5],
			&g[6], &g[7], &g[8], &g[9], &g[10], &g[11]);

	if (ret != 12)
		return -EINVAL;

	for (i = 0; i < 12; i++)
		tpo_td043->gamma[i] = g[i];

	tpo_td043_write_gamma(tpo_td043->spi, tpo_td043->gamma);

	return count;
}

static DEVICE_ATTR(vmirror, S_IRUGO | S_IWUSR,
		tpo_td043_vmirror_show, tpo_td043_vmirror_store);
static DEVICE_ATTR(mode, S_IRUGO | S_IWUSR,
		tpo_td043_mode_show, tpo_td043_mode_store);
static DEVICE_ATTR(gamma, S_IRUGO | S_IWUSR,
		tpo_td043_gamma_show, tpo_td043_gamma_store);

static struct attribute *tpo_td043_attrs[] = {
	&dev_attr_vmirror.attr,
	&dev_attr_mode.attr,
	&dev_attr_gamma.attr,
	NULL,
};

static struct attribute_group tpo_td043_attr_group = {
	.attrs = tpo_td043_attrs,
};

static const struct omap_video_timings tpo_td043_timings = {
	.x_res		= 800,
	.y_res		= 480,

	.pixel_clock	= 36000,

	.hsw		= 1,
	.hfp		= 68,
	.hbp		= 214,

	.vsw		= 1,
	.vfp		= 39,
	.vbp		= 34,
};

static int tpo_td043_power_on(struct omap_dss_device *dssdev)
{
	struct tpo_td043_device *tpo_td043 = dev_get_drvdata(&dssdev->dev);
	int nreset_gpio = dssdev->reset_gpio;
	int r;

	if (dssdev->state == OMAP_DSS_DISPLAY_ACTIVE)
		return 0;

	r = omapdss_dpi_display_enable(dssdev);
	if (r)
		goto err0;

	if (dssdev->platform_enable) {
		r = dssdev->platform_enable(dssdev);
		if (r)
			goto err1;
	}

	regulator_enable(tpo_td043->vcc_reg);

	/* wait for power up */
	msleep(160);

	if (gpio_is_valid(nreset_gpio))
		gpio_set_value(nreset_gpio, 1);

	tpo_td043_write(tpo_td043->spi, 2,
			TPO_R02_MODE(tpo_td043->mode) | TPO_R02_NCLK_RISING);
	tpo_td043_write(tpo_td043->spi, 3, TPO_R03_VAL_NORMAL);
	tpo_td043_write(tpo_td043->spi, 0x20, 0xf0);
	tpo_td043_write(tpo_td043->spi, 0x21, 0xf0);
	tpo_td043_write_mirror(tpo_td043->spi, tpo_td043->hmirror,
			tpo_td043->vmirror);
	tpo_td043_write_gamma(tpo_td043->spi, tpo_td043->gamma);

	return 0;
err1:
	omapdss_dpi_display_disable(dssdev);
err0:
	return r;
}

static void tpo_td043_power_off(struct omap_dss_device *dssdev)
{
	struct tpo_td043_device *tpo_td043 = dev_get_drvdata(&dssdev->dev);
	int nreset_gpio = dssdev->reset_gpio;

	if (dssdev->state != OMAP_DSS_DISPLAY_ACTIVE)
		return;

	tpo_td043_write(tpo_td043->spi, 3,
			TPO_R03_VAL_STANDBY | TPO_R03_EN_PWM);

	if (gpio_is_valid(nreset_gpio))
		gpio_set_value(nreset_gpio, 0);

	/* wait for at least 2 vsyncs before cutting off power */
	msleep(50);

	tpo_td043_write(tpo_td043->spi, 3, TPO_R03_VAL_STANDBY);

	regulator_disable(tpo_td043->vcc_reg);

	if (dssdev->platform_disable)
		dssdev->platform_disable(dssdev);

	omapdss_dpi_display_disable(dssdev);
}

static int tpo_td043_enable(struct omap_dss_device *dssdev)
{
	int ret;

	dev_dbg(&dssdev->dev, "enable\n");

	ret = tpo_td043_power_on(dssdev);
	if (ret)
		return ret;

	dssdev->state = OMAP_DSS_DISPLAY_ACTIVE;

	return 0;
}

static void tpo_td043_disable(struct omap_dss_device *dssdev)
{
	dev_dbg(&dssdev->dev, "disable\n");

	tpo_td043_power_off(dssdev);

	dssdev->state = OMAP_DSS_DISPLAY_DISABLED;
}

static int tpo_td043_suspend(struct omap_dss_device *dssdev)
{
	tpo_td043_power_off(dssdev);
	dssdev->state = OMAP_DSS_DISPLAY_SUSPENDED;
	return 0;
}

static int tpo_td043_resume(struct omap_dss_device *dssdev)
{
	int r = 0;

	r = tpo_td043_power_on(dssdev);
	if (r)
		return r;

	dssdev->state = OMAP_DSS_DISPLAY_ACTIVE;

	return 0;
}

static int tpo_td043_probe(struct omap_dss_device *dssdev)
{
	struct tpo_td043_device *tpo_td043 = dev_get_drvdata(&dssdev->dev);
	int nreset_gpio = dssdev->reset_gpio;
	int ret = 0;

	dev_dbg(&dssdev->dev, "probe\n");

	if (tpo_td043 == NULL) {
		dev_err(&dssdev->dev, "missing tpo_td043_device\n");
		return -ENODEV;
	}

	dssdev->panel.config = OMAP_DSS_LCD_TFT | OMAP_DSS_LCD_IHS |
				OMAP_DSS_LCD_IVS | OMAP_DSS_LCD_IPC;
	dssdev->panel.timings = tpo_td043_timings;
	dssdev->ctrl.pixel_size = 24;

	tpo_td043->mode = TPO_R02_MODE_800x480;
	memcpy(tpo_td043->gamma, tpo_td043_def_gamma, sizeof(tpo_td043->gamma));

	tpo_td043->vcc_reg = regulator_get(&dssdev->dev, "vcc");
	if (IS_ERR(tpo_td043->vcc_reg)) {
		dev_err(&dssdev->dev, "failed to get LCD VCC regulator\n");
		ret = PTR_ERR(tpo_td043->vcc_reg);
		goto fail_regulator;
	}

	if (gpio_is_valid(nreset_gpio)) {
		ret = gpio_request(nreset_gpio, "lcd reset");
		if (ret < 0) {
			dev_err(&dssdev->dev, "couldn't request reset GPIO\n");
			goto fail_gpio_req;
		}

		ret = gpio_direction_output(nreset_gpio, 0);
		if (ret < 0) {
			dev_err(&dssdev->dev, "couldn't set GPIO direction\n");
			goto fail_gpio_direction;
		}
	}

	ret = sysfs_create_group(&dssdev->dev.kobj, &tpo_td043_attr_group);
	if (ret)
		dev_warn(&dssdev->dev, "failed to create sysfs files\n");

	return 0;

fail_gpio_direction:
	gpio_free(nreset_gpio);
fail_gpio_req:
	regulator_put(tpo_td043->vcc_reg);
fail_regulator:
	kfree(tpo_td043);
	return ret;
}

static void tpo_td043_remove(struct omap_dss_device *dssdev)
{
	struct tpo_td043_device *tpo_td043 = dev_get_drvdata(&dssdev->dev);
	int nreset_gpio = dssdev->reset_gpio;

	dev_dbg(&dssdev->dev, "remove\n");

	sysfs_remove_group(&dssdev->dev.kobj, &tpo_td043_attr_group);
	regulator_put(tpo_td043->vcc_reg);
	if (gpio_is_valid(nreset_gpio))
		gpio_free(nreset_gpio);
}

static struct omap_dss_driver tpo_td043_driver = {
	.probe		= tpo_td043_probe,
	.remove		= tpo_td043_remove,

	.enable		= tpo_td043_enable,
	.disable	= tpo_td043_disable,
	.suspend	= tpo_td043_suspend,
	.resume		= tpo_td043_resume,
	.set_mirror	= tpo_td043_set_hmirror,
	.get_mirror	= tpo_td043_get_hmirror,

	.driver         = {
		.name	= "tpo_td043mtea1_panel",
		.owner  = THIS_MODULE,
	},
};

static int tpo_td043_spi_probe(struct spi_device *spi)
{
	struct omap_dss_device *dssdev = spi->dev.platform_data;
	struct tpo_td043_device *tpo_td043;
	int ret;

	if (dssdev == NULL) {
		dev_err(&spi->dev, "missing dssdev\n");
		return -ENODEV;
	}

	spi->bits_per_word = 16;
	spi->mode = SPI_MODE_0;

	ret = spi_setup(spi);
	if (ret < 0) {
		dev_err(&spi->dev, "spi_setup failed: %d\n", ret);
		return ret;
	}

	tpo_td043 = kzalloc(sizeof(*tpo_td043), GFP_KERNEL);
	if (tpo_td043 == NULL)
		return -ENOMEM;

	tpo_td043->spi = spi;
	dev_set_drvdata(&spi->dev, tpo_td043);
	dev_set_drvdata(&dssdev->dev, tpo_td043);

	omap_dss_register_driver(&tpo_td043_driver);

	return 0;
}

static int __devexit tpo_td043_spi_remove(struct spi_device *spi)
{
	struct tpo_td043_device *tpo_td043 = dev_get_drvdata(&spi->dev);

	omap_dss_unregister_driver(&tpo_td043_driver);
	kfree(tpo_td043);

	return 0;
}

static struct spi_driver tpo_td043_spi_driver = {
	.driver = {
		.name	= "tpo_td043mtea1_panel_spi",
		.bus	= &spi_bus_type,
		.owner	= THIS_MODULE,
	},
	.probe	= tpo_td043_spi_probe,
	.remove	= __devexit_p(tpo_td043_spi_remove),
};

static int __init tpo_td043_init(void)
{
	return spi_register_driver(&tpo_td043_spi_driver);
}

static void __exit tpo_td043_exit(void)
{
	spi_unregister_driver(&tpo_td043_spi_driver);
}

module_init(tpo_td043_init);
module_exit(tpo_td043_exit);

MODULE_AUTHOR("Gražvydas Ignotas <notasas@gmail.com>");
MODULE_DESCRIPTION("TPO TD043MTEA1 LCD Driver");
MODULE_LICENSE("GPL");
