// SPDX-License-Identifier: GPL-2.0
/*
 * Greybus operations
 *
 * Copyright 2014-2015 Google Inc.
 * Copyright 2014-2015 Linaro Ltd.
 */

#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/module.h>
#include <linux/sched.h>
#include <linux/wait.h>
#include <linux/workqueue.h>

#include "greybus.h"
#include "greybus_trace.h"

static struct kmem_cache *gb_operation_cache;
static struct kmem_cache *gb_message_cache;

/* Workqueue to handle Greybus operation completions. */
static struct workqueue_struct *gb_operation_completion_wq;

/* Wait queue for synchronous cancellations. */
static DECLARE_WAIT_QUEUE_HEAD(gb_operation_cancellation_queue);

/*
 * Protects updates to operation->errno.
 */
static DEFINE_SPINLOCK(gb_operations_lock);

static int gb_operation_response_send(struct gb_operation *operation,
					int errno);

/*
 * Increment operation active count and add to connection list unless the
 * connection is going away.
 *
 * Caller holds operation reference.
 */
static int gb_operation_get_active(struct gb_operation *operation)
{
	struct gb_connection *connection = operation->connection;
	unsigned long flags;

	spin_lock_irqsave(&connection->lock, flags);
	switch (connection->state) {
	case GB_CONNECTION_STATE_ENABLED:
		break;
	case GB_CONNECTION_STATE_ENABLED_TX:
		if (gb_operation_is_incoming(operation))
			goto err_unlock;
		break;
	case GB_CONNECTION_STATE_DISCONNECTING:
		if (!gb_operation_is_core(operation))
			goto err_unlock;
		break;
	default:
		goto err_unlock;
	}

	if (operation->active++ == 0)
		list_add_tail(&operation->links, &connection->operations);

	trace_gb_operation_get_active(operation);

	spin_unlock_irqrestore(&connection->lock, flags);

	return 0;

err_unlock:
	spin_unlock_irqrestore(&connection->lock, flags);

	return -ENOTCONN;
}

/* Caller holds operation reference. */
static void gb_operation_put_active(struct gb_operation *operation)
{
	struct gb_connection *connection = operation->connection;
	unsigned long flags;

	spin_lock_irqsave(&connection->lock, flags);

	trace_gb_operation_put_active(operation);

	if (--operation->active == 0) {
		list_del(&operation->links);
		if (atomic_read(&operation->waiters))
			wake_up(&gb_operation_cancellation_queue);
	}
	spin_unlock_irqrestore(&connection->lock, flags);
}

static bool gb_operation_is_active(struct gb_operation *operation)
{
	struct gb_connection *connection = operation->connection;
	unsigned long flags;
	bool ret;

	spin_lock_irqsave(&connection->lock, flags);
	ret = operation->active;
	spin_unlock_irqrestore(&connection->lock, flags);

	return ret;
}

/*
 * Set an operation's result.
 *
 * Initially an outgoing operation's errno value is -EBADR.
 * If no error occurs before sending the request message the only
 * valid value operation->errno can be set to is -EINPROGRESS,
 * indicating the request has been (or rather is about to be) sent.
 * At that point nobody should be looking at the result until the
 * response arrives.
 *
 * The first time the result gets set after the request has been
 * sent, that result "sticks."  That is, if two concurrent threads
 * race to set the result, the first one wins.  The return value
 * tells the caller whether its result was recorded; if not the
 * caller has nothing more to do.
 *
 * The result value -EILSEQ is reserved to signal an implementation
 * error; if it's ever observed, the code performing the request has
 * done something fundamentally wrong.  It is an error to try to set
 * the result to -EBADR, and attempts to do so result in a warning,
 * and -EILSEQ is used instead.  Similarly, the only valid result
 * value to set for an operation in initial state is -EINPROGRESS.
 * Attempts to do otherwise will also record a (successful) -EILSEQ
 * operation result.
 */
