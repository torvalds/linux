// SPDX-License-Identifier: GPL-2.0
/*
 * Sony ACX565AKM LCD Panel driver
 *
 * Copyright (C) 2019 Texas Instruments Incorporated
 *
 * Based on the omapdrm-specific panel-sony-acx565akm driver
 *
 * Copyright (C) 2010 Nokia Corporation
 * Author: Imre Deak <imre.deak@nokia.com>
 */

/*
 * TODO (to be addressed with hardware access to test the changes):
 *
 * - Update backlight support to use backlight_update_status() etc.
 * - Use prepare/unprepare for the basic power on/off of the backligt
 */

#include <linux/backlight.h>
#include <linux/delay.h>
#include <linux/gpio/consumer.h>
#include <linux/jiffies.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/sched.h>
#include <linux/spi/spi.h>
#include <video/mipi_display.h>

#include <drm/drm_connector.h>
#include <drm/drm_modes.h>
#include <drm/drm_panel.h>

#define CTRL_DISP_BRIGHTNESS_CTRL_ON		BIT(5)
#define CTRL_DISP_AMBIENT_LIGHT_CTRL_ON		BIT(4)
#define CTRL_DISP_BACKLIGHT_ON			BIT(2)
#define CTRL_DISP_AUTO_BRIGHTNESS_ON		BIT(1)

#define MIPID_CMD_WRITE_CABC		0x55
#define MIPID_CMD_READ_CABC		0x56

#define MIPID_VER_LPH8923		3
#define MIPID_VER_LS041Y3		4
#define MIPID_VER_L4F00311		8
#define MIPID_VER_ACX565AKM		9

struct acx565akm_panel {
	struct drm_panel panel;

	struct spi_device *spi;
	struct gpio_desc *reset_gpio;
	struct backlight_device *backlight;

	struct mutex mutex;

	const char *name;
	u8 display_id[3];
	int model;
	int revision;
	bool has_bc;
	bool has_cabc;

	bool enabled;
	unsigned int cabc_mode;
	/*
	 * Next value of jiffies when we can issue the next sleep in/out
	 * command.
	 */
	unsigned long hw_guard_end;
	unsigned long hw_guard_wait;		/* max guard time in jiffies */
};

#define to_acx565akm_device(p) container_of(p, struct acx565akm_panel, panel)

static void acx565akm_transfer(struct acx565akm_panel *lcd, int cmd,
			      const u8 *wbuf, int wlen, u8 *rbuf, int rlen)
{
	struct spi_message	m;
	struct spi_transfer	*x, xfer[5];
	int			ret;

	spi_message_init(&m);

	memset(xfer, 0, sizeof(xfer));
	x = &xfer[0];

	cmd &=  0xff;
	x->tx_buf = &cmd;
	x->bits_per_word = 9;
	x->len = 2;

	if (rlen > 1 && wlen == 0) {
		/*
		 * Between the command and the response data there is a
		 * dummy clock cycle. Add an extra bit after the command
		 * word to account for this.
		 */
		x->bits_per_word = 10;
		cmd <<= 1;
	}
	spi_message_add_tail(x, &m);

	if (wlen) {
		x++;
		x->tx_buf = wbuf;
		x->len = wlen;
		x->bits_per_word = 9;
		spi_message_add_tail(x, &m);
	}

	if (rlen) {
		x++;
		x->rx_buf	= rbuf;
		x->len		= rlen;
		spi_message_add_tail(x, &m);
	}

	ret = spi_sync(lcd->spi, &m);
	if (ret < 0)
		dev_dbg(&lcd->spi->dev, "spi_sync %d\n", ret);
}

static inline void acx565akm_cmd(struct acx565akm_panel *lcd, int cmd)
{
	acx565akm_transfer(lcd, cmd, NULL, 0, NULL, 0);
}

static inline void acx565akm_write(struct acx565akm_panel *lcd,
			       int reg, const u8 *buf, int len)
{
	acx565akm_transfer(lcd, reg, buf, len, NULL, 0);
}

static inline void acx565akm_read(struct acx565akm_panel *lcd,
			      int reg, u8 *buf, int len)
{
	acx565akm_transfer(lcd, reg, NULL, 0, buf, len);
}

/* -----------------------------------------------------------------------------
 * Auto Brightness Control Via sysfs
 */

static unsigned int acx565akm_get_cabc_mode(struct acx565akm_panel *lcd)
{
	return lcd->cabc_mode;
}

