// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Driver for the Himax HX-8357 LCD Controller
 *
 * Copyright 2012 Free Electrons
 */

#include <linux/delay.h>
#include <linux/gpio/consumer.h>
#include <linux/lcd.h>
#include <linux/mod_devicetable.h>
#include <linux/module.h>
#include <linux/property.h>
#include <linux/spi/spi.h>

#define HX8357_NUM_IM_PINS	3

#define HX8357_SWRESET			0x01
#define HX8357_GET_RED_CHANNEL		0x06
#define HX8357_GET_GREEN_CHANNEL	0x07
#define HX8357_GET_BLUE_CHANNEL		0x08
#define HX8357_GET_POWER_MODE		0x0a
#define HX8357_GET_MADCTL		0x0b
#define HX8357_GET_PIXEL_FORMAT		0x0c
#define HX8357_GET_DISPLAY_MODE		0x0d
#define HX8357_GET_SIGNAL_MODE		0x0e
#define HX8357_GET_DIAGNOSTIC_RESULT	0x0f
#define HX8357_ENTER_SLEEP_MODE		0x10
#define HX8357_EXIT_SLEEP_MODE		0x11
#define HX8357_ENTER_PARTIAL_MODE	0x12
#define HX8357_ENTER_NORMAL_MODE	0x13
#define HX8357_EXIT_INVERSION_MODE	0x20
#define HX8357_ENTER_INVERSION_MODE	0x21
#define HX8357_SET_DISPLAY_OFF		0x28
#define HX8357_SET_DISPLAY_ON		0x29
#define HX8357_SET_COLUMN_ADDRESS	0x2a
#define HX8357_SET_PAGE_ADDRESS		0x2b
#define HX8357_WRITE_MEMORY_START	0x2c
#define HX8357_READ_MEMORY_START	0x2e
#define HX8357_SET_PARTIAL_AREA		0x30
#define HX8357_SET_SCROLL_AREA		0x33
#define HX8357_SET_TEAR_OFF		0x34
#define HX8357_SET_TEAR_ON		0x35
#define HX8357_SET_ADDRESS_MODE		0x36
#define HX8357_SET_SCROLL_START		0x37
#define HX8357_EXIT_IDLE_MODE		0x38
#define HX8357_ENTER_IDLE_MODE		0x39
#define HX8357_SET_PIXEL_FORMAT		0x3a
#define HX8357_SET_PIXEL_FORMAT_DBI_3BIT	(0x1)
#define HX8357_SET_PIXEL_FORMAT_DBI_16BIT	(0x5)
#define HX8357_SET_PIXEL_FORMAT_DBI_18BIT	(0x6)
#define HX8357_SET_PIXEL_FORMAT_DPI_3BIT	(0x1 << 4)
#define HX8357_SET_PIXEL_FORMAT_DPI_16BIT	(0x5 << 4)
#define HX8357_SET_PIXEL_FORMAT_DPI_18BIT	(0x6 << 4)
#define HX8357_WRITE_MEMORY_CONTINUE	0x3c
#define HX8357_READ_MEMORY_CONTINUE	0x3e
#define HX8357_SET_TEAR_SCAN_LINES	0x44
#define HX8357_GET_SCAN_LINES		0x45
#define HX8357_READ_DDB_START		0xa1
#define HX8357_SET_DISPLAY_MODE		0xb4
#define HX8357_SET_DISPLAY_MODE_RGB_THROUGH	(0x3)
#define HX8357_SET_DISPLAY_MODE_RGB_INTERFACE	(1 << 4)
#define HX8357_SET_PANEL_DRIVING	0xc0
#define HX8357_SET_DISPLAY_FRAME	0xc5
#define HX8357_SET_RGB			0xc6
#define HX8357_SET_RGB_ENABLE_HIGH		(1 << 1)
#define HX8357_SET_GAMMA		0xc8
#define HX8357_SET_POWER		0xd0
#define HX8357_SET_VCOM			0xd1
#define HX8357_SET_POWER_NORMAL		0xd2
#define HX8357_SET_PANEL_RELATED	0xe9

