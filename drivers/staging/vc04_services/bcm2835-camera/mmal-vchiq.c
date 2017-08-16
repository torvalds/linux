/*
 * Broadcom BM2835 V4L2 driver
 *
 * Copyright © 2013 Raspberry Pi (Trading) Ltd.
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file COPYING in the main directory of this archive
 * for more details.
 *
 * Authors: Vincent Sanders <vincent.sanders@collabora.co.uk>
 *          Dave Stevenson <dsteve@broadcom.com>
 *          Simon Mellor <simellor@broadcom.com>
 *          Luke Diamand <luked@broadcom.com>
 *
 * V4L2 driver MMAL vchiq interface code
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/errno.h>
#include <linux/kernel.h>
#include <linux/mutex.h>
#include <linux/mm.h>
#include <linux/slab.h>
#include <linux/completion.h>
#include <linux/vmalloc.h>
#include <linux/btree.h>
#include <asm/cacheflush.h>
#include <media/videobuf2-vmalloc.h>

#include "mmal-common.h"
#include "mmal-vchiq.h"
#include "mmal-msg.h"

#define USE_VCHIQ_ARM
#include "interface/vchi/vchi.h"

/* maximum number of components supported */
#define VCHIQ_MMAL_MAX_COMPONENTS 4

/*#define FULL_MSG_DUMP 1*/

#ifdef DEBUG
static const char *const msg_type_names[] = {
	"UNKNOWN",
	"QUIT",
	"SERVICE_CLOSED",
	"GET_VERSION",
	"COMPONENT_CREATE",
	"COMPONENT_DESTROY",
	"COMPONENT_ENABLE",
	"COMPONENT_DISABLE",
	"PORT_INFO_GET",
	"PORT_INFO_SET",
	"PORT_ACTION",
	"BUFFER_FROM_HOST",
	"BUFFER_TO_HOST",
	"GET_STATS",
	"PORT_PARAMETER_SET",
	"PORT_PARAMETER_GET",
	"EVENT_TO_HOST",
	"GET_CORE_STATS_FOR_PORT",
	"OPAQUE_ALLOCATOR",
	"CONSUME_MEM",
	"LMK",
	"OPAQUE_ALLOCATOR_DESC",
	"DRM_GET_LHS32",
	"DRM_GET_TIME",
	"BUFFER_FROM_HOST_ZEROLEN",
	"PORT_FLUSH",
	"HOST_LOG",
};
#endif

static const char *const port_action_type_names[] = {
	"UNKNOWN",
	"ENABLE",
	"DISABLE",
	"FLUSH",
	"CONNECT",
	"DISCONNECT",
	"SET_REQUIREMENTS",
};

#if defined(DEBUG)
#if defined(FULL_MSG_DUMP)
#define DBG_DUMP_MSG(MSG, MSG_LEN, TITLE)				\
	do {								\
		pr_debug(TITLE" type:%s(%d) length:%d\n",		\
			 msg_type_names[(MSG)->h.type],			\
			 (MSG)->h.type, (MSG_LEN));			\
		print_hex_dump(KERN_DEBUG, "<<h: ", DUMP_PREFIX_OFFSET,	\
			       16, 4, (MSG),				\
			       sizeof(struct mmal_msg_header), 1);	\
		print_hex_dump(KERN_DEBUG, "<<p: ", DUMP_PREFIX_OFFSET,	\
			       16, 4,					\
			       ((u8 *)(MSG)) + sizeof(struct mmal_msg_header),\
			       (MSG_LEN) - sizeof(struct mmal_msg_header), 1); \
	} while (0)
#else
#define DBG_DUMP_MSG(MSG, MSG_LEN, TITLE)				\
	{								\
		pr_debug(TITLE" type:%s(%d) length:%d\n",		\
			 msg_type_names[(MSG)->h.type],			\
			 (MSG)->h.type, (MSG_LEN));			\
	}
#endif
#else
#define DBG_DUMP_MSG(MSG, MSG_LEN, TITLE)
#endif

struct vchiq_mmal_instance;

/* normal message context */
struct mmal_msg_context {
	struct vchiq_mmal_instance *instance;
	u32 handle;

	union {
		struct {
			/* work struct for defered callback - must come first */
			struct work_struct work;
			/* mmal instance */
			struct vchiq_mmal_instance *instance;
			/* mmal port */
			struct vchiq_mmal_port *port;
			/* actual buffer used to store bulk reply */
			struct mmal_buffer *buffer;
			/* amount of buffer used */
			unsigned long buffer_used;
			/* MMAL buffer flags */
			u32 mmal_flags;
			/* Presentation and Decode timestamps */
			s64 pts;
			s64 dts;

			int status;	/* context status */

		} bulk;		/* bulk data */

		struct {
			/* message handle to release */
			VCHI_HELD_MSG_T msg_handle;
			/* pointer to received message */
			struct mmal_msg *msg;
			/* received message length */
			u32 msg_len;
			/* completion upon reply */
			struct completion cmplt;
		} sync;		/* synchronous response */
	} u;

};

struct vchiq_mmal_context_map {
	/* ensure serialized access to the btree(contention should be low) */
	struct mutex lock;
	struct btree_head32 btree_head;
	u32 last_handle;
};

struct vchiq_mmal_instance {
	VCHI_SERVICE_HANDLE_T handle;

	/* ensure serialised access to service */
	struct mutex vchiq_mutex;

	/* ensure serialised access to bulk operations */
	struct mutex bulk_mutex;

	/* vmalloc page to receive scratch bulk xfers into */
	void *bulk_scratch;

	/* mapping table between context handles and mmal_msg_contexts */
	struct vchiq_mmal_context_map context_map;

	/* component to use next */
	int component_idx;
	struct vchiq_mmal_component component[VCHIQ_MMAL_MAX_COMPONENTS];
};

static int __must_check
mmal_context_map_init(struct vchiq_mmal_context_map *context_map)
{
	mutex_init(&context_map->lock);
	context_map->last_handle = 0;
	return btree_init32(&context_map->btree_head);
}

static void mmal_context_map_destroy(struct vchiq_mmal_context_map *context_map)
{
	mutex_lock(&context_map->lock);
	btree_destroy32(&context_map->btree_head);
	mutex_unlock(&context_map->lock);
}

static u32
mmal_context_map_create_handle(struct vchiq_mmal_context_map *context_map,
			       struct mmal_msg_context *msg_context,
			       gfp_t gfp)
{
	u32 handle;

	mutex_lock(&context_map->lock);

	while (1) {
		/* just use a simple count for handles, but do not use 0 */
		context_map->last_handle++;
		if (!context_map->last_handle)
			context_map->last_handle++;

		handle = context_map->last_handle;

		/* check if the handle is already in use */
		if (!btree_lookup32(&context_map->btree_head, handle))
			break;
	}

	if (btree_insert32(&context_map->btree_head, handle,
			   msg_context, gfp)) {
		/* probably out of memory */
		mutex_unlock(&context_map->lock);
		return 0;
	}

	mutex_unlock(&context_map->lock);
	return handle;
}

static struct mmal_msg_context *
mmal_context_map_lookup_handle(struct vchiq_mmal_context_map *context_map,
			       u32 handle)
{
	struct mmal_msg_context *msg_context;

	if (!handle)
		return NULL;

	mutex_lock(&context_map->lock);

	msg_context = btree_lookup32(&context_map->btree_head, handle);

	mutex_unlock(&context_map->lock);
	return msg_context;
}

static void
mmal_context_map_destroy_handle(struct vchiq_mmal_context_map *context_map,
				u32 handle)
{
	mutex_lock(&context_map->lock);
	btree_remove32(&context_map->btree_head, handle);
	mutex_unlock(&context_map->lock);
}

static struct mmal_msg_context *
get_msg_context(struct vchiq_mmal_instance *instance)
{
	struct mmal_msg_context *msg_context;

	/* todo: should this be allocated from a pool to avoid kzalloc */
	msg_context = kzalloc(sizeof(*msg_context), GFP_KERNEL);

	if (!msg_context)
		return ERR_PTR(-ENOMEM);

	msg_context->instance = instance;
	msg_context->handle =
		mmal_context_map_create_handle(&instance->context_map,
					       msg_context,
					       GFP_KERNEL);

	if (!msg_context->handle) {
		kfree(msg_context);
		return ERR_PTR(-ENOMEM);
	}

	return msg_context;
}

