// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Common library for ADIS16XXX devices
 *
 * Copyright 2012 Analog Devices Inc.
 *   Author: Lars-Peter Clausen <lars@metafoo.de>
 */

#include <linux/delay.h>
#include <linux/mutex.h>
#include <linux/device.h>
#include <linux/kernel.h>
#include <linux/spi/spi.h>
#include <linux/slab.h>
#include <linux/sysfs.h>
#include <linux/module.h>
#include <asm/unaligned.h>

#include <linux/iio/iio.h>
#include <linux/iio/sysfs.h>
#include <linux/iio/buffer.h>
#include <linux/iio/imu/adis.h>

#define ADIS_MSC_CTRL_DATA_RDY_EN	BIT(2)
#define ADIS_MSC_CTRL_DATA_RDY_POL_HIGH	BIT(1)
#define ADIS_MSC_CTRL_DATA_RDY_DIO2	BIT(0)
#define ADIS_GLOB_CMD_SW_RESET		BIT(7)

int adis_write_reg(struct adis *adis, unsigned int reg,
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
			.delay_usecs = adis->data->write_delay,
		}, {
			.tx_buf = adis->tx + 2,
			.bits_per_word = 8,
			.len = 2,
			.cs_change = 1,
			.delay_usecs = adis->data->write_delay,
		}, {
			.tx_buf = adis->tx + 4,
			.bits_per_word = 8,
			.len = 2,
			.cs_change = 1,
			.delay_usecs = adis->data->write_delay,
		}, {
			.tx_buf = adis->tx + 6,
			.bits_per_word = 8,
			.len = 2,
			.delay_usecs = adis->data->write_delay,
		}, {
			.tx_buf = adis->tx + 8,
			.bits_per_word = 8,
			.len = 2,
			.delay_usecs = adis->data->write_delay,
		},
	};

	mutex_lock(&adis->txrx_lock);

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
		/* fall through */
	case 2:
		adis->tx[4] = ADIS_WRITE_REG(reg + 1);
		adis->tx[5] = (value >> 8) & 0xff;
		/* fall through */
	case 1:
		adis->tx[2] = ADIS_WRITE_REG(reg);
		adis->tx[3] = value & 0xff;
		break;
	default:
		ret = -EINVAL;
		goto out_unlock;
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

out_unlock:
	mutex_unlock(&adis->txrx_lock);

	return ret;
}
EXPORT_SYMBOL_GPL(adis_write_reg);

/**
 * adis_read_reg() - read 2 bytes from a 16-bit register
 * @adis: The adis device
 * @reg: The address of the lower of the two registers
 * @val: The value read back from the device
 */
int adis_read_reg(struct adis *adis, unsigned int reg,
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
			.delay_usecs = adis->data->write_delay,
		}, {
			.tx_buf = adis->tx + 2,
			.bits_per_word = 8,
			.len = 2,
			.cs_change = 1,
			.delay_usecs = adis->data->read_delay,
		}, {
			.tx_buf = adis->tx + 4,
			.rx_buf = adis->rx,
			.bits_per_word = 8,
			.len = 2,
			.cs_change = 1,
			.delay_usecs = adis->data->read_delay,
		}, {
			.rx_buf = adis->rx + 2,
			.bits_per_word = 8,
			.len = 2,
			.delay_usecs = adis->data->read_delay,
		},
	};

	mutex_lock(&adis->txrx_lock);
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
		/* fall through */
	case 2:
		adis->tx[4] = ADIS_READ_REG(reg);
		adis->tx[5] = 0;
		spi_message_add_tail(&xfers[2], &msg);
		spi_message_add_tail(&xfers[3], &msg);
		break;
	default:
		ret = -EINVAL;
		goto out_unlock;
	}

	ret = spi_sync(adis->spi, &msg);
	if (ret) {
		dev_err(&adis->spi->dev, "Failed to read register 0x%02X: %d\n",
				reg, ret);
		goto out_unlock;
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

out_unlock:
	mutex_unlock(&adis->txrx_lock);

	return ret;
}
EXPORT_SYMBOL_GPL(adis_read_reg);

#ifdef CONFIG_DEBUG_FS

