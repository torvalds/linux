/* SPDX-License-Identifier: GPL-2.0 */
/*
 * System Control and Management Interface (SCMI) Message Protocol
 * driver common header file containing some definitions, structures
 * and function prototypes used in all the different SCMI protocols.
 *
 * Copyright (C) 2018 ARM Ltd.
 */
#ifndef _SCMI_COMMON_H
#define _SCMI_COMMON_H

#include <linux/bitfield.h>
#include <linux/completion.h>
#include <linux/device.h>
#include <linux/errno.h>
#include <linux/kernel.h>
#include <linux/scmi_protocol.h>
#include <linux/types.h>

#include <asm/unaligned.h>

#define PROTOCOL_REV_MINOR_MASK	GENMASK(15, 0)
#define PROTOCOL_REV_MAJOR_MASK	GENMASK(31, 16)
#define PROTOCOL_REV_MAJOR(x)	(u16)(FIELD_GET(PROTOCOL_REV_MAJOR_MASK, (x)))
#define PROTOCOL_REV_MINOR(x)	(u16)(FIELD_GET(PROTOCOL_REV_MINOR_MASK, (x)))
#define MAX_PROTOCOLS_IMP	16
#define MAX_OPPS		16

enum scmi_common_cmd {
	PROTOCOL_VERSION = 0x0,
	PROTOCOL_ATTRIBUTES = 0x1,
	PROTOCOL_MESSAGE_ATTRIBUTES = 0x2,
};

/**
 * struct scmi_msg_resp_prot_version - Response for a message
 *
 * @minor_version: Minor version of the ABI that firmware supports
 * @major_version: Major version of the ABI that firmware supports
 *
 * In general, ABI version changes follow the rule that minor version increments
 * are backward compatible. Major revision changes in ABI may not be
 * backward compatible.
 *
 * Response to a generic message with message type SCMI_MSG_VERSION
 */
struct scmi_msg_resp_prot_version {
	__le16 minor_version;
	__le16 major_version;
};

#define MSG_ID_MASK		GENMASK(7, 0)
#define MSG_XTRACT_ID(hdr)	FIELD_GET(MSG_ID_MASK, (hdr))
#define MSG_TYPE_MASK		GENMASK(9, 8)
#define MSG_XTRACT_TYPE(hdr)	FIELD_GET(MSG_TYPE_MASK, (hdr))
#define MSG_TYPE_COMMAND	0
#define MSG_TYPE_DELAYED_RESP	2
#define MSG_TYPE_NOTIFICATION	3
#define MSG_PROTOCOL_ID_MASK	GENMASK(17, 10)
#define MSG_XTRACT_PROT_ID(hdr)	FIELD_GET(MSG_PROTOCOL_ID_MASK, (hdr))
#define MSG_TOKEN_ID_MASK	GENMASK(27, 18)
#define MSG_XTRACT_TOKEN(hdr)	FIELD_GET(MSG_TOKEN_ID_MASK, (hdr))
#define MSG_TOKEN_MAX		(MSG_XTRACT_TOKEN(MSG_TOKEN_ID_MASK) + 1)

/**
 * struct scmi_msg_hdr - Message(Tx/Rx) header
 *
 * @id: The identifier of the message being sent
 * @protocol_id: The identifier of the protocol used to send @id message
 * @seq: The token to identify the message. When a message returns, the
 *	platform returns the whole message header unmodified including the
 *	token
 * @status: Status of the transfer once it's complete
 * @poll_completion: Indicate if the transfer needs to be polled for
 *	completion or interrupt mode is used
 */
struct scmi_msg_hdr {
	u8 id;
	u8 protocol_id;
	u16 seq;
	u32 status;
	bool poll_completion;
};

/**
 * pack_scmi_header() - packs and returns 32-bit header
 *
 * @hdr: pointer to header containing all the information on message id,
 *	protocol id and sequence id.
 *
 * Return: 32-bit packed message header to be sent to the platform.
 */
static inline u32 pack_scmi_header(struct scmi_msg_hdr *hdr)
{
	return FIELD_PREP(MSG_ID_MASK, hdr->id) |
		FIELD_PREP(MSG_TOKEN_ID_MASK, hdr->seq) |
		FIELD_PREP(MSG_PROTOCOL_ID_MASK, hdr->protocol_id);
}

/**
 * unpack_scmi_header() - unpacks and records message and protocol id
 *
 * @msg_hdr: 32-bit packed message header sent from the platform
 * @hdr: pointer to header to fetch message and protocol id.
 */
static inline void unpack_scmi_header(u32 msg_hdr, struct scmi_msg_hdr *hdr)
{
	hdr->id = MSG_XTRACT_ID(msg_hdr);
	hdr->protocol_id = MSG_XTRACT_PROT_ID(msg_hdr);
}

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
 * @transfer_id: Unique ID for debug & profiling purpose
 * @hdr: Transmit message header
 * @tx: Transmit message
 * @rx: Receive message, the buffer should be pre-allocated to store
 *	message. If request-ACK protocol is used, we can reuse the same
 *	buffer for the rx path as we use for the tx path.
 * @done: command message transmit completion event
 * @async_done: pointer to delayed response message received event completion
 */
struct scmi_xfer {
	int transfer_id;
	struct scmi_msg_hdr hdr;
	struct scmi_msg tx;
	struct scmi_msg rx;
	struct completion done;
	struct completion *async_done;
};

