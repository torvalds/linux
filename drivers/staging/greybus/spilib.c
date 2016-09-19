/*
 * Greybus SPI library
 *
 * Copyright 2014-2016 Google Inc.
 * Copyright 2014-2016 Linaro Ltd.
 *
 * Released under the GPLv2 only.
 */

#include <linux/bitops.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/spi/spi.h>

#include "greybus.h"
#include "spilib.h"

struct gb_spilib {
	struct gb_connection	*connection;
	struct device		*parent;
	struct spi_transfer	*first_xfer;
	struct spi_transfer	*last_xfer;
	struct spilib_ops	*ops;
	u32			rx_xfer_offset;
	u32			tx_xfer_offset;
	u32			last_xfer_size;
	unsigned int		op_timeout;
	u16			mode;
	u16			flags;
	u32			bits_per_word_mask;
	u8			num_chipselect;
	u32			min_speed_hz;
	u32			max_speed_hz;
};

#define GB_SPI_STATE_MSG_DONE		((void *)0)
#define GB_SPI_STATE_MSG_IDLE		((void *)1)
#define GB_SPI_STATE_MSG_RUNNING	((void *)2)
#define GB_SPI_STATE_OP_READY		((void *)3)
#define GB_SPI_STATE_OP_DONE		((void *)4)
#define GB_SPI_STATE_MSG_ERROR		((void *)-1)

#define XFER_TIMEOUT_TOLERANCE		200

static struct spi_master *get_master_from_spi(struct gb_spilib *spi)
{
	return gb_connection_get_data(spi->connection);
}

static int tx_header_fit_operation(u32 tx_size, u32 count, size_t data_max)
{
	size_t headers_size;

	data_max -= sizeof(struct gb_spi_transfer_request);
	headers_size = (count + 1) * sizeof(struct gb_spi_transfer);

	return tx_size + headers_size > data_max ? 0 : 1;
}

static size_t calc_rx_xfer_size(u32 rx_size, u32 *tx_xfer_size, u32 len,
				size_t data_max)
{
	size_t rx_xfer_size;

	data_max -= sizeof(struct gb_spi_transfer_response);

	if (rx_size + len > data_max)
		rx_xfer_size = data_max - rx_size;
	else
		rx_xfer_size = len;

	/* if this is a write_read, for symmetry read the same as write */
	if (*tx_xfer_size && rx_xfer_size > *tx_xfer_size)
		rx_xfer_size = *tx_xfer_size;
	if (*tx_xfer_size && rx_xfer_size < *tx_xfer_size)
		*tx_xfer_size = rx_xfer_size;

	return rx_xfer_size;
}

static size_t calc_tx_xfer_size(u32 tx_size, u32 count, size_t len,
				size_t data_max)
{
	size_t headers_size;

	data_max -= sizeof(struct gb_spi_transfer_request);
	headers_size = (count + 1) * sizeof(struct gb_spi_transfer);

	if (tx_size + headers_size + len > data_max)
		return data_max - (tx_size + sizeof(struct gb_spi_transfer));

	return len;
}

static void clean_xfer_state(struct gb_spilib *spi)
{
	spi->first_xfer = NULL;
	spi->last_xfer = NULL;
	spi->rx_xfer_offset = 0;
	spi->tx_xfer_offset = 0;
	spi->last_xfer_size = 0;
	spi->op_timeout = 0;
}

static bool is_last_xfer_done(struct gb_spilib *spi)
{
	struct spi_transfer *last_xfer = spi->last_xfer;

	if ((spi->tx_xfer_offset + spi->last_xfer_size == last_xfer->len) ||
	    (spi->rx_xfer_offset + spi->last_xfer_size == last_xfer->len))
		return true;

	return false;
}

