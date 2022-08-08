// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright (C) 2017-2020 Jacopo Mondi
 * Copyright (C) 2017-2020 Kieran Bingham
 * Copyright (C) 2017-2020 Laurent Pinchart
 * Copyright (C) 2017-2020 Niklas Söderlund
 * Copyright (C) 2016 Renesas Electronics Corporation
 * Copyright (C) 2015 Cogent Embedded, Inc.
 *
 * This file exports functions to control the Maxim MAX9271 GMSL serializer
 * chip. This is not a self-contained driver, as MAX9271 is usually embedded in
 * camera modules with at least one image sensor and optional additional
 * components, such as uController units or ISPs/DSPs.
 *
 * Drivers for the camera modules (i.e. rdacm20/21) are expected to use
 * functions exported from this library driver to maximize code re-use.
 */

#include <linux/delay.h>
#include <linux/i2c.h>
#include <linux/module.h>

#include "max9271.h"

static int max9271_read(struct max9271_device *dev, u8 reg)
{
	int ret;

	dev_dbg(&dev->client->dev, "%s(0x%02x)\n", __func__, reg);

	ret = i2c_smbus_read_byte_data(dev->client, reg);
	if (ret < 0)
		dev_dbg(&dev->client->dev,
			"%s: register 0x%02x read failed (%d)\n",
			__func__, reg, ret);

	return ret;
}

static int max9271_write(struct max9271_device *dev, u8 reg, u8 val)
{
	int ret;

	dev_dbg(&dev->client->dev, "%s(0x%02x, 0x%02x)\n", __func__, reg, val);

	ret = i2c_smbus_write_byte_data(dev->client, reg, val);
	if (ret < 0)
		dev_err(&dev->client->dev,
			"%s: register 0x%02x write failed (%d)\n",
			__func__, reg, ret);

	return ret;
}

/*
 * max9271_pclk_detect() - Detect valid pixel clock from image sensor
 *
 * Wait up to 10ms for a valid pixel clock.
 *
 * Returns 0 for success, < 0 for pixel clock not properly detected
 */
static int max9271_pclk_detect(struct max9271_device *dev)
{
	unsigned int i;
	int ret;

	for (i = 0; i < 100; i++) {
		ret = max9271_read(dev, 0x15);
		if (ret < 0)
			return ret;

		if (ret & MAX9271_PCLKDET)
			return 0;

		usleep_range(50, 100);
	}

	dev_err(&dev->client->dev, "Unable to detect valid pixel clock\n");

	return -EIO;
}

int max9271_set_serial_link(struct max9271_device *dev, bool enable)
{
	int ret;
	u8 val = MAX9271_REVCCEN | MAX9271_FWDCCEN;

	if (enable) {
		ret = max9271_pclk_detect(dev);
		if (ret)
			return ret;

		val |= MAX9271_SEREN;
	} else {
		val |= MAX9271_CLINKEN;
	}

	/*
	 * The serializer temporarily disables the reverse control channel for
	 * 350µs after starting/stopping the forward serial link, but the
	 * deserializer synchronization time isn't clearly documented.
	 *
	 * According to the serializer datasheet we should wait 3ms, while
	 * according to the deserializer datasheet we should wait 5ms.
	 *
	 * Short delays here appear to show bit-errors in the writes following.
	 * Therefore a conservative delay seems best here.
	 */
	max9271_write(dev, 0x04, val);
	usleep_range(5000, 8000);

	return 0;
}
EXPORT_SYMBOL_GPL(max9271_set_serial_link);

int max9271_configure_i2c(struct max9271_device *dev, u8 i2c_config)
{
	int ret;

	ret = max9271_write(dev, 0x0d, i2c_config);
	if (ret)
		return ret;

	/* The delay required after an I2C bus configuration change is not
	 * characterized in the serializer manual. Sleep up to 5msec to
	 * stay safe.
	 */
	usleep_range(3500, 5000);

	return 0;
}
EXPORT_SYMBOL_GPL(max9271_configure_i2c);

int max9271_set_high_threshold(struct max9271_device *dev, bool enable)
{
	int ret;

	ret = max9271_read(dev, 0x08);
	if (ret < 0)
		return ret;

	/*
	 * Enable or disable reverse channel high threshold to increase
	 * immunity to power supply noise.
	 */
	max9271_write(dev, 0x08, enable ? ret | BIT(0) : ret & ~BIT(0));
	usleep_range(2000, 2500);

	return 0;
}
EXPORT_SYMBOL_GPL(max9271_set_high_threshold);

int max9271_configure_gmsl_link(struct max9271_device *dev)
{
	/*
	 * Configure the GMSL link:
	 *
	 * - Double input mode, high data rate, 24-bit mode
	 * - Latch input data on PCLKIN rising edge
	 * - Enable HS/VS encoding
	 * - 1-bit parity error detection
	 *
	 * TODO: Make the GMSL link configuration parametric.
	 */
	max9271_write(dev, 0x07, MAX9271_DBL | MAX9271_HVEN |
		      MAX9271_EDC_1BIT_PARITY);
	usleep_range(5000, 8000);

	/*
	 * Adjust spread spectrum to +4% and auto-detect pixel clock
	 * and serial link rate.
	 */
	max9271_write(dev, 0x02, MAX9271_SPREAD_SPECT_4 | MAX9271_R02_RES |
		      MAX9271_PCLK_AUTODETECT | MAX9271_SERIAL_AUTODETECT);
	usleep_range(5000, 8000);

	return 0;
}
EXPORT_SYMBOL_GPL(max9271_configure_gmsl_link);