static bool gb_operation_result_set(struct gb_operation *operation, int result)
{
	unsigned long flags;
	int prev;

	if (result == -EINPROGRESS) {
		/*
		 * -EINPROGRESS is used to indicate the request is
		 * in flight.  It should be the first result value
		 * set after the initial -EBADR.  Issue a warning
		 * and record an implementation error if it's
		 * set at any other time.
		 */
		spin_lock_irqsave(&gb_operations_lock, flags);
		prev = operation->errno;
		if (prev == -EBADR)
			operation->errno = result;
		else
			operation->errno = -EILSEQ;
		spin_unlock_irqrestore(&gb_operations_lock, flags);
		WARN_ON(prev != -EBADR);

		return true;
	}

	/*
	 * The first result value set after a request has been sent
	 * will be the final result of the operation.  Subsequent
	 * attempts to set the result are ignored.
	 *
	 * Note that -EBADR is a reserved "initial state" result
	 * value.  Attempts to set this value result in a warning,
	 * and the result code is set to -EILSEQ instead.
	 */
	if (WARN_ON(result == -EBADR))
		result = -EILSEQ; /* Nobody should be setting -EBADR */

	spin_lock_irqsave(&gb_operations_lock, flags);
	prev = operation->errno;
	if (prev == -EINPROGRESS)
		operation->errno = result;	/* First and final result */
	spin_unlock_irqrestore(&gb_operations_lock, flags);

	return prev == -EINPROGRESS;
}

int gb_operation_result(struct gb_operation *operation)
{
	int result = operation->errno;

	WARN_ON(result == -EBADR);
	WARN_ON(result == -EINPROGRESS);

	return result;
}
EXPORT_SYMBOL_GPL(gb_operation_result);

/*
 * Looks up an outgoing operation on a connection and returns a refcounted
 * pointer if found, or NULL otherwise.
 */
static struct gb_operation *
gb_operation_find_outgoing(struct gb_connection *connection, u16 operation_id)
{
	struct gb_operation *operation;
	unsigned long flags;
	bool found = false;

	spin_lock_irqsave(&connection->lock, flags);
	list_for_each_entry(operation, &connection->operations, links)
		if (operation->id == operation_id &&
				!gb_operation_is_incoming(operation)) {
			gb_operation_get(operation);
			found = true;
			break;
		}
	spin_unlock_irqrestore(&connection->lock, flags);

	return found ? operation : NULL;
}

static int gb_message_send(struct gb_message *message, gfp_t gfp)
{
	struct gb_connection *connection = message->operation->connection;

	trace_gb_message_send(message);
	return connection->hd->driver->message_send(connection->hd,
					connection->hd_cport_id,
					message,
					gfp);
}

/*
 * Cancel a message we have passed to the host device layer to be sent.
 */
static void gb_message_cancel(struct gb_message *message)
{
	struct gb_host_device *hd = message->operation->connection->hd;

	hd->driver->message_cancel(message);
}

static void gb_operation_request_handle(struct gb_operation *operation)
{
	struct gb_connection *connection = operation->connection;
	int status;
	int ret;

	if (connection->handler) {
		status = connection->handler(operation);
	} else {
		dev_err(&connection->hd->dev,
			"%s: unexpected incoming request of type 0x%02x\n",
			connection->name, operation->type);

		status = -EPROTONOSUPPORT;
	}

	ret = gb_operation_response_send(operation, status);
	if (ret) {
		dev_err(&connection->hd->dev,
			"%s: failed to send response %d for type 0x%02x: %d\n",
			connection->name, status, operation->type, ret);
		return;
	}
}

/*
 * Process operation work.
 *
 * For incoming requests, call the protocol request handler. The operation
 * result should be -EINPROGRESS at this point.
 *
 * For outgoing requests, the operation result value should have
 * been set before queueing this.  The operation callback function
 * allows the original requester to know the request has completed
 * and its result is available.
 */
static void gb_operation_work(struct work_struct *work)
{
	struct gb_operation *operation;
	int ret;

	operation = container_of(work, struct gb_operation, work);

	if (gb_operation_is_incoming(operation)) {
		gb_operation_request_handle(operation);
	} else {
		ret = del_timer_sync(&operation->timer);
		if (!ret) {
			/* Cancel request message if scheduled by timeout. */
			if (gb_operation_result(operation) == -ETIMEDOUT)
				gb_message_cancel(operation->request);
		}

		operation->callback(operation);
	}

	gb_operation_put_active(operation);
	gb_operation_put(operation);
}

static void gb_operation_timeout(unsigned long arg)
{
	struct gb_operation *operation = (void *)arg;

	if (gb_operation_result_set(operation, -ETIMEDOUT)) {
		/*
		 * A stuck request message will be cancelled from the
		 * workqueue.
		 */
		queue_work(gb_operation_completion_wq, &operation->work);
	}
}