static void acx565akm_set_cabc_mode(struct acx565akm_panel *lcd,
				    unsigned int mode)
{
	u16 cabc_ctrl;

	lcd->cabc_mode = mode;
	if (!lcd->enabled)
		return;
	cabc_ctrl = 0;
	acx565akm_read(lcd, MIPID_CMD_READ_CABC, (u8 *)&cabc_ctrl, 1);
	cabc_ctrl &= ~3;
	cabc_ctrl |= (1 << 8) | (mode & 3);
	acx565akm_write(lcd, MIPID_CMD_WRITE_CABC, (u8 *)&cabc_ctrl, 2);
}

static unsigned int acx565akm_get_hw_cabc_mode(struct acx565akm_panel *lcd)
{
	u8 cabc_ctrl;

	acx565akm_read(lcd, MIPID_CMD_READ_CABC, &cabc_ctrl, 1);
	return cabc_ctrl & 3;
}

static const char * const acx565akm_cabc_modes[] = {
	"off",		/* always used when CABC is not supported */
	"ui",
	"still-image",
	"moving-image",
};

static ssize_t cabc_mode_show(struct device *dev,
			      struct device_attribute *attr,
			      char *buf)
{
	struct acx565akm_panel *lcd = dev_get_drvdata(dev);
	const char *mode_str;
	int mode;

	if (!lcd->has_cabc)
		mode = 0;
	else
		mode = acx565akm_get_cabc_mode(lcd);

	mode_str = "unknown";
	if (mode >= 0 && mode < ARRAY_SIZE(acx565akm_cabc_modes))
		mode_str = acx565akm_cabc_modes[mode];

	return sprintf(buf, "%s\n", mode_str);
}

static ssize_t cabc_mode_store(struct device *dev,
			       struct device_attribute *attr,
			       const char *buf, size_t count)
{
	struct acx565akm_panel *lcd = dev_get_drvdata(dev);
	unsigned int i;

	for (i = 0; i < ARRAY_SIZE(acx565akm_cabc_modes); i++) {
		const char *mode_str = acx565akm_cabc_modes[i];
		int cmp_len = strlen(mode_str);

		if (count > 0 && buf[count - 1] == '\n')
			count--;
		if (count != cmp_len)
			continue;

		if (strncmp(buf, mode_str, cmp_len) == 0)
			break;
	}

	if (i == ARRAY_SIZE(acx565akm_cabc_modes))
		return -EINVAL;

	if (!lcd->has_cabc && i != 0)
		return -EINVAL;

	mutex_lock(&lcd->mutex);
	acx565akm_set_cabc_mode(lcd, i);
	mutex_unlock(&lcd->mutex);

	return count;
}

static ssize_t cabc_available_modes_show(struct device *dev,
					 struct device_attribute *attr,
					 char *buf)
{
	struct acx565akm_panel *lcd = dev_get_drvdata(dev);
	unsigned int i;
	size_t len = 0;

	if (!lcd->has_cabc)
		return sprintf(buf, "%s\n", acx565akm_cabc_modes[0]);

	for (i = 0; i < ARRAY_SIZE(acx565akm_cabc_modes); i++)
		len += sprintf(&buf[len], "%s%s", i ? " " : "",
			       acx565akm_cabc_modes[i]);

	buf[len++] = '\n';

	return len;
}

static DEVICE_ATTR_RW(cabc_mode);
static DEVICE_ATTR_RO(cabc_available_modes);

static struct attribute *acx565akm_cabc_attrs[] = {
	&dev_attr_cabc_mode.attr,
	&dev_attr_cabc_available_modes.attr,
	NULL,
};

static const struct attribute_group acx565akm_cabc_attr_group = {
	.attrs = acx565akm_cabc_attrs,
};

/* -----------------------------------------------------------------------------
 * Backlight Device
 */

static int acx565akm_get_actual_brightness(struct acx565akm_panel *lcd)
{
	u8 bv;

	acx565akm_read(lcd, MIPI_DCS_GET_DISPLAY_BRIGHTNESS, &bv, 1);

	return bv;
}

static void acx565akm_set_brightness(struct acx565akm_panel *lcd, int level)
{
	u16 ctrl;
	int bv;

	bv = level | (1 << 8);
	acx565akm_write(lcd, MIPI_DCS_SET_DISPLAY_BRIGHTNESS, (u8 *)&bv, 2);

	acx565akm_read(lcd, MIPI_DCS_GET_CONTROL_DISPLAY, (u8 *)&ctrl, 1);
	if (level)
		ctrl |= CTRL_DISP_BRIGHTNESS_CTRL_ON |
			CTRL_DISP_BACKLIGHT_ON;
	else
		ctrl &= ~(CTRL_DISP_BRIGHTNESS_CTRL_ON |
			  CTRL_DISP_BACKLIGHT_ON);

	ctrl |= 1 << 8;
	acx565akm_write(lcd, MIPI_DCS_WRITE_CONTROL_DISPLAY, (u8 *)&ctrl, 2);
}