static struct mmal_msg_context *
lookup_msg_context(struct vchiq_mmal_instance *instance, u32 handle)
{
	return mmal_context_map_lookup_handle(&instance->context_map,
		handle);
}

static void
release_msg_context(struct mmal_msg_context *msg_context)
{
	mmal_context_map_destroy_handle(&msg_context->instance->context_map,
					msg_context->handle);
	kfree(msg_context);
}

/* deals with receipt of event to host message */
static void event_to_host_cb(struct vchiq_mmal_instance *instance,
			     struct mmal_msg *msg, u32 msg_len)
{
	pr_debug("unhandled event\n");
	pr_debug("component:%u port type:%d num:%d cmd:0x%x length:%d\n",
		 msg->u.event_to_host.client_component,
		 msg->u.event_to_host.port_type,
		 msg->u.event_to_host.port_num,
		 msg->u.event_to_host.cmd, msg->u.event_to_host.length);
}

/* workqueue scheduled callback
 *
 * we do this because it is important we do not call any other vchiq
 * sync calls from witin the message delivery thread
 */
static void buffer_work_cb(struct work_struct *work)
{
	struct mmal_msg_context *msg_context =
		container_of(work, struct mmal_msg_context, u.bulk.work);

	msg_context->u.bulk.port->buffer_cb(msg_context->u.bulk.instance,
					    msg_context->u.bulk.port,
					    msg_context->u.bulk.status,
					    msg_context->u.bulk.buffer,
					    msg_context->u.bulk.buffer_used,
					    msg_context->u.bulk.mmal_flags,
					    msg_context->u.bulk.dts,
					    msg_context->u.bulk.pts);

	/* release message context */
	release_msg_context(msg_context);
}

/* enqueue a bulk receive for a given message context */
static int bulk_receive(struct vchiq_mmal_instance *instance,
			struct mmal_msg *msg,
			struct mmal_msg_context *msg_context)
{
	unsigned long rd_len;
	unsigned long flags = 0;
	int ret;

	/* bulk mutex stops other bulk operations while we have a
	 * receive in progress - released in callback
	 */
	ret = mutex_lock_interruptible(&instance->bulk_mutex);
	if (ret != 0)
		return ret;

	rd_len = msg->u.buffer_from_host.buffer_header.length;

	/* take buffer from queue */
	spin_lock_irqsave(&msg_context->u.bulk.port->slock, flags);
	if (list_empty(&msg_context->u.bulk.port->buffers)) {
		spin_unlock_irqrestore(&msg_context->u.bulk.port->slock, flags);
		pr_err("buffer list empty trying to submit bulk receive\n");

		/* todo: this is a serious error, we should never have
		 * committed a buffer_to_host operation to the mmal
		 * port without the buffer to back it up (underflow
		 * handling) and there is no obvious way to deal with
		 * this - how is the mmal servie going to react when
		 * we fail to do the xfer and reschedule a buffer when
		 * it arrives? perhaps a starved flag to indicate a
		 * waiting bulk receive?
		 */

		mutex_unlock(&instance->bulk_mutex);

		return -EINVAL;
	}

	msg_context->u.bulk.buffer =
	    list_entry(msg_context->u.bulk.port->buffers.next,
		       struct mmal_buffer, list);
	list_del(&msg_context->u.bulk.buffer->list);

	spin_unlock_irqrestore(&msg_context->u.bulk.port->slock, flags);

	/* ensure we do not overrun the available buffer */
	if (rd_len > msg_context->u.bulk.buffer->buffer_size) {
		rd_len = msg_context->u.bulk.buffer->buffer_size;
		pr_warn("short read as not enough receive buffer space\n");
		/* todo: is this the correct response, what happens to
		 * the rest of the message data?
		 */
	}

	/* store length */
	msg_context->u.bulk.buffer_used = rd_len;
	msg_context->u.bulk.mmal_flags =
	    msg->u.buffer_from_host.buffer_header.flags;
	msg_context->u.bulk.dts = msg->u.buffer_from_host.buffer_header.dts;
	msg_context->u.bulk.pts = msg->u.buffer_from_host.buffer_header.pts;

	/* queue the bulk submission */
	vchi_service_use(instance->handle);
	ret = vchi_bulk_queue_receive(instance->handle,
				      msg_context->u.bulk.buffer->buffer,
				      /* Actual receive needs to be a multiple
				       * of 4 bytes
				       */
				      (rd_len + 3) & ~3,
				      VCHI_FLAGS_CALLBACK_WHEN_OP_COMPLETE |
				      VCHI_FLAGS_BLOCK_UNTIL_QUEUED,
				      msg_context);

	vchi_service_release(instance->handle);

	if (ret != 0) {
		/* callback will not be clearing the mutex */
		mutex_unlock(&instance->bulk_mutex);
	}

	return ret;
}

/* enque a dummy bulk receive for a given message context */
static int dummy_bulk_receive(struct vchiq_mmal_instance *instance,
			      struct mmal_msg_context *msg_context)
{
	int ret;

	/* bulk mutex stops other bulk operations while we have a
	 * receive in progress - released in callback
	 */
	ret = mutex_lock_interruptible(&instance->bulk_mutex);
	if (ret != 0)
		return ret;

	/* zero length indicates this was a dummy transfer */
	msg_context->u.bulk.buffer_used = 0;

	/* queue the bulk submission */
	vchi_service_use(instance->handle);

	ret = vchi_bulk_queue_receive(instance->handle,
				      instance->bulk_scratch,
				      8,
				      VCHI_FLAGS_CALLBACK_WHEN_OP_COMPLETE |
				      VCHI_FLAGS_BLOCK_UNTIL_QUEUED,
				      msg_context);

	vchi_service_release(instance->handle);

	if (ret != 0) {
		/* callback will not be clearing the mutex */
		mutex_unlock(&instance->bulk_mutex);
	}

	return ret;
}

/* data in message, memcpy from packet into output buffer */
static int inline_receive(struct vchiq_mmal_instance *instance,
			  struct mmal_msg *msg,
			  struct mmal_msg_context *msg_context)
{
	unsigned long flags = 0;

	/* take buffer from queue */
	spin_lock_irqsave(&msg_context->u.bulk.port->slock, flags);
	if (list_empty(&msg_context->u.bulk.port->buffers)) {
		spin_unlock_irqrestore(&msg_context->u.bulk.port->slock, flags);
		pr_err("buffer list empty trying to receive inline\n");

		/* todo: this is a serious error, we should never have
		 * committed a buffer_to_host operation to the mmal
		 * port without the buffer to back it up (with
		 * underflow handling) and there is no obvious way to
		 * deal with this. Less bad than the bulk case as we
		 * can just drop this on the floor but...unhelpful
		 */
		return -EINVAL;
	}

	msg_context->u.bulk.buffer =
	    list_entry(msg_context->u.bulk.port->buffers.next,
		       struct mmal_buffer, list);
	list_del(&msg_context->u.bulk.buffer->list);

	spin_unlock_irqrestore(&msg_context->u.bulk.port->slock, flags);

	memcpy(msg_context->u.bulk.buffer->buffer,
	       msg->u.buffer_from_host.short_data,
	       msg->u.buffer_from_host.payload_in_message);

	msg_context->u.bulk.buffer_used =
	    msg->u.buffer_from_host.payload_in_message;

	return 0;
}

/* queue the buffer availability with MMAL_MSG_TYPE_BUFFER_FROM_HOST */
static int
buffer_from_host(struct vchiq_mmal_instance *instance,
		 struct vchiq_mmal_port *port, struct mmal_buffer *buf)
{
	struct mmal_msg_context *msg_context;
	struct mmal_msg m;
	int ret;

	pr_debug("instance:%p buffer:%p\n", instance->handle, buf);

