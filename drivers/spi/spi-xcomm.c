// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Analog Devices AD-FMCOMMS1-EBZ board I2C-SPI bridge driver
 *
 * Copyright 2012 Analog Devices Inc.
 * Author: Lars-Peter Clausen <lars@metafoo.de>
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/delay.h>
#include <linux/i2c.h>
#include <linux/spi/spi.h>
#include <asm/unaligned.h>

#define SPI_XCOMM_SETTINGS_LEN_OFFSET		10
#define SPI_XCOMM_SETTINGS_3WIRE		BIT(6)
#define SPI_XCOMM_SETTINGS_CS_HIGH		BIT(5)
#define SPI_XCOMM_SETTINGS_SAMPLE_END		BIT(4)
#define SPI_XCOMM_SETTINGS_CPHA			BIT(3)
#define SPI_XCOMM_SETTINGS_CPOL			BIT(2)
#define SPI_XCOMM_SETTINGS_CLOCK_DIV_MASK	0x3
#define SPI_XCOMM_SETTINGS_CLOCK_DIV_64		0x2
#define SPI_XCOMM_SETTINGS_CLOCK_DIV_16		0x1
#define SPI_XCOMM_SETTINGS_CLOCK_DIV_4		0x0

#define SPI_XCOMM_CMD_UPDATE_CONFIG	0x03
#define SPI_XCOMM_CMD_WRITE		0x04

#define SPI_XCOMM_CLOCK 48000000

struct spi_xcomm {
	struct i2c_client *i2c;

	uint16_t settings;
	uint16_t chipselect;

	unsigned int current_speed;

	uint8_t buf[63];
};

static int spi_xcomm_sync_config(struct spi_xcomm *spi_xcomm, unsigned int len)
{
	uint16_t settings;
	uint8_t *buf = spi_xcomm->buf;

	settings = spi_xcomm->settings;
	settings |= len << SPI_XCOMM_SETTINGS_LEN_OFFSET;

	buf[0] = SPI_XCOMM_CMD_UPDATE_CONFIG;
	put_unaligned_be16(settings, &buf[1]);
	put_unaligned_be16(spi_xcomm->chipselect, &buf[3]);

	return i2c_master_send(spi_xcomm->i2c, buf, 5);
}

static void spi_xcomm_chipselect(struct spi_xcomm *spi_xcomm,
	struct spi_device *spi, int is_active)
{
	unsigned long cs = spi_get_chipselect(spi, 0);
	uint16_t chipselect = spi_xcomm->chipselect;

	if (is_active)
		chipselect |= BIT(cs);
	else
		chipselect &= ~BIT(cs);

	spi_xcomm->chipselect = chipselect;
}

static int spi_xcomm_setup_transfer(struct spi_xcomm *spi_xcomm,
	struct spi_device *spi, struct spi_transfer *t, unsigned int *settings)
{
	if (t->len > 62)
		return -EINVAL;

	if (t->speed_hz != spi_xcomm->current_speed) {
		unsigned int divider;

		divider = DIV_ROUND_UP(SPI_XCOMM_CLOCK, t->speed_hz);
		if (divider >= 64)
			*settings |= SPI_XCOMM_SETTINGS_CLOCK_DIV_64;
		else if (divider >= 16)
			*settings |= SPI_XCOMM_SETTINGS_CLOCK_DIV_16;
		else
			*settings |= SPI_XCOMM_SETTINGS_CLOCK_DIV_4;

		spi_xcomm->current_speed = t->speed_hz;
	}

	if (spi->mode & SPI_CPOL)
		*settings |= SPI_XCOMM_SETTINGS_CPOL;
	else
		*settings &= ~SPI_XCOMM_SETTINGS_CPOL;

	if (spi->mode & SPI_CPHA)
		*settings &= ~SPI_XCOMM_SETTINGS_CPHA;
	else
		*settings |= SPI_XCOMM_SETTINGS_CPHA;

	if (spi->mode & SPI_3WIRE)
		*settings |= SPI_XCOMM_SETTINGS_3WIRE;
	else
		*settings &= ~SPI_XCOMM_SETTINGS_3WIRE;

	return 0;
}

static int spi_xcomm_txrx_bufs(struct spi_xcomm *spi_xcomm,
	struct spi_device *spi, struct spi_transfer *t)
{
	int ret;