static void gb_operation_message_init(struct gb_host_device *hd,
				struct gb_message *message, u16 operation_id,
				size_t payload_size, u8 type)
{
	struct gb_operation_msg_hdr *header;

	header = message->buffer;

	message->header = header;
	message->payload = payload_size ? header + 1 : NULL;
	message->payload_size = payload_size;

	/*
	 * The type supplied for incoming message buffers will be
	 * GB_REQUEST_TYPE_INVALID. Such buffers will be overwritten by
	 * arriving data so there's no need to initialize the message header.
	 */
	if (type != GB_REQUEST_TYPE_INVALID) {
		u16 message_size = (u16)(sizeof(*header) + payload_size);

		/*
		 * For a request, the operation id gets filled in
		 * when the message is sent.  For a response, it
		 * will be copied from the request by the caller.
		 *
		 * The result field in a request message must be
		 * zero.  It will be set just prior to sending for
		 * a response.
		 */
		header->size = cpu_to_le16(message_size);
		header->operation_id = 0;
		header->type = type;
		header->result = 0;
	}
}

/*
 * Allocate a message to be used for an operation request or response.
 * Both types of message contain a common header.  The request message
 * for an outgoing operation is outbound, as is the response message
 * for an incoming operation.  The message header for an outbound
 * message is partially initialized here.
 *
 * The headers for inbound messages don't need to be initialized;
 * they'll be filled in by arriving data.
 *
 * Our message buffers have the following layout:
 *	message header  \_ these combined are
 *	message payload /  the message size
 */
static struct gb_message *
gb_operation_message_alloc(struct gb_host_device *hd, u8 type,
				size_t payload_size, gfp_t gfp_flags)
{
	struct gb_message *message;
	struct gb_operation_msg_hdr *header;
	size_t message_size = payload_size + sizeof(*header);

	if (message_size > hd->buffer_size_max) {
		dev_warn(&hd->dev, "requested message size too big (%zu > %zu)\n",
				message_size, hd->buffer_size_max);
		return NULL;
	}

	/* Allocate the message structure and buffer. */
	message = kmem_cache_zalloc(gb_message_cache, gfp_flags);
	if (!message)
		return NULL;

	message->buffer = kzalloc(message_size, gfp_flags);
	if (!message->buffer)
		goto err_free_message;

	/* Initialize the message.  Operation id is filled in later. */
	gb_operation_message_init(hd, message, 0, payload_size, type);

	return message;

err_free_message:
	kmem_cache_free(gb_message_cache, message);

	return NULL;
}

static void gb_operation_message_free(struct gb_message *message)
{
	kfree(message->buffer);
	kmem_cache_free(gb_message_cache, message);
}

/*
 * Map an enum gb_operation_status value (which is represented in a
 * message as a single byte) to an appropriate Linux negative errno.
 */
static int gb_operation_status_map(u8 status)
{
	switch (status) {
	case GB_OP_SUCCESS:
		return 0;
	case GB_OP_INTERRUPTED:
		return -EINTR;
	case GB_OP_TIMEOUT:
		return -ETIMEDOUT;
	case GB_OP_NO_MEMORY:
		return -ENOMEM;
	case GB_OP_PROTOCOL_BAD:
		return -EPROTONOSUPPORT;
	case GB_OP_OVERFLOW:
		return -EMSGSIZE;
	case GB_OP_INVALID:
		return -EINVAL;
	case GB_OP_RETRY:
		return -EAGAIN;
	case GB_OP_NONEXISTENT:
		return -ENODEV;
	case GB_OP_MALFUNCTION:
		return -EILSEQ;
	case GB_OP_UNKNOWN_ERROR:
	default:
		return -EIO;
	}
}

/*
 * Map a Linux errno value (from operation->errno) into the value
 * that should represent it in a response message status sent
 * over the wire.  Returns an enum gb_operation_status value (which
 * is represented in a message as a single byte).
 */
static u8 gb_operation_errno_map(int errno)
{
	switch (errno) {
	case 0:
		return GB_OP_SUCCESS;
	case -EINTR:
		return GB_OP_INTERRUPTED;
	case -ETIMEDOUT:
		return GB_OP_TIMEOUT;
	case -ENOMEM:
		return GB_OP_NO_MEMORY;
	case -EPROTONOSUPPORT:
		return GB_OP_PROTOCOL_BAD;
	case -EMSGSIZE:
		return GB_OP_OVERFLOW;	/* Could be underflow too */
	case -EINVAL:
		return GB_OP_INVALID;
	case -EAGAIN:
		return GB_OP_RETRY;
	case -EILSEQ:
		return GB_OP_MALFUNCTION;
	case -ENODEV:
		return GB_OP_NONEXISTENT;
	case -EIO:
	default:
		return GB_OP_UNKNOWN_ERROR;
	}
}

