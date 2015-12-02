/*
 * SPI bridge driver for the Greybus "generic" SPI module.
 *
 * Copyright 2014-2015 Google Inc.
 * Copyright 2014-2015 Linaro Ltd.
 *
 * Released under the GPLv2 only.
 */

#include <linux/bitops.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/spi/spi.h>

#include "greybus.h"

struct gb_spi {
	struct gb_connection	*connection;
	u16			mode;
	u16			flags;
	u32			bits_per_word_mask;
	u16			num_chipselect;
	u32			min_speed_hz;
	u32			max_speed_hz;
	struct spi_device	*spi_devices;
};

static struct spi_master *get_master_from_spi(struct gb_spi *spi)
{
	return spi->connection->private;
}

/* Routines to transfer data */
static struct gb_operation *
gb_spi_operation_create(struct gb_connection *connection,
			struct spi_message *msg, u32 *total_len)
{
	struct gb_spi_transfer_request *request;
	struct spi_device *dev = msg->spi;
	struct spi_transfer *xfer;
	struct gb_spi_transfer *gb_xfer;
	struct gb_operation *operation;
	u32 tx_size = 0, rx_size = 0, count = 0, request_size;
	void *tx_data;

	/* Find number of transfers queued and tx/rx length in the message */
	list_for_each_entry(xfer, &msg->transfers, transfer_list) {
		if (!xfer->tx_buf && !xfer->rx_buf) {
			dev_err(&connection->bundle->dev,
				"bufferless transfer, length %u\n", xfer->len);
			return NULL;
		}

		if (xfer->tx_buf)
			tx_size += xfer->len;
		if (xfer->rx_buf)
			rx_size += xfer->len;

		*total_len += xfer->len;
		count++;
	}

	/* Too many transfers ? */
	if (count > (u32)U16_MAX) {
		dev_err(&connection->bundle->dev,
			"transfer count (%u) too big\n", count);
		return NULL;
	}

	/*
	 * In addition to space for all message descriptors we need
	 * to have enough to hold all tx data.
	 */
	request_size = sizeof(*request);
	request_size += count * sizeof(*gb_xfer);
	request_size += tx_size;

	/* Response consists only of incoming data */
	operation = gb_operation_create(connection, GB_SPI_TYPE_TRANSFER,
					request_size, rx_size, GFP_KERNEL);
	if (!operation)
		return NULL;

	request = operation->request->payload;
	request->count = cpu_to_le16(count);
	request->mode = dev->mode;
	request->chip_select = dev->chip_select;

	gb_xfer = &request->transfers[0];
	tx_data = gb_xfer + count;	/* place tx data after last gb_xfer */

	/* Fill in the transfers array */
	list_for_each_entry(xfer, &msg->transfers, transfer_list) {
		gb_xfer->speed_hz = cpu_to_le32(xfer->speed_hz);
		gb_xfer->len = cpu_to_le32(xfer->len);
		gb_xfer->delay_usecs = cpu_to_le16(xfer->delay_usecs);
		gb_xfer->cs_change = xfer->cs_change;
		gb_xfer->bits_per_word = xfer->bits_per_word;

		/* Copy tx data */
		if (xfer->tx_buf) {
			memcpy(tx_data, xfer->tx_buf, xfer->len);
			tx_data += xfer->len;
			gb_xfer->rdwr |= GB_SPI_XFER_WRITE;
		}
		if (xfer->rx_buf)
			gb_xfer->rdwr |= GB_SPI_XFER_READ;
		gb_xfer++;
	}

	return operation;
}

static void gb_spi_decode_response(struct spi_message *msg,
				   struct gb_spi_transfer_response *response)
{
	struct spi_transfer *xfer;
	void *rx_data = response->data;

	list_for_each_entry(xfer, &msg->transfers, transfer_list) {
		/* Copy rx data */
		if (xfer->rx_buf) {
			memcpy(xfer->rx_buf, rx_data, xfer->len);
			rx_data += xfer->len;
		}
	}
}

static int gb_spi_transfer_one_message(struct spi_master *master,
				       struct spi_message *msg)
{
	struct gb_spi *spi = spi_master_get_devdata(master);
	struct gb_connection *connection = spi->connection;
	struct gb_spi_transfer_response *response;
	struct gb_operation *operation;
	u32 len = 0;
	int ret;

	operation = gb_spi_operation_create(connection, msg, &len);
	if (!operation)
		return -ENOMEM;

	ret = gb_operation_request_send_sync(operation);
	if (!ret) {
		response = operation->response->payload;
		if (response)
			gb_spi_decode_response(msg, response);
	} else {
		pr_err("transfer operation failed (%d)\n", ret);
	}

	gb_operation_put(operation);

	msg->actual_length = len;
	msg->status = 0;
	spi_finalize_current_message(master);

	return ret;
}

