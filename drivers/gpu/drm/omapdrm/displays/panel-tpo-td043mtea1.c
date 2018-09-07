/*
 * TPO TD043MTEA1 Panel driver
 *
 * Author: Gražvydas Ignotas <notasas@gmail.com>
 * Converted to new DSS device model: Tomi Valkeinen <tomi.valkeinen@ti.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include <linux/delay.h>
#include <linux/err.h>
#include <linux/gpio/consumer.h>
#include <linux/module.h>
#include <linux/regulator/consumer.h>
#include <linux/slab.h>
#include <linux/spi/spi.h>

#include "../dss/omapdss.h"

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
	105, 315, 381, 431, 490, 537, 579, 686, 780, 837, 880, 1023
};

struct panel_drv_data {
	struct omap_dss_device	dssdev;

	struct videomode vm;

	struct spi_device *spi;
	struct regulator *vcc_reg;
	struct gpio_desc *reset_gpio;
	u16 gamma[12];
	u32 mode;
	u32 vmirror:1;
	u32 powered_on:1;
	u32 spi_suspended:1;
	u32 power_on_resume:1;
};

static const struct videomode tpo_td043_vm = {
	.hactive	= 800,
	.vactive	= 480,

	.pixelclock	= 36000000,

	.hsync_len	= 1,
	.hfront_porch	= 68,
	.hback_porch	= 214,

	.vsync_len	= 1,
	.vfront_porch	= 39,
	.vback_porch	= 34,

	.flags		= DISPLAY_FLAGS_HSYNC_LOW | DISPLAY_FLAGS_VSYNC_LOW,
};

#define to_panel_data(p) container_of(p, struct panel_drv_data, dssdev)

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
	u8 reg4 = TPO_R04_NFLIP_H | TPO_R04_NFLIP_V |
		TPO_R04_CP_CLK_FREQ_1H | TPO_R04_VGL_FREQ_1H;
	if (h)
		reg4 &= ~TPO_R04_NFLIP_H;
	if (v)
		reg4 &= ~TPO_R04_NFLIP_V;

	return tpo_td043_write(spi, 4, reg4);
}

static ssize_t tpo_td043_vmirror_show(struct device *dev,
	struct device_attribute *attr, char *buf)
{
	struct panel_drv_data *ddata = dev_get_drvdata(dev);

	return snprintf(buf, PAGE_SIZE, "%d\n", ddata->vmirror);
}

static ssize_t tpo_td043_vmirror_store(struct device *dev,
	struct device_attribute *attr, const char *buf, size_t count)
{
	struct panel_drv_data *ddata = dev_get_drvdata(dev);
	int val;
	int ret;

	ret = kstrtoint(buf, 0, &val);
	if (ret < 0)
		return ret;

	val = !!val;

	ret = tpo_td043_write_mirror(ddata->spi, false, val);
	if (ret < 0)
		return ret;

	ddata->vmirror = val;

	return count;
}

static ssize_t tpo_td043_mode_show(struct device *dev,
	struct device_attribute *attr, char *buf)
{
	struct panel_drv_data *ddata = dev_get_drvdata(dev);

	return snprintf(buf, PAGE_SIZE, "%d\n", ddata->mode);
}

static ssize_t tpo_td043_mode_store(struct device *dev,
	struct device_attribute *attr, const char *buf, size_t count)
{
	struct panel_drv_data *ddata = dev_get_drvdata(dev);
	long val;
	int ret;

	ret = kstrtol(buf, 0, &val);
	if (ret != 0 || val & ~7)
		return -EINVAL;

	ddata->mode = val;

	val |= TPO_R02_NCLK_RISING;
	tpo_td043_write(ddata->spi, 2, val);

	return count;
}

static ssize_t tpo_td043_gamma_show(struct device *dev,
	struct device_attribute *attr, char *buf)
{
	struct panel_drv_data *ddata = dev_get_drvdata(dev);
	ssize_t len = 0;
	int ret;
	int i;

	for (i = 0; i < ARRAY_SIZE(ddata->gamma); i++) {
		ret = snprintf(buf + len, PAGE_SIZE - len, "%u ",
				ddata->gamma[i]);
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
	struct panel_drv_data *ddata = dev_get_drvdata(dev);
	unsigned int g[12];
	int ret;
	int i;

	ret = sscanf(buf, "%u %u %u %u %u %u %u %u %u %u %u %u",
			&g[0], &g[1], &g[2], &g[3], &g[4], &g[5],
			&g[6], &g[7], &g[8], &g[9], &g[10], &g[11]);

	if (ret != 12)
		return -EINVAL;

	for (i = 0; i < 12; i++)
		ddata->gamma[i] = g[i];

	tpo_td043_write_gamma(ddata->spi, ddata->gamma);

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

static const struct attribute_group tpo_td043_attr_group = {
	.attrs = tpo_td043_attrs,
};

static int tpo_td043_power_on(struct panel_drv_data *ddata)
{
	int r;

	if (ddata->powered_on)
		return 0;

	r = regulator_enable(ddata->vcc_reg);
	if (r != 0)
		return r;

	/* wait for panel to stabilize */
	msleep(160);

	gpiod_set_value(ddata->reset_gpio, 0);

	tpo_td043_write(ddata->spi, 2,
			TPO_R02_MODE(ddata->mode) | TPO_R02_NCLK_RISING);
	tpo_td043_write(ddata->spi, 3, TPO_R03_VAL_NORMAL);
	tpo_td043_write(ddata->spi, 0x20, 0xf0);
	tpo_td043_write(ddata->spi, 0x21, 0xf0);
	tpo_td043_write_mirror(ddata->spi, false, ddata->vmirror);
	tpo_td043_write_gamma(ddata->spi, ddata->gamma);

	ddata->powered_on = 1;
	return 0;
}

