/*
 * Greybus operations
 *
 * Copyright 2014 Google Inc.
 *
 * Released under the GPLv2 only.
 */

#ifndef __OPERATION_H
#define __OPERATION_H

#include <linux/completion.h>

struct gb_operation;

enum gb_operation_result {
	GB_OP_SUCCESS		= 0,
	GB_OP_INVALID		= 1,
	GB_OP_NO_MEMORY		= 2,
	GB_OP_INTERRUPTED	= 3,
	GB_OP_RETRY		= 4,
	GB_OP_PROTOCOL_BAD	= 5,
	GB_OP_OVERFLOW		= 6,
	GB_OP_TIMEOUT		= 0xff,
};

struct gb_message {
	struct gb_operation_msg_hdr	*header;
	void				*payload;
	size_t				size;	/* header + payload */
	struct gb_operation		*operation;

	void				*cookie;

	u8				buffer[];
};

/*
 * A Greybus operation is a remote procedure call performed over a
 * connection between the AP and a function on Greybus module.
 * Every operation consists of a request message sent to the other
 * end of the connection coupled with a reply returned to the
 * sender.
 *
 * The state for managing active requests on a connection is held in
 * the connection structure.
 *
 * YADA YADA
 *
 * submitting each request and providing its matching response to
 * the caller when it arrives.  Operations normally complete
 * asynchronously, and when an operation's response arrives its
 * callback function is executed.  The callback pointer is supplied
 * at the time the operation is submitted; a null callback pointer
 * causes synchronous operation--the caller is blocked until
 * the response arrives.  In addition, it is possible to await
 * the completion of a submitted asynchronous operation.
 *
 * A Greybus device operation includes a Greybus buffer to hold the
 * data sent to the device.  The only field within a Greybus
 * operation that should be used by a caller is the payload pointer,
 * which should be used to populate the request data.  This pointer
 * is guaranteed to be 64-bit aligned.
 * XXX and callback?
 */
typedef void (*gb_operation_callback)(struct gb_operation *);
struct gb_operation {
	struct gb_connection	*connection;
	struct gb_message	*request;
	struct gb_message	*response;
	u16			id;

	int			errno;		/* Operation result */

	struct work_struct	work;
	gb_operation_callback	callback;	/* If asynchronous */
	struct completion	completion;	/* Used if no callback */
	struct delayed_work	timeout_work;

	struct kref		kref;
	struct list_head	links;	/* connection->{operations,pending} */
};

void gb_connection_recv(struct gb_connection *connection,
					void *data, size_t size);

struct gb_operation *gb_operation_create(struct gb_connection *connection,
					u8 type, size_t request_size,
					size_t response_size);
void gb_operation_get(struct gb_operation *operation);
void gb_operation_put(struct gb_operation *operation);
static inline void gb_operation_destroy(struct gb_operation *operation)
{
	gb_operation_put(operation);
}

int gb_operation_request_send(struct gb_operation *operation,
				gb_operation_callback callback);
int gb_operation_response_send(struct gb_operation *operation);

void gb_operation_cancel(struct gb_operation *operation, int errno);

int gb_operation_status_map(u8 status);

void greybus_data_sent(struct greybus_host_device *hd,
				void *header, int status);

int gb_operation_init(void);
void gb_operation_exit(void);

#endif /* !__OPERATION_H */