	/* bulk mutex stops other bulk operations while we
	 * have a receive in progress
	 */
	if (mutex_lock_interruptible(&instance->bulk_mutex))
		return -EINTR;

	/* get context */
	msg_context = get_msg_context(instance);
	if (IS_ERR(msg_context)) {
		ret = PTR_ERR(msg_context);
		goto unlock;
	}

	/* store bulk message context for when data arrives */
	msg_context->u.bulk.instance = instance;
	msg_context->u.bulk.port = port;
	msg_context->u.bulk.buffer = NULL;	/* not valid until bulk xfer */
	msg_context->u.bulk.buffer_used = 0;

	/* initialise work structure ready to schedule callback */
	INIT_WORK(&msg_context->u.bulk.work, buffer_work_cb);

	/* prep the buffer from host message */
	memset(&m, 0xbc, sizeof(m));	/* just to make debug clearer */

	m.h.type = MMAL_MSG_TYPE_BUFFER_FROM_HOST;
	m.h.magic = MMAL_MAGIC;
	m.h.context = msg_context->handle;
	m.h.status = 0;

	/* drvbuf is our private data passed back */
	m.u.buffer_from_host.drvbuf.magic = MMAL_MAGIC;
	m.u.buffer_from_host.drvbuf.component_handle = port->component->handle;
	m.u.buffer_from_host.drvbuf.port_handle = port->handle;
	m.u.buffer_from_host.drvbuf.client_context = msg_context->handle;

	/* buffer header */
	m.u.buffer_from_host.buffer_header.cmd = 0;
	m.u.buffer_from_host.buffer_header.data =
		(u32)(unsigned long)buf->buffer;
	m.u.buffer_from_host.buffer_header.alloc_size = buf->buffer_size;
	m.u.buffer_from_host.buffer_header.length = 0;	/* nothing used yet */
	m.u.buffer_from_host.buffer_header.offset = 0;	/* no offset */
	m.u.buffer_from_host.buffer_header.flags = 0;	/* no flags */
	m.u.buffer_from_host.buffer_header.pts = MMAL_TIME_UNKNOWN;
	m.u.buffer_from_host.buffer_header.dts = MMAL_TIME_UNKNOWN;

	/* clear buffer type sepecific data */
	memset(&m.u.buffer_from_host.buffer_header_type_specific, 0,
	       sizeof(m.u.buffer_from_host.buffer_header_type_specific));

	/* no payload in message */
	m.u.buffer_from_host.payload_in_message = 0;

	vchi_service_use(instance->handle);

	ret = vchi_queue_kernel_message(instance->handle,
					&m,
					sizeof(struct mmal_msg_header) +
					sizeof(m.u.buffer_from_host));

	if (ret != 0) {
		release_msg_context(msg_context);
		/* todo: is this correct error value? */
	}

	vchi_service_release(instance->handle);

unlock:
	mutex_unlock(&instance->bulk_mutex);

	return ret;
}

/* submit a buffer to the mmal sevice
 *
 * the buffer_from_host uses size data from the ports next available
 * mmal_buffer and deals with there being no buffer available by
 * incrementing the underflow for later
 */
static int port_buffer_from_host(struct vchiq_mmal_instance *instance,
				 struct vchiq_mmal_port *port)
{
	int ret;
	struct mmal_buffer *buf;
	unsigned long flags = 0;

	if (!port->enabled)
		return -EINVAL;

	/* peek buffer from queue */
	spin_lock_irqsave(&port->slock, flags);
	if (list_empty(&port->buffers)) {
		port->buffer_underflow++;
		spin_unlock_irqrestore(&port->slock, flags);
		return -ENOSPC;
	}

	buf = list_entry(port->buffers.next, struct mmal_buffer, list);

	spin_unlock_irqrestore(&port->slock, flags);

	/* issue buffer to mmal service */
	ret = buffer_from_host(instance, port, buf);
	if (ret) {
		pr_err("adding buffer header failed\n");
		/* todo: how should this be dealt with */
	}

	return ret;
}

/* deals with receipt of buffer to host message */
static void buffer_to_host_cb(struct vchiq_mmal_instance *instance,
			      struct mmal_msg *msg, u32 msg_len)
{
	struct mmal_msg_context *msg_context;
	u32 handle;

	pr_debug("buffer_to_host_cb: instance:%p msg:%p msg_len:%d\n",
		 instance, msg, msg_len);

	if (msg->u.buffer_from_host.drvbuf.magic == MMAL_MAGIC) {
		handle = msg->u.buffer_from_host.drvbuf.client_context;
		msg_context = lookup_msg_context(instance, handle);

		if (!msg_context) {
			pr_err("drvbuf.client_context(%u) is invalid\n",
			       handle);
			return;
		}
	} else {
		pr_err("MMAL_MSG_TYPE_BUFFER_TO_HOST with bad magic\n");
		return;
	}

	if (msg->h.status != MMAL_MSG_STATUS_SUCCESS) {
		/* message reception had an error */
		pr_warn("error %d in reply\n", msg->h.status);

		msg_context->u.bulk.status = msg->h.status;

	} else if (msg->u.buffer_from_host.buffer_header.length == 0) {
		/* empty buffer */
		if (msg->u.buffer_from_host.buffer_header.flags &
		    MMAL_BUFFER_HEADER_FLAG_EOS) {
			msg_context->u.bulk.status =
			    dummy_bulk_receive(instance, msg_context);
			if (msg_context->u.bulk.status == 0)
				return;	/* successful bulk submission, bulk
					 * completion will trigger callback
					 */
		} else {
			/* do callback with empty buffer - not EOS though */
			msg_context->u.bulk.status = 0;
			msg_context->u.bulk.buffer_used = 0;
		}
	} else if (msg->u.buffer_from_host.payload_in_message == 0) {
		/* data is not in message, queue a bulk receive */
		msg_context->u.bulk.status =
		    bulk_receive(instance, msg, msg_context);
		if (msg_context->u.bulk.status == 0)
			return;	/* successful bulk submission, bulk
				 * completion will trigger callback
				 */

		/* failed to submit buffer, this will end badly */
		pr_err("error %d on bulk submission\n",
		       msg_context->u.bulk.status);

	} else if (msg->u.buffer_from_host.payload_in_message <=
		   MMAL_VC_SHORT_DATA) {
		/* data payload within message */
		msg_context->u.bulk.status = inline_receive(instance, msg,
							    msg_context);
	} else {
		pr_err("message with invalid short payload\n");

		/* signal error */
		msg_context->u.bulk.status = -EINVAL;
		msg_context->u.bulk.buffer_used =
		    msg->u.buffer_from_host.payload_in_message;
	}

	/* replace the buffer header */
	port_buffer_from_host(instance, msg_context->u.bulk.port);

	/* schedule the port callback */
	schedule_work(&msg_context->u.bulk.work);
}

static void bulk_receive_cb(struct vchiq_mmal_instance *instance,
			    struct mmal_msg_context *msg_context)
{
	/* bulk receive operation complete */
	mutex_unlock(&msg_context->u.bulk.instance->bulk_mutex);

	/* replace the buffer header */
	port_buffer_from_host(msg_context->u.bulk.instance,
			      msg_context->u.bulk.port);

	msg_context->u.bulk.status = 0;

	/* schedule the port callback */
	schedule_work(&msg_context->u.bulk.work);
}

static void bulk_abort_cb(struct vchiq_mmal_instance *instance,
			  struct mmal_msg_context *msg_context)
{
	pr_err("%s: bulk ABORTED msg_context:%p\n", __func__, msg_context);

	/* bulk receive operation complete */
	mutex_unlock(&msg_context->u.bulk.instance->bulk_mutex);

	/* replace the buffer header */
	port_buffer_from_host(msg_context->u.bulk.instance,
			      msg_context->u.bulk.port);

	msg_context->u.bulk.status = -EINTR;

	schedule_work(&msg_context->u.bulk.work);
}