static int setup_next_xfer(struct gb_spilib *spi, struct spi_message *msg)
{
	struct spi_transfer *last_xfer = spi->last_xfer;

	if (msg->state != GB_SPI_STATE_OP_DONE)
		return 0;

	/*
	 * if we transferred all content of the last transfer, reset values and
	 * check if this was the last transfer in the message
	 */
	if (is_last_xfer_done(spi)) {
		spi->tx_xfer_offset = 0;
		spi->rx_xfer_offset = 0;
		spi->op_timeout = 0;
		if (last_xfer == list_last_entry(&msg->transfers,
						 struct spi_transfer,
						 transfer_list))
			msg->state = GB_SPI_STATE_MSG_DONE;
		else
			spi->first_xfer = list_next_entry(last_xfer,
							  transfer_list);
		return 0;
	}

	spi->first_xfer = last_xfer;
	if (last_xfer->tx_buf)
		spi->tx_xfer_offset += spi->last_xfer_size;

	if (last_xfer->rx_buf)
		spi->rx_xfer_offset += spi->last_xfer_size;

	return 0;
}

static struct spi_transfer *get_next_xfer(struct spi_transfer *xfer,
					  struct spi_message *msg)
{
	if (xfer == list_last_entry(&msg->transfers, struct spi_transfer,
				    transfer_list))
		return NULL;

	return list_next_entry(xfer, transfer_list);
}

/* Routines to transfer data */
static struct gb_operation *gb_spi_operation_create(struct gb_spilib *spi,
		struct gb_connection *connection, struct spi_message *msg)
{
	struct gb_spi_transfer_request *request;
	struct spi_device *dev = msg->spi;
	struct spi_transfer *xfer;
	struct gb_spi_transfer *gb_xfer;
	struct gb_operation *operation;
	u32 tx_size = 0, rx_size = 0, count = 0, xfer_len = 0, request_size;
	u32 tx_xfer_size = 0, rx_xfer_size = 0, len;
	u32 total_len = 0;
	unsigned int xfer_timeout;
	size_t data_max;
	void *tx_data;

	data_max = gb_operation_get_payload_size_max(connection);
	xfer = spi->first_xfer;

	/* Find number of transfers queued and tx/rx length in the message */

	while (msg->state != GB_SPI_STATE_OP_READY) {
		msg->state = GB_SPI_STATE_MSG_RUNNING;
		spi->last_xfer = xfer;

		if (!xfer->tx_buf && !xfer->rx_buf) {
			dev_err(spi->parent,
				"bufferless transfer, length %u\n", xfer->len);
			msg->state = GB_SPI_STATE_MSG_ERROR;
			return NULL;
		}

		tx_xfer_size = 0;
		rx_xfer_size = 0;

		if (xfer->tx_buf) {
			len = xfer->len - spi->tx_xfer_offset;
			if (!tx_header_fit_operation(tx_size, count, data_max))
				break;
			tx_xfer_size = calc_tx_xfer_size(tx_size, count,
							 len, data_max);
			spi->last_xfer_size = tx_xfer_size;
		}

		if (xfer->rx_buf) {
			len = xfer->len - spi->rx_xfer_offset;
			rx_xfer_size = calc_rx_xfer_size(rx_size, &tx_xfer_size,
							 len, data_max);
			spi->last_xfer_size = rx_xfer_size;
		}

		tx_size += tx_xfer_size;
		rx_size += rx_xfer_size;

		total_len += spi->last_xfer_size;
		count++;

		xfer = get_next_xfer(xfer, msg);
		if (!xfer || total_len >= data_max)
			msg->state = GB_SPI_STATE_OP_READY;
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
	xfer = spi->first_xfer;
	while (msg->state != GB_SPI_STATE_OP_DONE) {
		if (xfer == spi->last_xfer)
			xfer_len = spi->last_xfer_size;
		else
			xfer_len = xfer->len;

		/* make sure we do not timeout in a slow transfer */
		xfer_timeout = xfer_len * 8 * MSEC_PER_SEC / xfer->speed_hz;
		xfer_timeout += GB_OPERATION_TIMEOUT_DEFAULT;

		if (xfer_timeout > spi->op_timeout)
			spi->op_timeout = xfer_timeout;

		gb_xfer->speed_hz = cpu_to_le32(xfer->speed_hz);
		gb_xfer->len = cpu_to_le32(xfer_len);
		gb_xfer->delay_usecs = cpu_to_le16(xfer->delay_usecs);
		gb_xfer->cs_change = xfer->cs_change;
		gb_xfer->bits_per_word = xfer->bits_per_word;

		/* Copy tx data */
		if (xfer->tx_buf) {
			gb_xfer->xfer_flags |= GB_SPI_XFER_WRITE;
			memcpy(tx_data, xfer->tx_buf + spi->tx_xfer_offset,
			       xfer_len);
			tx_data += xfer_len;
		}

		if (xfer->rx_buf)
			gb_xfer->xfer_flags |= GB_SPI_XFER_READ;

		if (xfer == spi->last_xfer) {
			if (!is_last_xfer_done(spi))
				gb_xfer->xfer_flags |= GB_SPI_XFER_INPROGRESS;
			msg->state = GB_SPI_STATE_OP_DONE;
			continue;
		}

		gb_xfer++;
		xfer = get_next_xfer(xfer, msg);
	}

	msg->actual_length += total_len;

	return operation;
}

static void gb_spi_decode_response(struct gb_spilib *spi,
				   struct spi_message *msg,
				   struct gb_spi_transfer_response *response)
{
	struct spi_transfer *xfer = spi->first_xfer;
	void *rx_data = response->data;
	u32 xfer_len;

