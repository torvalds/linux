// SPDX-License-Identifier: GPL-2.0+
/*
 * Toppoly TD043MTEA1 Panel Driver
 *
 * Copyright (C) 2019 Texas Instruments Incorporated
 *
 * Based on the omapdrm-specific panel-tpo-td043mtea1 driver
 *
 * Author: Gražvydas Ignotas <notasas@gmail.com>
 */

#include <linux/delay.h>
#include <linux/module.h>
#include <linux/regulator/consumer.h>
#include <linux/spi/spi.h>

#include <drm/drm_connector.h>
#include <drm/drm_modes.h>
#include <drm/drm_panel.h>

#define TPO_R02_MODE(x)			((x) & 7)
#define TPO_R02_MODE_800x480		7
#define TPO_R02_NCLK_RISING		BIT(3)
#define TPO_R02_HSYNC_HIGH		BIT(4)
#define TPO_R02_VSYNC_HIGH		BIT(5)

#define TPO_R03_NSTANDBY		BIT(0)
#define TPO_R03_EN_CP_CLK		BIT(1)
#define TPO_R03_EN_VGL_PUMP		BIT(2)
#define TPO_R03_EN_PWM			BIT(3)
#define TPO_R03_DRIVING_CAP_100		BIT(4)
#define TPO_R03_EN_PRE_CHARGE		BIT(6)
#define TPO_R03_SOFTWARE_CTL		BIT(7)

#define TPO_R04_NFLIP_H			BIT(0)
#define TPO_R04_NFLIP_V			BIT(1)
#define TPO_R04_CP_CLK_FREQ_1H		BIT(2)
#define TPO_R04_VGL_FREQ_1H		BIT(4)

#define TPO_R03_VAL_NORMAL \
	(TPO_R03_NSTANDBY | TPO_R03_EN_CP_CLK | TPO_R03_EN_VGL_PUMP | \
	 TPO_R03_EN_PWM | TPO_R03_DRIVING_CAP_100 | TPO_R03_EN_PRE_CHARGE | \
	 TPO_R03_SOFTWARE_CTL)

#define TPO_R03_VAL_STANDBY \
	(TPO_R03_DRIVING_CAP_100 | TPO_R03_EN_PRE_CHARGE | \
	 TPO_R03_SOFTWARE_CTL)

static const u16 td043mtea1_def_gamma[12] = {
	105, 315, 381, 431, 490, 537, 579, 686, 780, 837, 880, 1023
};

struct td043mtea1_panel {
	struct drm_panel panel;

	struct spi_device *spi;
	struct regulator *vcc_reg;
	struct gpio_desc *reset_gpio;

	unsigned int mode;
	u16 gamma[12];
	bool vmirror;
	bool powered_on;
	bool spi_suspended;
	bool power_on_resume;
};

#define to_td043mtea1_device(p) container_of(p, struct td043mtea1_panel, panel)

/* -----------------------------------------------------------------------------
 * Hardware Access
 */

static int td043mtea1_write(struct td043mtea1_panel *lcd, u8 addr, u8 value)
{
	struct spi_message msg;
	struct spi_transfer xfer;
	u16 data;
	int ret;

	spi_message_init(&msg);

	memset(&xfer, 0, sizeof(xfer));

	data = ((u16)addr << 10) | (1 << 8) | value;
	xfer.tx_buf = &data;
	xfer.bits_per_word = 16;
	xfer.len = 2;
	spi_message_add_tail(&xfer, &msg);

	ret = spi_sync(lcd->spi, &msg);
	if (ret < 0)
		dev_warn(&lcd->spi->dev, "failed to write to LCD reg (%d)\n",
			 ret);

	return ret;
}

static void td043mtea1_write_gamma(struct td043mtea1_panel *lcd)
{
	const u16 *gamma = lcd->gamma;
	unsigned int i;
	u8 val;

	/* gamma bits [9:8] */
	for (val = i = 0; i < 4; i++)
		val |= (gamma[i] & 0x300) >> ((i + 1) * 2);
	td043mtea1_write(lcd, 0x11, val);

	for (val = i = 0; i < 4; i++)
		val |= (gamma[i + 4] & 0x300) >> ((i + 1) * 2);
	td043mtea1_write(lcd, 0x12, val);

	for (val = i = 0; i < 4; i++)
		val |= (gamma[i + 8] & 0x300) >> ((i + 1) * 2);
	td043mtea1_write(lcd, 0x13, val);

	/* gamma bits [7:0] */
	for (i = 0; i < 12; i++)
		td043mtea1_write(lcd, 0x14 + i, gamma[i] & 0xff);
}