int adis_debugfs_reg_access(struct iio_dev *indio_dev,
	unsigned int reg, unsigned int writeval, unsigned int *readval)
{
	struct adis *adis = iio_device_get_drvdata(indio_dev);

	if (readval) {
		uint16_t val16;
		int ret;

		ret = adis_read_reg_16(adis, reg, &val16);
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

	if (adis->data->enable_irq)
		return adis->data->enable_irq(adis, enable);

	ret = adis_read_reg_16(adis, adis->data->msc_ctrl_reg, &msc);
	if (ret)
		goto error_ret;

	msc |= ADIS_MSC_CTRL_DATA_RDY_POL_HIGH;
	msc &= ~ADIS_MSC_CTRL_DATA_RDY_DIO2;
	if (enable)
		msc |= ADIS_MSC_CTRL_DATA_RDY_EN;
	else
		msc &= ~ADIS_MSC_CTRL_DATA_RDY_EN;

	ret = adis_write_reg_16(adis, adis->data->msc_ctrl_reg, msc);

error_ret:
	return ret;
}
EXPORT_SYMBOL(adis_enable_irq);

/**
 * adis_check_status() - Check the device for error conditions
 * @adis: The adis device
 *
 * Returns 0 on success, a negative error code otherwise
 */
int adis_check_status(struct adis *adis)
{
	uint16_t status;
	int ret;
	int i;

	ret = adis_read_reg_16(adis, adis->data->diag_stat_reg, &status);
	if (ret < 0)
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
EXPORT_SYMBOL_GPL(adis_check_status);

/**
 * adis_reset() - Reset the device
 * @adis: The adis device
 *
 * Returns 0 on success, a negative error code otherwise
 */
int adis_reset(struct adis *adis)
{
	int ret;

	ret = adis_write_reg_8(adis, adis->data->glob_cmd_reg,
			ADIS_GLOB_CMD_SW_RESET);
	if (ret)
		dev_err(&adis->spi->dev, "Failed to reset device: %d\n", ret);

	return ret;
}
EXPORT_SYMBOL_GPL(adis_reset);

static int adis_self_test(struct adis *adis)
{
	int ret;

	ret = adis_write_reg_16(adis, adis->data->msc_ctrl_reg,
			adis->data->self_test_mask);
	if (ret) {
		dev_err(&adis->spi->dev, "Failed to initiate self test: %d\n",
			ret);
		return ret;
	}

	msleep(adis->data->startup_delay);

	ret = adis_check_status(adis);

	if (adis->data->self_test_no_autoclear)
		adis_write_reg_16(adis, adis->data->msc_ctrl_reg, 0x00);

	return ret;
}

/**
 * adis_inital_startup() - Performs device self-test
 * @adis: The adis device
 *
 * Returns 0 if the device is operational, a negative error code otherwise.
 *
 * This function should be called early on in the device initialization sequence
 * to ensure that the device is in a sane and known state and that it is usable.
 */
int adis_initial_startup(struct adis *adis)
{
	int ret;

	ret = adis_self_test(adis);
	if (ret) {
		dev_err(&adis->spi->dev, "Self-test failed, trying reset.\n");
		adis_reset(adis);
		msleep(adis->data->startup_delay);
		ret = adis_self_test(adis);
		if (ret) {
			dev_err(&adis->spi->dev, "Second self-test failed, giving up.\n");
			return ret;
		}
	}

	return 0;
}
EXPORT_SYMBOL_GPL(adis_initial_startup);

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

	mutex_lock(&indio_dev->mlock);

	ret = adis_read_reg(adis, chan->address, &uval,
			chan->scan_type.storagebits / 8);
	if (ret)
		goto err_unlock;

	if (uval & error_mask) {
		ret = adis_check_status(adis);
		if (ret)
			goto err_unlock;
	}

	if (chan->scan_type.sign == 's')
		*val = sign_extend32(uval, chan->scan_type.realbits - 1);
	else
		*val = uval & ((1 << chan->scan_type.realbits) - 1);

	ret = IIO_VAL_INT;
err_unlock:
	mutex_unlock(&indio_dev->mlock);
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
	mutex_init(&adis->txrx_lock);
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

	return adis_enable_irq(adis, false);
}
EXPORT_SYMBOL_GPL(adis_init);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Lars-Peter Clausen <lars@metafoo.de>");
MODULE_DESCRIPTION("Common library code for ADIS16XXX devices");
