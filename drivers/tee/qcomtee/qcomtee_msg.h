/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 */

#ifndef QCOMTEE_MSG_H
#define QCOMTEE_MSG_H

#include <linux/bitfield.h>

/**
 * DOC: ''Qualcomm TEE'' (QTEE) Transport Message
 *
 * There are two buffers shared with QTEE: inbound and outbound buffers.
 * The inbound buffer is used for direct object invocation, and the outbound
 * buffer is used to make a request from QTEE to the kernel; i.e., a callback
 * request.
 *
 * The unused tail of the outbound buffer is also used for sending and
 * receiving asynchronous messages. An asynchronous message is independent of
 * the current object invocation (i.e., contents of the inbound buffer) or
 * callback request (i.e., the head of the outbound buffer); see
 * qcomtee_get_async_buffer(). It is used by endpoints (QTEE or kernel) as an
 * optimization to reduce the number of context switches between the secure and
 * non-secure worlds.
 *
 * For instance, QTEE never sends an explicit callback request to release an
 * object in the kernel. Instead, it sends asynchronous release messages in the
 * outbound buffer when QTEE returns from the previous direct object invocation,
 * or appends asynchronous release messages after the current callback request.
 *
 * QTEE supports two types of arguments in a message: buffer and object
 * arguments. Depending on the direction of data flow, they could be input
 * buffer (IO) to QTEE, output buffer (OB) from QTEE, input object (IO) to QTEE,
 * or output object (OO) from QTEE. Object arguments hold object IDs. Buffer
 * arguments hold (offset, size) pairs into the inbound or outbound buffers.
 *
 * QTEE holds an object table for objects it hosts and exposes to the kernel.
 * An object ID is an index to the object table in QTEE.
 *
 * For the direct object invocation message format in the inbound buffer, see
 * &struct qcomtee_msg_object_invoke. For the callback request message format
 * in the outbound buffer, see &struct qcomtee_msg_callback. For the message
 * format for asynchronous messages in the outbound buffer, see
 * &struct qcomtee_async_msg_hdr.
 */

/**
 * define QCOMTEE_MSG_OBJECT_NS_BIT - Non-secure bit
 *
 * Object ID is a globally unique 32-bit number. IDs referencing objects
 * in the kernel should have %QCOMTEE_MSG_OBJECT_NS_BIT set.
 */
#define QCOMTEE_MSG_OBJECT_NS_BIT BIT(31)

/* Static object IDs recognized by QTEE. */
#define QCOMTEE_MSG_OBJECT_NULL (0U)
#define QCOMTEE_MSG_OBJECT_ROOT (1U)

/* Definitions from QTEE as part of the transport protocol. */

/* qcomtee_msg_arg is an argument as recognized by QTEE. */
union qcomtee_msg_arg {
	struct {
		u32 offset;
		u32 size;
	} b;
	u32 o;
};

/* BI and BO payloads in QTEE messages should be at 64-bit boundaries. */
#define qcomtee_msg_offset_align(o) ALIGN((o), sizeof(u64))

/* Operations for objects are 32-bit. Transport uses the upper 16 bits. */
#define QCOMTEE_MSG_OBJECT_OP_MASK GENMASK(15, 0)

/* Reserved Operation IDs sent to QTEE: */
/* QCOMTEE_MSG_OBJECT_OP_RELEASE - Reduces the refcount and releases the object.
 * QCOMTEE_MSG_OBJECT_OP_RETAIN  - Increases the refcount.
 *
 * These operation IDs are valid for all objects.
 */

#define QCOMTEE_MSG_OBJECT_OP_RELEASE (QCOMTEE_MSG_OBJECT_OP_MASK - 0)
#define QCOMTEE_MSG_OBJECT_OP_RETAIN  (QCOMTEE_MSG_OBJECT_OP_MASK - 1)

/* Subset of operations supported by QTEE root object. */

#define QCOMTEE_ROOT_OP_REG_WITH_CREDENTIALS	5
#define QCOMTEE_ROOT_OP_NOTIFY_DOMAIN_CHANGE	4
#define QCOMTEE_ROOT_OP_ADCI_ACCEPT		8
#define QCOMTEE_ROOT_OP_ADCI_SHUTDOWN		9

/* Subset of operations supported by client_env object. */

#define QCOMTEE_CLIENT_ENV_OPEN 0

/* List of available QTEE service UIDs and subset of operations. */