int max9271_set_gpios(struct max9271_device *dev, u8 gpio_mask)
{
	int ret;

	ret = max9271_read(dev, 0x0f);
	if (ret < 0)
		return 0;

	ret |= gpio_mask;
	ret = max9271_write(dev, 0x0f, ret);
	if (ret < 0) {
		dev_err(&dev->client->dev, "Failed to set gpio (%d)\n", ret);
		return ret;
	}

	usleep_range(3500, 5000);

	return 0;
}
EXPORT_SYMBOL_GPL(max9271_set_gpios);

int max9271_clear_gpios(struct max9271_device *dev, u8 gpio_mask)
{
	int ret;

	ret = max9271_read(dev, 0x0f);
	if (ret < 0)
		return 0;

	ret &= ~gpio_mask;
	ret = max9271_write(dev, 0x0f, ret);
	if (ret < 0) {
		dev_err(&dev->client->dev, "Failed to clear gpio (%d)\n", ret);
		return ret;
	}

	usleep_range(3500, 5000);

	return 0;
}
EXPORT_SYMBOL_GPL(max9271_clear_gpios);

int max9271_enable_gpios(struct max9271_device *dev, u8 gpio_mask)
{
	int ret;

	ret = max9271_read(dev, 0x0e);
	if (ret < 0)
		return 0;

	/* BIT(0) reserved: GPO is always enabled. */
	ret |= (gpio_mask & ~BIT(0));
	ret = max9271_write(dev, 0x0e, ret);
	if (ret < 0) {
		dev_err(&dev->client->dev, "Failed to enable gpio (%d)\n", ret);
		return ret;
	}

	usleep_range(3500, 5000);

	return 0;
}
EXPORT_SYMBOL_GPL(max9271_enable_gpios);

int max9271_disable_gpios(struct max9271_device *dev, u8 gpio_mask)
{
	int ret;

	ret = max9271_read(dev, 0x0e);
	if (ret < 0)
		return 0;

	/* BIT(0) reserved: GPO cannot be disabled */
	ret &= ~(gpio_mask | BIT(0));
	ret = max9271_write(dev, 0x0e, ret);
	if (ret < 0) {
		dev_err(&dev->client->dev, "Failed to disable gpio (%d)\n", ret);
		return ret;
	}

	usleep_range(3500, 5000);

	return 0;
}
EXPORT_SYMBOL_GPL(max9271_disable_gpios);

int max9271_verify_id(struct max9271_device *dev)
{
	int ret;

	ret = max9271_read(dev, 0x1e);
	if (ret < 0) {
		dev_err(&dev->client->dev, "MAX9271 ID read failed (%d)\n",
			ret);
		return ret;
	}

	if (ret != MAX9271_ID) {
		dev_err(&dev->client->dev, "MAX9271 ID mismatch (0x%02x)\n",
			ret);
		return -ENXIO;
	}

	return 0;
}
EXPORT_SYMBOL_GPL(max9271_verify_id);

int max9271_set_address(struct max9271_device *dev, u8 addr)
{
	int ret;

	ret = max9271_write(dev, 0x00, addr << 1);
	if (ret < 0) {
		dev_err(&dev->client->dev,
			"MAX9271 I2C address change failed (%d)\n", ret);
		return ret;
	}
	usleep_range(3500, 5000);

	return 0;
}
EXPORT_SYMBOL_GPL(max9271_set_address);

int max9271_set_deserializer_address(struct max9271_device *dev, u8 addr)
{
	int ret;

	ret = max9271_write(dev, 0x01, addr << 1);
	if (ret < 0) {
		dev_err(&dev->client->dev,
			"MAX9271 deserializer address set failed (%d)\n", ret);
		return ret;
	}
	usleep_range(3500, 5000);

	return 0;
}
EXPORT_SYMBOL_GPL(max9271_set_deserializer_address);

int max9271_set_translation(struct max9271_device *dev, u8 source, u8 dest)
{
	int ret;

	ret = max9271_write(dev, 0x09, source << 1);
	if (ret < 0) {
		dev_err(&dev->client->dev,
			"MAX9271 I2C translation setup failed (%d)\n", ret);
		return ret;
	}
	usleep_range(3500, 5000);

	ret = max9271_write(dev, 0x0a, dest << 1);
	if (ret < 0) {
		dev_err(&dev->client->dev,
			"MAX9271 I2C translation setup failed (%d)\n", ret);
		return ret;
	}
	usleep_range(3500, 5000);

	return 0;
}
EXPORT_SYMBOL_GPL(max9271_set_translation);

MODULE_DESCRIPTION("Maxim MAX9271 GMSL Serializer");
MODULE_AUTHOR("Jacopo Mondi");
MODULE_LICENSE("GPL v2");