	if (t->tx_buf) {
		spi_xcomm->buf[0] = SPI_XCOMM_CMD_WRITE;
		memcpy(spi_xcomm->buf + 1, t->tx_buf, t->len);

		ret = i2c_master_send(spi_xcomm->i2c, spi_xcomm->buf, t->len + 1);
		if (ret < 0)
			return ret;
		else if (ret != t->len + 1)
			return -EIO;
	} else if (t->rx_buf) {
		ret = i2c_master_recv(spi_xcomm->i2c, t->rx_buf, t->len);
		if (ret < 0)
			return ret;
		else if (ret != t->len)
			return -EIO;
	}

	return t->len;
}

static int spi_xcomm_transfer_one(struct spi_master *master,
	struct spi_message *msg)
{
	struct spi_xcomm *spi_xcomm = spi_master_get_devdata(master);
	unsigned int settings = spi_xcomm->settings;
	struct spi_device *spi = msg->spi;
	unsigned cs_change = 0;
	struct spi_transfer *t;
	bool is_first = true;
	int status = 0;
	bool is_last;

	spi_xcomm_chipselect(spi_xcomm, spi, true);

	list_for_each_entry(t, &msg->transfers, transfer_list) {

		if (!t->tx_buf && !t->rx_buf && t->len) {
			status = -EINVAL;
			break;
		}

		status = spi_xcomm_setup_transfer(spi_xcomm, spi, t, &settings);
		if (status < 0)
			break;

		is_last = list_is_last(&t->transfer_list, &msg->transfers);
		cs_change = t->cs_change;

		if (cs_change ^ is_last)
			settings |= BIT(5);
		else
			settings &= ~BIT(5);

		if (t->rx_buf) {
			spi_xcomm->settings = settings;
			status = spi_xcomm_sync_config(spi_xcomm, t->len);
			if (status < 0)
				break;
		} else if (settings != spi_xcomm->settings || is_first) {
			spi_xcomm->settings = settings;
			status = spi_xcomm_sync_config(spi_xcomm, 0);
			if (status < 0)
				break;
		}

		if (t->len) {
			status = spi_xcomm_txrx_bufs(spi_xcomm, spi, t);

			if (status < 0)
				break;

			if (status > 0)
				msg->actual_length += status;
		}
		status = 0;

		spi_transfer_delay_exec(t);

		is_first = false;
	}

	if (status != 0 || !cs_change)
		spi_xcomm_chipselect(spi_xcomm, spi, false);

	msg->status = status;
	spi_finalize_current_message(master);

	return status;
}

static int spi_xcomm_probe(struct i2c_client *i2c)
{
	struct spi_xcomm *spi_xcomm;
	struct spi_master *master;
	int ret;

	master = spi_alloc_master(&i2c->dev, sizeof(*spi_xcomm));
	if (!master)
		return -ENOMEM;

	spi_xcomm = spi_master_get_devdata(master);
	spi_xcomm->i2c = i2c;

	master->num_chipselect = 16;
	master->mode_bits = SPI_CPHA | SPI_CPOL | SPI_3WIRE;
	master->bits_per_word_mask = SPI_BPW_MASK(8);
	master->flags = SPI_MASTER_HALF_DUPLEX;
	master->transfer_one_message = spi_xcomm_transfer_one;
	master->dev.of_node = i2c->dev.of_node;
	i2c_set_clientdata(i2c, master);

	ret = devm_spi_register_master(&i2c->dev, master);
	if (ret < 0)
		spi_master_put(master);

	return ret;
}

static const struct i2c_device_id spi_xcomm_ids[] = {
	{ "spi-xcomm" },
	{ },
};
MODULE_DEVICE_TABLE(i2c, spi_xcomm_ids);

static struct i2c_driver spi_xcomm_driver = {
	.driver = {
		.name	= "spi-xcomm",
	},
	.id_table	= spi_xcomm_ids,
	.probe_new	= spi_xcomm_probe,
};
module_i2c_driver(spi_xcomm_driver);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Lars-Peter Clausen <lars@metafoo.de>");
MODULE_DESCRIPTION("Analog Devices AD-FMCOMMS1-EBZ board I2C-SPI bridge driver");