static void tpo_td043_power_off(struct panel_drv_data *ddata)
{
	if (!ddata->powered_on)
		return;

	tpo_td043_write(ddata->spi, 3,
			TPO_R03_VAL_STANDBY | TPO_R03_EN_PWM);

	gpiod_set_value(ddata->reset_gpio, 1);

	/* wait for at least 2 vsyncs before cutting off power */
	msleep(50);

	tpo_td043_write(ddata->spi, 3, TPO_R03_VAL_STANDBY);

	regulator_disable(ddata->vcc_reg);

	ddata->powered_on = 0;
}

static int tpo_td043_connect(struct omap_dss_device *src,
			     struct omap_dss_device *dst)
{
	return 0;
}

static void tpo_td043_disconnect(struct omap_dss_device *src,
				 struct omap_dss_device *dst)
{
}

static int tpo_td043_enable(struct omap_dss_device *dssdev)
{
	struct panel_drv_data *ddata = to_panel_data(dssdev);
	struct omap_dss_device *src = dssdev->src;
	int r;

	if (!omapdss_device_is_connected(dssdev))
		return -ENODEV;

	if (omapdss_device_is_enabled(dssdev))
		return 0;

	r = src->ops->enable(src);
	if (r)
		return r;

	/*
	 * If we are resuming from system suspend, SPI clocks might not be
	 * enabled yet, so we'll program the LCD from SPI PM resume callback.
	 */
	if (!ddata->spi_suspended) {
		r = tpo_td043_power_on(ddata);
		if (r) {
			src->ops->disable(src);
			return r;
		}
	}

	dssdev->state = OMAP_DSS_DISPLAY_ACTIVE;

	return 0;
}

static void tpo_td043_disable(struct omap_dss_device *dssdev)
{
	struct panel_drv_data *ddata = to_panel_data(dssdev);
	struct omap_dss_device *src = dssdev->src;

	if (!omapdss_device_is_enabled(dssdev))
		return;

	src->ops->disable(src);

	if (!ddata->spi_suspended)
		tpo_td043_power_off(ddata);

	dssdev->state = OMAP_DSS_DISPLAY_DISABLED;
}

static void tpo_td043_get_timings(struct omap_dss_device *dssdev,
				  struct videomode *vm)
{
	struct panel_drv_data *ddata = to_panel_data(dssdev);

	*vm = ddata->vm;
}

static const struct omap_dss_device_ops tpo_td043_ops = {
	.connect	= tpo_td043_connect,
	.disconnect	= tpo_td043_disconnect,

	.enable		= tpo_td043_enable,
	.disable	= tpo_td043_disable,

	.get_timings	= tpo_td043_get_timings,
};