static int acx565akm_bl_update_status_locked(struct backlight_device *dev)
{
	struct acx565akm_panel *lcd = dev_get_drvdata(&dev->dev);
	int level;

	if (dev->props.fb_blank == FB_BLANK_UNBLANK &&
	    dev->props.power == FB_BLANK_UNBLANK)
		level = dev->props.brightness;
	else
		level = 0;

	acx565akm_set_brightness(lcd, level);

	return 0;
}

static int acx565akm_bl_update_status(struct backlight_device *dev)
{
	struct acx565akm_panel *lcd = dev_get_drvdata(&dev->dev);
	int ret;

	mutex_lock(&lcd->mutex);
	ret = acx565akm_bl_update_status_locked(dev);
	mutex_unlock(&lcd->mutex);

	return ret;
}

static int acx565akm_bl_get_intensity(struct backlight_device *dev)
{
	struct acx565akm_panel *lcd = dev_get_drvdata(&dev->dev);
	unsigned int intensity;

	mutex_lock(&lcd->mutex);

	if (dev->props.fb_blank == FB_BLANK_UNBLANK &&
	    dev->props.power == FB_BLANK_UNBLANK)
		intensity = acx565akm_get_actual_brightness(lcd);
	else
		intensity = 0;

	mutex_unlock(&lcd->mutex);

	return intensity;
}

static const struct backlight_ops acx565akm_bl_ops = {
	.get_brightness = acx565akm_bl_get_intensity,
	.update_status  = acx565akm_bl_update_status,
};

static int acx565akm_backlight_init(struct acx565akm_panel *lcd)
{
	struct backlight_properties props = {
		.fb_blank = FB_BLANK_UNBLANK,
		.power = FB_BLANK_UNBLANK,
		.type = BACKLIGHT_RAW,
	};
	int ret;

	lcd->backlight = backlight_device_register(lcd->name, &lcd->spi->dev,
						   lcd, &acx565akm_bl_ops,
						   &props);
	if (IS_ERR(lcd->backlight)) {
		ret = PTR_ERR(lcd->backlight);
		lcd->backlight = NULL;
		return ret;
	}

	if (lcd->has_cabc) {
		ret = sysfs_create_group(&lcd->backlight->dev.kobj,
					 &acx565akm_cabc_attr_group);
		if (ret < 0) {
			dev_err(&lcd->spi->dev,
				"%s failed to create sysfs files\n", __func__);
			backlight_device_unregister(lcd->backlight);
			return ret;
		}

		lcd->cabc_mode = acx565akm_get_hw_cabc_mode(lcd);
	}

	lcd->backlight->props.max_brightness = 255;
	lcd->backlight->props.brightness = acx565akm_get_actual_brightness(lcd);

	acx565akm_bl_update_status_locked(lcd->backlight);

	return 0;
}

static void acx565akm_backlight_cleanup(struct acx565akm_panel *lcd)
{
	if (lcd->has_cabc)
		sysfs_remove_group(&lcd->backlight->dev.kobj,
				   &acx565akm_cabc_attr_group);

	backlight_device_unregister(lcd->backlight);
}

/* -----------------------------------------------------------------------------
 * DRM Bridge Operations
 */

static void acx565akm_set_sleep_mode(struct acx565akm_panel *lcd, int on)
{
	int cmd = on ? MIPI_DCS_ENTER_SLEEP_MODE : MIPI_DCS_EXIT_SLEEP_MODE;
	unsigned long wait;

	/*
	 * We have to keep 120msec between sleep in/out commands.
	 * (8.2.15, 8.2.16).
	 */
	wait = lcd->hw_guard_end - jiffies;
	if ((long)wait > 0 && wait <= lcd->hw_guard_wait) {
		set_current_state(TASK_UNINTERRUPTIBLE);
		schedule_timeout(wait);
	}

	acx565akm_cmd(lcd, cmd);

	lcd->hw_guard_wait = msecs_to_jiffies(120);
	lcd->hw_guard_end = jiffies + lcd->hw_guard_wait;
}

static void acx565akm_set_display_state(struct acx565akm_panel *lcd,
					int enabled)
{
	int cmd = enabled ? MIPI_DCS_SET_DISPLAY_ON : MIPI_DCS_SET_DISPLAY_OFF;

	acx565akm_cmd(lcd, cmd);
}

