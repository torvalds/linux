// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Common library for ADIS16XXX devices
 *
 * Copyright 2012 Analog Devices Inc.
 *   Author: Lars-Peter Clausen <lars@metafoo.de>
 */

#include <linux/delay.h>
#include <linux/gpio/consumer.h>
#include <linux/mutex.h>
#include <linux/device.h>
#include <linux/kernel.h>
#include <linux/spi/spi.h>
#include <linux/module.h>
#include <asm/unaligned.h>

#include <linux/iio/iio.h>
#include <linux/iio/imu/adis.h>

#define ADIS_MSC_CTRL_DATA_RDY_EN	BIT(2)
#define ADIS_MSC_CTRL_DATA_RDY_POL_HIGH	BIT(1)
#define ADIS_MSC_CTRL_DATA_RDY_DIO2	BIT(0)
#define ADIS_GLOB_CMD_SW_RESET		BIT(7)

/**
 * __adis_write_reg() - write N bytes to register (unlocked version)
 * @adis: The adis device
 * @reg: The address of the lower of the two registers
 * @value: The value to write to device (up to 4 bytes)
 * @size: The size of the @value (in bytes)
 */
int __adis_write_reg(struct adis *adis, unsigned int reg,
	unsigned int value, unsigned int size)
{
	unsigned int page = reg / ADIS_PAGE_SIZE;
	int ret, i;
	struct spi_message msg;
	struct spi_transfer xfers[] = {
		{
			.tx_buf = adis->tx,
			.bits_per_word = 8,
			.len = 2,
			.cs_change = 1,
			.delay.value = adis->data->write_delay,
			.delay.unit = SPI_DELAY_UNIT_USECS,
			.cs_change_delay.value = adis->data->cs_change_delay,
			.cs_change_delay.unit = SPI_DELAY_UNIT_USECS,
		}, {
			.tx_buf = adis->tx + 2,
			.bits_per_word = 8,
			.len = 2,
			.cs_change = 1,
			.delay.value = adis->data->write_delay,
			.delay.unit = SPI_DELAY_UNIT_USECS,
			.cs_change_delay.value = adis->data->cs_change_delay,
			.cs_change_delay.unit = SPI_DELAY_UNIT_USECS,
		}, {
			.tx_buf = adis->tx + 4,
			.bits_per_word = 8,
			.len = 2,
			.cs_change = 1,
			.delay.value = adis->data->write_delay,
			.delay.unit = SPI_DELAY_UNIT_USECS,
			.cs_change_delay.value = adis->data->cs_change_delay,
			.cs_change_delay.unit = SPI_DELAY_UNIT_USECS,
		}, {
			.tx_buf = adis->tx + 6,
			.bits_per_word = 8,
			.len = 2,
			.delay.value = adis->data->write_delay,
			.delay.unit = SPI_DELAY_UNIT_USECS,
		}, {
			.tx_buf = adis->tx + 8,
			.bits_per_word = 8,
			.len = 2,
			.delay.value = adis->data->write_delay,
			.delay.unit = SPI_DELAY_UNIT_USECS,
		},
	};

	spi_message_init(&msg);

	if (adis->current_page != page) {
		adis->tx[0] = ADIS_WRITE_REG(ADIS_REG_PAGE_ID);
		adis->tx[1] = page;
		spi_message_add_tail(&xfers[0], &msg);
	}

	switch (size) {
	case 4:
		adis->tx[8] = ADIS_WRITE_REG(reg + 3);
		adis->tx[9] = (value >> 24) & 0xff;
		adis->tx[6] = ADIS_WRITE_REG(reg + 2);
		adis->tx[7] = (value >> 16) & 0xff;
		fallthrough;
	case 2:
		adis->tx[4] = ADIS_WRITE_REG(reg + 1);
		adis->tx[5] = (value >> 8) & 0xff;
		fallthrough;
	case 1:
		adis->tx[2] = ADIS_WRITE_REG(reg);
		adis->tx[3] = value & 0xff;
		break;
	default:
		return -EINVAL;
	}

	xfers[size].cs_change = 0;

	for (i = 1; i <= size; i++)
		spi_message_add_tail(&xfers[i], &msg);

	ret = spi_sync(adis->spi, &msg);
	if (ret) {
		dev_err(&adis->spi->dev, "Failed to write register 0x%02X: %d\n",
				reg, ret);
	} else {
		adis->current_page = page;
	}

	return ret;
}
EXPORT_SYMBOL_GPL(__adis_write_reg);

