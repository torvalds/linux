// SPDX-License-Identifier: GPL-2.0
/*
 * System Control and Management Interface (SCMI) Message Protocol
 * driver common header file containing some definitions, structures
 * and function prototypes used in all the different SCMI protocols.
 *
 * Copyright (C) 2018 ARM Ltd.
 */

#include <linux/completion.h>
#include <linux/scmi_protocol.h>
#include <linux/types.h>

/**
 * struct scmi_msg_hdr - Message(Tx/Rx) header
 *
 * @id: The identifier of the command being sent
 * @protocol_id: The identifier of the protocol used to send @id command
 * @seq: The token to identify the message. when a message/command returns,
 *       the platform returns the whole message header unmodified including
 *	 the token.
 */
struct scmi_msg_hdr {
	u8 id;
	u8 protocol_id;
	u16 seq;
	u32 status;
	bool poll_completion;
};

/**
 * struct scmi_msg - Message(Tx/Rx) structure
 *
 * @buf: Buffer pointer
 * @len: Length of data in the Buffer
 */
struct scmi_msg {
	void *buf;
	size_t len;
};

/**
 * struct scmi_xfer - Structure representing a message flow
 *
 * @hdr: Transmit message header
 * @tx: Transmit message
 * @rx: Receive message, the buffer should be pre-allocated to store
 *	message. If request-ACK protocol is used, we can reuse the same
 *	buffer for the rx path as we use for the tx path.
 * @done: completion event
 */

struct scmi_xfer {
	void *con_priv;
	struct scmi_msg_hdr hdr;
	struct scmi_msg tx;
	struct scmi_msg rx;
	struct completion done;
};

void scmi_one_xfer_put(const struct scmi_handle *h, struct scmi_xfer *xfer);
int scmi_do_xfer(const struct scmi_handle *h, struct scmi_xfer *xfer);
int scmi_one_xfer_init(const struct scmi_handle *h, u8 msg_id, u8 prot_id,
		       size_t tx_size, size_t rx_size, struct scmi_xfer **p);
int scmi_handle_put(const struct scmi_handle *handle);
struct scmi_handle *scmi_handle_get(struct device *dev);
