/* SPDX-License-Identifier: GPL-2.0 */
/*
 * System Control and Management Interface (SCMI) Message Protocol
 * protocols common header file containing some definitions, structures
 * and function prototypes used in all the different SCMI protocols.
 *
 * Copyright (C) 2022 ARM Ltd.
 */
#ifndef _SCMI_PROTOCOLS_H
#define _SCMI_PROTOCOLS_H

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

#include <linux/unaligned.h>

#define PROTOCOL_REV_MINOR_MASK	GENMASK(15, 0)
#define PROTOCOL_REV_MAJOR_MASK	GENMASK(31, 16)
#define PROTOCOL_REV_MAJOR(x)	((u16)(FIELD_GET(PROTOCOL_REV_MAJOR_MASK, (x))))
#define PROTOCOL_REV_MINOR(x)	((u16)(FIELD_GET(PROTOCOL_REV_MINOR_MASK, (x))))

#define SCMI_PROTOCOL_VENDOR_BASE	0x80

#define MSG_SUPPORTS_FASTCHANNEL(x)	((x) & BIT(0))

enum scmi_common_cmd {
	PROTOCOL_VERSION = 0x0,
	PROTOCOL_ATTRIBUTES = 0x1,
	PROTOCOL_MESSAGE_ATTRIBUTES = 0x2,
	NEGOTIATE_PROTOCOL_VERSION = 0x10,
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
 * @flags: Optional flags associated to this xfer.
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
#define SCMI_XFER_FLAG_IS_RAW	BIT(0)
#define SCMI_XFER_IS_RAW(x)	((x)->flags & SCMI_XFER_FLAG_IS_RAW)
#define SCMI_XFER_FLAG_CHAN_SET	BIT(1)
#define SCMI_XFER_IS_CHAN_SET(x)	\
	((x)->flags & SCMI_XFER_FLAG_CHAN_SET)
	int flags;
	/* A lock to protect state and busy fields */
	spinlock_t lock;
	void *priv;
};

struct scmi_xfer_ops;
struct scmi_proto_helpers_ops;

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
	const struct scmi_proto_helpers_ops *hops;
	int (*set_priv)(const struct scmi_protocol_handle *ph, void *priv,
			u32 version);
	void *(*get_priv)(const struct scmi_protocol_handle *ph);
};

/**
 * struct scmi_iterator_state  - Iterator current state descriptor
 * @desc_index: Starting index for the current mulit-part request.
 * @num_returned: Number of returned items in the last multi-part reply.
 * @num_remaining: Number of remaining items in the multi-part message.
 * @max_resources: Maximum acceptable number of items, configured by the caller
 *		   depending on the underlying resources that it is querying.
 * @loop_idx: The iterator loop index in the current multi-part reply.
 * @rx_len: Size in bytes of the currenly processed message; it can be used by
 *	    the user of the iterator to verify a reply size.
 * @priv: Optional pointer to some additional state-related private data setup
 *	  by the caller during the iterations.
 */
struct scmi_iterator_state {
	unsigned int desc_index;
	unsigned int num_returned;
	unsigned int num_remaining;
	unsigned int max_resources;
	unsigned int loop_idx;
	size_t rx_len;
	void *priv;
};

/**
 * struct scmi_iterator_ops  - Custom iterator operations
 * @prepare_message: An operation to provide the custom logic to fill in the
 *		     SCMI command request pointed by @message. @desc_index is
 *		     a reference to the next index to use in the multi-part
 *		     request.
 * @update_state: An operation to provide the custom logic to update the
 *		  iterator state from the actual message response.
 * @process_response: An operation to provide the custom logic needed to process
 *		      each chunk of the multi-part message.
 */
struct scmi_iterator_ops {
	void (*prepare_message)(void *message, unsigned int desc_index,
				const void *priv);
	int (*update_state)(struct scmi_iterator_state *st,
			    const void *response, void *priv);
	int (*process_response)(const struct scmi_protocol_handle *ph,
				const void *response,
				struct scmi_iterator_state *st, void *priv);
};

struct scmi_fc_db_info {
	int width;
	u64 set;
	u64 mask;
	void __iomem *addr;
};

struct scmi_fc_info {
	void __iomem *set_addr;
	void __iomem *get_addr;
	struct scmi_fc_db_info *set_db;
	u32 rate_limit;
};