static int tpo_td043_probe(struct spi_device *spi)
{
	struct panel_drv_data *ddata;
	struct omap_dss_device *dssdev;
	struct gpio_desc *gpio;
	int r;

	dev_dbg(&spi->dev, "%s\n", __func__);

	spi->bits_per_word = 16;
	spi->mode = SPI_MODE_0;

	r = spi_setup(spi);
	if (r < 0) {
		dev_err(&spi->dev, "spi_setup failed: %d\n", r);
		return r;
	}

	ddata = devm_kzalloc(&spi->dev, sizeof(*ddata), GFP_KERNEL);
	if (ddata == NULL)
		return -ENOMEM;

	dev_set_drvdata(&spi->dev, ddata);

	ddata->spi = spi;

	ddata->mode = TPO_R02_MODE_800x480;
	memcpy(ddata->gamma, tpo_td043_def_gamma, sizeof(ddata->gamma));

	ddata->vcc_reg = devm_regulator_get(&spi->dev, "vcc");
	if (IS_ERR(ddata->vcc_reg)) {
		dev_err(&spi->dev, "failed to get LCD VCC regulator\n");
		return PTR_ERR(ddata->vcc_reg);
	}

	gpio = devm_gpiod_get(&spi->dev, "reset", GPIOD_OUT_HIGH);
	if (IS_ERR(gpio)) {
		dev_err(&spi->dev, "failed to get reset gpio\n");
		return PTR_ERR(gpio);
	}

	ddata->reset_gpio = gpio;

	r = sysfs_create_group(&spi->dev.kobj, &tpo_td043_attr_group);
	if (r) {
		dev_err(&spi->dev, "failed to create sysfs files\n");
		return r;
	}

	ddata->vm = tpo_td043_vm;

	dssdev = &ddata->dssdev;
	dssdev->dev = &spi->dev;
	dssdev->ops = &tpo_td043_ops;
	dssdev->type = OMAP_DISPLAY_TYPE_DPI;
	dssdev->owner = THIS_MODULE;
	dssdev->of_ports = BIT(0);

	/*
	 * Note: According to the panel documentation:
	 * SYNC needs to be driven on the FALLING edge
	 */
	dssdev->bus_flags = DRM_BUS_FLAG_DE_HIGH | DRM_BUS_FLAG_SYNC_POSEDGE
			  | DRM_BUS_FLAG_PIXDATA_NEGEDGE;

	omapdss_display_init(dssdev);
	omapdss_device_register(dssdev);

	return 0;
}

static int tpo_td043_remove(struct spi_device *spi)
{
	struct panel_drv_data *ddata = dev_get_drvdata(&spi->dev);
	struct omap_dss_device *dssdev = &ddata->dssdev;

	dev_dbg(&ddata->spi->dev, "%s\n", __func__);

	omapdss_device_unregister(dssdev);

	tpo_td043_disable(dssdev);

	sysfs_remove_group(&spi->dev.kobj, &tpo_td043_attr_group);

	return 0;
}

#ifdef CONFIG_PM_SLEEP
static int tpo_td043_spi_suspend(struct device *dev)
{
	struct panel_drv_data *ddata = dev_get_drvdata(dev);

	dev_dbg(dev, "tpo_td043_spi_suspend, tpo %p\n", ddata);

	ddata->power_on_resume = ddata->powered_on;
	tpo_td043_power_off(ddata);
	ddata->spi_suspended = 1;

	return 0;
}

static int tpo_td043_spi_resume(struct device *dev)
{
	struct panel_drv_data *ddata = dev_get_drvdata(dev);
	int ret;

	dev_dbg(dev, "tpo_td043_spi_resume\n");

	if (ddata->power_on_resume) {
		ret = tpo_td043_power_on(ddata);
		if (ret)
			return ret;
	}
	ddata->spi_suspended = 0;

	return 0;
}
#endif

static SIMPLE_DEV_PM_OPS(tpo_td043_spi_pm,
	tpo_td043_spi_suspend, tpo_td043_spi_resume);

static const struct of_device_id tpo_td043_of_match[] = {
	{ .compatible = "omapdss,tpo,td043mtea1", },
	{},
};

MODULE_DEVICE_TABLE(of, tpo_td043_of_match);

static struct spi_driver tpo_td043_spi_driver = {
	.driver = {
		.name	= "panel-tpo-td043mtea1",
		.pm	= &tpo_td043_spi_pm,
		.of_match_table = tpo_td043_of_match,
		.suppress_bind_attrs = true,
	},
	.probe	= tpo_td043_probe,
	.remove	= tpo_td043_remove,
};

module_spi_driver(tpo_td043_spi_driver);

MODULE_ALIAS("spi:tpo,td043mtea1");
MODULE_AUTHOR("Gražvydas Ignotas <notasas@gmail.com>");
MODULE_DESCRIPTION("TPO TD043MTEA1 LCD Driver");
MODULE_LICENSE("GPL");