/* incoming event service callback */
static void service_callback(void *param,
			     const VCHI_CALLBACK_REASON_T reason,
			     void *bulk_ctx)
{
	struct vchiq_mmal_instance *instance = param;
	int status;
	u32 msg_len;
	struct mmal_msg *msg;
	VCHI_HELD_MSG_T msg_handle;
	struct mmal_msg_context *msg_context;

	if (!instance) {
		pr_err("Message callback passed NULL instance\n");
		return;
	}

	switch (reason) {
	case VCHI_CALLBACK_MSG_AVAILABLE:
		status = vchi_msg_hold(instance->handle, (void **)&msg,
				       &msg_len, VCHI_FLAGS_NONE, &msg_handle);
		if (status) {
			pr_err("Unable to dequeue a message (%d)\n", status);
			break;
		}

		DBG_DUMP_MSG(msg, msg_len, "<<< reply message");

		/* handling is different for buffer messages */
		switch (msg->h.type) {
		case MMAL_MSG_TYPE_BUFFER_FROM_HOST:
			vchi_held_msg_release(&msg_handle);
			break;

		case MMAL_MSG_TYPE_EVENT_TO_HOST:
			event_to_host_cb(instance, msg, msg_len);
			vchi_held_msg_release(&msg_handle);

			break;

		case MMAL_MSG_TYPE_BUFFER_TO_HOST:
			buffer_to_host_cb(instance, msg, msg_len);
			vchi_held_msg_release(&msg_handle);
			break;

		default:
			/* messages dependent on header context to complete */
			if (!msg->h.context) {
				pr_err("received message context was null!\n");
				vchi_held_msg_release(&msg_handle);
				break;
			}

			msg_context = lookup_msg_context(instance,
							 msg->h.context);
			if (!msg_context) {
				pr_err("received invalid message context %u!\n",
				       msg->h.context);
				vchi_held_msg_release(&msg_handle);
				break;
			}

			/* fill in context values */
			msg_context->u.sync.msg_handle = msg_handle;
			msg_context->u.sync.msg = msg;
			msg_context->u.sync.msg_len = msg_len;

			/* todo: should this check (completion_done()
			 * == 1) for no one waiting? or do we need a
			 * flag to tell us the completion has been
			 * interrupted so we can free the message and
			 * its context. This probably also solves the
			 * message arriving after interruption todo
			 * below
			 */

			/* complete message so caller knows it happened */
			complete(&msg_context->u.sync.cmplt);
			break;
		}

		break;

	case VCHI_CALLBACK_BULK_RECEIVED:
		bulk_receive_cb(instance, bulk_ctx);
		break;

	case VCHI_CALLBACK_BULK_RECEIVE_ABORTED:
		bulk_abort_cb(instance, bulk_ctx);
		break;

	case VCHI_CALLBACK_SERVICE_CLOSED:
		/* TODO: consider if this requires action if received when
		 * driver is not explicitly closing the service
		 */
		break;

	default:
		pr_err("Received unhandled message reason %d\n", reason);
		break;
	}
}

static int send_synchronous_mmal_msg(struct vchiq_mmal_instance *instance,
				     struct mmal_msg *msg,
				     unsigned int payload_len,
				     struct mmal_msg **msg_out,
				     VCHI_HELD_MSG_T *msg_handle_out)
{
	struct mmal_msg_context *msg_context;
	int ret;

	/* payload size must not cause message to exceed max size */
	if (payload_len >
	    (MMAL_MSG_MAX_SIZE - sizeof(struct mmal_msg_header))) {
		pr_err("payload length %d exceeds max:%d\n", payload_len,
		      (int)(MMAL_MSG_MAX_SIZE -
			    sizeof(struct mmal_msg_header)));
		return -EINVAL;
	}

	msg_context = get_msg_context(instance);
	if (IS_ERR(msg_context))
		return PTR_ERR(msg_context);

	init_completion(&msg_context->u.sync.cmplt);

	msg->h.magic = MMAL_MAGIC;
	msg->h.context = msg_context->handle;
	msg->h.status = 0;

	DBG_DUMP_MSG(msg, (sizeof(struct mmal_msg_header) + payload_len),
		     ">>> sync message");

	vchi_service_use(instance->handle);

	ret = vchi_queue_kernel_message(instance->handle,
					msg,
					sizeof(struct mmal_msg_header) +
					payload_len);

	vchi_service_release(instance->handle);

	if (ret) {
		pr_err("error %d queuing message\n", ret);
		release_msg_context(msg_context);
		return ret;
	}

	ret = wait_for_completion_timeout(&msg_context->u.sync.cmplt, 3 * HZ);
	if (ret <= 0) {
		pr_err("error %d waiting for sync completion\n", ret);
		if (ret == 0)
			ret = -ETIME;
		/* todo: what happens if the message arrives after aborting */
		release_msg_context(msg_context);
		return ret;
	}

	*msg_out = msg_context->u.sync.msg;
	*msg_handle_out = msg_context->u.sync.msg_handle;
	release_msg_context(msg_context);

	return 0;
}

static void dump_port_info(struct vchiq_mmal_port *port)
{
	pr_debug("port handle:0x%x enabled:%d\n", port->handle, port->enabled);

	pr_debug("buffer minimum num:%d size:%d align:%d\n",
		 port->minimum_buffer.num,
		 port->minimum_buffer.size, port->minimum_buffer.alignment);

	pr_debug("buffer recommended num:%d size:%d align:%d\n",
		 port->recommended_buffer.num,
		 port->recommended_buffer.size,
		 port->recommended_buffer.alignment);

	pr_debug("buffer current values num:%d size:%d align:%d\n",
		 port->current_buffer.num,
		 port->current_buffer.size, port->current_buffer.alignment);

	pr_debug("elementary stream: type:%d encoding:0x%x variant:0x%x\n",
		 port->format.type,
		 port->format.encoding, port->format.encoding_variant);

	pr_debug("		    bitrate:%d flags:0x%x\n",
		 port->format.bitrate, port->format.flags);

	if (port->format.type == MMAL_ES_TYPE_VIDEO) {
		pr_debug
		    ("es video format: width:%d height:%d colourspace:0x%x\n",
		     port->es.video.width, port->es.video.height,
		     port->es.video.color_space);

		pr_debug("		 : crop xywh %d,%d,%d,%d\n",
			 port->es.video.crop.x,
			 port->es.video.crop.y,
			 port->es.video.crop.width, port->es.video.crop.height);
		pr_debug("		 : framerate %d/%d  aspect %d/%d\n",
			 port->es.video.frame_rate.num,
			 port->es.video.frame_rate.den,
			 port->es.video.par.num, port->es.video.par.den);
	}
}

static void port_to_mmal_msg(struct vchiq_mmal_port *port, struct mmal_port *p)
{
	/* todo do readonly fields need setting at all? */
	p->type = port->type;
	p->index = port->index;
	p->index_all = 0;
	p->is_enabled = port->enabled;
	p->buffer_num_min = port->minimum_buffer.num;
	p->buffer_size_min = port->minimum_buffer.size;
	p->buffer_alignment_min = port->minimum_buffer.alignment;
	p->buffer_num_recommended = port->recommended_buffer.num;
	p->buffer_size_recommended = port->recommended_buffer.size;

	/* only three writable fields in a port */
	p->buffer_num = port->current_buffer.num;
	p->buffer_size = port->current_buffer.size;
	p->userdata = (u32)(unsigned long)port;
}

static int port_info_set(struct vchiq_mmal_instance *instance,
			 struct vchiq_mmal_port *port)
{
	int ret;
	struct mmal_msg m;
	struct mmal_msg *rmsg;
	VCHI_HELD_MSG_T rmsg_handle;

	pr_debug("setting port info port %p\n", port);
	if (!port)
		return -1;
	dump_port_info(port);

	m.h.type = MMAL_MSG_TYPE_PORT_INFO_SET;

	m.u.port_info_set.component_handle = port->component->handle;
	m.u.port_info_set.port_type = port->type;
	m.u.port_info_set.port_index = port->index;

	port_to_mmal_msg(port, &m.u.port_info_set.port);

	/* elementary stream format setup */
	m.u.port_info_set.format.type = port->format.type;
	m.u.port_info_set.format.encoding = port->format.encoding;
	m.u.port_info_set.format.encoding_variant =
	    port->format.encoding_variant;
	m.u.port_info_set.format.bitrate = port->format.bitrate;
	m.u.port_info_set.format.flags = port->format.flags;