#define HX8369_SET_DISPLAY_BRIGHTNESS		0x51
#define HX8369_WRITE_CABC_DISPLAY_VALUE		0x53
#define HX8369_WRITE_CABC_BRIGHT_CTRL		0x55
#define HX8369_WRITE_CABC_MIN_BRIGHTNESS	0x5e
#define HX8369_SET_POWER			0xb1
#define HX8369_SET_DISPLAY_MODE			0xb2
#define HX8369_SET_DISPLAY_WAVEFORM_CYC		0xb4
#define HX8369_SET_VCOM				0xb6
#define HX8369_SET_EXTENSION_COMMAND		0xb9
#define HX8369_SET_GIP				0xd5
#define HX8369_SET_GAMMA_CURVE_RELATED		0xe0

struct hx8357_data {
	struct gpio_descs	*im_pins;
	struct gpio_desc	*reset;
	struct spi_device	*spi;
	int			state;
};

static u8 hx8357_seq_power[] = {
	HX8357_SET_POWER, 0x44, 0x41, 0x06,
};

static u8 hx8357_seq_vcom[] = {
	HX8357_SET_VCOM, 0x40, 0x10,
};

static u8 hx8357_seq_power_normal[] = {
	HX8357_SET_POWER_NORMAL, 0x05, 0x12,
};

static u8 hx8357_seq_panel_driving[] = {
	HX8357_SET_PANEL_DRIVING, 0x14, 0x3b, 0x00, 0x02, 0x11,
};

static u8 hx8357_seq_display_frame[] = {
	HX8357_SET_DISPLAY_FRAME, 0x0c,
};

static u8 hx8357_seq_panel_related[] = {
	HX8357_SET_PANEL_RELATED, 0x01,
};

static u8 hx8357_seq_undefined1[] = {
	0xea, 0x03, 0x00, 0x00,
};

static u8 hx8357_seq_undefined2[] = {
	0xeb, 0x40, 0x54, 0x26, 0xdb,
};

static u8 hx8357_seq_gamma[] = {
	HX8357_SET_GAMMA, 0x00, 0x15, 0x00, 0x22, 0x00,
	0x08, 0x77, 0x26, 0x77, 0x22, 0x04, 0x00,
};

static u8 hx8357_seq_address_mode[] = {
	HX8357_SET_ADDRESS_MODE, 0xc0,
};

static u8 hx8357_seq_pixel_format[] = {
	HX8357_SET_PIXEL_FORMAT,
	HX8357_SET_PIXEL_FORMAT_DPI_18BIT |
	HX8357_SET_PIXEL_FORMAT_DBI_18BIT,
};

static u8 hx8357_seq_column_address[] = {
	HX8357_SET_COLUMN_ADDRESS, 0x00, 0x00, 0x01, 0x3f,
};

static u8 hx8357_seq_page_address[] = {
	HX8357_SET_PAGE_ADDRESS, 0x00, 0x00, 0x01, 0xdf,
};

static u8 hx8357_seq_rgb[] = {
	HX8357_SET_RGB, 0x02,
};

static u8 hx8357_seq_display_mode[] = {
	HX8357_SET_DISPLAY_MODE,
	HX8357_SET_DISPLAY_MODE_RGB_THROUGH |
	HX8357_SET_DISPLAY_MODE_RGB_INTERFACE,
};

static u8 hx8369_seq_write_CABC_min_brightness[] = {
	HX8369_WRITE_CABC_MIN_BRIGHTNESS, 0x00,
};

static u8 hx8369_seq_write_CABC_control[] = {
	HX8369_WRITE_CABC_DISPLAY_VALUE, 0x24,
};

static u8 hx8369_seq_set_display_brightness[] = {
	HX8369_SET_DISPLAY_BRIGHTNESS, 0xFF,
};

static u8 hx8369_seq_write_CABC_control_setting[] = {
	HX8369_WRITE_CABC_BRIGHT_CTRL, 0x02,
};

static u8 hx8369_seq_extension_command[] = {
	HX8369_SET_EXTENSION_COMMAND, 0xff, 0x83, 0x69,
};

static u8 hx8369_seq_display_related[] = {
	HX8369_SET_DISPLAY_MODE, 0x00, 0x2b, 0x03, 0x03, 0x70, 0x00,
	0xff, 0x00, 0x00, 0x00, 0x00, 0x03, 0x03, 0x00,	0x01,
};

