// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * NXP SC18IS602/603 SPI driver
 *
 * Copyright (C) Guenter Roeck <linux@roeck-us.net>
 */

#include <linux/kernel.h>
#include <linux/err.h>
#include <linux/mod_devicetable.h>
#include <linux/module.h>
#include <linux/spi/spi.h>
#include <linux/i2c.h>
#include <linux/delay.h>
#include <linux/pm_runtime.h>
#include <linux/platform_data/sc18is602.h>
#include <linux/property.h>

#include <linux/gpio/consumer.h>

enum chips { sc18is602, sc18is602b, sc18is603 };

#define SC18IS602_BUFSIZ		200
#define SC18IS602_CLOCK			7372000

#define SC18IS602_MODE_CPHA		BIT(2)
#define SC18IS602_MODE_CPOL		BIT(3)
#define SC18IS602_MODE_LSB_FIRST	BIT(5)
#define SC18IS602_MODE_CLOCK_DIV_4	0x0
#define SC18IS602_MODE_CLOCK_DIV_16	0x1
#define SC18IS602_MODE_CLOCK_DIV_64	0x2
#define SC18IS602_MODE_CLOCK_DIV_128	0x3

struct sc18is602 {
	struct spi_controller	*host;
	struct device		*dev;
	u8			ctrl;
	u32			freq;
	u32			speed;

	/* I2C data */
	struct i2c_client	*client;
	enum chips		id;
	u8			buffer[SC18IS602_BUFSIZ + 1];
	int			tlen;	/* Data queued for tx in buffer */
	int			rindex;	/* Receive data index in buffer */

	struct gpio_desc	*reset;
};

static int sc18is602_wait_ready(struct sc18is602 *hw, int len)
{
	int i, err;
	int usecs = 1000000 * len / hw->speed + 1;
	u8 dummy[1];

	for (i = 0; i < 10; i++) {
		err = i2c_master_recv(hw->client, dummy, 1);
		if (err >= 0)
			return 0;
		usleep_range(usecs, usecs * 2);
	}
	return -ETIMEDOUT;
}

static int sc18is602_txrx(struct sc18is602 *hw, struct spi_message *msg,
			  struct spi_transfer *t, bool do_transfer)
{
	unsigned int len = t->len;
	int ret;

	if (hw->tlen == 0) {
		/* First byte (I2C command) is chip select */
		hw->buffer[0] = 1 << spi_get_chipselect(msg->spi, 0);
		hw->tlen = 1;
		hw->rindex = 0;
	}
	/*
	 * We can not immediately send data to the chip, since each I2C message
	 * resembles a full SPI message (from CS active to CS inactive).
	 * Enqueue messages up to the first read or until do_transfer is true.
	 */
	if (t->tx_buf) {
		memcpy(&hw->buffer[hw->tlen], t->tx_buf, len);
		hw->tlen += len;
		if (t->rx_buf)
			do_transfer = true;
		else
			hw->rindex = hw->tlen - 1;
	} else if (t->rx_buf) {
		/*
		 * For receive-only transfers we still need to perform a dummy
		 * write to receive data from the SPI chip.
		 * Read data starts at the end of transmit data (minus 1 to
		 * account for CS).
		 */
		hw->rindex = hw->tlen - 1;
		memset(&hw->buffer[hw->tlen], 0, len);
		hw->tlen += len;
		do_transfer = true;
	}

	if (do_transfer && hw->tlen > 1) {
		ret = sc18is602_wait_ready(hw, SC18IS602_BUFSIZ);
		if (ret < 0)
			return ret;
		ret = i2c_master_send(hw->client, hw->buffer, hw->tlen);
		if (ret < 0)
			return ret;
		if (ret != hw->tlen)
			return -EIO;

		if (t->rx_buf) {
			int rlen = hw->rindex + len;

			ret = sc18is602_wait_ready(hw, hw->tlen);
			if (ret < 0)
				return ret;
			ret = i2c_master_recv(hw->client, hw->buffer, rlen);
			if (ret < 0)
				return ret;
			if (ret != rlen)
				return -EIO;
			memcpy(t->rx_buf, &hw->buffer[hw->rindex], len);
		}
		hw->tlen = 0;
	}
	return len;
}

static int sc18is602_setup_transfer(struct sc18is602 *hw, u32 hz, u8 mode)
{
	u8 ctrl = 0;
	int ret;

	if (mode & SPI_CPHA)
		ctrl |= SC18IS602_MODE_CPHA;
	if (mode & SPI_CPOL)
		ctrl |= SC18IS602_MODE_CPOL;
	if (mode & SPI_LSB_FIRST)
		ctrl |= SC18IS602_MODE_LSB_FIRST;

	/* Find the closest clock speed */
	if (hz >= hw->freq / 4) {
		ctrl |= SC18IS602_MODE_CLOCK_DIV_4;
		hw->speed = hw->freq / 4;
	} else if (hz >= hw->freq / 16) {
		ctrl |= SC18IS602_MODE_CLOCK_DIV_16;
		hw->speed = hw->freq / 16;
	} else if (hz >= hw->freq / 64) {
		ctrl |= SC18IS602_MODE_CLOCK_DIV_64;
		hw->speed = hw->freq / 64;
	} else {
		ctrl |= SC18IS602_MODE_CLOCK_DIV_128;
		hw->speed = hw->freq / 128;
	}

	/*
	 * Don't do anything if the control value did not change. The initial
	 * value of 0xff for hw->ctrl ensures that the correct mode will be set
	 * with the first call to this function.
	 */
	if (ctrl == hw->ctrl)
		return 0;

	ret = i2c_smbus_write_byte_data(hw->client, 0xf0, ctrl);
	if (ret < 0)
		return ret;

	hw->ctrl = ctrl;

	return 0;
}