	memcpy(&m.u.port_info_set.es, &port->es,
	       sizeof(union mmal_es_specific_format));

	m.u.port_info_set.format.extradata_size = port->format.extradata_size;
	memcpy(&m.u.port_info_set.extradata, port->format.extradata,
	       port->format.extradata_size);

	ret = send_synchronous_mmal_msg(instance, &m,
					sizeof(m.u.port_info_set),
					&rmsg, &rmsg_handle);
	if (ret)
		return ret;

	if (rmsg->h.type != MMAL_MSG_TYPE_PORT_INFO_SET) {
		/* got an unexpected message type in reply */
		ret = -EINVAL;
		goto release_msg;
	}

	/* return operation status */
	ret = -rmsg->u.port_info_get_reply.status;

	pr_debug("%s:result:%d component:0x%x port:%d\n", __func__, ret,
		 port->component->handle, port->handle);

release_msg:
	vchi_held_msg_release(&rmsg_handle);

	return ret;
}

/* use port info get message to retrieve port information */
static int port_info_get(struct vchiq_mmal_instance *instance,
			 struct vchiq_mmal_port *port)
{
	int ret;
	struct mmal_msg m;
	struct mmal_msg *rmsg;
	VCHI_HELD_MSG_T rmsg_handle;

	/* port info time */
	m.h.type = MMAL_MSG_TYPE_PORT_INFO_GET;
	m.u.port_info_get.component_handle = port->component->handle;
	m.u.port_info_get.port_type = port->type;
	m.u.port_info_get.index = port->index;

	ret = send_synchronous_mmal_msg(instance, &m,
					sizeof(m.u.port_info_get),
					&rmsg, &rmsg_handle);
	if (ret)
		return ret;

	if (rmsg->h.type != MMAL_MSG_TYPE_PORT_INFO_GET) {
		/* got an unexpected message type in reply */
		ret = -EINVAL;
		goto release_msg;
	}

	/* return operation status */
	ret = -rmsg->u.port_info_get_reply.status;
	if (ret != MMAL_MSG_STATUS_SUCCESS)
		goto release_msg;

	if (rmsg->u.port_info_get_reply.port.is_enabled == 0)
		port->enabled = false;
	else
		port->enabled = true;

	/* copy the values out of the message */
	port->handle = rmsg->u.port_info_get_reply.port_handle;

	/* port type and index cached to use on port info set because
	 * it does not use a port handle
	 */
	port->type = rmsg->u.port_info_get_reply.port_type;
	port->index = rmsg->u.port_info_get_reply.port_index;

	port->minimum_buffer.num =
	    rmsg->u.port_info_get_reply.port.buffer_num_min;
	port->minimum_buffer.size =
	    rmsg->u.port_info_get_reply.port.buffer_size_min;
	port->minimum_buffer.alignment =
	    rmsg->u.port_info_get_reply.port.buffer_alignment_min;

	port->recommended_buffer.alignment =
	    rmsg->u.port_info_get_reply.port.buffer_alignment_min;
	port->recommended_buffer.num =
	    rmsg->u.port_info_get_reply.port.buffer_num_recommended;

	port->current_buffer.num = rmsg->u.port_info_get_reply.port.buffer_num;
	port->current_buffer.size =
	    rmsg->u.port_info_get_reply.port.buffer_size;

	/* stream format */
	port->format.type = rmsg->u.port_info_get_reply.format.type;
	port->format.encoding = rmsg->u.port_info_get_reply.format.encoding;
	port->format.encoding_variant =
	    rmsg->u.port_info_get_reply.format.encoding_variant;
	port->format.bitrate = rmsg->u.port_info_get_reply.format.bitrate;
	port->format.flags = rmsg->u.port_info_get_reply.format.flags;

	/* elementary stream format */
	memcpy(&port->es,
	       &rmsg->u.port_info_get_reply.es,
	       sizeof(union mmal_es_specific_format));
	port->format.es = &port->es;

	port->format.extradata_size =
	    rmsg->u.port_info_get_reply.format.extradata_size;
	memcpy(port->format.extradata,
	       rmsg->u.port_info_get_reply.extradata,
	       port->format.extradata_size);

	pr_debug("received port info\n");
	dump_port_info(port);

release_msg:

	pr_debug("%s:result:%d component:0x%x port:%d\n",
		 __func__, ret, port->component->handle, port->handle);

	vchi_held_msg_release(&rmsg_handle);

	return ret;
}

/* create comonent on vc */
static int create_component(struct vchiq_mmal_instance *instance,
			    struct vchiq_mmal_component *component,
			    const char *name)
{
	int ret;
	struct mmal_msg m;
	struct mmal_msg *rmsg;
	VCHI_HELD_MSG_T rmsg_handle;

	/* build component create message */
	m.h.type = MMAL_MSG_TYPE_COMPONENT_CREATE;
	m.u.component_create.client_component = (u32)(unsigned long)component;
	strncpy(m.u.component_create.name, name,
		sizeof(m.u.component_create.name));

	ret = send_synchronous_mmal_msg(instance, &m,
					sizeof(m.u.component_create),
					&rmsg, &rmsg_handle);
	if (ret)
		return ret;

	if (rmsg->h.type != m.h.type) {
		/* got an unexpected message type in reply */
		ret = -EINVAL;
		goto release_msg;
	}

	ret = -rmsg->u.component_create_reply.status;
	if (ret != MMAL_MSG_STATUS_SUCCESS)
		goto release_msg;

	/* a valid component response received */
	component->handle = rmsg->u.component_create_reply.component_handle;
	component->inputs = rmsg->u.component_create_reply.input_num;
	component->outputs = rmsg->u.component_create_reply.output_num;
	component->clocks = rmsg->u.component_create_reply.clock_num;

	pr_debug("Component handle:0x%x in:%d out:%d clock:%d\n",
		 component->handle,
		 component->inputs, component->outputs, component->clocks);

release_msg:
	vchi_held_msg_release(&rmsg_handle);

	return ret;
}

/* destroys a component on vc */
static int destroy_component(struct vchiq_mmal_instance *instance,
			     struct vchiq_mmal_component *component)
{
	int ret;
	struct mmal_msg m;
	struct mmal_msg *rmsg;
	VCHI_HELD_MSG_T rmsg_handle;

	m.h.type = MMAL_MSG_TYPE_COMPONENT_DESTROY;
	m.u.component_destroy.component_handle = component->handle;

	ret = send_synchronous_mmal_msg(instance, &m,
					sizeof(m.u.component_destroy),
					&rmsg, &rmsg_handle);
	if (ret)
		return ret;

	if (rmsg->h.type != m.h.type) {
		/* got an unexpected message type in reply */
		ret = -EINVAL;
		goto release_msg;
	}

	ret = -rmsg->u.component_destroy_reply.status;

release_msg:

	vchi_held_msg_release(&rmsg_handle);

	return ret;
}

/* enable a component on vc */
static int enable_component(struct vchiq_mmal_instance *instance,
			    struct vchiq_mmal_component *component)
{
	int ret;
	struct mmal_msg m;
	struct mmal_msg *rmsg;
	VCHI_HELD_MSG_T rmsg_handle;

	m.h.type = MMAL_MSG_TYPE_COMPONENT_ENABLE;
	m.u.component_enable.component_handle = component->handle;

	ret = send_synchronous_mmal_msg(instance, &m,
					sizeof(m.u.component_enable),
					&rmsg, &rmsg_handle);
	if (ret)
		return ret;

	if (rmsg->h.type != m.h.type) {
		/* got an unexpected message type in reply */
		ret = -EINVAL;
		goto release_msg;
	}

	ret = -rmsg->u.component_enable_reply.status;

release_msg:
	vchi_held_msg_release(&rmsg_handle);

	return ret;
}

/* disable a component on vc */
static int disable_component(struct vchiq_mmal_instance *instance,
			     struct vchiq_mmal_component *component)
{
	int ret;
	struct mmal_msg m;
	struct mmal_msg *rmsg;
	VCHI_HELD_MSG_T rmsg_handle;