bool gb_operation_response_alloc(struct gb_operation *operation,
					size_t response_size, gfp_t gfp)
{
	struct gb_host_device *hd = operation->connection->hd;
	struct gb_operation_msg_hdr *request_header;
	struct gb_message *response;
	u8 type;

	type = operation->type | GB_MESSAGE_TYPE_RESPONSE;
	response = gb_operation_message_alloc(hd, type, response_size, gfp);
	if (!response)
		return false;
	response->operation = operation;

	/*
	 * Size and type get initialized when the message is
	 * allocated.  The errno will be set before sending.  All
	 * that's left is the operation id, which we copy from the
	 * request message header (as-is, in little-endian order).
	 */
	request_header = operation->request->header;
	response->header->operation_id = request_header->operation_id;
	operation->response = response;

	return true;
}
EXPORT_SYMBOL_GPL(gb_operation_response_alloc);

/*
 * Create a Greybus operation to be sent over the given connection.
 * The request buffer will be big enough for a payload of the given
 * size.
 *
 * For outgoing requests, the request message's header will be
 * initialized with the type of the request and the message size.
 * Outgoing operations must also specify the response buffer size,
 * which must be sufficient to hold all expected response data.  The
 * response message header will eventually be overwritten, so there's
 * no need to initialize it here.
 *
 * Request messages for incoming operations can arrive in interrupt
 * context, so they must be allocated with GFP_ATOMIC.  In this case
 * the request buffer will be immediately overwritten, so there is
 * no need to initialize the message header.  Responsibility for
 * allocating a response buffer lies with the incoming request
 * handler for a protocol.  So we don't allocate that here.
 *
 * Returns a pointer to the new operation or a null pointer if an
 * error occurs.
 */
static struct gb_operation *
gb_operation_create_common(struct gb_connection *connection, u8 type,
				size_t request_size, size_t response_size,
				unsigned long op_flags, gfp_t gfp_flags)
{
	struct gb_host_device *hd = connection->hd;
	struct gb_operation *operation;

	operation = kmem_cache_zalloc(gb_operation_cache, gfp_flags);
	if (!operation)
		return NULL;
	operation->connection = connection;

	operation->request = gb_operation_message_alloc(hd, type, request_size,
							gfp_flags);
	if (!operation->request)
		goto err_cache;
	operation->request->operation = operation;

	/* Allocate the response buffer for outgoing operations */
	if (!(op_flags & GB_OPERATION_FLAG_INCOMING)) {
		if (!gb_operation_response_alloc(operation, response_size,
						 gfp_flags)) {
			goto err_request;
		}

		setup_timer(&operation->timer, gb_operation_timeout,
			    (unsigned long)operation);
	}

	operation->flags = op_flags;
	operation->type = type;
	operation->errno = -EBADR;  /* Initial value--means "never set" */

	INIT_WORK(&operation->work, gb_operation_work);
	init_completion(&operation->completion);
	kref_init(&operation->kref);
	atomic_set(&operation->waiters, 0);

	return operation;

err_request:
	gb_operation_message_free(operation->request);
err_cache:
	kmem_cache_free(gb_operation_cache, operation);

	return NULL;
}

/*
 * Create a new operation associated with the given connection.  The
 * request and response sizes provided are the number of bytes
 * required to hold the request/response payload only.  Both of
 * these are allowed to be 0.  Note that 0x00 is reserved as an
 * invalid operation type for all protocols, and this is enforced
 * here.
 */
struct gb_operation *
gb_operation_create_flags(struct gb_connection *connection,
				u8 type, size_t request_size,
				size_t response_size, unsigned long flags,
				gfp_t gfp)
{
	struct gb_operation *operation;

	if (WARN_ON_ONCE(type == GB_REQUEST_TYPE_INVALID))
		return NULL;
	if (WARN_ON_ONCE(type & GB_MESSAGE_TYPE_RESPONSE))
		type &= ~GB_MESSAGE_TYPE_RESPONSE;

	if (WARN_ON_ONCE(flags & ~GB_OPERATION_FLAG_USER_MASK))
		flags &= GB_OPERATION_FLAG_USER_MASK;

	operation = gb_operation_create_common(connection, type,
						request_size, response_size,
						flags, gfp);
	if (operation)
		trace_gb_operation_create(operation);

	return operation;
}
EXPORT_SYMBOL_GPL(gb_operation_create_flags);