static u8 hx8369_seq_panel_waveform_cycle[] = {
	HX8369_SET_DISPLAY_WAVEFORM_CYC, 0x0a, 0x1d, 0x80, 0x06, 0x02,
};

static u8 hx8369_seq_set_address_mode[] = {
	HX8357_SET_ADDRESS_MODE, 0x00,
};

static u8 hx8369_seq_vcom[] = {
	HX8369_SET_VCOM, 0x3e, 0x3e,
};

static u8 hx8369_seq_gip[] = {
	HX8369_SET_GIP, 0x00, 0x01, 0x03, 0x25, 0x01, 0x02, 0x28, 0x70,
	0x11, 0x13, 0x00, 0x00, 0x40, 0x26, 0x51, 0x37, 0x00, 0x00, 0x71,
	0x35, 0x60, 0x24, 0x07, 0x0f, 0x04, 0x04,
};

static u8 hx8369_seq_power[] = {
	HX8369_SET_POWER, 0x01, 0x00, 0x34, 0x03, 0x00, 0x11, 0x11, 0x32,
	0x2f, 0x3f, 0x3f, 0x01, 0x3a, 0x01, 0xe6, 0xe6, 0xe6, 0xe6, 0xe6,
};

static u8 hx8369_seq_gamma_curve_related[] = {
	HX8369_SET_GAMMA_CURVE_RELATED, 0x00, 0x0d, 0x19, 0x2f, 0x3b, 0x3d,
	0x2e, 0x4a, 0x08, 0x0e, 0x0f, 0x14, 0x16, 0x14, 0x14, 0x14, 0x1e,
	0x00, 0x0d, 0x19, 0x2f, 0x3b, 0x3d, 0x2e, 0x4a, 0x08, 0x0e, 0x0f,
	0x14, 0x16, 0x14, 0x14, 0x14, 0x1e,
};

static int hx8357_spi_write_then_read(struct lcd_device *lcdev,
				u8 *txbuf, u16 txlen,
				u8 *rxbuf, u16 rxlen)
{
	struct hx8357_data *lcd = lcd_get_data(lcdev);
	struct spi_message msg;
	struct spi_transfer xfer[2];
	u16 *local_txbuf = NULL;
	int ret = 0;

	memset(xfer, 0, sizeof(xfer));
	spi_message_init(&msg);

	if (txlen) {
		int i;

		local_txbuf = kcalloc(txlen, sizeof(*local_txbuf), GFP_KERNEL);

		if (!local_txbuf)
			return -ENOMEM;

		for (i = 0; i < txlen; i++) {
			local_txbuf[i] = txbuf[i];
			if (i > 0)
				local_txbuf[i] |= 1 << 8;
		}

		xfer[0].len = 2 * txlen;
		xfer[0].bits_per_word = 9;
		xfer[0].tx_buf = local_txbuf;
		spi_message_add_tail(&xfer[0], &msg);
	}

	if (rxlen) {
		xfer[1].len = rxlen;
		xfer[1].bits_per_word = 8;
		xfer[1].rx_buf = rxbuf;
		spi_message_add_tail(&xfer[1], &msg);
	}

	ret = spi_sync(lcd->spi, &msg);
	if (ret < 0)
		dev_err(&lcdev->dev, "Couldn't send SPI data\n");

	if (txlen)
		kfree(local_txbuf);

	return ret;
}

static inline int hx8357_spi_write_array(struct lcd_device *lcdev,
					u8 *value, u8 len)
{
	return hx8357_spi_write_then_read(lcdev, value, len, NULL, 0);
}

static inline int hx8357_spi_write_byte(struct lcd_device *lcdev,
					u8 value)
{
	return hx8357_spi_write_then_read(lcdev, &value, 1, NULL, 0);
}

static int hx8357_enter_standby(struct lcd_device *lcdev)
{
	int ret;

	ret = hx8357_spi_write_byte(lcdev, HX8357_SET_DISPLAY_OFF);
	if (ret < 0)
		return ret;

	usleep_range(10000, 12000);

	ret = hx8357_spi_write_byte(lcdev, HX8357_ENTER_SLEEP_MODE);
	if (ret < 0)
		return ret;

	/*
	 * The controller needs 120ms when entering in sleep mode before we can
	 * send the command to go off sleep mode
	 */
	msleep(120);

	return 0;
}