	m.h.type = MMAL_MSG_TYPE_COMPONENT_DISABLE;
	m.u.component_disable.component_handle = component->handle;

	ret = send_synchronous_mmal_msg(instance, &m,
					sizeof(m.u.component_disable),
					&rmsg, &rmsg_handle);
	if (ret)
		return ret;

	if (rmsg->h.type != m.h.type) {
		/* got an unexpected message type in reply */
		ret = -EINVAL;
		goto release_msg;
	}

	ret = -rmsg->u.component_disable_reply.status;

release_msg:

	vchi_held_msg_release(&rmsg_handle);

	return ret;
}

/* get version of mmal implementation */
static int get_version(struct vchiq_mmal_instance *instance,
		       u32 *major_out, u32 *minor_out)
{
	int ret;
	struct mmal_msg m;
	struct mmal_msg *rmsg;
	VCHI_HELD_MSG_T rmsg_handle;

	m.h.type = MMAL_MSG_TYPE_GET_VERSION;

	ret = send_synchronous_mmal_msg(instance, &m,
					sizeof(m.u.version),
					&rmsg, &rmsg_handle);
	if (ret)
		return ret;

	if (rmsg->h.type != m.h.type) {
		/* got an unexpected message type in reply */
		ret = -EINVAL;
		goto release_msg;
	}

	*major_out = rmsg->u.version.major;
	*minor_out = rmsg->u.version.minor;

release_msg:
	vchi_held_msg_release(&rmsg_handle);

	return ret;
}

/* do a port action with a port as a parameter */
static int port_action_port(struct vchiq_mmal_instance *instance,
			    struct vchiq_mmal_port *port,
			    enum mmal_msg_port_action_type action_type)
{
	int ret;
	struct mmal_msg m;
	struct mmal_msg *rmsg;
	VCHI_HELD_MSG_T rmsg_handle;

	m.h.type = MMAL_MSG_TYPE_PORT_ACTION;
	m.u.port_action_port.component_handle = port->component->handle;
	m.u.port_action_port.port_handle = port->handle;
	m.u.port_action_port.action = action_type;

	port_to_mmal_msg(port, &m.u.port_action_port.port);

	ret = send_synchronous_mmal_msg(instance, &m,
					sizeof(m.u.port_action_port),
					&rmsg, &rmsg_handle);
	if (ret)
		return ret;

	if (rmsg->h.type != MMAL_MSG_TYPE_PORT_ACTION) {
		/* got an unexpected message type in reply */
		ret = -EINVAL;
		goto release_msg;
	}

	ret = -rmsg->u.port_action_reply.status;

	pr_debug("%s:result:%d component:0x%x port:%d action:%s(%d)\n",
		 __func__,
		 ret, port->component->handle, port->handle,
		 port_action_type_names[action_type], action_type);

release_msg:
	vchi_held_msg_release(&rmsg_handle);

	return ret;
}

/* do a port action with handles as parameters */
static int port_action_handle(struct vchiq_mmal_instance *instance,
			      struct vchiq_mmal_port *port,
			      enum mmal_msg_port_action_type action_type,
			      u32 connect_component_handle,
			      u32 connect_port_handle)
{
	int ret;
	struct mmal_msg m;
	struct mmal_msg *rmsg;
	VCHI_HELD_MSG_T rmsg_handle;

	m.h.type = MMAL_MSG_TYPE_PORT_ACTION;

	m.u.port_action_handle.component_handle = port->component->handle;
	m.u.port_action_handle.port_handle = port->handle;
	m.u.port_action_handle.action = action_type;

	m.u.port_action_handle.connect_component_handle =
	    connect_component_handle;
	m.u.port_action_handle.connect_port_handle = connect_port_handle;

	ret = send_synchronous_mmal_msg(instance, &m,
					sizeof(m.u.port_action_handle),
					&rmsg, &rmsg_handle);
	if (ret)
		return ret;

	if (rmsg->h.type != MMAL_MSG_TYPE_PORT_ACTION) {
		/* got an unexpected message type in reply */
		ret = -EINVAL;
		goto release_msg;
	}

	ret = -rmsg->u.port_action_reply.status;

	pr_debug("%s:result:%d component:0x%x port:%d action:%s(%d)" \
		 " connect component:0x%x connect port:%d\n",
		 __func__,
		 ret, port->component->handle, port->handle,
		 port_action_type_names[action_type],
		 action_type, connect_component_handle, connect_port_handle);

release_msg:
	vchi_held_msg_release(&rmsg_handle);

	return ret;
}

static int port_parameter_set(struct vchiq_mmal_instance *instance,
			      struct vchiq_mmal_port *port,
			      u32 parameter_id, void *value, u32 value_size)
{
	int ret;
	struct mmal_msg m;
	struct mmal_msg *rmsg;
	VCHI_HELD_MSG_T rmsg_handle;

	m.h.type = MMAL_MSG_TYPE_PORT_PARAMETER_SET;

	m.u.port_parameter_set.component_handle = port->component->handle;
	m.u.port_parameter_set.port_handle = port->handle;
	m.u.port_parameter_set.id = parameter_id;
	m.u.port_parameter_set.size = (2 * sizeof(u32)) + value_size;
	memcpy(&m.u.port_parameter_set.value, value, value_size);

	ret = send_synchronous_mmal_msg(instance, &m,
					(4 * sizeof(u32)) + value_size,
					&rmsg, &rmsg_handle);
	if (ret)
		return ret;

	if (rmsg->h.type != MMAL_MSG_TYPE_PORT_PARAMETER_SET) {
		/* got an unexpected message type in reply */
		ret = -EINVAL;
		goto release_msg;
	}

	ret = -rmsg->u.port_parameter_set_reply.status;

	pr_debug("%s:result:%d component:0x%x port:%d parameter:%d\n",
		 __func__,
		 ret, port->component->handle, port->handle, parameter_id);

release_msg:
	vchi_held_msg_release(&rmsg_handle);

	return ret;
}

static int port_parameter_get(struct vchiq_mmal_instance *instance,
			      struct vchiq_mmal_port *port,
			      u32 parameter_id, void *value, u32 *value_size)
{
	int ret;
	struct mmal_msg m;
	struct mmal_msg *rmsg;
	VCHI_HELD_MSG_T rmsg_handle;

	m.h.type = MMAL_MSG_TYPE_PORT_PARAMETER_GET;

	m.u.port_parameter_get.component_handle = port->component->handle;
	m.u.port_parameter_get.port_handle = port->handle;
	m.u.port_parameter_get.id = parameter_id;
	m.u.port_parameter_get.size = (2 * sizeof(u32)) + *value_size;

	ret = send_synchronous_mmal_msg(instance, &m,
					sizeof(struct
					       mmal_msg_port_parameter_get),
					&rmsg, &rmsg_handle);
	if (ret)
		return ret;

	if (rmsg->h.type != MMAL_MSG_TYPE_PORT_PARAMETER_GET) {
		/* got an unexpected message type in reply */
		pr_err("Incorrect reply type %d\n", rmsg->h.type);
		ret = -EINVAL;
		goto release_msg;
	}

	ret = -rmsg->u.port_parameter_get_reply.status;
	/* port_parameter_get_reply.size includes the header,
	 * whilst *value_size doesn't.
	 */
	rmsg->u.port_parameter_get_reply.size -= (2 * sizeof(u32));

	if (ret || rmsg->u.port_parameter_get_reply.size > *value_size) {
		/* Copy only as much as we have space for
		 * but report true size of parameter
		 */
		memcpy(value, &rmsg->u.port_parameter_get_reply.value,
		       *value_size);
		*value_size = rmsg->u.port_parameter_get_reply.size;
	} else
		memcpy(value, &rmsg->u.port_parameter_get_reply.value,
		       rmsg->u.port_parameter_get_reply.size);

	pr_debug("%s:result:%d component:0x%x port:%d parameter:%d\n", __func__,
		 ret, port->component->handle, port->handle, parameter_id);

release_msg:
	vchi_held_msg_release(&rmsg_handle);

	return ret;
}

