// SPDX-License-Identifier: GPL-2.0+
/*
 * LCD-OLinuXino support for panel driver
 *
 * Copyright (C) 2018 Olimex Ltd.
 *   Author: Stefan Mavrodiev <stefan@olimex.com>
 */

#include <linux/backlight.h>
#include <linux/crc32.h>
#include <linux/gpio/consumer.h>
#include <linux/i2c.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/of.h>
#include <linux/regulator/consumer.h>

#include <video/videomode.h>
#include <video/display_timing.h>

#include <drm/drm_device.h>
#include <drm/drm_modes.h>
#include <drm/drm_panel.h>

#define LCD_OLINUXINO_HEADER_MAGIC	0x4F4CB727
#define LCD_OLINUXINO_DATA_LEN		256

struct lcd_olinuxino_mode {
	u32 pixelclock;
	u32 hactive;
	u32 hfp;
	u32 hbp;
	u32 hpw;
	u32 vactive;
	u32 vfp;
	u32 vbp;
	u32 vpw;
	u32 refresh;
	u32 flags;
};

struct lcd_olinuxino_info {
	char name[32];
	u32 width_mm;
	u32 height_mm;
	u32 bpc;
	u32 bus_format;
	u32 bus_flag;
} __attribute__((__packed__));

struct lcd_olinuxino_eeprom {
	u32 header;
	u32 id;
	char revision[4];
	u32 serial;
	struct lcd_olinuxino_info info;
	u32 num_modes;
	u8 reserved[180];
	u32 checksum;
} __attribute__((__packed__));

struct lcd_olinuxino {
	struct drm_panel panel;
	struct device *dev;
	struct i2c_client *client;
	struct mutex mutex;

	bool prepared;
	bool enabled;

	struct backlight_device *backlight;
	struct regulator *supply;
	struct gpio_desc *enable_gpio;

	struct lcd_olinuxino_eeprom eeprom;
};

static inline struct lcd_olinuxino *to_lcd_olinuxino(struct drm_panel *panel)
{
	return container_of(panel, struct lcd_olinuxino, panel);
}

static int lcd_olinuxino_disable(struct drm_panel *panel)
{
	struct lcd_olinuxino *lcd = to_lcd_olinuxino(panel);

	if (!lcd->enabled)
		return 0;

	backlight_disable(lcd->backlight);

	lcd->enabled = false;

	return 0;
}

static int lcd_olinuxino_unprepare(struct drm_panel *panel)
{
	struct lcd_olinuxino *lcd = to_lcd_olinuxino(panel);

	if (!lcd->prepared)
		return 0;

	gpiod_set_value_cansleep(lcd->enable_gpio, 0);
	regulator_disable(lcd->supply);

	lcd->prepared = false;

	return 0;
}

static int lcd_olinuxino_prepare(struct drm_panel *panel)
{
	struct lcd_olinuxino *lcd = to_lcd_olinuxino(panel);
	int ret;

	if (lcd->prepared)
		return 0;

	ret = regulator_enable(lcd->supply);
	if (ret < 0)
		return ret;

	gpiod_set_value_cansleep(lcd->enable_gpio, 1);
	lcd->prepared = true;

	return 0;
}

static int lcd_olinuxino_enable(struct drm_panel *panel)
{
	struct lcd_olinuxino *lcd = to_lcd_olinuxino(panel);

	if (lcd->enabled)
		return 0;

	backlight_enable(lcd->backlight);

	lcd->enabled = true;

	return 0;
}

static int lcd_olinuxino_get_modes(struct drm_panel *panel)
{
	struct lcd_olinuxino *lcd = to_lcd_olinuxino(panel);
	struct drm_connector *connector = lcd->panel.connector;
	struct lcd_olinuxino_info *lcd_info = &lcd->eeprom.info;
	struct drm_device *drm = lcd->panel.drm;
	struct lcd_olinuxino_mode *lcd_mode;
	struct drm_display_mode *mode;
	u32 i, num = 0;

	for (i = 0; i < lcd->eeprom.num_modes; i++) {
		lcd_mode = (struct lcd_olinuxino_mode *)
			   &lcd->eeprom.reserved[i * sizeof(*lcd_mode)];

		mode = drm_mode_create(drm);
		if (!mode) {
			dev_err(drm->dev, "failed to add mode %ux%u@%u\n",
				lcd_mode->hactive,
				lcd_mode->vactive,
				lcd_mode->refresh);
				continue;
		}

		mode->clock = lcd_mode->pixelclock;
		mode->hdisplay = lcd_mode->hactive;
		mode->hsync_start = lcd_mode->hactive + lcd_mode->hfp;
		mode->hsync_end = lcd_mode->hactive + lcd_mode->hfp +
				  lcd_mode->hpw;
		mode->htotal = lcd_mode->hactive + lcd_mode->hfp +
			       lcd_mode->hpw + lcd_mode->hbp;
		mode->vdisplay = lcd_mode->vactive;
		mode->vsync_start = lcd_mode->vactive + lcd_mode->vfp;
		mode->vsync_end = lcd_mode->vactive + lcd_mode->vfp +
				  lcd_mode->vpw;
		mode->vtotal = lcd_mode->vactive + lcd_mode->vfp +
			       lcd_mode->vpw + lcd_mode->vbp;
		mode->vrefresh = lcd_mode->refresh;

		/* Always make the first mode preferred */
		if (i == 0)
			mode->type |= DRM_MODE_TYPE_PREFERRED;
		mode->type |= DRM_MODE_TYPE_DRIVER;

		drm_mode_set_name(mode);
		drm_mode_probed_add(connector, mode);

		num++;
	}

	connector->display_info.width_mm = lcd_info->width_mm;
	connector->display_info.height_mm = lcd_info->height_mm;
	connector->display_info.bpc = lcd_info->bpc;

	if (lcd_info->bus_format)
		drm_display_info_set_bus_formats(&connector->display_info,
						 &lcd_info->bus_format, 1);
	connector->display_info.bus_flags = lcd_info->bus_flag;

	return num;
}

