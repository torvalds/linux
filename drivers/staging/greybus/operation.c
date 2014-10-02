/*
 * Greybus operations
 *
 * Copyright 2014 Google Inc.
 *
 * Released under the GPLv2 only.
 */

#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/module.h>
#include <linux/workqueue.h>

#include "greybus.h"

/*
 * All operation messages (both requests and responses) begin with
 * a common header that encodes the size of the data (header
 * included).  This header also contains a unique identifier, which
 * is used to keep track of in-flight operations.  Finally, the
 * header contains a operation type field, whose interpretation is
 * dependent on what type of device lies on the other end of the
 * connection.  Response messages are distinguished from request
 * messages by setting the high bit (0x80) in the operation type
 * value.
 *
 * The wire format for all numeric fields in the header is little
 * endian.  Any operation-specific data begins immediately after the
 * header, and is 64-bit aligned.
 */
struct gb_operation_msg_hdr {
	__le16	size;	/* Size in bytes of header + payload */
	__le16	id;	/* Operation unique id */
	__u8	type;	/* E.g GB_I2C_TYPE_* or GB_GPIO_TYPE_* */
	/* 3 bytes pad, must be zero (ignore when read) */
} __aligned(sizeof(u64));

/* XXX Could be per-host device, per-module, or even per-connection */
static DEFINE_SPINLOCK(gb_operations_lock);

/*
 * An operations's response message has arrived.  If no callback was
 * supplied it was submitted for asynchronous completion, so we notify
 * any waiters.  Otherwise we assume calling the completion is enough
 * and nobody else will be waiting.
 */
void gb_operation_complete(struct gb_operation *operation)
{
	if (operation->callback)
		operation->callback(operation);
	else
		complete_all(&operation->completion);
}

/*
 * Wait for a submitted operatnoi to complete */
int gb_operation_wait(struct gb_operation *operation)
{
	int ret;

	ret = wait_for_completion_interruptible(&operation->completion);
	/* If interrupted, cancel the in-flight buffer */
	if (ret < 0)
		ret = greybus_kill_gbuf(operation->gbuf);
	return ret;

}

/*
 * Submit an outbound operation.  The caller has filled in any
 * payload so the request message is ready to go.  If non-null,
 * the callback function supplied will be called when the response
 * message has arrived indicating the operation is complete.  A null
 * callback function is used for a synchronous request; return from
 * this function won't occur until the operation is complete (or an
 * interrupt occurs).
 */
int gb_operation_submit(struct gb_operation *operation,
			gb_operation_callback callback)
{
	int ret;

	/* XXX
	 * gfp is probably GFP_ATOMIC but really I think
	 * the gfp mask should go away.
	 */
	operation->callback = callback;
	ret = greybus_submit_gbuf(operation->gbuf, GFP_KERNEL);
	if (ret)
		return ret;
	if (!callback)
		ret = gb_operation_wait(operation);

	return ret;
}

/*
 * Called when a greybus request message has actually been sent.
 */
static void gbuf_out_callback(struct gbuf *gbuf)
{
	/* Record it's been submitted; need response now */
}

/*
 * Create a Greybus operation having a buffer big enough for an
 * outgoing payload of the given size to be sent over the given
 * connection.
 *
 * Returns a pointer to the new operation or a null pointer if a
 * failure occurs due to memory exhaustion.
 */
struct gb_operation *gb_operation_create(struct gb_connection *connection,
					size_t size)
{
	struct gb_operation *operation;
	struct gb_operation_msg_hdr *header;
	struct gbuf *gbuf;

	/* XXX Use a slab cache */
	operation = kzalloc(sizeof(*operation), GFP_KERNEL);
	if (!operation)
		return NULL;

	/* Our buffer holds a header in addition to the requested payload */
	size += sizeof(*header);
	gbuf = greybus_alloc_gbuf(connection->function->interface->gmod,
				connection->cport_id,
				gbuf_out_callback, size,
				GFP_KERNEL, operation);
	if (gbuf) {
		kfree(operation);
		return NULL;
	}

	operation->connection = connection;		/* XXX refcount? */

	/* Fill in the header structure and payload pointer */
	operation->gbuf = gbuf;
	header = (struct gb_operation_msg_hdr *)&gbuf->transfer_buffer;
	header->id = 0;
	header->size = size;
	operation->payload = (char *)header + sizeof(*header);

	operation->callback = NULL;	/* set at submit time */
	init_completion(&operation->completion);

	spin_lock_irq(&gb_operations_lock);
	list_add_tail(&operation->links, &connection->operations);
	spin_unlock_irq(&gb_operations_lock);

	return operation;
}

/*
 * Destroy a previously created operation.
 */
void gb_operation_destroy(struct gb_operation *operation)
{
	if (WARN_ON(!operation))
		return;

	/* XXX Make sure it's not in flight */
	spin_lock_irq(&gb_operations_lock);
	list_del(&operation->links);
	spin_unlock_irq(&gb_operations_lock);

	greybus_free_gbuf(operation->gbuf);

	kfree(operation);
}