void scmi_xfer_put(const struct scmi_handle *h, struct scmi_xfer *xfer);
int scmi_do_xfer(const struct scmi_handle *h, struct scmi_xfer *xfer);
int scmi_do_xfer_with_response(const struct scmi_handle *h,
			       struct scmi_xfer *xfer);
int scmi_xfer_get_init(const struct scmi_handle *h, u8 msg_id, u8 prot_id,
		       size_t tx_size, size_t rx_size, struct scmi_xfer **p);
void scmi_reset_rx_to_maxsz(const struct scmi_handle *handle,
			    struct scmi_xfer *xfer);
int scmi_handle_put(const struct scmi_handle *handle);
struct scmi_handle *scmi_handle_get(struct device *dev);
void scmi_set_handle(struct scmi_device *scmi_dev);
int scmi_version_get(const struct scmi_handle *h, u8 protocol, u32 *version);
void scmi_setup_protocol_implemented(const struct scmi_handle *handle,
				     u8 *prot_imp);

int scmi_base_protocol_init(struct scmi_handle *h);

int __init scmi_bus_init(void);
void __exit scmi_bus_exit(void);

#define DECLARE_SCMI_REGISTER_UNREGISTER(func)		\
	int __init scmi_##func##_register(void);	\
	void __exit scmi_##func##_unregister(void)
DECLARE_SCMI_REGISTER_UNREGISTER(clock);
DECLARE_SCMI_REGISTER_UNREGISTER(perf);
DECLARE_SCMI_REGISTER_UNREGISTER(power);
DECLARE_SCMI_REGISTER_UNREGISTER(reset);
DECLARE_SCMI_REGISTER_UNREGISTER(sensors);
DECLARE_SCMI_REGISTER_UNREGISTER(system);

#define DEFINE_SCMI_PROTOCOL_REGISTER_UNREGISTER(id, name) \
int __init scmi_##name##_register(void) \
{ \
	return scmi_protocol_register((id), &scmi_##name##_protocol_init); \
} \
\
void __exit scmi_##name##_unregister(void) \
{ \
	scmi_protocol_unregister((id)); \
}

/* SCMI Transport */
/**
 * struct scmi_chan_info - Structure representing a SCMI channel information
 *
 * @dev: Reference to device in the SCMI hierarchy corresponding to this
 *	 channel
 * @handle: Pointer to SCMI entity handle
 * @transport_info: Transport layer related information
 */
struct scmi_chan_info {
	struct device *dev;
	struct scmi_handle *handle;
	void *transport_info;
};

/**
 * struct scmi_transport_ops - Structure representing a SCMI transport ops
 *
 * @chan_available: Callback to check if channel is available or not
 * @chan_setup: Callback to allocate and setup a channel
 * @chan_free: Callback to free a channel
 * @send_message: Callback to send a message
 * @mark_txdone: Callback to mark tx as done
 * @fetch_response: Callback to fetch response
 * @fetch_notification: Callback to fetch notification
 * @clear_channel: Callback to clear a channel
 * @poll_done: Callback to poll transfer status
 */
struct scmi_transport_ops {
	bool (*chan_available)(struct device *dev, int idx);
	int (*chan_setup)(struct scmi_chan_info *cinfo, struct device *dev,
			  bool tx);
	int (*chan_free)(int id, void *p, void *data);
	int (*send_message)(struct scmi_chan_info *cinfo,
			    struct scmi_xfer *xfer);
	void (*mark_txdone)(struct scmi_chan_info *cinfo, int ret);
	void (*fetch_response)(struct scmi_chan_info *cinfo,
			       struct scmi_xfer *xfer);
	void (*fetch_notification)(struct scmi_chan_info *cinfo,
				   size_t max_len, struct scmi_xfer *xfer);
	void (*clear_channel)(struct scmi_chan_info *cinfo);
	bool (*poll_done)(struct scmi_chan_info *cinfo, struct scmi_xfer *xfer);
};

/**
 * struct scmi_desc - Description of SoC integration
 *
 * @ops: Pointer to the transport specific ops structure
 * @max_rx_timeout_ms: Timeout for communication with SoC (in Milliseconds)
 * @max_msg: Maximum number of messages that can be pending
 *	simultaneously in the system
 * @max_msg_size: Maximum size of data per message that can be handled.
 */
struct scmi_desc {
	const struct scmi_transport_ops *ops;
	int max_rx_timeout_ms;
	int max_msg;
	int max_msg_size;
};

extern const struct scmi_desc scmi_mailbox_desc;
#ifdef CONFIG_HAVE_ARM_SMCCC_DISCOVERY
extern const struct scmi_desc scmi_smc_desc;
#endif

void scmi_rx_callback(struct scmi_chan_info *cinfo, u32 msg_hdr);
void scmi_free_channel(struct scmi_chan_info *cinfo, struct idr *idr, int id);

/* shmem related declarations */
struct scmi_shared_mem;

void shmem_tx_prepare(struct scmi_shared_mem __iomem *shmem,
		      struct scmi_xfer *xfer);
u32 shmem_read_header(struct scmi_shared_mem __iomem *shmem);
void shmem_fetch_response(struct scmi_shared_mem __iomem *shmem,
			  struct scmi_xfer *xfer);
void shmem_fetch_notification(struct scmi_shared_mem __iomem *shmem,
			      size_t max_len, struct scmi_xfer *xfer);
void shmem_clear_channel(struct scmi_shared_mem __iomem *shmem);
bool shmem_poll_done(struct scmi_shared_mem __iomem *shmem,
		     struct scmi_xfer *xfer);

#endif /* _SCMI_COMMON_H */