struct gb_operation *
gb_operation_create_core(struct gb_connection *connection,
				u8 type, size_t request_size,
				size_t response_size, unsigned long flags,
				gfp_t gfp)
{
	struct gb_operation *operation;

	flags |= GB_OPERATION_FLAG_CORE;

	operation = gb_operation_create_common(connection, type,
						request_size, response_size,
						flags, gfp);
	if (operation)
		trace_gb_operation_create_core(operation);

	return operation;
}
/* Do not export this function. */

size_t gb_operation_get_payload_size_max(struct gb_connection *connection)
{
	struct gb_host_device *hd = connection->hd;

	return hd->buffer_size_max - sizeof(struct gb_operation_msg_hdr);
}
EXPORT_SYMBOL_GPL(gb_operation_get_payload_size_max);

static struct gb_operation *
gb_operation_create_incoming(struct gb_connection *connection, u16 id,
				u8 type, void *data, size_t size)
{
	struct gb_operation *operation;
	size_t request_size;
	unsigned long flags = GB_OPERATION_FLAG_INCOMING;

	/* Caller has made sure we at least have a message header. */
	request_size = size - sizeof(struct gb_operation_msg_hdr);

	if (!id)
		flags |= GB_OPERATION_FLAG_UNIDIRECTIONAL;

	operation = gb_operation_create_common(connection, type,
						request_size,
						GB_REQUEST_TYPE_INVALID,
						flags, GFP_ATOMIC);
	if (!operation)
		return NULL;

	operation->id = id;
	memcpy(operation->request->header, data, size);
	trace_gb_operation_create_incoming(operation);

	return operation;
}

/*
 * Get an additional reference on an operation.
 */
void gb_operation_get(struct gb_operation *operation)
{
	kref_get(&operation->kref);
}
EXPORT_SYMBOL_GPL(gb_operation_get);

/*
 * Destroy a previously created operation.
 */
static void _gb_operation_destroy(struct kref *kref)
{
	struct gb_operation *operation;

	operation = container_of(kref, struct gb_operation, kref);

	trace_gb_operation_destroy(operation);

	if (operation->response)
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
	if (WARN_ON(!operation))
		return;

	kref_put(&operation->kref, _gb_operation_destroy);
}
EXPORT_SYMBOL_GPL(gb_operation_put);

/* Tell the requester we're done */
static void gb_operation_sync_callback(struct gb_operation *operation)
{
	complete(&operation->completion);
}

/**
 * gb_operation_request_send() - send an operation request message
 * @operation:	the operation to initiate
 * @callback:	the operation completion callback
 * @timeout:	operation timeout in milliseconds, or zero for no timeout
 * @gfp:	the memory flags to use for any allocations
 *
 * The caller has filled in any payload so the request message is ready to go.
 * The callback function supplied will be called when the response message has
 * arrived, a unidirectional request has been sent, or the operation is
 * cancelled, indicating that the operation is complete. The callback function
 * can fetch the result of the operation using gb_operation_result() if
 * desired.
 *
 * Return: 0 if the request was successfully queued in the host-driver queues,
 * or a negative errno.
 */
int gb_operation_request_send(struct gb_operation *operation,
				gb_operation_callback callback,
				unsigned int timeout,
				gfp_t gfp)
{
	struct gb_connection *connection = operation->connection;
	struct gb_operation_msg_hdr *header;
	unsigned int cycle;
	int ret;

	if (gb_connection_is_offloaded(connection))
		return -EBUSY;

	if (!callback)
		return -EINVAL;

	/*
	 * Record the callback function, which is executed in
	 * non-atomic (workqueue) context when the final result
	 * of an operation has been set.
	 */
	operation->callback = callback;

	/*
	 * Assign the operation's id, and store it in the request header.
	 * Zero is a reserved operation id for unidirectional operations.
	 */
	if (gb_operation_is_unidirectional(operation)) {
		operation->id = 0;
	} else {
		cycle = (unsigned int)atomic_inc_return(&connection->op_cycle);
		operation->id = (u16)(cycle % U16_MAX + 1);
	}

	header = operation->request->header;
	header->operation_id = cpu_to_le16(operation->id);

	gb_operation_result_set(operation, -EINPROGRESS);

	/*
	 * Get an extra reference on the operation. It'll be dropped when the
	 * operation completes.
	 */
	gb_operation_get(operation);
	ret = gb_operation_get_active(operation);
	if (ret)
		goto err_put;

	ret = gb_message_send(operation->request, gfp);
	if (ret)
		goto err_put_active;

	if (timeout) {
		operation->timer.expires = jiffies + msecs_to_jiffies(timeout);
		add_timer(&operation->timer);
	}

	return 0;

err_put_active:
	gb_operation_put_active(operation);
err_put:
	gb_operation_put(operation);

	return ret;
}
EXPORT_SYMBOL_GPL(gb_operation_request_send);

