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
 * The top bit of the type in an operation message header indicates
 * whether the message is a request (bit clear) or response (bit set)
 */
#define GB_OPERATION_TYPE_RESPONSE	0x80

#define OPERATION_TIMEOUT_DEFAULT	1000	/* milliseconds */

/*
 * XXX This needs to be coordinated with host driver parameters
 * XXX May need to reduce to allow for message header within a page
 */
#define GB_OPERATION_MESSAGE_SIZE_MAX	4096

static struct kmem_cache *gb_operation_cache;

/* Workqueue to handle Greybus operation completions. */
static struct workqueue_struct *gb_operation_workqueue;

/*
 * All operation messages (both requests and responses) begin with
 * a header that encodes the size of the data (header included).
 * This header also contains a unique identifier, which is used to
 * keep track of in-flight operations.  The header contains an
 * operation type field, whose interpretation is dependent on what
 * type of protocol is used over the connection.
 *
 * The high bit (0x80) of the operation type field is used to
 * indicate whether the message is a request (clear) or a response
 * (set).
 *
 * Response messages include an additional status byte, which
 * communicates the result of the corresponding request.  A zero
 * status value means the operation completed successfully.  Any
 * other value indicates an error; in this case, the payload of the
 * response message (if any) is ignored.  The status byte must be
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
	/* 2 bytes pad, must be zero (ignore when read) */
} __aligned(sizeof(u64));

/* XXX Could be per-host device, per-module, or even per-connection */
static DEFINE_SPINLOCK(gb_operations_lock);

static void gb_operation_result_set(struct gb_operation *operation, int result)
{
	operation->errno = result;
}

int gb_operation_result(struct gb_operation *operation)
{
	return operation->errno;
}

static void gb_pending_operation_insert(struct gb_operation *operation)
{
	struct gb_connection *connection = operation->connection;
	struct gb_operation_msg_hdr *header;

	/*
	 * Assign the operation's id and move it into its
	 * connection's pending list.
	 */
	spin_lock_irq(&gb_operations_lock);
	operation->id = ++connection->op_cycle;
	list_move_tail(&operation->links, &connection->pending);
	spin_unlock_irq(&gb_operations_lock);

	/* Store the operation id in the request header */
	header = operation->request->header;
	header->operation_id = cpu_to_le16(operation->id);
}

static void gb_pending_operation_remove(struct gb_operation *operation)
{
	struct gb_connection *connection = operation->connection;

	/* Take us off of the list of pending operations */
	spin_lock_irq(&gb_operations_lock);
	list_move_tail(&operation->links, &connection->operations);
	spin_unlock_irq(&gb_operations_lock);
}

static struct gb_operation *
gb_pending_operation_find(struct gb_connection *connection, u16 operation_id)
{
	struct gb_operation *operation;
	bool found = false;

	spin_lock_irq(&gb_operations_lock);
	list_for_each_entry(operation, &connection->pending, links)
		if (operation->id == operation_id) {
			found = true;
			break;
		}
	spin_unlock_irq(&gb_operations_lock);

	return found ? operation : NULL;
}

static int gb_message_send(struct gb_message *message, gfp_t gfp_mask)
{
	struct gb_connection *connection = message->operation->connection;
	u16 dest_cport_id = connection->interface_cport_id;
	int ret = 0;

	message->cookie = connection->hd->driver->buffer_send(connection->hd,
					dest_cport_id,
					message->header,
					message->size,
					gfp_mask);
	if (IS_ERR(message->cookie)) {
		ret = PTR_ERR(message->cookie);
		message->cookie = NULL;
	}
	return ret;
}

/*
 * Cancel a message whose buffer we have passed to the host device
 * layer to be sent.
 */
static void gb_message_cancel(struct gb_message *message)
{
	struct greybus_host_device *hd;

	if (!message->cookie)
		return;	/* Don't bother if the message isn't in flight */

	hd = message->operation->connection->hd;
	hd->driver->buffer_cancel(message->cookie);
}