/* disables a port and drains buffers from it */
static int port_disable(struct vchiq_mmal_instance *instance,
			struct vchiq_mmal_port *port)
{
	int ret;
	struct list_head *q, *buf_head;
	unsigned long flags = 0;

	if (!port->enabled)
		return 0;

	port->enabled = false;

	ret = port_action_port(instance, port,
			       MMAL_MSG_PORT_ACTION_TYPE_DISABLE);
	if (ret == 0) {
		/* drain all queued buffers on port */
		spin_lock_irqsave(&port->slock, flags);

		list_for_each_safe(buf_head, q, &port->buffers) {
			struct mmal_buffer *mmalbuf;

			mmalbuf = list_entry(buf_head, struct mmal_buffer,
					     list);
			list_del(buf_head);
			if (port->buffer_cb)
				port->buffer_cb(instance,
						port, 0, mmalbuf, 0, 0,
						MMAL_TIME_UNKNOWN,
						MMAL_TIME_UNKNOWN);
		}

		spin_unlock_irqrestore(&port->slock, flags);

		ret = port_info_get(instance, port);
	}

	return ret;
}

/* enable a port */
static int port_enable(struct vchiq_mmal_instance *instance,
		       struct vchiq_mmal_port *port)
{
	unsigned int hdr_count;
	struct list_head *buf_head;
	int ret;

	if (port->enabled)
		return 0;

	/* ensure there are enough buffers queued to cover the buffer headers */
	if (port->buffer_cb) {
		hdr_count = 0;
		list_for_each(buf_head, &port->buffers) {
			hdr_count++;
		}
		if (hdr_count < port->current_buffer.num)
			return -ENOSPC;
	}

	ret = port_action_port(instance, port,
			       MMAL_MSG_PORT_ACTION_TYPE_ENABLE);
	if (ret)
		goto done;

	port->enabled = true;

	if (port->buffer_cb) {
		/* send buffer headers to videocore */
		hdr_count = 1;
		list_for_each(buf_head, &port->buffers) {
			struct mmal_buffer *mmalbuf;

			mmalbuf = list_entry(buf_head, struct mmal_buffer,
					     list);
			ret = buffer_from_host(instance, port, mmalbuf);
			if (ret)
				goto done;

			hdr_count++;
			if (hdr_count > port->current_buffer.num)
				break;
		}
	}

	ret = port_info_get(instance, port);

done:
	return ret;
}

/* ------------------------------------------------------------------
 * Exported API
 *------------------------------------------------------------------
 */

int vchiq_mmal_port_set_format(struct vchiq_mmal_instance *instance,
			       struct vchiq_mmal_port *port)
{
	int ret;

	if (mutex_lock_interruptible(&instance->vchiq_mutex))
		return -EINTR;

	ret = port_info_set(instance, port);
	if (ret)
		goto release_unlock;

	/* read what has actually been set */
	ret = port_info_get(instance, port);

release_unlock:
	mutex_unlock(&instance->vchiq_mutex);

	return ret;
}

int vchiq_mmal_port_parameter_set(struct vchiq_mmal_instance *instance,
				  struct vchiq_mmal_port *port,
				  u32 parameter, void *value, u32 value_size)
{
	int ret;

	if (mutex_lock_interruptible(&instance->vchiq_mutex))
		return -EINTR;

	ret = port_parameter_set(instance, port, parameter, value, value_size);

	mutex_unlock(&instance->vchiq_mutex);

	return ret;
}

int vchiq_mmal_port_parameter_get(struct vchiq_mmal_instance *instance,
				  struct vchiq_mmal_port *port,
				  u32 parameter, void *value, u32 *value_size)
{
	int ret;

	if (mutex_lock_interruptible(&instance->vchiq_mutex))
		return -EINTR;

	ret = port_parameter_get(instance, port, parameter, value, value_size);

	mutex_unlock(&instance->vchiq_mutex);

	return ret;
}

/* enable a port
 *
 * enables a port and queues buffers for satisfying callbacks if we
 * provide a callback handler
 */
int vchiq_mmal_port_enable(struct vchiq_mmal_instance *instance,
			   struct vchiq_mmal_port *port,
			   vchiq_mmal_buffer_cb buffer_cb)
{
	int ret;

	if (mutex_lock_interruptible(&instance->vchiq_mutex))
		return -EINTR;

	/* already enabled - noop */
	if (port->enabled) {
		ret = 0;
		goto unlock;
	}

	port->buffer_cb = buffer_cb;

	ret = port_enable(instance, port);

unlock:
	mutex_unlock(&instance->vchiq_mutex);

	return ret;
}

int vchiq_mmal_port_disable(struct vchiq_mmal_instance *instance,
			    struct vchiq_mmal_port *port)
{
	int ret;

	if (mutex_lock_interruptible(&instance->vchiq_mutex))
		return -EINTR;

	if (!port->enabled) {
		mutex_unlock(&instance->vchiq_mutex);
		return 0;
	}

	ret = port_disable(instance, port);

	mutex_unlock(&instance->vchiq_mutex);

	return ret;
}

/* ports will be connected in a tunneled manner so data buffers
 * are not handled by client.
 */
int vchiq_mmal_port_connect_tunnel(struct vchiq_mmal_instance *instance,
				   struct vchiq_mmal_port *src,
				   struct vchiq_mmal_port *dst)
{
	int ret;

	if (mutex_lock_interruptible(&instance->vchiq_mutex))
		return -EINTR;

	/* disconnect ports if connected */
	if (src->connected) {
		ret = port_disable(instance, src);
		if (ret) {
			pr_err("failed disabling src port(%d)\n", ret);
			goto release_unlock;
		}

		/* do not need to disable the destination port as they
		 * are connected and it is done automatically
		 */

		ret = port_action_handle(instance, src,
					 MMAL_MSG_PORT_ACTION_TYPE_DISCONNECT,
					 src->connected->component->handle,
					 src->connected->handle);
		if (ret < 0) {
			pr_err("failed disconnecting src port\n");
			goto release_unlock;
		}
		src->connected->enabled = false;
		src->connected = NULL;
	}

	if (!dst) {
		/* do not make new connection */
		ret = 0;
		pr_debug("not making new connection\n");
		goto release_unlock;
	}

	/* copy src port format to dst */
	dst->format.encoding = src->format.encoding;
	dst->es.video.width = src->es.video.width;
	dst->es.video.height = src->es.video.height;
	dst->es.video.crop.x = src->es.video.crop.x;
	dst->es.video.crop.y = src->es.video.crop.y;
	dst->es.video.crop.width = src->es.video.crop.width;
	dst->es.video.crop.height = src->es.video.crop.height;
	dst->es.video.frame_rate.num = src->es.video.frame_rate.num;
	dst->es.video.frame_rate.den = src->es.video.frame_rate.den;

	/* set new format */
	ret = port_info_set(instance, dst);
	if (ret) {
		pr_debug("setting port info failed\n");
		goto release_unlock;
	}

	/* read what has actually been set */
	ret = port_info_get(instance, dst);
	if (ret) {
		pr_debug("read back port info failed\n");
		goto release_unlock;
	}

	/* connect two ports together */
	ret = port_action_handle(instance, src,
				 MMAL_MSG_PORT_ACTION_TYPE_CONNECT,
				 dst->component->handle, dst->handle);
	if (ret < 0) {
		pr_debug("connecting port %d:%d to %d:%d failed\n",
			 src->component->handle, src->handle,
			 dst->component->handle, dst->handle);
		goto release_unlock;
	}
	src->connected = dst;

release_unlock:

	mutex_unlock(&instance->vchiq_mutex);

	return ret;
}

int vchiq_mmal_submit_buffer(struct vchiq_mmal_instance *instance,
			     struct vchiq_mmal_port *port,
			     struct mmal_buffer *buffer)
{
	unsigned long flags = 0;

	spin_lock_irqsave(&port->slock, flags);
	list_add_tail(&buffer->list, &port->buffers);
	spin_unlock_irqrestore(&port->slock, flags);