/**
 * struct scmi_proto_helpers_ops  - References to common protocol helpers
 * @extended_name_get: A common helper function to retrieve extended naming
 *		       for the specified resource using the specified command.
 *		       Result is returned as a NULL terminated string in the
 *		       pre-allocated area pointed to by @name with maximum
 *		       capacity of @len bytes.
 * @iter_response_init: A common helper to initialize a generic iterator to
 *			parse multi-message responses: when run the iterator
 *			will take care to send the initial command request as
 *			specified by @msg_id and @tx_size and then to parse the
 *			multi-part responses using the custom operations
 *			provided in @ops.
 * @iter_response_run: A common helper to trigger the run of a previously
 *		       initialized iterator.
 * @protocol_msg_check: A common helper to check is a specific protocol message
 *			is supported.
 * @fastchannel_init: A common helper used to initialize FC descriptors by
 *		      gathering FC descriptions from the SCMI platform server.
 * @fastchannel_db_ring: A common helper to ring a FC doorbell.
 * @get_max_msg_size: A common helper to get the maximum message size.
 */
struct scmi_proto_helpers_ops {
	int (*extended_name_get)(const struct scmi_protocol_handle *ph,
				 u8 cmd_id, u32 res_id, u32 *flags, char *name,
				 size_t len);
	void *(*iter_response_init)(const struct scmi_protocol_handle *ph,
				    struct scmi_iterator_ops *ops,
				    unsigned int max_resources, u8 msg_id,
				    size_t tx_size, void *priv);
	int (*iter_response_run)(void *iter);
	int (*protocol_msg_check)(const struct scmi_protocol_handle *ph,
				  u32 message_id, u32 *attributes);
	void (*fastchannel_init)(const struct scmi_protocol_handle *ph,
				 u8 describe_id, u32 message_id,
				 u32 valid_size, u32 domain,
				 void __iomem **p_addr,
				 struct scmi_fc_db_info **p_db,
				 u32 *rate_limit);
	void (*fastchannel_db_ring)(struct scmi_fc_db_info *db);
	int (*get_max_msg_size)(const struct scmi_protocol_handle *ph);
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
 * @supported_version: The highest version currently supported for this
 *		       protocol by the agent. Each protocol implementation
 *		       in the agent is supposed to downgrade to match the
 *		       protocol version supported by the platform.
 * @vendor_id: A firmware vendor string for vendor protocols matching.
 *	       Ignored when @id identifies a standard protocol, cannot be NULL
 *	       otherwise.
 * @sub_vendor_id: A firmware sub_vendor string for vendor protocols matching.
 *		   Ignored if NULL or when @id identifies a standard protocol.
 * @impl_ver: A firmware implementation version for vendor protocols matching.
 *	      Ignored if zero or if @id identifies a standard protocol.
 *
 * Note that vendor protocols matching at load time is performed by attempting
 * the closest match first against the tuple (vendor, sub_vendor, impl_ver)
 */
struct scmi_protocol {
	const u8				id;
	struct module				*owner;
	const scmi_prot_init_ph_fn_t		instance_init;
	const scmi_prot_init_ph_fn_t		instance_deinit;
	const void				*ops;
	const struct scmi_protocol_events	*events;
	unsigned int				supported_version;
	char					*vendor_id;
	char					*sub_vendor_id;
	u32					impl_ver;
};

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

#define DECLARE_SCMI_REGISTER_UNREGISTER(func)		\
	int __init scmi_##func##_register(void);	\
	void __exit scmi_##func##_unregister(void)
DECLARE_SCMI_REGISTER_UNREGISTER(base);
DECLARE_SCMI_REGISTER_UNREGISTER(clock);
DECLARE_SCMI_REGISTER_UNREGISTER(perf);
DECLARE_SCMI_REGISTER_UNREGISTER(pinctrl);
DECLARE_SCMI_REGISTER_UNREGISTER(power);
DECLARE_SCMI_REGISTER_UNREGISTER(reset);
DECLARE_SCMI_REGISTER_UNREGISTER(sensors);
DECLARE_SCMI_REGISTER_UNREGISTER(voltage);
DECLARE_SCMI_REGISTER_UNREGISTER(system);
DECLARE_SCMI_REGISTER_UNREGISTER(powercap);

#endif /* _SCMI_PROTOCOLS_H */