#if 0
static void gb_operation_request_handle(struct gb_operation *operation)
{
	struct gb_protocol *protocol = operation->connection->protocol;
	struct gb_operation_msg_hdr *header;

	header = operation->request->header;

	/*
	 * If the protocol has no incoming request handler, report
	 * an error and mark the request bad.
	 */
	if (protocol->request_recv) {
		protocol->request_recv(header->type, operation);
		return;
	}

	gb_connection_err(operation->connection,
		"unexpected incoming request type 0x%02hhx\n", header->type);
	gb_operation_result_set(operation, -EPROTONOSUPPORT);
}
#endif

/*
 * Complete an operation in non-atomic context.  The operation's
 * result value should have been set before queueing this.
 */
static void gb_operation_work(struct work_struct *work)
{
	struct gb_operation *operation;

	operation = container_of(work, struct gb_operation, work);
	if (WARN_ON(!operation->callback))
		return;

	operation->callback(operation);
	operation->callback = NULL;
	gb_operation_put(operation);
}

/*
 * Timeout call for the operation.
 */
static void gb_operation_timeout(struct work_struct *work)
{
	struct gb_operation *operation;

	operation = container_of(work, struct gb_operation, timeout_work.work);
	gb_operation_cancel(operation, -ETIMEDOUT);
}

/*
 * Given a pointer to the header in a message sent on a given host
 * device, return the associated message structure.  (This "header"
 * is just the buffer pointer we supply to the host device for
 * sending.)
 */
static struct gb_message *
gb_hd_message_find(struct greybus_host_device *hd, void *header)
{
	struct gb_message *message;
	u8 *result;

	result = (u8 *)header - hd->buffer_headroom - sizeof(*message);
	message = (struct gb_message *)result;

	return message;
}

/*
 * Allocate a message to be used for an operation request or
 * response.  For outgoing messages, both types of message contain a
 * common header, which is filled in here.  Incoming requests or
 * responses also contain the same header, but there's no need to
 * initialize it here (it'll be overwritten by the incoming
 * message).
 *
 * Our message structure consists of:
 *	message structure
 *	headroom
 *	message header  \_ these combined are
 *	message payload /  the message size
 */
static struct gb_message *
gb_operation_message_alloc(struct greybus_host_device *hd, u8 type,
				size_t payload_size, gfp_t gfp_flags)
{
	struct gb_message *message;
	struct gb_operation_msg_hdr *header;
	size_t message_size = payload_size + sizeof(*header);
	size_t size;
	u8 *buffer;

	if (message_size > hd->buffer_size_max)
		return NULL;

	size = sizeof(*message) + hd->buffer_headroom + message_size;
	message = kzalloc(size, gfp_flags);
	if (!message)
		return NULL;
	buffer = &message->buffer[0];
	header = (struct gb_operation_msg_hdr *)(buffer + hd->buffer_headroom);

	/* Fill in the header structure */
	header->size = cpu_to_le16(message_size);
	header->operation_id = 0;	/* Filled in when submitted */
	header->type = type;

	message->header = header;
	message->payload = header + 1;
	message->size = message_size;

	return message;
}

static void gb_operation_message_free(struct gb_message *message)
{
	kfree(message);
}

/*
 * Map an enum gb_operation_status value (which is represented in a
 * message as a single byte) to an appropriate Linux negative errno.
 */
int gb_operation_status_map(u8 status)
{
	switch (status) {
	case GB_OP_SUCCESS:
		return 0;
	case GB_OP_INVALID:
		return -EINVAL;
	case GB_OP_NO_MEMORY:
		return -ENOMEM;
	case GB_OP_INTERRUPTED:
		return -EINTR;
	case GB_OP_RETRY:
		return -EAGAIN;
	case GB_OP_PROTOCOL_BAD:
		return -EPROTONOSUPPORT;
	case GB_OP_OVERFLOW:
		return -E2BIG;
	case GB_OP_TIMEOUT:
		return -ETIMEDOUT;
	default:
		return -EIO;
	}
}