/**
 * __adis_read_reg() - read N bytes from register (unlocked version)
 * @adis: The adis device
 * @reg: The address of the lower of the two registers
 * @val: The value read back from the device
 * @size: The size of the @val buffer
 */
int __adis_read_reg(struct adis *adis, unsigned int reg,
	unsigned int *val, unsigned int size)
{
	unsigned int page = reg / ADIS_PAGE_SIZE;
	struct spi_message msg;
	int ret;
	struct spi_transfer xfers[] = {
		{
			.tx_buf = adis->tx,
			.bits_per_word = 8,
			.len = 2,
			.cs_change = 1,
			.delay.value = adis->data->write_delay,
			.delay.unit = SPI_DELAY_UNIT_USECS,
			.cs_change_delay.value = adis->data->cs_change_delay,
			.cs_change_delay.unit = SPI_DELAY_UNIT_USECS,
		}, {
			.tx_buf = adis->tx + 2,
			.bits_per_word = 8,
			.len = 2,
			.cs_change = 1,
			.delay.value = adis->data->read_delay,
			.delay.unit = SPI_DELAY_UNIT_USECS,
			.cs_change_delay.value = adis->data->cs_change_delay,
			.cs_change_delay.unit = SPI_DELAY_UNIT_USECS,
		}, {
			.tx_buf = adis->tx + 4,
			.rx_buf = adis->rx,
			.bits_per_word = 8,
			.len = 2,
			.cs_change = 1,
			.delay.value = adis->data->read_delay,
			.delay.unit = SPI_DELAY_UNIT_USECS,
			.cs_change_delay.value = adis->data->cs_change_delay,
			.cs_change_delay.unit = SPI_DELAY_UNIT_USECS,
		}, {
			.rx_buf = adis->rx + 2,
			.bits_per_word = 8,
			.len = 2,
			.delay.value = adis->data->read_delay,
			.delay.unit = SPI_DELAY_UNIT_USECS,
		},
	};

	spi_message_init(&msg);

	if (adis->current_page != page) {
		adis->tx[0] = ADIS_WRITE_REG(ADIS_REG_PAGE_ID);
		adis->tx[1] = page;
		spi_message_add_tail(&xfers[0], &msg);
	}

	switch (size) {
	case 4:
		adis->tx[2] = ADIS_READ_REG(reg + 2);
		adis->tx[3] = 0;
		spi_message_add_tail(&xfers[1], &msg);
		fallthrough;
	case 2:
		adis->tx[4] = ADIS_READ_REG(reg);
		adis->tx[5] = 0;
		spi_message_add_tail(&xfers[2], &msg);
		spi_message_add_tail(&xfers[3], &msg);
		break;
	default:
		return -EINVAL;
	}

	ret = spi_sync(adis->spi, &msg);
	if (ret) {
		dev_err(&adis->spi->dev, "Failed to read register 0x%02X: %d\n",
				reg, ret);
		return ret;
	} else {
		adis->current_page = page;
	}

	switch (size) {
	case 4:
		*val = get_unaligned_be32(adis->rx);
		break;
	case 2:
		*val = get_unaligned_be16(adis->rx + 2);
		break;
	}

	return ret;
}
EXPORT_SYMBOL_GPL(__adis_read_reg);
/**
 * __adis_update_bits_base() - ADIS Update bits function - Unlocked version
 * @adis: The adis device
 * @reg: The address of the lower of the two registers
 * @mask: Bitmask to change
 * @val: Value to be written
 * @size: Size of the register to update
 *
 * Updates the desired bits of @reg in accordance with @mask and @val.
 */
int __adis_update_bits_base(struct adis *adis, unsigned int reg, const u32 mask,
			    const u32 val, u8 size)
{
	int ret;
	u32 __val;

	ret = __adis_read_reg(adis, reg, &__val, size);
	if (ret)
		return ret;

	__val = (__val & ~mask) | (val & mask);

	return __adis_write_reg(adis, reg, __val, size);
}
EXPORT_SYMBOL_GPL(__adis_update_bits_base);

#ifdef CONFIG_DEBUG_FS