static int td043mtea1_write_mirror(struct td043mtea1_panel *lcd)
{
	u8 reg4 = TPO_R04_NFLIP_H | TPO_R04_NFLIP_V |
		TPO_R04_CP_CLK_FREQ_1H | TPO_R04_VGL_FREQ_1H;
	if (lcd->vmirror)
		reg4 &= ~TPO_R04_NFLIP_V;

	return td043mtea1_write(lcd, 4, reg4);
}

static int td043mtea1_power_on(struct td043mtea1_panel *lcd)
{
	int ret;

	if (lcd->powered_on)
		return 0;

	ret = regulator_enable(lcd->vcc_reg);
	if (ret < 0)
		return ret;

	/* Wait for the panel to stabilize. */
	msleep(160);

	gpiod_set_value(lcd->reset_gpio, 0);

	td043mtea1_write(lcd, 2, TPO_R02_MODE(lcd->mode) | TPO_R02_NCLK_RISING);
	td043mtea1_write(lcd, 3, TPO_R03_VAL_NORMAL);
	td043mtea1_write(lcd, 0x20, 0xf0);
	td043mtea1_write(lcd, 0x21, 0xf0);
	td043mtea1_write_mirror(lcd);
	td043mtea1_write_gamma(lcd);

	lcd->powered_on = true;

	return 0;
}

static void td043mtea1_power_off(struct td043mtea1_panel *lcd)
{
	if (!lcd->powered_on)
		return;

	td043mtea1_write(lcd, 3, TPO_R03_VAL_STANDBY | TPO_R03_EN_PWM);

	gpiod_set_value(lcd->reset_gpio, 1);

	/* wait for at least 2 vsyncs before cutting off power */
	msleep(50);

	td043mtea1_write(lcd, 3, TPO_R03_VAL_STANDBY);

	regulator_disable(lcd->vcc_reg);

	lcd->powered_on = false;
}

/* -----------------------------------------------------------------------------
 * sysfs
 */

static ssize_t vmirror_show(struct device *dev, struct device_attribute *attr,
			    char *buf)
{
	struct td043mtea1_panel *lcd = dev_get_drvdata(dev);

	return sysfs_emit(buf, "%d\n", lcd->vmirror);
}

static ssize_t vmirror_store(struct device *dev, struct device_attribute *attr,
			     const char *buf, size_t count)
{
	struct td043mtea1_panel *lcd = dev_get_drvdata(dev);
	int val;
	int ret;

	ret = kstrtoint(buf, 0, &val);
	if (ret < 0)
		return ret;

	lcd->vmirror = !!val;

	ret = td043mtea1_write_mirror(lcd);
	if (ret < 0)
		return ret;

	return count;
}

static ssize_t mode_show(struct device *dev, struct device_attribute *attr,
			 char *buf)
{
	struct td043mtea1_panel *lcd = dev_get_drvdata(dev);

	return sysfs_emit(buf, "%d\n", lcd->mode);
}

static ssize_t mode_store(struct device *dev, struct device_attribute *attr,
			  const char *buf, size_t count)
{
	struct td043mtea1_panel *lcd = dev_get_drvdata(dev);
	long val;
	int ret;

	ret = kstrtol(buf, 0, &val);
	if (ret != 0 || val & ~7)
		return -EINVAL;

	lcd->mode = val;

	val |= TPO_R02_NCLK_RISING;
	td043mtea1_write(lcd, 2, val);

	return count;
}

static ssize_t gamma_show(struct device *dev, struct device_attribute *attr,
			  char *buf)
{
	struct td043mtea1_panel *lcd = dev_get_drvdata(dev);
	ssize_t len = 0;
	unsigned int i;
	int ret;

	for (i = 0; i < ARRAY_SIZE(lcd->gamma); i++) {
		ret = snprintf(buf + len, PAGE_SIZE - len, "%u ",
			       lcd->gamma[i]);
		if (ret < 0)
			return ret;
		len += ret;
	}
	buf[len - 1] = '\n';

	return len;
}