/*
 * Create a Greybus operation to be sent over the given connection.
 * The request buffer will be big enough for a payload of the given
 * size.  Outgoing requests must specify the size of the response
 * buffer size, which must be sufficient to hold all expected
 * response data.
 *
 * Incoming requests will supply a response size of 0, and in that
 * case no response buffer is allocated.  (A response always
 * includes a status byte, so 0 is not a valid size.)  Whatever
 * handles the operation request is responsible for allocating the
 * response buffer.
 *
 * Returns a pointer to the new operation or a null pointer if an
 * error occurs.
 */
static struct gb_operation *
gb_operation_create_common(struct gb_connection *connection, bool outgoing,
				u8 type, size_t request_size,
				size_t response_size)
{
	struct greybus_host_device *hd = connection->hd;
	struct gb_operation *operation;
	gfp_t gfp_flags = response_size ? GFP_KERNEL : GFP_ATOMIC;

	operation = kmem_cache_zalloc(gb_operation_cache, gfp_flags);
	if (!operation)
		return NULL;
	operation->connection = connection;

	operation->request = gb_operation_message_alloc(hd, type, request_size,
							gfp_flags);
	if (!operation->request)
		goto err_cache;
	operation->request->operation = operation;

	if (outgoing) {
		type |= GB_OPERATION_TYPE_RESPONSE;
		operation->response = gb_operation_message_alloc(hd, type,
						response_size, GFP_KERNEL);
		if (!operation->response)
			goto err_request;
		operation->response->operation = operation;
	}

	INIT_WORK(&operation->work, gb_operation_work);
	operation->callback = NULL;	/* set at submit time */
	init_completion(&operation->completion);
	INIT_DELAYED_WORK(&operation->timeout_work, gb_operation_timeout);
	kref_init(&operation->kref);

	spin_lock_irq(&gb_operations_lock);
	list_add_tail(&operation->links, &connection->operations);
	spin_unlock_irq(&gb_operations_lock);

	return operation;

err_request:
	gb_operation_message_free(operation->request);
err_cache:
	kmem_cache_free(gb_operation_cache, operation);

	return NULL;
}

struct gb_operation *gb_operation_create(struct gb_connection *connection,
					u8 type, size_t request_size,
					size_t response_size)
{
	return gb_operation_create_common(connection, true, type,
					request_size, response_size);
}

static struct gb_operation *
gb_operation_create_incoming(struct gb_connection *connection,
					u8 type, size_t request_size,
					size_t response_size)
{
	return gb_operation_create_common(connection, false, type,
					request_size, response_size);
}

/*
 * Get an additional reference on an operation.
 */
void gb_operation_get(struct gb_operation *operation)
{
	kref_get(&operation->kref);
}

/*
 * Destroy a previously created operation.
 */
static void _gb_operation_destroy(struct kref *kref)
{
	struct gb_operation *operation;

	operation = container_of(kref, struct gb_operation, kref);

	/* XXX Make sure it's not in flight */
	spin_lock_irq(&gb_operations_lock);
	list_del(&operation->links);
	spin_unlock_irq(&gb_operations_lock);

	gb_operation_message_free(operation->response);
	gb_operation_message_free(operation->request);

	kmem_cache_free(gb_operation_cache, operation);
}

/*
 * Drop a reference on an operation, and destroy it when the last
 * one is gone.
 */
void gb_operation_put(struct gb_operation *operation)
{
	if (!WARN_ON(!operation))
		kref_put(&operation->kref, _gb_operation_destroy);
}

/* Tell the requester we're done */
static void gb_operation_sync_callback(struct gb_operation *operation)
{
	complete(&operation->completion);
}

/*
 * Send an operation request message.  The caller has filled in
 * any payload so the request message is ready to go.  If non-null,
 * the callback function supplied will be called when the response
 * message has arrived indicating the operation is complete.  In
 * that case, the callback function is responsible for fetching the
 * result of the operation using gb_operation_result() if desired,
 * and dropping the final reference to (i.e., destroying) the
 * operation.  A null callback function is used for a synchronous
 * request; in that case return from this function won't occur until
 * the operation is complete.
 */
