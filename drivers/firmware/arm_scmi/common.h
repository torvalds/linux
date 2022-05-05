/* SPDX-License-Identifier: GPL-2.0 */
/*
 * System Control and Management Interface (SCMI) Message Protocol
 * driver common header file containing some definitions, structures
 * and function prototypes used in all the different SCMI protocols.
 *
 * Copyright (C) 2018-2021 ARM Ltd.
 */
#ifndef _SCMI_COMMON_H
#define _SCMI_COMMON_H

#include <linux/bitfield.h>
#include <linux/completion.h>
#include <linux/device.h>
#include <linux/errno.h>
#include <linux/kernel.h>
#include <linux/hashtable.h>
#include <linux/list.h>
#include <linux/module.h>
#include <linux/refcount.h>
#include <linux/scmi_protocol.h>
#include <linux/spinlock.h>
#include <linux/types.h>

#include <asm/unaligned.h>

#include "notify.h"

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

/*
 * Size of @pending_xfers hashtable included in @scmi_xfers_info; ideally, in
 * order to minimize space and collisions, this should equal max_msg, i.e. the
 * maximum number of in-flight messages on a specific platform, but such value
 * is only available at runtime while kernel hashtables are statically sized:
 * pick instead as a fixed static size the maximum number of entries that can
 * fit the whole table into one 4k page.
 */
#define SCMI_PENDING_XFERS_HT_ORDER_SZ		9

/**
 * struct scmi_msg_hdr - Message(Tx/Rx) header
 *
 * @id: The identifier of the message being sent
 * @protocol_id: The identifier of the protocol used to send @id message
 * @type: The SCMI type for this message
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
	u8 type;
	u16 seq;
	u32 status;
	bool poll_completion;
};

/**
 * pack_scmi_header() - packs and returns 32-bit header
 *
 * @hdr: pointer to header containing all the information on message id,
 *	protocol id, sequence id and type.
 *
 * Return: 32-bit packed message header to be sent to the platform.
 */