static int gb_spi_setup(struct spi_device *spi)
{
	/* Nothing to do for now */
	return 0;
}

static void gb_spi_cleanup(struct spi_device *spi)
{
	/* Nothing to do for now */
}


/* Routines to get controller information */

/*
 * Map Greybus spi mode bits/flags/bpw into Linux ones.
 * All bits are same for now and so these macro's return same values.
 */
#define gb_spi_mode_map(mode) mode
#define gb_spi_flags_map(flags) flags

static int gb_spi_get_master_config(struct gb_spi *spi)
{
	struct gb_spi_master_config_response response;
	u16 mode, flags;
	int ret;

	ret = gb_operation_sync(spi->connection, GB_SPI_TYPE_MASTER_CONFIG,
				NULL, 0, &response, sizeof(response));
	if (ret < 0)
		return ret;

	mode = le16_to_cpu(response.mode);
	spi->mode = gb_spi_mode_map(mode);

	flags = le16_to_cpu(response.flags);
	spi->flags = gb_spi_flags_map(flags);

	spi->bits_per_word_mask = le32_to_cpu(response.bits_per_word_mask);
	spi->num_chipselect = le16_to_cpu(response.num_chipselect);

	spi->min_speed_hz = le32_to_cpu(response.min_speed_hz);
	spi->max_speed_hz = le32_to_cpu(response.max_speed_hz);

	return 0;
}

static int gb_spi_setup_device(struct gb_spi *spi, uint16_t cs)
{
	struct spi_master *master = get_master_from_spi(spi);
	struct gb_spi_device_config_request request;
	struct gb_spi_device_config_response response;
	struct spi_board_info spi_board = { {0} };
	struct spi_device *spidev = &spi->spi_devices[cs];
	int ret;

	request.chip_select = cpu_to_le16(cs);

	ret = gb_operation_sync(spi->connection, GB_SPI_TYPE_DEVICE_CONFIG,
				&request, sizeof(request),
				&response, sizeof(response));
	if (ret < 0)
		return ret;

	memcpy(spi_board.modalias, response.name, sizeof(spi_board.modalias));
	spi_board.mode		= le16_to_cpu(response.mode);
	spi_board.bus_num	= master->bus_num;
	spi_board.chip_select	= cs;
	spi_board.max_speed_hz	= le32_to_cpu(response.max_speed_hz);

	spidev = spi_new_device(master, &spi_board);
	if (!spidev)
		ret = -EINVAL;

	return 0;
}

static int gb_spi_init(struct gb_spi *spi)
{
	int ret;

	/* get master configuration */
	ret = gb_spi_get_master_config(spi);
	if (ret)
		return ret;

	spi->spi_devices = kcalloc(spi->num_chipselect,
				   sizeof(struct spi_device), GFP_KERNEL);
	if (!spi->spi_devices)
		return -ENOMEM;

	return ret;
}


static int gb_spi_connection_init(struct gb_connection *connection)
{
	struct gb_spi *spi;
	struct spi_master *master;
	int ret;
	int i;

	/* Allocate master with space for data */
	master = spi_alloc_master(&connection->bundle->dev, sizeof(*spi));
	if (!master) {
		dev_err(&connection->bundle->dev, "cannot alloc SPI master\n");
		return -ENOMEM;
	}

	spi = spi_master_get_devdata(master);
	spi->connection = connection;
	connection->private = master;

	ret = gb_spi_init(spi);
	if (ret)
		goto out_err;

	master->bus_num = -1; /* Allow spi-core to allocate it dynamically */
	master->num_chipselect = spi->num_chipselect;
	master->mode_bits = spi->mode;
	master->flags = spi->flags;
	master->bits_per_word_mask = spi->bits_per_word_mask;

	/* Attach methods */
	master->cleanup = gb_spi_cleanup;
	master->setup = gb_spi_setup;
	master->transfer_one_message = gb_spi_transfer_one_message;

	ret = spi_register_master(master);

	/* now, fetch the devices configuration */
	for (i = 0; i < spi->num_chipselect; i++) {
		ret = gb_spi_setup_device(spi, i);
		if (ret < 0)
			break;
	}

	return ret;

out_err:
	spi_master_put(master);

	return ret;
}

static void gb_spi_connection_exit(struct gb_connection *connection)
{
	struct spi_master *master = connection->private;

	spi_unregister_master(master);
}

static struct gb_protocol spi_protocol = {
	.name			= "spi",
	.id			= GREYBUS_PROTOCOL_SPI,
	.major			= GB_SPI_VERSION_MAJOR,
	.minor			= GB_SPI_VERSION_MINOR,
	.connection_init	= gb_spi_connection_init,
	.connection_exit	= gb_spi_connection_exit,
	.request_recv		= NULL,
};

gb_builtin_protocol_driver(spi_protocol);