	/* the port previously underflowed because it was missing a
	 * mmal_buffer which has just been added, submit that buffer
	 * to the mmal service.
	 */
	if (port->buffer_underflow) {
		port_buffer_from_host(instance, port);
		port->buffer_underflow--;
	}

	return 0;
}

/* Initialise a mmal component and its ports
 *
 */
int vchiq_mmal_component_init(struct vchiq_mmal_instance *instance,
			      const char *name,
			      struct vchiq_mmal_component **component_out)
{
	int ret;
	int idx;		/* port index */
	struct vchiq_mmal_component *component;

	if (mutex_lock_interruptible(&instance->vchiq_mutex))
		return -EINTR;

	if (instance->component_idx == VCHIQ_MMAL_MAX_COMPONENTS) {
		ret = -EINVAL;	/* todo is this correct error? */
		goto unlock;
	}

	component = &instance->component[instance->component_idx];

	ret = create_component(instance, component, name);
	if (ret < 0)
		goto unlock;

	/* ports info needs gathering */
	component->control.type = MMAL_PORT_TYPE_CONTROL;
	component->control.index = 0;
	component->control.component = component;
	spin_lock_init(&component->control.slock);
	INIT_LIST_HEAD(&component->control.buffers);
	ret = port_info_get(instance, &component->control);
	if (ret < 0)
		goto release_component;

	for (idx = 0; idx < component->inputs; idx++) {
		component->input[idx].type = MMAL_PORT_TYPE_INPUT;
		component->input[idx].index = idx;
		component->input[idx].component = component;
		spin_lock_init(&component->input[idx].slock);
		INIT_LIST_HEAD(&component->input[idx].buffers);
		ret = port_info_get(instance, &component->input[idx]);
		if (ret < 0)
			goto release_component;
	}

	for (idx = 0; idx < component->outputs; idx++) {
		component->output[idx].type = MMAL_PORT_TYPE_OUTPUT;
		component->output[idx].index = idx;
		component->output[idx].component = component;
		spin_lock_init(&component->output[idx].slock);
		INIT_LIST_HEAD(&component->output[idx].buffers);
		ret = port_info_get(instance, &component->output[idx]);
		if (ret < 0)
			goto release_component;
	}

	for (idx = 0; idx < component->clocks; idx++) {
		component->clock[idx].type = MMAL_PORT_TYPE_CLOCK;
		component->clock[idx].index = idx;
		component->clock[idx].component = component;
		spin_lock_init(&component->clock[idx].slock);
		INIT_LIST_HEAD(&component->clock[idx].buffers);
		ret = port_info_get(instance, &component->clock[idx]);
		if (ret < 0)
			goto release_component;
	}

	instance->component_idx++;

	*component_out = component;

	mutex_unlock(&instance->vchiq_mutex);

	return 0;

release_component:
	destroy_component(instance, component);
unlock:
	mutex_unlock(&instance->vchiq_mutex);

	return ret;
}

/*
 * cause a mmal component to be destroyed
 */
int vchiq_mmal_component_finalise(struct vchiq_mmal_instance *instance,
				  struct vchiq_mmal_component *component)
{
	int ret;

	if (mutex_lock_interruptible(&instance->vchiq_mutex))
		return -EINTR;

	if (component->enabled)
		ret = disable_component(instance, component);

	ret = destroy_component(instance, component);

	mutex_unlock(&instance->vchiq_mutex);

	return ret;
}

/*
 * cause a mmal component to be enabled
 */
int vchiq_mmal_component_enable(struct vchiq_mmal_instance *instance,
				struct vchiq_mmal_component *component)
{
	int ret;

	if (mutex_lock_interruptible(&instance->vchiq_mutex))
		return -EINTR;

	if (component->enabled) {
		mutex_unlock(&instance->vchiq_mutex);
		return 0;
	}

	ret = enable_component(instance, component);
	if (ret == 0)
		component->enabled = true;

	mutex_unlock(&instance->vchiq_mutex);

	return ret;
}

/*
 * cause a mmal component to be enabled
 */
int vchiq_mmal_component_disable(struct vchiq_mmal_instance *instance,
				 struct vchiq_mmal_component *component)
{
	int ret;

	if (mutex_lock_interruptible(&instance->vchiq_mutex))
		return -EINTR;

	if (!component->enabled) {
		mutex_unlock(&instance->vchiq_mutex);
		return 0;
	}

	ret = disable_component(instance, component);
	if (ret == 0)
		component->enabled = false;

	mutex_unlock(&instance->vchiq_mutex);

	return ret;
}

int vchiq_mmal_version(struct vchiq_mmal_instance *instance,
		       u32 *major_out, u32 *minor_out)
{
	int ret;

	if (mutex_lock_interruptible(&instance->vchiq_mutex))
		return -EINTR;

	ret = get_version(instance, major_out, minor_out);

	mutex_unlock(&instance->vchiq_mutex);

	return ret;
}

int vchiq_mmal_finalise(struct vchiq_mmal_instance *instance)
{
	int status = 0;

	if (!instance)
		return -EINVAL;

	if (mutex_lock_interruptible(&instance->vchiq_mutex))
		return -EINTR;

	vchi_service_use(instance->handle);

	status = vchi_service_close(instance->handle);
	if (status != 0)
		pr_err("mmal-vchiq: VCHIQ close failed");

	mutex_unlock(&instance->vchiq_mutex);

	vfree(instance->bulk_scratch);

	mmal_context_map_destroy(&instance->context_map);

	kfree(instance);

	return status;
}

int vchiq_mmal_init(struct vchiq_mmal_instance **out_instance)
{
	int status;
	struct vchiq_mmal_instance *instance;
	static VCHI_CONNECTION_T *vchi_connection;
	static VCHI_INSTANCE_T vchi_instance;
	SERVICE_CREATION_T params = {
		.version		= VCHI_VERSION_EX(VC_MMAL_VER, VC_MMAL_MIN_VER),
		.service_id		= VC_MMAL_SERVER_NAME,
		.connection		= vchi_connection,
		.rx_fifo_size		= 0,
		.tx_fifo_size		= 0,
		.callback		= service_callback,
		.callback_param		= NULL,
		.want_unaligned_bulk_rx = 1,
		.want_unaligned_bulk_tx = 1,
		.want_crc		= 0
	};

	/* compile time checks to ensure structure size as they are
	 * directly (de)serialised from memory.
	 */

	/* ensure the header structure has packed to the correct size */
	BUILD_BUG_ON(sizeof(struct mmal_msg_header) != 24);

	/* ensure message structure does not exceed maximum length */
	BUILD_BUG_ON(sizeof(struct mmal_msg) > MMAL_MSG_MAX_SIZE);

	/* mmal port struct is correct size */
	BUILD_BUG_ON(sizeof(struct mmal_port) != 64);

	/* create a vchi instance */
	status = vchi_initialise(&vchi_instance);
	if (status) {
		pr_err("Failed to initialise VCHI instance (status=%d)\n",
		       status);
		return -EIO;
	}

	status = vchi_connect(NULL, 0, vchi_instance);
	if (status) {
		pr_err("Failed to connect VCHI instance (status=%d)\n", status);
		return -EIO;
	}

	instance = kzalloc(sizeof(*instance), GFP_KERNEL);

	if (!instance)
		return -ENOMEM;

	mutex_init(&instance->vchiq_mutex);
	mutex_init(&instance->bulk_mutex);

	instance->bulk_scratch = vmalloc(PAGE_SIZE);

	status = mmal_context_map_init(&instance->context_map);
	if (status) {
		pr_err("Failed to init context map (status=%d)\n", status);
		kfree(instance);
		return status;
	}

	params.callback_param = instance;

	status = vchi_service_open(vchi_instance, &params, &instance->handle);
	if (status) {
		pr_err("Failed to open VCHI service connection (status=%d)\n",
		       status);
		goto err_close_services;
	}

	vchi_service_release(instance->handle);

	*out_instance = instance;

	return 0;

err_close_services:

	vchi_service_close(instance->handle);
	vfree(instance->bulk_scratch);
	kfree(instance);
	return -ENODEV;
}