static inline u32 pack_scmi_header(struct scmi_msg_hdr *hdr)
{
	return FIELD_PREP(MSG_ID_MASK, hdr->id) |
		FIELD_PREP(MSG_TYPE_MASK, hdr->type) |
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
	hdr->type = MSG_XTRACT_TYPE(msg_hdr);
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
 * @pending: True for xfers added to @pending_xfers hashtable
 * @node: An hlist_node reference used to store this xfer, alternatively, on
 *	  the free list @free_xfers or in the @pending_xfers hashtable
 * @users: A refcount to track the active users for this xfer.
 *	   This is meant to protect against the possibility that, when a command
 *	   transaction times out concurrently with the reception of a valid
 *	   response message, the xfer could be finally put on the TX path, and
 *	   so vanish, while on the RX path scmi_rx_callback() is still
 *	   processing it: in such a case this refcounting will ensure that, even
 *	   though the timed-out transaction will anyway cause the command
 *	   request to be reported as failed by time-out, the underlying xfer
 *	   cannot be discarded and possibly reused until the last one user on
 *	   the RX path has released it.
 * @busy: An atomic flag to ensure exclusive write access to this xfer
 * @state: The current state of this transfer, with states transitions deemed
 *	   valid being:
 *	    - SCMI_XFER_SENT_OK -> SCMI_XFER_RESP_OK [ -> SCMI_XFER_DRESP_OK ]
 *	    - SCMI_XFER_SENT_OK -> SCMI_XFER_DRESP_OK
 *	      (Missing synchronous response is assumed OK and ignored)
 * @lock: A spinlock to protect state and busy fields.
 * @priv: A pointer for transport private usage.
 */
struct scmi_xfer {
	int transfer_id;
	struct scmi_msg_hdr hdr;
	struct scmi_msg tx;
	struct scmi_msg rx;
	struct completion done;
	struct completion *async_done;
	bool pending;
	struct hlist_node node;
	refcount_t users;
#define SCMI_XFER_FREE		0
#define SCMI_XFER_BUSY		1
	atomic_t busy;
#define SCMI_XFER_SENT_OK	0
#define SCMI_XFER_RESP_OK	1
#define SCMI_XFER_DRESP_OK	2
	int state;
	/* A lock to protect state and busy fields */
	spinlock_t lock;
	void *priv;
};

/*
 * An helper macro to lookup an xfer from the @pending_xfers hashtable
 * using the message sequence number token as a key.
 */
#define XFER_FIND(__ht, __k)					\
({								\
	typeof(__k) k_ = __k;					\
	struct scmi_xfer *xfer_ = NULL;				\
								\
	hash_for_each_possible((__ht), xfer_, node, k_)		\
		if (xfer_->hdr.seq == k_)			\
			break;					\
	xfer_;							\
})

struct scmi_xfer_ops;

/**
 * struct scmi_protocol_handle  - Reference to an initialized protocol instance
 *
 * @dev: A reference to the associated SCMI instance device (handle->dev).
 * @xops: A reference to a struct holding refs to the core xfer operations that
 *	  can be used by the protocol implementation to generate SCMI messages.
 * @set_priv: A method to set protocol private data for this instance.
 * @get_priv: A method to get protocol private data previously set.
 *
 * This structure represents a protocol initialized against specific SCMI
 * instance and it will be used as follows:
 * - as a parameter fed from the core to the protocol initialization code so
 *   that it can access the core xfer operations to build and generate SCMI
 *   messages exclusively for the specific underlying protocol instance.
 * - as an opaque handle fed by an SCMI driver user when it tries to access
 *   this protocol through its own protocol operations.
 *   In this case this handle will be returned as an opaque object together
 *   with the related protocol operations when the SCMI driver tries to access
 *   the protocol.
 */
struct scmi_protocol_handle {
	struct device *dev;
	const struct scmi_xfer_ops *xops;
	int (*set_priv)(const struct scmi_protocol_handle *ph, void *priv);
	void *(*get_priv)(const struct scmi_protocol_handle *ph);
};

/**
 * struct scmi_xfer_ops  - References to the core SCMI xfer operations.
 * @version_get: Get this version protocol.
 * @xfer_get_init: Initialize one struct xfer if any xfer slot is free.
 * @reset_rx_to_maxsz: Reset rx size to max transport size.
 * @do_xfer: Do the SCMI transfer.
 * @do_xfer_with_response: Do the SCMI transfer waiting for a response.
 * @xfer_put: Free the xfer slot.
 *
 * Note that all this operations expect a protocol handle as first parameter;
 * they then internally use it to infer the underlying protocol number: this
 * way is not possible for a protocol implementation to forge messages for
 * another protocol.
 */
struct scmi_xfer_ops {
	int (*version_get)(const struct scmi_protocol_handle *ph, u32 *version);
	int (*xfer_get_init)(const struct scmi_protocol_handle *ph, u8 msg_id,
			     size_t tx_size, size_t rx_size,
			     struct scmi_xfer **p);
	void (*reset_rx_to_maxsz)(const struct scmi_protocol_handle *ph,
				  struct scmi_xfer *xfer);
	int (*do_xfer)(const struct scmi_protocol_handle *ph,
		       struct scmi_xfer *xfer);
	int (*do_xfer_with_response)(const struct scmi_protocol_handle *ph,
				     struct scmi_xfer *xfer);
	void (*xfer_put)(const struct scmi_protocol_handle *ph,
			 struct scmi_xfer *xfer);
};

struct scmi_revision_info *
scmi_revision_area_get(const struct scmi_protocol_handle *ph);
int scmi_handle_put(const struct scmi_handle *handle);
struct scmi_handle *scmi_handle_get(struct device *dev);
void scmi_set_handle(struct scmi_device *scmi_dev);
void scmi_setup_protocol_implemented(const struct scmi_protocol_handle *ph,
				     u8 *prot_imp);

typedef int (*scmi_prot_init_ph_fn_t)(const struct scmi_protocol_handle *);

/**
 * struct scmi_protocol  - Protocol descriptor
 * @id: Protocol ID.
 * @owner: Module reference if any.
 * @instance_init: Mandatory protocol initialization function.
 * @instance_deinit: Optional protocol de-initialization function.
 * @ops: Optional reference to the operations provided by the protocol and
 *	 exposed in scmi_protocol.h.
 * @events: An optional reference to the events supported by this protocol.
 */
struct scmi_protocol {
	const u8				id;
	struct module				*owner;
	const scmi_prot_init_ph_fn_t		instance_init;
	const scmi_prot_init_ph_fn_t		instance_deinit;
	const void				*ops;
	const struct scmi_protocol_events	*events;
};

int __init scmi_bus_init(void);
void __exit scmi_bus_exit(void);

#define DECLARE_SCMI_REGISTER_UNREGISTER(func)		\
	int __init scmi_##func##_register(void);	\
	void __exit scmi_##func##_unregister(void)
DECLARE_SCMI_REGISTER_UNREGISTER(base);
DECLARE_SCMI_REGISTER_UNREGISTER(clock);
DECLARE_SCMI_REGISTER_UNREGISTER(perf);
DECLARE_SCMI_REGISTER_UNREGISTER(power);
DECLARE_SCMI_REGISTER_UNREGISTER(reset);
DECLARE_SCMI_REGISTER_UNREGISTER(sensors);
DECLARE_SCMI_REGISTER_UNREGISTER(voltage);
DECLARE_SCMI_REGISTER_UNREGISTER(system);

#define DEFINE_SCMI_PROTOCOL_REGISTER_UNREGISTER(name, proto)	\
static const struct scmi_protocol *__this_proto = &(proto);	\
								\
int __init scmi_##name##_register(void)				\
{								\
	return scmi_protocol_register(__this_proto);		\
}								\
								\
void __exit scmi_##name##_unregister(void)			\
{								\
	scmi_protocol_unregister(__this_proto);			\
}

const struct scmi_protocol *scmi_protocol_get(int protocol_id);
void scmi_protocol_put(int protocol_id);

int scmi_protocol_acquire(const struct scmi_handle *handle, u8 protocol_id);
void scmi_protocol_release(const struct scmi_handle *handle, u8 protocol_id);

/* SCMI Transport */
/**
 * struct scmi_chan_info - Structure representing a SCMI channel information
 *
 * @dev: Reference to device in the SCMI hierarchy corresponding to this
 *	 channel
 * @handle: Pointer to SCMI entity handle
 * @no_completion_irq: Flag to indicate that this channel has no completion
 *		       interrupt mechanism for synchronous commands.
 *		       This can be dynamically set by transports at run-time
 *		       inside their provided .chan_setup().
 * @transport_info: Transport layer related information
 */
struct scmi_chan_info {
	struct device *dev;
	struct scmi_handle *handle;
	bool no_completion_irq;
	void *transport_info;
};

/**
 * struct scmi_transport_ops - Structure representing a SCMI transport ops
 *
 * @link_supplier: Optional callback to add link to a supplier device
 * @chan_available: Callback to check if channel is available or not
 * @chan_setup: Callback to allocate and setup a channel
 * @chan_free: Callback to free a channel
 * @get_max_msg: Optional callback to provide max_msg dynamically
 *		 Returns the maximum number of messages for the channel type
 *		 (tx or rx) that can be pending simultaneously in the system
 * @send_message: Callback to send a message
 * @mark_txdone: Callback to mark tx as done
 * @fetch_response: Callback to fetch response
 * @fetch_notification: Callback to fetch notification
 * @clear_channel: Callback to clear a channel
 * @poll_done: Callback to poll transfer status
 */
struct scmi_transport_ops {
	int (*link_supplier)(struct device *dev);
	bool (*chan_available)(struct device *dev, int idx);
	int (*chan_setup)(struct scmi_chan_info *cinfo, struct device *dev,
			  bool tx);
	int (*chan_free)(int id, void *p, void *data);
	unsigned int (*get_max_msg)(struct scmi_chan_info *base_cinfo);
	int (*send_message)(struct scmi_chan_info *cinfo,
			    struct scmi_xfer *xfer);
	void (*mark_txdone)(struct scmi_chan_info *cinfo, int ret,
			    struct scmi_xfer *xfer);
	void (*fetch_response)(struct scmi_chan_info *cinfo,
			       struct scmi_xfer *xfer);
	void (*fetch_notification)(struct scmi_chan_info *cinfo,
				   size_t max_len, struct scmi_xfer *xfer);
	void (*clear_channel)(struct scmi_chan_info *cinfo);
	bool (*poll_done)(struct scmi_chan_info *cinfo, struct scmi_xfer *xfer);
};

int scmi_protocol_device_request(const struct scmi_device_id *id_table);
void scmi_protocol_device_unrequest(const struct scmi_device_id *id_table);
struct scmi_device *scmi_child_dev_find(struct device *parent,
					int prot_id, const char *name);

/**
 * struct scmi_desc - Description of SoC integration
 *
 * @transport_init: An optional function that a transport can provide to
 *		    initialize some transport-specific setup during SCMI core
 *		    initialization, so ahead of SCMI core probing.
 * @transport_exit: An optional function that a transport can provide to
 *		    de-initialize some transport-specific setup during SCMI core
 *		    de-initialization, so after SCMI core removal.
 * @ops: Pointer to the transport specific ops structure
 * @max_rx_timeout_ms: Timeout for communication with SoC (in Milliseconds)
 * @max_msg: Maximum number of messages for a channel type (tx or rx) that can
 *	be pending simultaneously in the system. May be overridden by the
 *	get_max_msg op.
 * @max_msg_size: Maximum size of data per message that can be handled.
 * @force_polling: Flag to force this whole transport to use SCMI core polling
 *		   mechanism instead of completion interrupts even if available.
 * @sync_cmds_completed_on_ret: Flag to indicate that the transport assures
 *				synchronous-command messages are atomically
 *				completed on .send_message: no need to poll
 *				actively waiting for a response.
 *				Used by core internally only when polling is
 *				selected as a waiting for reply method: i.e.
 *				if a completion irq was found use that anyway.
 * @atomic_enabled: Flag to indicate that this transport, which is assured not
 *		    to sleep anywhere on the TX path, can be used in atomic mode
 *		    when requested.
 */
struct scmi_desc {
	int (*transport_init)(void);
	void (*transport_exit)(void);
	const struct scmi_transport_ops *ops;
	int max_rx_timeout_ms;
	int max_msg;
	int max_msg_size;
	const bool force_polling;
	const bool sync_cmds_completed_on_ret;
	const bool atomic_enabled;
};

#ifdef CONFIG_ARM_SCMI_TRANSPORT_MAILBOX
extern const struct scmi_desc scmi_mailbox_desc;
#endif
#ifdef CONFIG_ARM_SCMI_TRANSPORT_SMC
extern const struct scmi_desc scmi_smc_desc;
#endif
#ifdef CONFIG_ARM_SCMI_TRANSPORT_VIRTIO
extern const struct scmi_desc scmi_virtio_desc;
#endif
#ifdef CONFIG_ARM_SCMI_TRANSPORT_OPTEE
extern const struct scmi_desc scmi_optee_desc;
#endif

void scmi_rx_callback(struct scmi_chan_info *cinfo, u32 msg_hdr, void *priv);
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

/* declarations for message passing transports */
struct scmi_msg_payld;

/* Maximum overhead of message w.r.t. struct scmi_desc.max_msg_size */
#define SCMI_MSG_MAX_PROT_OVERHEAD (2 * sizeof(__le32))

size_t msg_response_size(struct scmi_xfer *xfer);
size_t msg_command_size(struct scmi_xfer *xfer);
void msg_tx_prepare(struct scmi_msg_payld *msg, struct scmi_xfer *xfer);
u32 msg_read_header(struct scmi_msg_payld *msg);
void msg_fetch_response(struct scmi_msg_payld *msg, size_t len,
			struct scmi_xfer *xfer);
void msg_fetch_notification(struct scmi_msg_payld *msg, size_t len,
			    size_t max_len, struct scmi_xfer *xfer);

void scmi_notification_instance_data_set(const struct scmi_handle *handle,
					 void *priv);
void *scmi_notification_instance_data_get(const struct scmi_handle *handle);
#endif /* _SCMI_COMMON_H */
