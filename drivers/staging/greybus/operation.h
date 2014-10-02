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

enum gb_operation_status {
	GB_OP_SUCCESS		= 0,
	GB_OP_INVALID		= 1,
	GB_OP_NO_MEMORY		= 2,
	GB_OP_INTERRUPTED	= 3,
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
struct gb_operation;
typedef void (*gb_operation_callback)(struct gb_operation *);
struct gb_operation {
	struct gb_connection	*connection;
	struct gbuf		*gbuf;
	void			*payload;	/* sender data */
	gb_operation_callback	callback;	/* If asynchronous */
	struct completion	completion;	/* Used if no callback */
	u8			result;

	struct list_head	links;		/* connection->operations */
};

struct gb_operation *gb_operation_create(struct gb_connection *connection,
					size_t size);
void gb_operation_destroy(struct gb_operation *operation);

int gb_operation_wait(struct gb_operation *operation);
void gb_operation_complete(struct gb_operation *operation);

#endif /* !__OPERATION_H */