static const struct drm_panel_funcs lcd_olinuxino_funcs = {
	.disable = lcd_olinuxino_disable,
	.unprepare = lcd_olinuxino_unprepare,
	.prepare = lcd_olinuxino_prepare,
	.enable = lcd_olinuxino_enable,
	.get_modes = lcd_olinuxino_get_modes,
};

static int lcd_olinuxino_probe(struct i2c_client *client,
			       const struct i2c_device_id *id)
{
	struct device *dev = &client->dev;
	struct lcd_olinuxino *lcd;
	u32 checksum, i;
	int ret = 0;

	if (!i2c_check_functionality(client->adapter, I2C_FUNC_I2C |
				     I2C_FUNC_SMBUS_READ_I2C_BLOCK))
		return -ENODEV;

	lcd = devm_kzalloc(dev, sizeof(*lcd), GFP_KERNEL);
	if (!lcd)
		return -ENOMEM;

	i2c_set_clientdata(client, lcd);
	lcd->dev = dev;
	lcd->client = client;

	mutex_init(&lcd->mutex);

	/* Copy data into buffer */
	for (i = 0; i < LCD_OLINUXINO_DATA_LEN; i += I2C_SMBUS_BLOCK_MAX) {
		mutex_lock(&lcd->mutex);
		ret = i2c_smbus_read_i2c_block_data(client,
						    i,
						    I2C_SMBUS_BLOCK_MAX,
						    (u8 *)&lcd->eeprom + i);
		mutex_unlock(&lcd->mutex);
		if (ret < 0) {
			dev_err(dev, "error reading from device at %02x\n", i);
			return ret;
		}
	}

	/* Check configuration checksum */
	checksum = ~crc32(~0, (u8 *)&lcd->eeprom, 252);
	if (checksum != lcd->eeprom.checksum) {
		dev_err(dev, "configuration checksum does not match!\n");
		return -EINVAL;
	}

	/* Check magic header */
	if (lcd->eeprom.header != LCD_OLINUXINO_HEADER_MAGIC) {
		dev_err(dev, "magic header does not match\n");
		return -EINVAL;
	}

	dev_info(dev, "Detected %s, Rev. %s, Serial: %08x\n",
		 lcd->eeprom.info.name,
		 lcd->eeprom.revision,
		 lcd->eeprom.serial);

	/*
	 * The eeprom can hold up to 4 modes.
	 * If the stored value is bigger, overwrite it.
	 */
	if (lcd->eeprom.num_modes > 4) {
		dev_warn(dev, "invalid number of modes, falling back to 4\n");
		lcd->eeprom.num_modes = 4;
	}

	lcd->enabled = false;
	lcd->prepared = false;

	lcd->supply = devm_regulator_get(dev, "power");
	if (IS_ERR(lcd->supply))
		return PTR_ERR(lcd->supply);

	lcd->enable_gpio = devm_gpiod_get(dev, "enable", GPIOD_OUT_LOW);
	if (IS_ERR(lcd->enable_gpio))
		return PTR_ERR(lcd->enable_gpio);

	lcd->backlight = devm_of_find_backlight(dev);
	if (IS_ERR(lcd->backlight))
		return PTR_ERR(lcd->backlight);

	drm_panel_init(&lcd->panel, dev, &lcd_olinuxino_funcs,
		       DRM_MODE_CONNECTOR_DPI);

	return drm_panel_add(&lcd->panel);
}

static int lcd_olinuxino_remove(struct i2c_client *client)
{
	struct lcd_olinuxino *panel = i2c_get_clientdata(client);

	drm_panel_remove(&panel->panel);

	lcd_olinuxino_disable(&panel->panel);
	lcd_olinuxino_unprepare(&panel->panel);

	return 0;
}

static const struct of_device_id lcd_olinuxino_of_ids[] = {
	{ .compatible = "olimex,lcd-olinuxino" },
	{ }
};
MODULE_DEVICE_TABLE(of, lcd_olinuxino_of_ids);

static struct i2c_driver lcd_olinuxino_driver = {
	.driver = {
		.name = "lcd_olinuxino",
		.of_match_table = lcd_olinuxino_of_ids,
	},
	.probe = lcd_olinuxino_probe,
	.remove = lcd_olinuxino_remove,
};

module_i2c_driver(lcd_olinuxino_driver);

MODULE_AUTHOR("Stefan Mavrodiev <stefan@olimex.com>");
MODULE_DESCRIPTION("LCD-OLinuXino driver");
MODULE_LICENSE("GPL");