/*
 * Send a synchronous operation.  This function is expected to
 * block, returning only when the response has arrived, (or when an
 * error is detected.  The return value is the result of the
 * operation.
 */
int gb_operation_request_send_sync_timeout(struct gb_operation *operation,
						unsigned int timeout)
{
	int ret;

	ret = gb_operation_request_send(operation, gb_operation_sync_callback,
					timeout, GFP_KERNEL);
	if (ret)
		return ret;

	ret = wait_for_completion_interruptible(&operation->completion);
	if (ret < 0) {
		/* Cancel the operation if interrupted */
		gb_operation_cancel(operation, -ECANCELED);
	}

	return gb_operation_result(operation);
}
EXPORT_SYMBOL_GPL(gb_operation_request_send_sync_timeout);

/*
 * Send a response for an incoming operation request.  A non-zero
 * errno indicates a failed operation.
 *
 * If there is any response payload, the incoming request handler is
 * responsible for allocating the response message.  Otherwise the
 * it can simply supply the result errno; this function will
 * allocate the response message if necessary.
 */
static int gb_operation_response_send(struct gb_operation *operation,
					int errno)
{
	struct gb_connection *connection = operation->connection;
	int ret;

	if (!operation->response &&
			!gb_operation_is_unidirectional(operation)) {
		if (!gb_operation_response_alloc(operation, 0, GFP_KERNEL))
			return -ENOMEM;
	}

	/* Record the result */
	if (!gb_operation_result_set(operation, errno)) {
		dev_err(&connection->hd->dev, "request result already set\n");
		return -EIO;	/* Shouldn't happen */
	}

	/* Sender of request does not care about response. */
	if (gb_operation_is_unidirectional(operation))
		return 0;

	/* Reference will be dropped when message has been sent. */
	gb_operation_get(operation);
	ret = gb_operation_get_active(operation);
	if (ret)
		goto err_put;

	/* Fill in the response header and send it */
	operation->response->header->result = gb_operation_errno_map(errno);

	ret = gb_message_send(operation->response, GFP_KERNEL);
	if (ret)
		goto err_put_active;

	return 0;

err_put_active:
	gb_operation_put_active(operation);
err_put:
	gb_operation_put(operation);

	return ret;
}

/*
 * This function is called when a message send request has completed.
 */
void greybus_message_sent(struct gb_host_device *hd,
					struct gb_message *message, int status)
{
	struct gb_operation *operation = message->operation;
	struct gb_connection *connection = operation->connection;

	/*
	 * If the message was a response, we just need to drop our
	 * reference to the operation.  If an error occurred, report
	 * it.
	 *
	 * For requests, if there's no error and the operation in not
	 * unidirectional, there's nothing more to do until the response
	 * arrives. If an error occurred attempting to send it, or if the
	 * operation is unidrectional, record the result of the operation and
	 * schedule its completion.
	 */
	if (message == operation->response) {
		if (status) {
			dev_err(&connection->hd->dev,
				"%s: error sending response 0x%02x: %d\n",
				connection->name, operation->type, status);
		}

		gb_operation_put_active(operation);
		gb_operation_put(operation);
	} else if (status || gb_operation_is_unidirectional(operation)) {
		if (gb_operation_result_set(operation, status)) {
			queue_work(gb_operation_completion_wq,
					&operation->work);
		}
	}
}
EXPORT_SYMBOL_GPL(greybus_message_sent);

/*
 * We've received data on a connection, and it doesn't look like a
 * response, so we assume it's a request.
 *
 * This is called in interrupt context, so just copy the incoming
 * data into the request buffer and handle the rest via workqueue.
 */