static ssize_t gamma_store(struct device *dev, struct device_attribute *attr,
			   const char *buf, size_t count)
{
	struct td043mtea1_panel *lcd = dev_get_drvdata(dev);
	unsigned int g[12];
	unsigned int i;
	int ret;

	ret = sscanf(buf, "%u %u %u %u %u %u %u %u %u %u %u %u",
		     &g[0], &g[1], &g[2], &g[3], &g[4], &g[5],
		     &g[6], &g[7], &g[8], &g[9], &g[10], &g[11]);
	if (ret != 12)
		return -EINVAL;

	for (i = 0; i < 12; i++)
		lcd->gamma[i] = g[i];

	td043mtea1_write_gamma(lcd);

	return count;
}

static DEVICE_ATTR_RW(vmirror);
static DEVICE_ATTR_RW(mode);
static DEVICE_ATTR_RW(gamma);

static struct attribute *td043mtea1_attrs[] = {
	&dev_attr_vmirror.attr,
	&dev_attr_mode.attr,
	&dev_attr_gamma.attr,
	NULL,
};

static const struct attribute_group td043mtea1_attr_group = {
	.attrs = td043mtea1_attrs,
};

/* -----------------------------------------------------------------------------
 * Panel Operations
 */

static int td043mtea1_unprepare(struct drm_panel *panel)
{
	struct td043mtea1_panel *lcd = to_td043mtea1_device(panel);

	if (!lcd->spi_suspended)
		td043mtea1_power_off(lcd);

	return 0;
}

static int td043mtea1_prepare(struct drm_panel *panel)
{
	struct td043mtea1_panel *lcd = to_td043mtea1_device(panel);
	int ret;

	/*
	 * If we are resuming from system suspend, SPI might not be enabled
	 * yet, so we'll program the LCD from SPI PM resume callback.
	 */
	if (lcd->spi_suspended)
		return 0;

	ret = td043mtea1_power_on(lcd);
	if (ret) {
		dev_err(&lcd->spi->dev, "%s: power on failed (%d)\n",
			__func__, ret);
		return ret;
	}

	return 0;
}

static const struct drm_display_mode td043mtea1_mode = {
	.clock = 36000,
	.hdisplay = 800,
	.hsync_start = 800 + 68,
	.hsync_end = 800 + 68 + 1,
	.htotal = 800 + 68 + 1 + 214,
	.vdisplay = 480,
	.vsync_start = 480 + 39,
	.vsync_end = 480 + 39 + 1,
	.vtotal = 480 + 39 + 1 + 34,
	.type = DRM_MODE_TYPE_DRIVER | DRM_MODE_TYPE_PREFERRED,
	.flags = DRM_MODE_FLAG_NHSYNC | DRM_MODE_FLAG_NVSYNC,
	.width_mm = 94,
	.height_mm = 56,
};

static int td043mtea1_get_modes(struct drm_panel *panel,
				struct drm_connector *connector)
{
	struct drm_display_mode *mode;

	mode = drm_mode_duplicate(connector->dev, &td043mtea1_mode);
	if (!mode)
		return -ENOMEM;

	drm_mode_set_name(mode);
	drm_mode_probed_add(connector, mode);

	connector->display_info.width_mm = td043mtea1_mode.width_mm;
	connector->display_info.height_mm = td043mtea1_mode.height_mm;
	/*
	 * FIXME: According to the datasheet sync signals are sampled on the
	 * rising edge of the clock, but the code running on the OMAP3 Pandora
	 * indicates sampling on the falling edge. This should be tested on a
	 * real device.
	 */
	connector->display_info.bus_flags = DRM_BUS_FLAG_DE_HIGH
					  | DRM_BUS_FLAG_SYNC_SAMPLE_NEGEDGE
					  | DRM_BUS_FLAG_PIXDATA_SAMPLE_POSEDGE;

	return 1;
}

static const struct drm_panel_funcs td043mtea1_funcs = {
	.unprepare = td043mtea1_unprepare,
	.prepare = td043mtea1_prepare,
	.get_modes = td043mtea1_get_modes,
};