static int hx8357_exit_standby(struct lcd_device *lcdev)
{
	int ret;

	ret = hx8357_spi_write_byte(lcdev, HX8357_EXIT_SLEEP_MODE);
	if (ret < 0)
		return ret;

	/*
	 * The controller needs 120ms when exiting from sleep mode before we
	 * can send the command to enter in sleep mode
	 */
	msleep(120);

	ret = hx8357_spi_write_byte(lcdev, HX8357_SET_DISPLAY_ON);
	if (ret < 0)
		return ret;

	return 0;
}

static void hx8357_lcd_reset(struct lcd_device *lcdev)
{
	struct hx8357_data *lcd = lcd_get_data(lcdev);

	/* Reset the screen */
	gpiod_set_value(lcd->reset, 0);
	usleep_range(10000, 12000);
	gpiod_set_value(lcd->reset, 1);
	usleep_range(10000, 12000);
	gpiod_set_value(lcd->reset, 0);

	/* The controller needs 120ms to recover from reset */
	msleep(120);
}

static int hx8357_lcd_init(struct lcd_device *lcdev)
{
	struct hx8357_data *lcd = lcd_get_data(lcdev);
	int ret;

	/*
	 * Set the interface selection pins to SPI mode, with three
	 * wires
	 */
	if (lcd->im_pins) {
		gpiod_set_value_cansleep(lcd->im_pins->desc[0], 1);
		gpiod_set_value_cansleep(lcd->im_pins->desc[1], 0);
		gpiod_set_value_cansleep(lcd->im_pins->desc[2], 1);
	}

	ret = hx8357_spi_write_array(lcdev, hx8357_seq_power,
				ARRAY_SIZE(hx8357_seq_power));
	if (ret < 0)
		return ret;

	ret = hx8357_spi_write_array(lcdev, hx8357_seq_vcom,
				ARRAY_SIZE(hx8357_seq_vcom));
	if (ret < 0)
		return ret;

	ret = hx8357_spi_write_array(lcdev, hx8357_seq_power_normal,
				ARRAY_SIZE(hx8357_seq_power_normal));
	if (ret < 0)
		return ret;

	ret = hx8357_spi_write_array(lcdev, hx8357_seq_panel_driving,
				ARRAY_SIZE(hx8357_seq_panel_driving));
	if (ret < 0)
		return ret;

	ret = hx8357_spi_write_array(lcdev, hx8357_seq_display_frame,
				ARRAY_SIZE(hx8357_seq_display_frame));
	if (ret < 0)
		return ret;

	ret = hx8357_spi_write_array(lcdev, hx8357_seq_panel_related,
				ARRAY_SIZE(hx8357_seq_panel_related));
	if (ret < 0)
		return ret;

	ret = hx8357_spi_write_array(lcdev, hx8357_seq_undefined1,
				ARRAY_SIZE(hx8357_seq_undefined1));
	if (ret < 0)
		return ret;

	ret = hx8357_spi_write_array(lcdev, hx8357_seq_undefined2,
				ARRAY_SIZE(hx8357_seq_undefined2));
	if (ret < 0)
		return ret;

	ret = hx8357_spi_write_array(lcdev, hx8357_seq_gamma,
				ARRAY_SIZE(hx8357_seq_gamma));
	if (ret < 0)
		return ret;

	ret = hx8357_spi_write_array(lcdev, hx8357_seq_address_mode,
				ARRAY_SIZE(hx8357_seq_address_mode));
	if (ret < 0)
		return ret;

	ret = hx8357_spi_write_array(lcdev, hx8357_seq_pixel_format,
				ARRAY_SIZE(hx8357_seq_pixel_format));
	if (ret < 0)
		return ret;

	ret = hx8357_spi_write_array(lcdev, hx8357_seq_column_address,
				ARRAY_SIZE(hx8357_seq_column_address));
	if (ret < 0)
		return ret;

	ret = hx8357_spi_write_array(lcdev, hx8357_seq_page_address,
				ARRAY_SIZE(hx8357_seq_page_address));
	if (ret < 0)
		return ret;

	ret = hx8357_spi_write_array(lcdev, hx8357_seq_rgb,
				ARRAY_SIZE(hx8357_seq_rgb));
	if (ret < 0)
		return ret;

	ret = hx8357_spi_write_array(lcdev, hx8357_seq_display_mode,
				ARRAY_SIZE(hx8357_seq_display_mode));
	if (ret < 0)
		return ret;

	ret = hx8357_spi_write_byte(lcdev, HX8357_EXIT_SLEEP_MODE);
	if (ret < 0)
		return ret;

	/*
	 * The controller needs 120ms to fully recover from exiting sleep mode
	 */
	msleep(120);

	ret = hx8357_spi_write_byte(lcdev, HX8357_SET_DISPLAY_ON);
	if (ret < 0)
		return ret;

	usleep_range(5000, 7000);

	ret = hx8357_spi_write_byte(lcdev, HX8357_WRITE_MEMORY_START);
	if (ret < 0)
		return ret;

	return 0;
}

