// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include "qcomtee.h"

#define QCOMTEE_ASYNC_VERSION_1_0 0x00010000U /* Maj: 0x0001, Min: 0x0000. */
#define QCOMTEE_ASYNC_VERSION_1_1 0x00010001U /* Maj: 0x0001, Min: 0x0001. */
#define QCOMTEE_ASYNC_VERSION_1_2 0x00010002U /* Maj: 0x0001, Min: 0x0002. */
#define QCOMTEE_ASYNC_VERSION_CURRENT QCOMTEE_ASYNC_VERSION_1_2

#define QCOMTEE_ASYNC_VERSION_MAJOR(n) upper_16_bits(n)
#define QCOMTEE_ASYNC_VERSION_MINOR(n) lower_16_bits(n)

#define QCOMTEE_ASYNC_VERSION_CURRENT_MAJOR \
	QCOMTEE_ASYNC_VERSION_MAJOR(QCOMTEE_ASYNC_VERSION_CURRENT)
#define QCOMTEE_ASYNC_VERSION_CURRENT_MINOR \
	QCOMTEE_ASYNC_VERSION_MINOR(QCOMTEE_ASYNC_VERSION_CURRENT)

/**
 * struct qcomtee_async_msg_hdr - Asynchronous message header format.
 * @version: current async protocol version of the remote endpoint.
 * @op: async operation.
 *
 * @version specifies the endpoint's (QTEE or driver) supported async protocol.
 * For example, if QTEE sets @version to %QCOMTEE_ASYNC_VERSION_1_1, QTEE
 * handles operations supported in %QCOMTEE_ASYNC_VERSION_1_1 or
 * %QCOMTEE_ASYNC_VERSION_1_0. @op determines the message format.
 */
struct qcomtee_async_msg_hdr {
	u32 version;
	u32 op;
};

/* Size of an empty async message. */
#define QCOMTEE_ASYNC_MSG_ZERO sizeof(struct qcomtee_async_msg_hdr)

/**
 * struct qcomtee_async_release_msg - Release asynchronous message.
 * @hdr: message header as &struct qcomtee_async_msg_hdr.
 * @counts: number of objects in @object_ids.
 * @object_ids: array of object IDs that should be released.
 *
 * Available in Maj = 0x0001, Min >= 0x0000.
 */
struct qcomtee_async_release_msg {
	struct qcomtee_async_msg_hdr hdr;
	u32 counts;
	u32 object_ids[] __counted_by(counts);
};

/**
 * qcomtee_get_async_buffer() - Get the start of the asynchronous message.
 * @oic: context used for the current invocation.
 * @async_buffer: return buffer to extract from or fill in async messages.
 *
 * If @oic is used for direct object invocation, the whole outbound buffer
 * is available for the async message. If @oic is used for a callback request,
 * the tail of the outbound buffer (after the callback request message) is
 * available for the async message.
 *
 * The start of the async buffer is aligned, see qcomtee_msg_offset_align().
 */
static void qcomtee_get_async_buffer(struct qcomtee_object_invoke_ctx *oic,
				     struct qcomtee_buffer *async_buffer)
{
	struct qcomtee_msg_callback *msg;
	unsigned int offset;
	int i;

	if (!(oic->flags & QCOMTEE_OIC_FLAG_BUSY)) {
		/* The outbound buffer is empty. Using the whole buffer. */
		offset = 0;
	} else {
		msg = (struct qcomtee_msg_callback *)oic->out_msg.addr;

		/* Start offset in a message for buffer arguments. */
		offset = qcomtee_msg_buffer_args(struct qcomtee_msg_callback,
						 qcomtee_msg_args(msg));

		/* Add size of IB arguments. */
		qcomtee_msg_for_each_input_buffer(i, msg)
			offset += qcomtee_msg_offset_align(msg->args[i].b.size);

		/* Add size of OB arguments. */
		qcomtee_msg_for_each_output_buffer(i, msg)
			offset += qcomtee_msg_offset_align(msg->args[i].b.size);
	}

	async_buffer->addr = oic->out_msg.addr + offset;
	async_buffer->size = oic->out_msg.size - offset;
}

/**
 * async_release() - Process QTEE async release requests.
 * @oic: context used for the current invocation.
 * @msg: async message for object release.
 * @size: size of the async buffer available.
 *
 * Return: Size of the outbound buffer used when processing @msg.
 */
static size_t async_release(struct qcomtee_object_invoke_ctx *oic,
			    struct qcomtee_async_msg_hdr *async_msg,
			    size_t size)
{
	struct qcomtee_async_release_msg *msg;
	struct qcomtee_object *object;
	int i;

	msg = (struct qcomtee_async_release_msg *)async_msg;

	for (i = 0; i < msg->counts; i++) {
		object = qcomtee_idx_erase(oic, msg->object_ids[i]);
		qcomtee_object_put(object);
	}

	return struct_size(msg, object_ids, msg->counts);
}

/**
 * qcomtee_fetch_async_reqs() - Fetch and process asynchronous messages.
 * @oic: context used for the current invocation.
 *
 * Calls handlers to process the requested operations in the async message.
 * Currently, only supports async release requests.
 */
void qcomtee_fetch_async_reqs(struct qcomtee_object_invoke_ctx *oic)
{
	struct qcomtee_async_msg_hdr *async_msg;
	struct qcomtee_buffer async_buffer;
	size_t consumed, used = 0;
	u16 major_ver;

	qcomtee_get_async_buffer(oic, &async_buffer);

	while (async_buffer.size - used > QCOMTEE_ASYNC_MSG_ZERO) {
		async_msg = (struct qcomtee_async_msg_hdr *)(async_buffer.addr +
							     used);
		/*
		 * QTEE assumes that the unused space of the async buffer is
		 * zeroed; so if version is zero, the buffer is unused.
		 */
		if (async_msg->version == 0)
			goto out;

		major_ver = QCOMTEE_ASYNC_VERSION_MAJOR(async_msg->version);
		/* Major version mismatch is a compatibility break. */
		if (major_ver != QCOMTEE_ASYNC_VERSION_CURRENT_MAJOR) {
			pr_err("Async message version mismatch (%u != %u)\n",
			       major_ver, QCOMTEE_ASYNC_VERSION_CURRENT_MAJOR);

			goto out;
		}

		switch (async_msg->op) {
		case QCOMTEE_MSG_OBJECT_OP_RELEASE:
			consumed = async_release(oic, async_msg,
						 async_buffer.size - used);
			break;
		default:
			pr_err("Unsupported async message %u\n", async_msg->op);
			goto out;
		}

		/* Supported operation but unable to parse the message. */
		if (!consumed) {
			pr_err("Unable to parse async message for op %u\n",
			       async_msg->op);
			goto out;
		}

		/* Next async message. */
		used += qcomtee_msg_offset_align(consumed);
	}

out:
	/* Reset the async buffer so async requests do not loop to QTEE. */
	memzero_explicit(async_buffer.addr, async_buffer.size);
}