static void gb_connection_recv_request(struct gb_connection *connection,
				const struct gb_operation_msg_hdr *header,
				void *data, size_t size)
{
	struct gb_operation *operation;
	u16 operation_id;
	u8 type;
	int ret;

	operation_id = le16_to_cpu(header->operation_id);
	type = header->type;

	operation = gb_operation_create_incoming(connection, operation_id,
						type, data, size);
	if (!operation) {
		dev_err(&connection->hd->dev,
			"%s: can't create incoming operation\n",
			connection->name);
		return;
	}

	ret = gb_operation_get_active(operation);
	if (ret) {
		gb_operation_put(operation);
		return;
	}
	trace_gb_message_recv_request(operation->request);

	/*
	 * The initial reference to the operation will be dropped when the
	 * request handler returns.
	 */
	if (gb_operation_result_set(operation, -EINPROGRESS))
		queue_work(connection->wq, &operation->work);
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
				const struct gb_operation_msg_hdr *header,
				void *data, size_t size)
{
	struct gb_operation *operation;
	struct gb_message *message;
	size_t message_size;
	u16 operation_id;
	int errno;

	operation_id = le16_to_cpu(header->operation_id);

	if (!operation_id) {
		dev_err_ratelimited(&connection->hd->dev,
				"%s: invalid response id 0 received\n",
				connection->name);
		return;
	}

	operation = gb_operation_find_outgoing(connection, operation_id);
	if (!operation) {
		dev_err_ratelimited(&connection->hd->dev,
				"%s: unexpected response id 0x%04x received\n",
				connection->name, operation_id);
		return;
	}

	errno = gb_operation_status_map(header->result);
	message = operation->response;
	message_size = sizeof(*header) + message->payload_size;
	if (!errno && size > message_size) {
		dev_err_ratelimited(&connection->hd->dev,
				"%s: malformed response 0x%02x received (%zu > %zu)\n",
				connection->name, header->type,
				size, message_size);
		errno = -EMSGSIZE;
	} else if (!errno && size < message_size) {
		if (gb_operation_short_response_allowed(operation)) {
			message->payload_size = size - sizeof(*header);
		} else {
			dev_err_ratelimited(&connection->hd->dev,
					"%s: short response 0x%02x received (%zu < %zu)\n",
					connection->name, header->type,
					size, message_size);
			errno = -EMSGSIZE;
		}
	}

	/* We must ignore the payload if a bad status is returned */
	if (errno)
		size = sizeof(*header);

	/* The rest will be handled in work queue context */
	if (gb_operation_result_set(operation, errno)) {
		memcpy(message->buffer, data, size);

		trace_gb_message_recv_response(message);

		queue_work(gb_operation_completion_wq, &operation->work);
	}

	gb_operation_put(operation);
}

/*
 * Handle data arriving on a connection.  As soon as we return the
 * supplied data buffer will be reused (so unless we do something
 * with, it's effectively dropped).
 */
void gb_connection_recv(struct gb_connection *connection,
				void *data, size_t size)
{
	struct gb_operation_msg_hdr header;
	struct device *dev = &connection->hd->dev;
	size_t msg_size;

	if (connection->state == GB_CONNECTION_STATE_DISABLED ||
			gb_connection_is_offloaded(connection)) {
		dev_warn_ratelimited(dev, "%s: dropping %zu received bytes\n",
				connection->name, size);
		return;
	}

	if (size < sizeof(header)) {
		dev_err_ratelimited(dev, "%s: short message received\n",
				connection->name);
		return;
	}

	/* Use memcpy as data may be unaligned */
	memcpy(&header, data, sizeof(header));
	msg_size = le16_to_cpu(header.size);
	if (size < msg_size) {
		dev_err_ratelimited(dev,
				"%s: incomplete message 0x%04x of type 0x%02x received (%zu < %zu)\n",
				connection->name,
				le16_to_cpu(header.operation_id),
				header.type, size, msg_size);
		return;		/* XXX Should still complete operation */
	}

	if (header.type & GB_MESSAGE_TYPE_RESPONSE) {
		gb_connection_recv_response(connection,	&header, data,
						msg_size);
	} else {
		gb_connection_recv_request(connection, &header, data,
						msg_size);
	}
}

/*
 * Cancel an outgoing operation synchronously, and record the given error to
 * indicate why.
 */
void gb_operation_cancel(struct gb_operation *operation, int errno)
{
	if (WARN_ON(gb_operation_is_incoming(operation)))
		return;

	if (gb_operation_result_set(operation, errno)) {
		gb_message_cancel(operation->request);
		queue_work(gb_operation_completion_wq, &operation->work);
	}
	trace_gb_message_cancel_outgoing(operation->request);

	atomic_inc(&operation->waiters);
	wait_event(gb_operation_cancellation_queue,
			!gb_operation_is_active(operation));
	atomic_dec(&operation->waiters);
}
EXPORT_SYMBOL_GPL(gb_operation_cancel);

/*
 * Cancel an incoming operation synchronously. Called during connection tear
 * down.
 */