static int hx8369_lcd_init(struct lcd_device *lcdev)
{
	int ret;

	ret = hx8357_spi_write_array(lcdev, hx8369_seq_extension_command,
				ARRAY_SIZE(hx8369_seq_extension_command));
	if (ret < 0)
		return ret;
	usleep_range(10000, 12000);

	ret = hx8357_spi_write_array(lcdev, hx8369_seq_display_related,
				ARRAY_SIZE(hx8369_seq_display_related));
	if (ret < 0)
		return ret;

	ret = hx8357_spi_write_array(lcdev, hx8369_seq_panel_waveform_cycle,
				ARRAY_SIZE(hx8369_seq_panel_waveform_cycle));
	if (ret < 0)
		return ret;

	ret = hx8357_spi_write_array(lcdev, hx8369_seq_set_address_mode,
				ARRAY_SIZE(hx8369_seq_set_address_mode));
	if (ret < 0)
		return ret;

	ret = hx8357_spi_write_array(lcdev, hx8369_seq_vcom,
				ARRAY_SIZE(hx8369_seq_vcom));
	if (ret < 0)
		return ret;

	ret = hx8357_spi_write_array(lcdev, hx8369_seq_gip,
				ARRAY_SIZE(hx8369_seq_gip));
	if (ret < 0)
		return ret;

	ret = hx8357_spi_write_array(lcdev, hx8369_seq_power,
				ARRAY_SIZE(hx8369_seq_power));
	if (ret < 0)
		return ret;

	ret = hx8357_spi_write_byte(lcdev, HX8357_EXIT_SLEEP_MODE);
	if (ret < 0)
		return ret;

	/*
	 * The controller needs 120ms to fully recover from exiting sleep mode
	 */
	msleep(120);

	ret = hx8357_spi_write_array(lcdev, hx8369_seq_gamma_curve_related,
				ARRAY_SIZE(hx8369_seq_gamma_curve_related));
	if (ret < 0)
		return ret;

	ret = hx8357_spi_write_byte(lcdev, HX8357_EXIT_SLEEP_MODE);
	if (ret < 0)
		return ret;
	usleep_range(1000, 1200);

	ret = hx8357_spi_write_array(lcdev, hx8369_seq_write_CABC_control,
				ARRAY_SIZE(hx8369_seq_write_CABC_control));
	if (ret < 0)
		return ret;
	usleep_range(10000, 12000);

	ret = hx8357_spi_write_array(lcdev,
			hx8369_seq_write_CABC_control_setting,
			ARRAY_SIZE(hx8369_seq_write_CABC_control_setting));
	if (ret < 0)
		return ret;

	ret = hx8357_spi_write_array(lcdev,
			hx8369_seq_write_CABC_min_brightness,
			ARRAY_SIZE(hx8369_seq_write_CABC_min_brightness));
	if (ret < 0)
		return ret;
	usleep_range(10000, 12000);

	ret = hx8357_spi_write_array(lcdev, hx8369_seq_set_display_brightness,
				ARRAY_SIZE(hx8369_seq_set_display_brightness));
	if (ret < 0)
		return ret;

	ret = hx8357_spi_write_byte(lcdev, HX8357_SET_DISPLAY_ON);
	if (ret < 0)
		return ret;

	return 0;
}