static int acx565akm_power_on(struct acx565akm_panel *lcd)
{
	/*FIXME tweak me */
	msleep(50);

	gpiod_set_value(lcd->reset_gpio, 1);

	if (lcd->enabled) {
		dev_dbg(&lcd->spi->dev, "panel already enabled\n");
		return 0;
	}

	/*
	 * We have to meet all the following delay requirements:
	 * 1. tRW: reset pulse width 10usec (7.12.1)
	 * 2. tRT: reset cancel time 5msec (7.12.1)
	 * 3. Providing PCLK,HS,VS signals for 2 frames = ~50msec worst
	 *    case (7.6.2)
	 * 4. 120msec before the sleep out command (7.12.1)
	 */
	msleep(120);

	acx565akm_set_sleep_mode(lcd, 0);
	lcd->enabled = true;

	/* 5msec between sleep out and the next command. (8.2.16) */
	usleep_range(5000, 10000);
	acx565akm_set_display_state(lcd, 1);
	acx565akm_set_cabc_mode(lcd, lcd->cabc_mode);

	return acx565akm_bl_update_status_locked(lcd->backlight);
}

static void acx565akm_power_off(struct acx565akm_panel *lcd)
{
	if (!lcd->enabled)
		return;

	acx565akm_set_display_state(lcd, 0);
	acx565akm_set_sleep_mode(lcd, 1);
	lcd->enabled = false;
	/*
	 * We have to provide PCLK,HS,VS signals for 2 frames (worst case
	 * ~50msec) after sending the sleep in command and asserting the
	 * reset signal. We probably could assert the reset w/o the delay
	 * but we still delay to avoid possible artifacts. (7.6.1)
	 */
	msleep(50);

	gpiod_set_value(lcd->reset_gpio, 0);

	/* FIXME need to tweak this delay */
	msleep(100);
}

static int acx565akm_disable(struct drm_panel *panel)
{
	struct acx565akm_panel *lcd = to_acx565akm_device(panel);

	mutex_lock(&lcd->mutex);
	acx565akm_power_off(lcd);
	mutex_unlock(&lcd->mutex);

	return 0;
}

static int acx565akm_enable(struct drm_panel *panel)
{
	struct acx565akm_panel *lcd = to_acx565akm_device(panel);

	mutex_lock(&lcd->mutex);
	acx565akm_power_on(lcd);
	mutex_unlock(&lcd->mutex);

	return 0;
}

static const struct drm_display_mode acx565akm_mode = {
	.clock = 24000,
	.hdisplay = 800,
	.hsync_start = 800 + 28,
	.hsync_end = 800 + 28 + 4,
	.htotal = 800 + 28 + 4 + 24,
	.vdisplay = 480,
	.vsync_start = 480 + 3,
	.vsync_end = 480 + 3 + 3,
	.vtotal = 480 + 3 + 3 + 4,
	.vrefresh = 57,
	.type = DRM_MODE_TYPE_DRIVER | DRM_MODE_TYPE_PREFERRED,
	.flags = DRM_MODE_FLAG_NHSYNC | DRM_MODE_FLAG_NVSYNC,
	.width_mm = 77,
	.height_mm = 46,
};

static int acx565akm_get_modes(struct drm_panel *panel,
			       struct drm_connector *connector)
{
	struct drm_display_mode *mode;

	mode = drm_mode_duplicate(connector->dev, &acx565akm_mode);
	if (!mode)
		return -ENOMEM;

	drm_mode_set_name(mode);
	drm_mode_probed_add(connector, mode);

	connector->display_info.width_mm = acx565akm_mode.width_mm;
	connector->display_info.height_mm = acx565akm_mode.height_mm;
	connector->display_info.bus_flags = DRM_BUS_FLAG_DE_HIGH
					  | DRM_BUS_FLAG_SYNC_SAMPLE_POSEDGE
					  | DRM_BUS_FLAG_PIXDATA_SAMPLE_NEGEDGE;

	return 1;
}

static const struct drm_panel_funcs acx565akm_funcs = {
	.disable = acx565akm_disable,
	.enable = acx565akm_enable,
	.get_modes = acx565akm_get_modes,
};

/* -----------------------------------------------------------------------------
 * Probe, Detect and Remove
 */