int adis_debugfs_reg_access(struct iio_dev *indio_dev,
	unsigned int reg, unsigned int writeval, unsigned int *readval)
{
	struct adis *adis = iio_device_get_drvdata(indio_dev);

	if (readval) {
		uint16_t val16;
		int ret;

		ret = adis_read_reg_16(adis, reg, &val16);
		if (ret == 0)
			*readval = val16;

		return ret;
	} else {
		return adis_write_reg_16(adis, reg, writeval);
	}
}
EXPORT_SYMBOL(adis_debugfs_reg_access);

#endif

/**
 * adis_enable_irq() - Enable or disable data ready IRQ
 * @adis: The adis device
 * @enable: Whether to enable the IRQ
 *
 * Returns 0 on success, negative error code otherwise
 */
int adis_enable_irq(struct adis *adis, bool enable)
{
	int ret = 0;
	uint16_t msc;

	mutex_lock(&adis->state_lock);

	if (adis->data->enable_irq) {
		ret = adis->data->enable_irq(adis, enable);
		goto out_unlock;
	}

	ret = __adis_read_reg_16(adis, adis->data->msc_ctrl_reg, &msc);
	if (ret)
		goto out_unlock;

	msc |= ADIS_MSC_CTRL_DATA_RDY_POL_HIGH;
	msc &= ~ADIS_MSC_CTRL_DATA_RDY_DIO2;
	if (enable)
		msc |= ADIS_MSC_CTRL_DATA_RDY_EN;
	else
		msc &= ~ADIS_MSC_CTRL_DATA_RDY_EN;

	ret = __adis_write_reg_16(adis, adis->data->msc_ctrl_reg, msc);

out_unlock:
	mutex_unlock(&adis->state_lock);
	return ret;
}
EXPORT_SYMBOL(adis_enable_irq);

/**
 * __adis_check_status() - Check the device for error conditions (unlocked)
 * @adis: The adis device
 *
 * Returns 0 on success, a negative error code otherwise
 */
int __adis_check_status(struct adis *adis)
{
	uint16_t status;
	int ret;
	int i;

	ret = __adis_read_reg_16(adis, adis->data->diag_stat_reg, &status);
	if (ret)
		return ret;

	status &= adis->data->status_error_mask;

	if (status == 0)
		return 0;

	for (i = 0; i < 16; ++i) {
		if (status & BIT(i)) {
			dev_err(&adis->spi->dev, "%s.\n",
				adis->data->status_error_msgs[i]);
		}
	}

	return -EIO;
}
EXPORT_SYMBOL_GPL(__adis_check_status);

/**
 * __adis_reset() - Reset the device (unlocked version)
 * @adis: The adis device
 *
 * Returns 0 on success, a negative error code otherwise
 */
int __adis_reset(struct adis *adis)
{
	int ret;
	const struct adis_timeout *timeouts = adis->data->timeouts;

	ret = __adis_write_reg_8(adis, adis->data->glob_cmd_reg,
			ADIS_GLOB_CMD_SW_RESET);
	if (ret) {
		dev_err(&adis->spi->dev, "Failed to reset device: %d\n", ret);
		return ret;
	}

	msleep(timeouts->sw_reset_ms);

	return 0;
}
EXPORT_SYMBOL_GPL(__adis_reset);

static int adis_self_test(struct adis *adis)
{
	int ret;
	const struct adis_timeout *timeouts = adis->data->timeouts;

	ret = __adis_write_reg_16(adis, adis->data->self_test_reg,
				  adis->data->self_test_mask);
	if (ret) {
		dev_err(&adis->spi->dev, "Failed to initiate self test: %d\n",
			ret);
		return ret;
	}

	msleep(timeouts->self_test_ms);

	ret = __adis_check_status(adis);

	if (adis->data->self_test_no_autoclear)
		__adis_write_reg_16(adis, adis->data->self_test_reg, 0x00);

	return ret;
}

/**
 * __adis_initial_startup() - Device initial setup
 * @adis: The adis device
 *
 * The function performs a HW reset via a reset pin that should be specified
 * via GPIOLIB. If no pin is configured a SW reset will be performed.
 * The RST pin for the ADIS devices should be configured as ACTIVE_LOW.
 *
 * After the self-test operation is performed, the function will also check
 * that the product ID is as expected. This assumes that drivers providing
 * 'prod_id_reg' will also provide the 'prod_id'.
 *
 * Returns 0 if the device is operational, a negative error code otherwise.
 *
 * This function should be called early on in the device initialization sequence
 * to ensure that the device is in a sane and known state and that it is usable.
 */