#define POWER_IS_ON(pwr)	((pwr) <= FB_BLANK_NORMAL)

static int hx8357_set_power(struct lcd_device *lcdev, int power)
{
	struct hx8357_data *lcd = lcd_get_data(lcdev);
	int ret = 0;

	if (POWER_IS_ON(power) && !POWER_IS_ON(lcd->state))
		ret = hx8357_exit_standby(lcdev);
	else if (!POWER_IS_ON(power) && POWER_IS_ON(lcd->state))
		ret = hx8357_enter_standby(lcdev);

	if (ret == 0)
		lcd->state = power;
	else
		dev_warn(&lcdev->dev, "failed to set power mode %d\n", power);

	return ret;
}

static int hx8357_get_power(struct lcd_device *lcdev)
{
	struct hx8357_data *lcd = lcd_get_data(lcdev);

	return lcd->state;
}

static const struct lcd_ops hx8357_ops = {
	.set_power	= hx8357_set_power,
	.get_power	= hx8357_get_power,
};

typedef int (*hx8357_init_fn)(struct lcd_device *);

static int hx8357_probe(struct spi_device *spi)
{
	struct device *dev = &spi->dev;
	struct lcd_device *lcdev;
	struct hx8357_data *lcd;
	hx8357_init_fn init_fn;
	int i, ret;

	lcd = devm_kzalloc(dev, sizeof(*lcd), GFP_KERNEL);
	if (!lcd)
		return -ENOMEM;

	ret = spi_setup(spi);
	if (ret < 0)
		return dev_err_probe(dev, ret, "SPI setup failed.\n");

	lcd->spi = spi;

	init_fn = device_get_match_data(dev);
	if (!init_fn)
		return -EINVAL;

	lcd->reset = devm_gpiod_get(dev, "reset", GPIOD_OUT_LOW);
	if (IS_ERR(lcd->reset))
		return dev_err_probe(dev, PTR_ERR(lcd->reset), "failed to request reset GPIO\n");
	gpiod_set_consumer_name(lcd->reset, "hx8357-reset");

	lcd->im_pins = devm_gpiod_get_array_optional(dev, "im", GPIOD_OUT_LOW);
	if (IS_ERR(lcd->im_pins))
		return dev_err_probe(dev, PTR_ERR(lcd->im_pins), "failed to request im GPIOs\n");
	if (lcd->im_pins) {
		if (lcd->im_pins->ndescs < HX8357_NUM_IM_PINS)
			return dev_err_probe(dev, -EINVAL, "not enough im GPIOs\n");

		for (i = 0; i < HX8357_NUM_IM_PINS; i++)
			gpiod_set_consumer_name(lcd->im_pins->desc[i], "im_pins");
	}

	lcdev = devm_lcd_device_register(dev, "mxsfb", dev, lcd, &hx8357_ops);
	if (IS_ERR(lcdev)) {
		ret = PTR_ERR(lcdev);
		return ret;
	}
	spi_set_drvdata(spi, lcdev);

	hx8357_lcd_reset(lcdev);

	ret = init_fn(lcdev);
	if (ret)
		return dev_err_probe(dev, ret, "Couldn't initialize panel\n");

	dev_info(dev, "Panel probed\n");

	return 0;
}

static const struct of_device_id hx8357_dt_ids[] = {
	{
		.compatible = "himax,hx8357",
		.data = hx8357_lcd_init,
	},
	{
		.compatible = "himax,hx8369",
		.data = hx8369_lcd_init,
	},
	{}
};
MODULE_DEVICE_TABLE(of, hx8357_dt_ids);

static struct spi_driver hx8357_driver = {
	.probe  = hx8357_probe,
	.driver = {
		.name = "hx8357",
		.of_match_table = hx8357_dt_ids,
	},
};

module_spi_driver(hx8357_driver);

MODULE_AUTHOR("Maxime Ripard <maxime.ripard@free-electrons.com>");
MODULE_DESCRIPTION("Himax HX-8357 LCD Driver");
MODULE_LICENSE("GPL");
