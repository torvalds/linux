// SPDX-License-Identifier: GPL-2.0
/*
 * For transports using message passing.
 *
 * Derived from shm.c.
 *
 * Copyright (C) 2019-2024 ARM Ltd.
 * Copyright (C) 2020-2021 OpenSynergy GmbH
 */

#include <linux/types.h>

#include "common.h"

/*
 * struct scmi_msg_payld - Transport SDU layout
 *
 * The SCMI specification requires all parameters, message headers, return
 * arguments or any protocol data to be expressed in little endian format only.
 */
struct scmi_msg_payld {
	__le32 msg_header;
	__le32 msg_payload[];
};

/**
 * msg_command_size() - Actual size of transport SDU for command.
 *
 * @xfer: message which core has prepared for sending
 *
 * Return: transport SDU size.
 */
static size_t msg_command_size(struct scmi_xfer *xfer)
{
	return sizeof(struct scmi_msg_payld) + xfer->tx.len;
}

/**
 * msg_response_size() - Maximum size of transport SDU for response.
 *
 * @xfer: message which core has prepared for sending
 *
 * Return: transport SDU size.
 */
static size_t msg_response_size(struct scmi_xfer *xfer)
{
	return sizeof(struct scmi_msg_payld) + sizeof(__le32) + xfer->rx.len;
}

/**
 * msg_tx_prepare() - Set up transport SDU for command.
 *
 * @msg: transport SDU for command
 * @xfer: message which is being sent
 */
static void msg_tx_prepare(struct scmi_msg_payld *msg, struct scmi_xfer *xfer)
{
	msg->msg_header = cpu_to_le32(pack_scmi_header(&xfer->hdr));
	if (xfer->tx.buf)
		memcpy(msg->msg_payload, xfer->tx.buf, xfer->tx.len);
}

/**
 * msg_read_header() - Read SCMI header from transport SDU.
 *
 * @msg: transport SDU
 *
 * Return: SCMI header
 */
static u32 msg_read_header(struct scmi_msg_payld *msg)
{
	return le32_to_cpu(msg->msg_header);
}

/**
 * msg_fetch_response() - Fetch response SCMI payload from transport SDU.
 *
 * @msg: transport SDU with response
 * @len: transport SDU size
 * @xfer: message being responded to
 */
static void msg_fetch_response(struct scmi_msg_payld *msg,
			       size_t len, struct scmi_xfer *xfer)
{
	size_t prefix_len = sizeof(*msg) + sizeof(msg->msg_payload[0]);

	xfer->hdr.status = le32_to_cpu(msg->msg_payload[0]);
	xfer->rx.len = min_t(size_t, xfer->rx.len,
			     len >= prefix_len ? len - prefix_len : 0);

	/* Take a copy to the rx buffer.. */
	memcpy(xfer->rx.buf, &msg->msg_payload[1], xfer->rx.len);
}

/**
 * msg_fetch_notification() - Fetch notification payload from transport SDU.
 *
 * @msg: transport SDU with notification
 * @len: transport SDU size
 * @max_len: maximum SCMI payload size to fetch
 * @xfer: notification message
 */
static void msg_fetch_notification(struct scmi_msg_payld *msg, size_t len,
				   size_t max_len, struct scmi_xfer *xfer)
{
	xfer->rx.len = min_t(size_t, max_len,
			     len >= sizeof(*msg) ? len - sizeof(*msg) : 0);

	/* Take a copy to the rx buffer.. */
	memcpy(xfer->rx.buf, msg->msg_payload, xfer->rx.len);
}

static const struct scmi_message_operations scmi_msg_ops = {
	.tx_prepare = msg_tx_prepare,
	.command_size = msg_command_size,
	.response_size = msg_response_size,
	.read_header = msg_read_header,
	.fetch_response = msg_fetch_response,
	.fetch_notification = msg_fetch_notification,
};

const struct scmi_message_operations *scmi_message_operations_get(void)
{
	return &scmi_msg_ops;
}