int __adis_initial_startup(struct adis *adis)
{
	const struct adis_timeout *timeouts = adis->data->timeouts;
	struct gpio_desc *gpio;
	uint16_t prod_id;
	int ret;

	/* check if the device has rst pin low */
	gpio = devm_gpiod_get_optional(&adis->spi->dev, "reset", GPIOD_OUT_HIGH);
	if (IS_ERR(gpio))
		return PTR_ERR(gpio);

	if (gpio) {
		msleep(10);
		/* bring device out of reset */
		gpiod_set_value_cansleep(gpio, 0);
		msleep(timeouts->reset_ms);
	} else {
		ret = __adis_reset(adis);
		if (ret)
			return ret;
	}

	ret = adis_self_test(adis);
	if (ret)
		return ret;

	adis_enable_irq(adis, false);

	if (!adis->data->prod_id_reg)
		return 0;

	ret = adis_read_reg_16(adis, adis->data->prod_id_reg, &prod_id);
	if (ret)
		return ret;

	if (prod_id != adis->data->prod_id)
		dev_warn(&adis->spi->dev,
			 "Device ID(%u) and product ID(%u) do not match.\n",
			 adis->data->prod_id, prod_id);

	return 0;
}
EXPORT_SYMBOL_GPL(__adis_initial_startup);

/**
 * adis_single_conversion() - Performs a single sample conversion
 * @indio_dev: The IIO device
 * @chan: The IIO channel
 * @error_mask: Mask for the error bit
 * @val: Result of the conversion
 *
 * Returns IIO_VAL_INT on success, a negative error code otherwise.
 *
 * The function performs a single conversion on a given channel and post
 * processes the value accordingly to the channel spec. If a error_mask is given
 * the function will check if the mask is set in the returned raw value. If it
 * is set the function will perform a self-check. If the device does not report
 * a error bit in the channels raw value set error_mask to 0.
 */
int adis_single_conversion(struct iio_dev *indio_dev,
	const struct iio_chan_spec *chan, unsigned int error_mask, int *val)
{
	struct adis *adis = iio_device_get_drvdata(indio_dev);
	unsigned int uval;
	int ret;

	mutex_lock(&adis->state_lock);

	ret = __adis_read_reg(adis, chan->address, &uval,
			chan->scan_type.storagebits / 8);
	if (ret)
		goto err_unlock;

	if (uval & error_mask) {
		ret = __adis_check_status(adis);
		if (ret)
			goto err_unlock;
	}

	if (chan->scan_type.sign == 's')
		*val = sign_extend32(uval, chan->scan_type.realbits - 1);
	else
		*val = uval & ((1 << chan->scan_type.realbits) - 1);

	ret = IIO_VAL_INT;
err_unlock:
	mutex_unlock(&adis->state_lock);
	return ret;
}
EXPORT_SYMBOL_GPL(adis_single_conversion);

/**
 * adis_init() - Initialize adis device structure
 * @adis:	The adis device
 * @indio_dev:	The iio device
 * @spi:	The spi device
 * @data:	Chip specific data
 *
 * Returns 0 on success, a negative error code otherwise.
 *
 * This function must be called, before any other adis helper function may be
 * called.
 */
int adis_init(struct adis *adis, struct iio_dev *indio_dev,
	struct spi_device *spi, const struct adis_data *data)
{
	if (!data || !data->timeouts) {
		dev_err(&spi->dev, "No config data or timeouts not defined!\n");
		return -EINVAL;
	}

	mutex_init(&adis->state_lock);
	adis->spi = spi;
	adis->data = data;
	iio_device_set_drvdata(indio_dev, adis);

	if (data->has_paging) {
		/* Need to set the page before first read/write */
		adis->current_page = -1;
	} else {
		/* Page will always be 0 */
		adis->current_page = 0;
	}

	return 0;
}
EXPORT_SYMBOL_GPL(adis_init);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Lars-Peter Clausen <lars@metafoo.de>");
MODULE_DESCRIPTION("Common library code for ADIS16XXX devices");
