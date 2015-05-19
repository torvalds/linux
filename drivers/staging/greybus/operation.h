/*
 * Greybus operations
 *
 * Copyright 2014 Google Inc.
 * Copyright 2014 Linaro Ltd.
 *
 * Released under the GPLv2 only.
 */

#ifndef __OPERATION_H
#define __OPERATION_H

#include <linux/completion.h>

struct gb_operation;

/*
 * No protocol may define an operation that has numeric value 0x00.
 * It is reserved as an explicitly invalid value.
 */
#define GB_OPERATION_TYPE_INVALID	((u8)0x00)

/*
 * The top bit of the type in an operation message header indicates
 * whether the message is a request (bit clear) or response (bit set)
 */
#define GB_MESSAGE_TYPE_RESPONSE	((u8)0x80)

enum gb_operation_result {
	GB_OP_SUCCESS		= 0x00,
	GB_OP_INTERRUPTED	= 0x01,
	GB_OP_TIMEOUT		= 0x02,
	GB_OP_NO_MEMORY		= 0x03,
	GB_OP_PROTOCOL_BAD	= 0x04,
	GB_OP_OVERFLOW		= 0x05,
	GB_OP_INVALID		= 0x06,
	GB_OP_RETRY		= 0x07,
	GB_OP_NONEXISTENT	= 0x08,
	GB_OP_UNKNOWN_ERROR	= 0xfe,
	GB_OP_MALFUNCTION	= 0xff,
};

/*
 * All operation messages (both requests and responses) begin with
 * a header that encodes the size of the message (header included).
 * This header also contains a unique identifier, that associates a
 * response message with its operation.  The header contains an
 * operation type field, whose interpretation is dependent on what
 * type of protocol is used over the connection.  The high bit
 * (0x80) of the operation type field is used to indicate whether
 * the message is a request (clear) or a response (set).
 *
 * Response messages include an additional result byte, which
 * communicates the result of the corresponding request.  A zero
 * result value means the operation completed successfully.  Any
 * other value indicates an error; in this case, the payload of the
 * response message (if any) is ignored.  The result byte must be
 * zero in the header for a request message.
 *
 * The wire format for all numeric fields in the header is little
 * endian.  Any operation-specific data begins immediately after the
 * header, and is 64-bit aligned.
 */
struct gb_operation_msg_hdr {
	__le16	size;		/* Size in bytes of header + payload */
	__le16	operation_id;	/* Operation unique id */
	__u8	type;		/* E.g GB_I2C_TYPE_* or GB_GPIO_TYPE_* */
	__u8	result;		/* Result of request (in responses only) */
	__u8	pad[2];		/* must be zero (ignore when read) */
} __aligned(sizeof(u64));

#define GB_OPERATION_MESSAGE_SIZE_MIN	sizeof(struct gb_operation_msg_hdr)
#define GB_OPERATION_MESSAGE_SIZE_MAX	U16_MAX

/*
 * Protocol code should only examine the payload and payload_size
 * fields.  All other fields are intended to be private to the
 * operations core code.
 */
struct gb_message {
	struct gb_operation		*operation;
	void				*cookie;
	struct gb_operation_msg_hdr	*header;

	void				*payload;
	size_t				payload_size;

	void				*buffer;
};

/*
 * A Greybus operation is a remote procedure call performed over a
 * connection between two UniPro interfaces.
 *
 * Every operation consists of a request message sent to the other
 * end of the connection coupled with a reply message returned to
 * the sender.  Every operation has a type, whose interpretation is
 * dependent on the protocol associated with the connection.
 *
 * Only four things in an operation structure are intended to be
 * directly usable by protocol handlers:  the operation's connection
 * pointer; the operation type; the request message payload (and
 * size); and the response message payload (and size).  Note that a
 * message with a 0-byte payload has a null message payload pointer.
 *
 * In addition, every operation has a result, which is an errno
 * value.  Protocol handlers access the operation result using
 * gb_operation_result().
 */
typedef void (*gb_operation_callback)(struct gb_operation *);
struct gb_operation {
	struct gb_connection	*connection;
	struct gb_message	*request;
	struct gb_message	*response;
	u8			type;

	u16			id;
	int			errno;		/* Operation result */

	struct work_struct	work;
	gb_operation_callback	callback;	/* If asynchronous */
	struct completion	completion;	/* Used if no callback */

	struct kref		kref;
	struct list_head	links;		/* connection->operations */
};

void gb_connection_recv(struct gb_connection *connection,
					void *data, size_t size);

int gb_operation_result(struct gb_operation *operation);

struct gb_operation *gb_operation_create(struct gb_connection *connection,
					u8 type, size_t request_size,
					size_t response_size);
void gb_operation_get(struct gb_operation *operation);
void gb_operation_put(struct gb_operation *operation);
static inline void gb_operation_destroy(struct gb_operation *operation)
{
	gb_operation_put(operation);
}

bool gb_operation_response_alloc(struct gb_operation *operation,
					size_t response_size);

int gb_operation_request_send(struct gb_operation *operation,
				gb_operation_callback callback);
int gb_operation_request_send_sync(struct gb_operation *operation);
int gb_operation_response_send(struct gb_operation *operation, int errno);

void gb_operation_cancel(struct gb_operation *operation, int errno);

void greybus_message_sent(struct greybus_host_device *hd,
				struct gb_message *message, int status);

int gb_operation_sync(struct gb_connection *connection, int type,
		      void *request, int request_size,
		      void *response, int response_size);

int gb_operation_init(void);
void gb_operation_exit(void);

#endif /* !__OPERATION_H */