#define QCOMTEE_FEATURE_VER_UID		2033
#define QCOMTEE_FEATURE_VER_OP_GET	0
/* Get QTEE version number. */
#define QCOMTEE_FEATURE_VER_OP_GET_QTEE_ID 10
#define QTEE_VERSION_GET_MAJOR(x) (((x) >> 22) & 0xffU)
#define QTEE_VERSION_GET_MINOR(x) (((x) >> 12) & 0xffU)
#define QTEE_VERSION_GET_PATCH(x) ((x) >> 0 & 0xfffU)

/* Response types as returned from qcomtee_object_invoke_ctx_invoke(). */

/* The message contains a callback request. */
#define QCOMTEE_RESULT_INBOUND_REQ_NEEDED 3

/**
 * struct qcomtee_msg_object_invoke - Direct object invocation message.
 * @ctx: object ID hosted in QTEE.
 * @op: operation for the object.
 * @counts: number of different types of arguments in @args.
 * @args: array of arguments.
 *
 * @counts consists of 4 * 4-bit fields. Bits 0 - 3 represent the number of
 * input buffers, bits 4 - 7 represent the number of output buffers,
 * bits 8 - 11 represent the number of input objects, and bits 12 - 15
 * represent the number of output objects. The remaining bits should be zero.
 *
 *    15            12 11             8 7              4 3              0
 *   +----------------+----------------+----------------+----------------+
 *   |  #OO objects   |  #IO objects   |  #OB buffers   |  #IB buffers   |
 *   +----------------+----------------+----------------+----------------+
 *
 * The maximum number of arguments of each type is defined by
 * %QCOMTEE_ARGS_PER_TYPE.
 */
struct qcomtee_msg_object_invoke {
	u32 cxt;
	u32 op;
	u32 counts;
	union qcomtee_msg_arg args[];
};

/* Bit masks for the four 4-bit nibbles holding the counts. */
#define QCOMTEE_MASK_IB GENMASK(3, 0)
#define QCOMTEE_MASK_OB GENMASK(7, 4)
#define QCOMTEE_MASK_IO GENMASK(11, 8)
#define QCOMTEE_MASK_OO GENMASK(15, 12)

/**
 * struct qcomtee_msg_callback - Callback request message.
 * @result: result of operation @op on the object referenced by @cxt.
 * @cxt: object ID hosted in the kernel.
 * @op: operation for the object.
 * @counts: number of different types of arguments in @args.
 * @args: array of arguments.
 *
 * For details of @counts, see &qcomtee_msg_object_invoke.counts.
 */
struct qcomtee_msg_callback {
	u32 result;
	u32 cxt;
	u32 op;
	u32 counts;
	union qcomtee_msg_arg args[];
};

/* Offset in the message for the beginning of the buffer argument's contents. */
#define qcomtee_msg_buffer_args(t, n) \
	qcomtee_msg_offset_align(struct_size_t(t, args, n))
/* Pointer to the beginning of a buffer argument's content at an offset. */
#define qcomtee_msg_offset_to_ptr(m, off) ((void *)&((char *)(m))[(off)])

/* Some helpers to manage msg.counts. */

static inline unsigned int qcomtee_msg_num_ib(u32 counts)
{
	return FIELD_GET(QCOMTEE_MASK_IB, counts);
}

static inline unsigned int qcomtee_msg_num_ob(u32 counts)
{
	return FIELD_GET(QCOMTEE_MASK_OB, counts);
}

static inline unsigned int qcomtee_msg_num_io(u32 counts)
{
	return FIELD_GET(QCOMTEE_MASK_IO, counts);
}

static inline unsigned int qcomtee_msg_num_oo(u32 counts)
{
	return FIELD_GET(QCOMTEE_MASK_OO, counts);
}

static inline unsigned int qcomtee_msg_idx_ib(u32 counts)
{
	return 0;
}

static inline unsigned int qcomtee_msg_idx_ob(u32 counts)
{
	return qcomtee_msg_num_ib(counts);
}

static inline unsigned int qcomtee_msg_idx_io(u32 counts)
{
	return qcomtee_msg_idx_ob(counts) + qcomtee_msg_num_ob(counts);
}

static inline unsigned int qcomtee_msg_idx_oo(u32 counts)
{
	return qcomtee_msg_idx_io(counts) + qcomtee_msg_num_io(counts);
}

#define qcomtee_msg_for_each(i, first, num) \
	for ((i) = (first); (i) < (first) + (num); (i)++)