	while (xfer) {
		/* Copy rx data */
		if (xfer->rx_buf) {
			if (xfer == spi->first_xfer)
				xfer_len = xfer->len - spi->rx_xfer_offset;
			else if (xfer == spi->last_xfer)
				xfer_len = spi->last_xfer_size;
			else
				xfer_len = xfer->len;

			memcpy(xfer->rx_buf + spi->rx_xfer_offset, rx_data,
			       xfer_len);
			rx_data += xfer_len;
		}

		if (xfer == spi->last_xfer)
			break;

		xfer = list_next_entry(xfer, transfer_list);
	}
}

static int gb_spi_transfer_one_message(struct spi_master *master,
				       struct spi_message *msg)
{
	struct gb_spilib *spi = spi_master_get_devdata(master);
	struct gb_connection *connection = spi->connection;
	struct gb_spi_transfer_response *response;
	struct gb_operation *operation;
	int ret = 0;

	spi->first_xfer = list_first_entry_or_null(&msg->transfers,
						   struct spi_transfer,
						   transfer_list);
	if (!spi->first_xfer) {
		ret = -ENOMEM;
		goto out;
	}

	msg->state = GB_SPI_STATE_MSG_IDLE;

	while (msg->state != GB_SPI_STATE_MSG_DONE &&
	       msg->state != GB_SPI_STATE_MSG_ERROR) {
		operation = gb_spi_operation_create(spi, connection, msg);
		if (!operation) {
			msg->state = GB_SPI_STATE_MSG_ERROR;
			ret = -EINVAL;
			continue;
		}

		ret = gb_operation_request_send_sync_timeout(operation,
							     spi->op_timeout);
		if (!ret) {
			response = operation->response->payload;
			if (response)
				gb_spi_decode_response(spi, msg, response);
		} else {
			dev_err(spi->parent,
				"transfer operation failed: %d\n", ret);
			msg->state = GB_SPI_STATE_MSG_ERROR;
		}

		gb_operation_put(operation);
		setup_next_xfer(spi, msg);
	}

out:
	msg->status = ret;
	clean_xfer_state(spi);
	spi_finalize_current_message(master);

