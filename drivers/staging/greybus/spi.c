/*
 * SPI bridge driver for the Greybus "generic" SPI module.
 *
 * Copyright 2014 Google Inc.
 * Copyright 2014 Linaro Ltd.
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

	/* Modes supported by spi controller */
	u16			mode;
	/* constraints of the spi controller */
	u16			flags;

	/*
	 * copied from kernel:
	 *
	 * A mask indicating which values of bits_per_word are supported by the
	 * controller. Bit n indicates that a bits_per_word n+1 is suported. If
	 * set, the SPI core will reject any transfer with an unsupported
	 * bits_per_word. If not set, this value is simply ignored, and it's up
	 * to the individual driver to perform any validation.
	 */
	u32			bits_per_word_mask;

	/*
	 * chipselects will be integral to many controllers; some others might
	 * use board-specific GPIOs.
	 */
	u16			num_chipselect;
};

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


/* Routines to get controller infomation */

/*
 * Map Greybus spi mode bits/flags/bpw into Linux ones.
 * All bits are same for now and so these macro's return same values.
 */
#define gb_spi_mode_map(mode) mode
#define gb_spi_flags_map(flags) flags

static int gb_spi_mode_operation(struct gb_spi *spi)
{
	struct gb_spi_mode_response response;
	u16 mode;
	int ret;

	ret = gb_operation_sync(spi->connection, GB_SPI_TYPE_MODE,
				NULL, 0, &response, sizeof(response));
	if (ret)
		return ret;

	mode = le16_to_cpu(response.mode);
	spi->mode = gb_spi_mode_map(mode);

	return 0;
}

static int gb_spi_flags_operation(struct gb_spi *spi)
{
	struct gb_spi_flags_response response;
	u16 flags;
	int ret;

	ret = gb_operation_sync(spi->connection, GB_SPI_TYPE_FLAGS,
				NULL, 0, &response, sizeof(response));
	if (ret)
		return ret;

	flags = le16_to_cpu(response.flags);
	spi->flags = gb_spi_flags_map(flags);

	return 0;
}

static int gb_spi_bpw_operation(struct gb_spi *spi)
{
	struct gb_spi_bpw_response response;
	int ret;

	ret = gb_operation_sync(spi->connection, GB_SPI_TYPE_BITS_PER_WORD_MASK,
				NULL, 0, &response, sizeof(response));
	if (ret)
		return ret;

	spi->bits_per_word_mask = le32_to_cpu(response.bits_per_word_mask);

	return 0;
}

static int gb_spi_chipselect_operation(struct gb_spi *spi)
{
	struct gb_spi_chipselect_response response;
	int ret;

	ret = gb_operation_sync(spi->connection, GB_SPI_TYPE_NUM_CHIPSELECT,
				NULL, 0, &response, sizeof(response));
	if (ret)
		return ret;

	spi->num_chipselect = le16_to_cpu(response.num_chipselect);

	return 0;
}

/*
 * Initialize the spi device. This includes verifying we can support it (based
 * on the protocol version it advertises). If that's OK, we get and cached its
 * mode bits & flags.
 */
static int gb_spi_init(struct gb_spi *spi)
{
	int ret;

	/* mode never changes, just get it once */
	ret = gb_spi_mode_operation(spi);
	if (ret)
		return ret;

	/* flags never changes, just get it once */
	ret = gb_spi_flags_operation(spi);
	if (ret)
		return ret;

	/* total number of chipselects never changes, just get it once */
	ret = gb_spi_chipselect_operation(spi);
	if (ret)
		return ret;

	/* bits-per-word-mask never changes, just get it once */
	return gb_spi_bpw_operation(spi);
}

static int gb_spi_connection_init(struct gb_connection *connection)
{
	struct gb_spi *spi;
	struct spi_master *master;
	int ret;

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
	if (!ret)
		return 0;

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