int gb_operation_request_send(struct gb_operation *operation,
				gb_operation_callback callback)
{
	unsigned long timeout;
	int ret;

	if (operation->connection->state != GB_CONNECTION_STATE_ENABLED)
		return -ENOTCONN;

	/*
	 * First, get an extra reference on the operation.
	 * It'll be dropped when the operation completes.
	 */
	gb_operation_get(operation);

	/* A null callback pointer means synchronous return */
	if (callback)
		operation->callback = callback;
	else
		operation->callback = gb_operation_sync_callback;
	gb_pending_operation_insert(operation);

	/*
	 * We impose a time limit for requests to complete.  We need
	 * to set the timer before we send the request though, so we
	 * don't lose a race with the receipt of the resposne.
	 */
	timeout = msecs_to_jiffies(OPERATION_TIMEOUT_DEFAULT);
	schedule_delayed_work(&operation->timeout_work, timeout);

	/* All set, send the request */
	ret = gb_message_send(operation->request, GFP_KERNEL);
	if (ret || callback)
		return ret;

	/* Cancel the operation if interrupted */
	ret = wait_for_completion_interruptible(&operation->completion);
	if (ret < 0)
		gb_operation_cancel(operation, -EINTR);

	return gb_operation_result(operation);
}

/*
 * Send a response for an incoming operation request.
 */
int gb_operation_response_send(struct gb_operation *operation)
{
	gb_operation_destroy(operation);

	return 0;
}

/*
 * This function is called when a buffer send request has completed.
 * The "header" is the message header--the beginning of what we
 * asked to have sent.
 */
void
greybus_data_sent(struct greybus_host_device *hd, void *header, int status)
{
	struct gb_message *message;
	struct gb_operation *operation;

	/* If there's no error, there's really nothing to do */
	if (!status)
		return;	/* Mark it complete? */

	/* XXX Right now we assume we're an outgoing request */
	message = gb_hd_message_find(hd, header);
	operation = message->operation;
	gb_operation_result_set(operation, status);
	queue_work(gb_operation_workqueue, &operation->work);
}
EXPORT_SYMBOL_GPL(greybus_data_sent);

/*
 * We've received data on a connection, and it doesn't look like a
 * response, so we assume it's a request.
 *
 * This is called in interrupt context, so just copy the incoming
 * data into the request buffer and handle the rest via workqueue.
 */
void gb_connection_recv_request(struct gb_connection *connection,
	u16 operation_id, u8 type, void *data, size_t size)
{
	struct gb_operation *operation;

	operation = gb_operation_create_incoming(connection, type, size, 0);
	if (!operation) {
		gb_connection_err(connection, "can't create operation");
		return;		/* XXX Respond with pre-allocated ENOMEM */
	}
	operation->id = operation_id;
	memcpy(operation->request->header, data, size);

	/* XXX Right now this will just complete the operation */
	gb_operation_result_set(operation, -ENOSYS);
	queue_work(gb_operation_workqueue, &operation->work);
}

/*
 * We've received data that appears to be an operation response
 * message.  Look up the operation, and record that we've received
 * its response.
 *
 * This is called in interrupt context, so just copy the incoming
 * data into the response buffer and handle the rest via workqueue.
 */
static void gb_connection_recv_response(struct gb_connection *connection,
				u16 operation_id, void *data, size_t size)
{
	struct gb_operation *operation;
	struct gb_message *message;
	struct gb_operation_msg_hdr *header;
	int result;

	operation = gb_pending_operation_find(connection, operation_id);
	if (!operation) {
		gb_connection_err(connection, "operation not found");
		return;
	}

	cancel_delayed_work(&operation->timeout_work);
	gb_pending_operation_remove(operation);

	message = operation->response;
	if (size <= message->size) {
		/* Transfer the operation result from the response header */
		header = message->header;
		result = gb_operation_status_map(header->result);
	} else {
		gb_connection_err(connection, "recv buffer too small");
		result = -E2BIG;
	}

	/* We must ignore the payload if a bad status is returned */
	if (!result)
		memcpy(message->header, data, size);

	/* The rest will be handled in work queue context */
	gb_operation_result_set(operation, result);
	queue_work(gb_operation_workqueue, &operation->work);
}