	return ret;
}

static int gb_spi_prepare_transfer_hardware(struct spi_master *master)
{
	struct gb_spilib *spi = spi_master_get_devdata(master);

	return spi->ops->prepare_transfer_hardware(spi->parent);
}

static int gb_spi_unprepare_transfer_hardware(struct spi_master *master)
{
	struct gb_spilib *spi = spi_master_get_devdata(master);

	spi->ops->unprepare_transfer_hardware(spi->parent);

	return 0;
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

static int gb_spi_get_master_config(struct gb_spilib *spi)
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
	spi->num_chipselect = response.num_chipselect;

	spi->min_speed_hz = le32_to_cpu(response.min_speed_hz);
	spi->max_speed_hz = le32_to_cpu(response.max_speed_hz);

	return 0;
}

static int gb_spi_setup_device(struct gb_spilib *spi, u8 cs)
{
	struct spi_master *master = get_master_from_spi(spi);
	struct gb_spi_device_config_request request;
	struct gb_spi_device_config_response response;
	struct spi_board_info spi_board = { {0} };
	struct spi_device *spidev;
	int ret;
	u8 dev_type;

	request.chip_select = cs;

	ret = gb_operation_sync(spi->connection, GB_SPI_TYPE_DEVICE_CONFIG,
				&request, sizeof(request),
				&response, sizeof(response));
	if (ret < 0)
		return ret;

	dev_type = response.device_type;

	if (dev_type == GB_SPI_SPI_DEV)
		strlcpy(spi_board.modalias, "spidev",
			sizeof(spi_board.modalias));
	else if (dev_type == GB_SPI_SPI_NOR)
		strlcpy(spi_board.modalias, "spi-nor",
			sizeof(spi_board.modalias));
	else if (dev_type == GB_SPI_SPI_MODALIAS)
		memcpy(spi_board.modalias, response.name,
		       sizeof(spi_board.modalias));
	else
		return -EINVAL;

	spi_board.mode		= le16_to_cpu(response.mode);
	spi_board.bus_num	= master->bus_num;
	spi_board.chip_select	= cs;
	spi_board.max_speed_hz	= le32_to_cpu(response.max_speed_hz);

	spidev = spi_new_device(master, &spi_board);
	if (!spidev)
		return -EINVAL;

	return 0;
}

int gb_spilib_master_init(struct gb_connection *connection, struct device *dev,
			  struct spilib_ops *ops)
{
	struct gb_spilib *spi;
	struct spi_master *master;
	int ret;
	u8 i;

	/* Allocate master with space for data */
	master = spi_alloc_master(dev, sizeof(*spi));
	if (!master) {
		dev_err(dev, "cannot alloc SPI master\n");
		return -ENOMEM;
	}

	spi = spi_master_get_devdata(master);
	spi->connection = connection;
	gb_connection_set_data(connection, master);
	spi->parent = dev;
	spi->ops = ops;

	/* get master configuration */
	ret = gb_spi_get_master_config(spi);
	if (ret)
		goto exit_spi_put;

	master->bus_num = -1; /* Allow spi-core to allocate it dynamically */
	master->num_chipselect = spi->num_chipselect;
	master->mode_bits = spi->mode;
	master->flags = spi->flags;
	master->bits_per_word_mask = spi->bits_per_word_mask;

	/* Attach methods */
	master->cleanup = gb_spi_cleanup;
	master->setup = gb_spi_setup;
	master->transfer_one_message = gb_spi_transfer_one_message;

	if (ops && ops->prepare_transfer_hardware) {
		master->prepare_transfer_hardware =
			gb_spi_prepare_transfer_hardware;
	}

	if (ops && ops->unprepare_transfer_hardware) {
		master->unprepare_transfer_hardware =
			gb_spi_unprepare_transfer_hardware;
	}

	master->auto_runtime_pm = true;

	ret = spi_register_master(master);
	if (ret < 0)
		goto exit_spi_put;

	/* now, fetch the devices configuration */
	for (i = 0; i < spi->num_chipselect; i++) {
		ret = gb_spi_setup_device(spi, i);
		if (ret < 0) {
			dev_err(dev, "failed to allocate spi device %d: %d\n",
				i, ret);
			goto exit_spi_unregister;
		}
	}

	return 0;

exit_spi_unregister:
	spi_unregister_master(master);
exit_spi_put:
	spi_master_put(master);

	return ret;
}
EXPORT_SYMBOL_GPL(gb_spilib_master_init);

void gb_spilib_master_exit(struct gb_connection *connection)
{
	struct spi_master *master = gb_connection_get_data(connection);

	spi_unregister_master(master);
	spi_master_put(master);
}
EXPORT_SYMBOL_GPL(gb_spilib_master_exit);

MODULE_LICENSE("GPL v2");