static int acx565akm_detect(struct acx565akm_panel *lcd)
{
	__be32 value;
	u32 status;
	int ret = 0;

	/*
	 * After being taken out of reset the panel needs 5ms before the first
	 * command can be sent.
	 */
	gpiod_set_value(lcd->reset_gpio, 1);
	usleep_range(5000, 10000);

	acx565akm_read(lcd, MIPI_DCS_GET_DISPLAY_STATUS, (u8 *)&value, 4);
	status = __be32_to_cpu(value);
	lcd->enabled = (status & (1 << 17)) && (status & (1 << 10));

	dev_dbg(&lcd->spi->dev,
		"LCD panel %s by bootloader (status 0x%04x)\n",
		lcd->enabled ? "enabled" : "disabled ", status);

	acx565akm_read(lcd, MIPI_DCS_GET_DISPLAY_ID, lcd->display_id, 3);
	dev_dbg(&lcd->spi->dev, "MIPI display ID: %02x%02x%02x\n",
		lcd->display_id[0], lcd->display_id[1], lcd->display_id[2]);

	switch (lcd->display_id[0]) {
	case 0x10:
		lcd->model = MIPID_VER_ACX565AKM;
		lcd->name = "acx565akm";
		lcd->has_bc = 1;
		lcd->has_cabc = 1;
		break;
	case 0x29:
		lcd->model = MIPID_VER_L4F00311;
		lcd->name = "l4f00311";
		break;
	case 0x45:
		lcd->model = MIPID_VER_LPH8923;
		lcd->name = "lph8923";
		break;
	case 0x83:
		lcd->model = MIPID_VER_LS041Y3;
		lcd->name = "ls041y3";
		break;
	default:
		lcd->name = "unknown";
		dev_err(&lcd->spi->dev, "unknown display ID\n");
		ret = -ENODEV;
		goto done;
	}

	lcd->revision = lcd->display_id[1];

	dev_info(&lcd->spi->dev, "%s rev %02x panel detected\n",
		 lcd->name, lcd->revision);

done:
	if (!lcd->enabled)
		gpiod_set_value(lcd->reset_gpio, 0);

	return ret;
}

static int acx565akm_probe(struct spi_device *spi)
{
	struct acx565akm_panel *lcd;
	int ret;

	lcd = devm_kzalloc(&spi->dev, sizeof(*lcd), GFP_KERNEL);
	if (!lcd)
		return -ENOMEM;

	spi_set_drvdata(spi, lcd);
	spi->mode = SPI_MODE_3;

	lcd->spi = spi;
	mutex_init(&lcd->mutex);

	lcd->reset_gpio = devm_gpiod_get(&spi->dev, "reset", GPIOD_OUT_LOW);
	if (IS_ERR(lcd->reset_gpio)) {
		dev_err(&spi->dev, "failed to get reset GPIO\n");
		return PTR_ERR(lcd->reset_gpio);
	}

	ret = acx565akm_detect(lcd);
	if (ret < 0) {
		dev_err(&spi->dev, "panel detection failed\n");
		return ret;
	}

	if (lcd->has_bc) {
		ret = acx565akm_backlight_init(lcd);
		if (ret < 0)
			return ret;
	}

	drm_panel_init(&lcd->panel, &lcd->spi->dev, &acx565akm_funcs,
		       DRM_MODE_CONNECTOR_DPI);

	ret = drm_panel_add(&lcd->panel);
	if (ret < 0) {
		if (lcd->has_bc)
			acx565akm_backlight_cleanup(lcd);
		return ret;
	}

	return 0;
}

static int acx565akm_remove(struct spi_device *spi)
{
	struct acx565akm_panel *lcd = spi_get_drvdata(spi);

	drm_panel_remove(&lcd->panel);

	if (lcd->has_bc)
		acx565akm_backlight_cleanup(lcd);

	drm_panel_disable(&lcd->panel);
	drm_panel_unprepare(&lcd->panel);

	return 0;
}

static const struct of_device_id acx565akm_of_match[] = {
	{ .compatible = "sony,acx565akm", },
	{ /* sentinel */ },
};

MODULE_DEVICE_TABLE(of, acx565akm_of_match);

static const struct spi_device_id acx565akm_ids[] = {
	{ "acx565akm", 0 },
	{ /* sentinel */ }
};

MODULE_DEVICE_TABLE(spi, acx565akm_ids);

static struct spi_driver acx565akm_driver = {
	.probe		= acx565akm_probe,
	.remove		= acx565akm_remove,
	.id_table	= acx565akm_ids,
	.driver		= {
		.name	= "panel-sony-acx565akm",
		.of_match_table = acx565akm_of_match,
	},
};

module_spi_driver(acx565akm_driver);

MODULE_AUTHOR("Nokia Corporation");
MODULE_DESCRIPTION("Sony ACX565AKM LCD Panel Driver");
MODULE_LICENSE("GPL");