static int sc18is602_check_transfer(struct spi_device *spi,
				    struct spi_transfer *t, int tlen)
{
	if (t && t->len + tlen > SC18IS602_BUFSIZ + 1)
		return -EINVAL;

	return 0;
}

static int sc18is602_transfer_one(struct spi_controller *host,
				  struct spi_message *m)
{
	struct sc18is602 *hw = spi_controller_get_devdata(host);
	struct spi_device *spi = m->spi;
	struct spi_transfer *t;
	int status = 0;

	hw->tlen = 0;
	list_for_each_entry(t, &m->transfers, transfer_list) {
		bool do_transfer;

		status = sc18is602_check_transfer(spi, t, hw->tlen);
		if (status < 0)
			break;

		status = sc18is602_setup_transfer(hw, t->speed_hz, spi->mode);
		if (status < 0)
			break;

		do_transfer = t->cs_change || list_is_last(&t->transfer_list,
							   &m->transfers);

		if (t->len) {
			status = sc18is602_txrx(hw, m, t, do_transfer);
			if (status < 0)
				break;
			m->actual_length += status;
		}
		status = 0;

		spi_transfer_delay_exec(t);
	}
	m->status = status;
	spi_finalize_current_message(host);

	return status;
}

static size_t sc18is602_max_transfer_size(struct spi_device *spi)
{
	return SC18IS602_BUFSIZ;
}

static int sc18is602_setup(struct spi_device *spi)
{
	struct sc18is602 *hw = spi_controller_get_devdata(spi->controller);

	/* SC18IS602 does not support CS2 */
	if (hw->id == sc18is602 && (spi_get_chipselect(spi, 0) == 2))
		return -ENXIO;

	return 0;
}

static int sc18is602_probe(struct i2c_client *client)
{
	struct device *dev = &client->dev;
	struct sc18is602_platform_data *pdata = dev_get_platdata(dev);
	struct sc18is602 *hw;
	struct spi_controller *host;

	if (!i2c_check_functionality(client->adapter, I2C_FUNC_I2C |
				     I2C_FUNC_SMBUS_WRITE_BYTE_DATA))
		return -EINVAL;

	host = devm_spi_alloc_host(dev, sizeof(struct sc18is602));
	if (!host)
		return -ENOMEM;

	device_set_node(&host->dev, dev_fwnode(dev));

	hw = spi_controller_get_devdata(host);

	/* assert reset and then release */
	hw->reset = devm_gpiod_get_optional(dev, "reset", GPIOD_OUT_HIGH);
	if (IS_ERR(hw->reset))
		return PTR_ERR(hw->reset);
	gpiod_set_value_cansleep(hw->reset, 0);

	hw->host = host;
	hw->client = client;
	hw->dev = dev;
	hw->ctrl = 0xff;

	hw->id = (uintptr_t)i2c_get_match_data(client);
	switch (hw->id) {
	case sc18is602:
	case sc18is602b:
		host->num_chipselect = 4;
		hw->freq = SC18IS602_CLOCK;
		break;
	case sc18is603:
		host->num_chipselect = 2;
		if (pdata)
			hw->freq = pdata->clock_frequency;
		else
			device_property_read_u32(dev, "clock-frequency", &hw->freq);
		if (!hw->freq)
			hw->freq = SC18IS602_CLOCK;
		break;
	}
	host->bus_num = dev_fwnode(dev) ? -1 : client->adapter->nr;
	host->mode_bits = SPI_CPHA | SPI_CPOL | SPI_LSB_FIRST;
	host->bits_per_word_mask = SPI_BPW_MASK(8);
	host->setup = sc18is602_setup;
	host->transfer_one_message = sc18is602_transfer_one;
	host->max_transfer_size = sc18is602_max_transfer_size;
	host->max_message_size = sc18is602_max_transfer_size;
	host->min_speed_hz = hw->freq / 128;
	host->max_speed_hz = hw->freq / 4;

	return devm_spi_register_controller(dev, host);
}

static const struct i2c_device_id sc18is602_id[] = {
	{ "sc18is602", sc18is602 },
	{ "sc18is602b", sc18is602b },
	{ "sc18is603", sc18is603 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, sc18is602_id);

static const struct of_device_id sc18is602_of_match[] = {
	{
		.compatible = "nxp,sc18is602",
		.data = (void *)sc18is602
	},
	{
		.compatible = "nxp,sc18is602b",
		.data = (void *)sc18is602b
	},
	{
		.compatible = "nxp,sc18is603",
		.data = (void *)sc18is603
	},
	{ },
};
MODULE_DEVICE_TABLE(of, sc18is602_of_match);

static struct i2c_driver sc18is602_driver = {
	.driver = {
		.name = "sc18is602",
		.of_match_table = sc18is602_of_match,
	},
	.probe = sc18is602_probe,
	.id_table = sc18is602_id,
};

module_i2c_driver(sc18is602_driver);

MODULE_DESCRIPTION("SC18IS602/603 SPI Host Driver");
MODULE_AUTHOR("Guenter Roeck");
MODULE_LICENSE("GPL");