#define qcomtee_msg_for_each_input_buffer(i, m)                  \
	qcomtee_msg_for_each(i, qcomtee_msg_idx_ib((m)->counts), \
			     qcomtee_msg_num_ib((m)->counts))

#define qcomtee_msg_for_each_output_buffer(i, m)                 \
	qcomtee_msg_for_each(i, qcomtee_msg_idx_ob((m)->counts), \
			     qcomtee_msg_num_ob((m)->counts))

#define qcomtee_msg_for_each_input_object(i, m)                  \
	qcomtee_msg_for_each(i, qcomtee_msg_idx_io((m)->counts), \
			     qcomtee_msg_num_io((m)->counts))

#define qcomtee_msg_for_each_output_object(i, m)                 \
	qcomtee_msg_for_each(i, qcomtee_msg_idx_oo((m)->counts), \
			     qcomtee_msg_num_oo((m)->counts))

/* Sum of arguments in a message. */
#define qcomtee_msg_args(m) \
	(qcomtee_msg_idx_oo((m)->counts) + qcomtee_msg_num_oo((m)->counts))

static inline void qcomtee_msg_init(struct qcomtee_msg_object_invoke *msg,
				    u32 cxt, u32 op, int in_buffer,
				    int out_buffer, int in_object,
				    int out_object)
{
	u32 counts = 0;

	counts |= (in_buffer & 0xfU);
	counts |= ((out_buffer - in_buffer) & 0xfU) << 4;
	counts |= ((in_object - out_buffer) & 0xfU) << 8;
	counts |= ((out_object - in_object) & 0xfU) << 12;

	msg->cxt = cxt;
	msg->op = op;
	msg->counts = counts;
}

/* Generic error codes. */
#define QCOMTEE_MSG_OK			0 /* non-specific success code. */
#define QCOMTEE_MSG_ERROR		1 /* non-specific error. */
#define QCOMTEE_MSG_ERROR_INVALID	2 /* unsupported/unrecognized request. */
#define QCOMTEE_MSG_ERROR_SIZE_IN	3 /* supplied buffer/string too large. */
#define QCOMTEE_MSG_ERROR_SIZE_OUT	4 /* supplied output buffer too small. */
#define QCOMTEE_MSG_ERROR_USERBASE	10 /* start of user-defined error range. */

/* Transport layer error codes. */
#define QCOMTEE_MSG_ERROR_DEFUNCT	-90 /* object no longer exists. */
#define QCOMTEE_MSG_ERROR_ABORT		-91 /* calling thread must exit. */
#define QCOMTEE_MSG_ERROR_BADOBJ	-92 /* invalid object context. */
#define QCOMTEE_MSG_ERROR_NOSLOTS	-93 /* caller's object table full. */
#define QCOMTEE_MSG_ERROR_MAXARGS	-94 /* too many args. */
#define QCOMTEE_MSG_ERROR_MAXDATA	-95 /* buffers too large. */
#define QCOMTEE_MSG_ERROR_UNAVAIL	-96 /* the request could not be processed. */
#define QCOMTEE_MSG_ERROR_KMEM		-97 /* kernel out of memory. */
#define QCOMTEE_MSG_ERROR_REMOTE	-98 /* local method sent to remote object. */
#define QCOMTEE_MSG_ERROR_BUSY		-99 /* Object is busy. */
#define QCOMTEE_MSG_ERROR_TIMEOUT	-103 /* Call Back Object invocation timed out. */

static inline void qcomtee_msg_set_result(struct qcomtee_msg_callback *cb_msg,
					  int err)
{
	if (!err) {
		cb_msg->result = QCOMTEE_MSG_OK;
	} else if (err < 0) {
		/* If err < 0, then it is a transport error. */
		switch (err) {
		case -ENOMEM:
			cb_msg->result = QCOMTEE_MSG_ERROR_KMEM;
			break;
		case -ENODEV:
			cb_msg->result = QCOMTEE_MSG_ERROR_DEFUNCT;
			break;
		case -ENOSPC:
		case -EBUSY:
			cb_msg->result = QCOMTEE_MSG_ERROR_BUSY;
			break;
		case -EBADF:
		case -EINVAL:
			cb_msg->result = QCOMTEE_MSG_ERROR_UNAVAIL;
			break;
		default:
			cb_msg->result = QCOMTEE_MSG_ERROR;
		}
	} else {
		/* If err > 0, then it is user defined error, pass it as is. */
		cb_msg->result = err;
	}
}

#endif /* QCOMTEE_MSG_H */