/*
 * Handle data arriving on a connection.  As soon as we return the
 * supplied data buffer will be reused (so unless we do something
 * with, it's effectively dropped).
 */
void gb_connection_recv(struct gb_connection *connection,
				void *data, size_t size)
{
	struct gb_operation_msg_hdr *header;
	size_t msg_size;
	u16 operation_id;

	if (connection->state != GB_CONNECTION_STATE_ENABLED) {
		gb_connection_err(connection, "dropping %zu received bytes",
			size);
		return;
	}

	if (size < sizeof(*header)) {
		gb_connection_err(connection, "message too small");
		return;
	}

	header = data;
	msg_size = (size_t)le16_to_cpu(header->size);
	if (msg_size > size) {
		gb_connection_err(connection, "incomplete message");
		return;		/* XXX Should still complete operation */
	}

	operation_id = le16_to_cpu(header->operation_id);
	if (header->type & GB_OPERATION_TYPE_RESPONSE)
		gb_connection_recv_response(connection, operation_id,
						data, msg_size);
	else
		gb_connection_recv_request(connection, operation_id,
						header->type, data, msg_size);
}

/*
 * Cancel an operation, and record the given error to indicate why.
 */
void gb_operation_cancel(struct gb_operation *operation, int errno)
{
	gb_operation_result_set(operation, errno);
	gb_message_cancel(operation->request);
	gb_message_cancel(operation->response);
}

/**
 * gb_operation_sync: implement a "simple" synchronous gb operation.
 * @connection: the Greybus connection to send this to
 * @type: the type of operation to send
 * @request: pointer to a memory buffer to copy the request from
 * @request_size: size of @request
 * @response: pointer to a memory buffer to copy the response to
 * @response_size: the size of @response.
 *
 * This function implements a simple synchronous Greybus operation.  It sends
 * the provided operation request and waits (sleeps) until the corresponding
 * operation response message has been successfully received, or an error
 * occurs.  @request and @response are buffers to hold the request and response
 * data respectively, and if they are not NULL, their size must be specified in
 * @request_size and @response_size.
 *
 * If a response payload is to come back, and @response is not NULL,
 * @response_size number of bytes will be copied into @response if the operation
 * is successful.
 *
 * If there is an error, the response buffer is left alone.
 */
int gb_operation_sync(struct gb_connection *connection, int type,
		      void *request, int request_size,
		      void *response, int response_size)
{
	struct gb_operation *operation;
	int ret;

	if ((response_size && !response) ||
	    (request_size && !request))
		return -EINVAL;

	operation = gb_operation_create(connection, type,
					request_size, response_size);
	if (!operation)
		return -ENOMEM;

	if (request_size)
		memcpy(&operation->request->payload, request, request_size);

	/* Synchronous operation--no callback */
	ret = gb_operation_request_send(operation, NULL);
	if (ret)
		pr_err("version operation failed (%d)\n", ret);
	else
		if (response_size)
			memcpy(response, operation->response->payload,
			       response_size);
	gb_operation_destroy(operation);

	return ret;
}

int gb_operation_init(void)
{
	gb_operation_cache = kmem_cache_create("gb_operation_cache",
				sizeof(struct gb_operation), 0, 0, NULL);
	if (!gb_operation_cache)
		return -ENOMEM;

	gb_operation_workqueue = alloc_workqueue("greybus_operation", 0, 1);
	if (!gb_operation_workqueue) {
		kmem_cache_destroy(gb_operation_cache);
		gb_operation_cache = NULL;
		return -ENOMEM;
	}

	return 0;
}

void gb_operation_exit(void)
{
	destroy_workqueue(gb_operation_workqueue);
	gb_operation_workqueue = NULL;
	kmem_cache_destroy(gb_operation_cache);
	gb_operation_cache = NULL;
}