/* -----------------------------------------------------------------------------
 * Power Management, Probe and Remove
 */

static int __maybe_unused td043mtea1_suspend(struct device *dev)
{
	struct td043mtea1_panel *lcd = dev_get_drvdata(dev);

	if (lcd->powered_on) {
		td043mtea1_power_off(lcd);
		lcd->powered_on = true;
	}

	lcd->spi_suspended = true;

	return 0;
}

static int __maybe_unused td043mtea1_resume(struct device *dev)
{
	struct td043mtea1_panel *lcd = dev_get_drvdata(dev);
	int ret;

	lcd->spi_suspended = false;

	if (lcd->powered_on) {
		lcd->powered_on = false;
		ret = td043mtea1_power_on(lcd);
		if (ret)
			return ret;
	}

	return 0;
}

static SIMPLE_DEV_PM_OPS(td043mtea1_pm_ops, td043mtea1_suspend,
			 td043mtea1_resume);

static int td043mtea1_probe(struct spi_device *spi)
{
	struct td043mtea1_panel *lcd;
	int ret;

	lcd = devm_kzalloc(&spi->dev, sizeof(*lcd), GFP_KERNEL);
	if (lcd == NULL)
		return -ENOMEM;

	spi_set_drvdata(spi, lcd);
	lcd->spi = spi;
	lcd->mode = TPO_R02_MODE_800x480;
	memcpy(lcd->gamma, td043mtea1_def_gamma, sizeof(lcd->gamma));

	lcd->vcc_reg = devm_regulator_get(&spi->dev, "vcc");
	if (IS_ERR(lcd->vcc_reg))
		return dev_err_probe(&spi->dev, PTR_ERR(lcd->vcc_reg),
				     "failed to get VCC regulator\n");

	lcd->reset_gpio = devm_gpiod_get(&spi->dev, "reset", GPIOD_OUT_HIGH);
	if (IS_ERR(lcd->reset_gpio))
		return dev_err_probe(&spi->dev, PTR_ERR(lcd->reset_gpio),
				     "failed to get reset GPIO\n");

	spi->bits_per_word = 16;
	spi->mode = SPI_MODE_0;

	ret = spi_setup(spi);
	if (ret < 0) {
		dev_err(&spi->dev, "failed to setup SPI: %d\n", ret);
		return ret;
	}

	ret = sysfs_create_group(&spi->dev.kobj, &td043mtea1_attr_group);
	if (ret < 0) {
		dev_err(&spi->dev, "failed to create sysfs files\n");
		return ret;
	}

	drm_panel_init(&lcd->panel, &lcd->spi->dev, &td043mtea1_funcs,
		       DRM_MODE_CONNECTOR_DPI);

	drm_panel_add(&lcd->panel);

	return 0;
}

static void td043mtea1_remove(struct spi_device *spi)
{
	struct td043mtea1_panel *lcd = spi_get_drvdata(spi);

	drm_panel_remove(&lcd->panel);
	drm_panel_disable(&lcd->panel);
	drm_panel_unprepare(&lcd->panel);

	sysfs_remove_group(&spi->dev.kobj, &td043mtea1_attr_group);
}

static const struct of_device_id td043mtea1_of_match[] = {
	{ .compatible = "tpo,td043mtea1", },
	{ /* sentinel */ },
};

MODULE_DEVICE_TABLE(of, td043mtea1_of_match);

static const struct spi_device_id td043mtea1_ids[] = {
	{ "td043mtea1", 0 },
	{ /* sentinel */ }
};

MODULE_DEVICE_TABLE(spi, td043mtea1_ids);

static struct spi_driver td043mtea1_driver = {
	.probe		= td043mtea1_probe,
	.remove		= td043mtea1_remove,
	.id_table	= td043mtea1_ids,
	.driver		= {
		.name	= "panel-tpo-td043mtea1",
		.pm	= &td043mtea1_pm_ops,
		.of_match_table = td043mtea1_of_match,
	},
};

module_spi_driver(td043mtea1_driver);

MODULE_AUTHOR("Gražvydas Ignotas <notasas@gmail.com>");
MODULE_DESCRIPTION("TPO TD043MTEA1 Panel Driver");
MODULE_LICENSE("GPL");