void gb_operation_cancel_incoming(struct gb_operation *operation, int errno)
{
	if (WARN_ON(!gb_operation_is_incoming(operation)))
		return;

	if (!gb_operation_is_unidirectional(operation)) {
		/*
		 * Make sure the request handler has submitted the response
		 * before cancelling it.
		 */
		flush_work(&operation->work);
		if (!gb_operation_result_set(operation, errno))
			gb_message_cancel(operation->response);
	}
	trace_gb_message_cancel_incoming(operation->response);

	atomic_inc(&operation->waiters);
	wait_event(gb_operation_cancellation_queue,
			!gb_operation_is_active(operation));
	atomic_dec(&operation->waiters);
}

/**
 * gb_operation_sync_timeout() - implement a "simple" synchronous operation
 * @connection: the Greybus connection to send this to
 * @type: the type of operation to send
 * @request: pointer to a memory buffer to copy the request from
 * @request_size: size of @request
 * @response: pointer to a memory buffer to copy the response to
 * @response_size: the size of @response.
 * @timeout: operation timeout in milliseconds
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
int gb_operation_sync_timeout(struct gb_connection *connection, int type,
				void *request, int request_size,
				void *response, int response_size,
				unsigned int timeout)
{
	struct gb_operation *operation;
	int ret;

	if ((response_size && !response) ||
	    (request_size && !request))
		return -EINVAL;

	operation = gb_operation_create(connection, type,
					request_size, response_size,
					GFP_KERNEL);
	if (!operation)
		return -ENOMEM;

	if (request_size)
		memcpy(operation->request->payload, request, request_size);

	ret = gb_operation_request_send_sync_timeout(operation, timeout);
	if (ret) {
		dev_err(&connection->hd->dev,
			"%s: synchronous operation id 0x%04x of type 0x%02x failed: %d\n",
			connection->name, operation->id, type, ret);
	} else {
		if (response_size) {
			memcpy(response, operation->response->payload,
			       response_size);
		}
	}

	gb_operation_put(operation);

	return ret;
}
EXPORT_SYMBOL_GPL(gb_operation_sync_timeout);

/**
 * gb_operation_unidirectional_timeout() - initiate a unidirectional operation
 * @connection:		connection to use
 * @type:		type of operation to send
 * @request:		memory buffer to copy the request from
 * @request_size:	size of @request
 * @timeout:		send timeout in milliseconds
 *
 * Initiate a unidirectional operation by sending a request message and
 * waiting for it to be acknowledged as sent by the host device.
 *
 * Note that successful send of a unidirectional operation does not imply that
 * the request as actually reached the remote end of the connection.
 */
int gb_operation_unidirectional_timeout(struct gb_connection *connection,
				int type, void *request, int request_size,
				unsigned int timeout)
{
	struct gb_operation *operation;
	int ret;

	if (request_size && !request)
		return -EINVAL;

	operation = gb_operation_create_flags(connection, type,
					request_size, 0,
					GB_OPERATION_FLAG_UNIDIRECTIONAL,
					GFP_KERNEL);
	if (!operation)
		return -ENOMEM;

	if (request_size)
		memcpy(operation->request->payload, request, request_size);

	ret = gb_operation_request_send_sync_timeout(operation, timeout);
	if (ret) {
		dev_err(&connection->hd->dev,
			"%s: unidirectional operation of type 0x%02x failed: %d\n",
			connection->name, type, ret);
	}

	gb_operation_put(operation);

	return ret;
}
EXPORT_SYMBOL_GPL(gb_operation_unidirectional_timeout);

int __init gb_operation_init(void)
{
	gb_message_cache = kmem_cache_create("gb_message_cache",
				sizeof(struct gb_message), 0, 0, NULL);
	if (!gb_message_cache)
		return -ENOMEM;

	gb_operation_cache = kmem_cache_create("gb_operation_cache",
				sizeof(struct gb_operation), 0, 0, NULL);
	if (!gb_operation_cache)
		goto err_destroy_message_cache;

	gb_operation_completion_wq = alloc_workqueue("greybus_completion",
				0, 0);
	if (!gb_operation_completion_wq)
		goto err_destroy_operation_cache;

	return 0;

err_destroy_operation_cache:
	kmem_cache_destroy(gb_operation_cache);
	gb_operation_cache = NULL;
err_destroy_message_cache:
	kmem_cache_destroy(gb_message_cache);
	gb_message_cache = NULL;

	return -ENOMEM;
}

void gb_operation_exit(void)
{
	destroy_workqueue(gb_operation_completion_wq);
	gb_operation_completion_wq = NULL;
	kmem_cache_destroy(gb_operation_cache);
	gb_operation_cache = NULL;
	kmem_cache_destroy(gb_message_cache);
	gb_message_cache = NULL;
}
