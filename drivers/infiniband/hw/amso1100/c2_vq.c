/*
 * Copyright (c) 2005 Ammasso, Inc. All rights reserved.
 * Copyright (c) 2005 Open Grid Computing, Inc. All rights reserved.
 *
 * This software is available to you under a choice of one of two
 * licenses.  You may choose to be licensed under the terms of the GNU
 * General Public License (GPL) Version 2, available from the file
 * COPYING in the main directory of this source tree, or the
 * OpenIB.org BSD license below:
 *
 *     Redistribution and use in source and binary forms, with or
 *     without modification, are permitted provided that the following
 *     conditions are met:
 *
 *      - Redistributions of source code must retain the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer.
 *
 *      - Redistributions in binary form must reproduce the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer in the documentation and/or other materials
 *        provided with the distribution.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */
#include <linux/slab.h>
#include <linux/spinlock.h>

#include "c2_vq.h"
#include "c2_provider.h"

/*
 * Verbs Request Objects:
 *
 * VQ Request Objects are allocated by the kernel verbs handlers.
 * They contain a wait object, a refcnt, an atomic bool indicating that the
 * adapter has replied, and a copy of the verb reply work request.
 * A pointer to the VQ Request Object is passed down in the context
 * field of the work request message, and reflected back by the adapter
 * in the verbs reply message.  The function handle_vq() in the interrupt
 * path will use this pointer to:
 * 	1) append a copy of the verbs reply message
 * 	2) mark that the reply is ready
 * 	3) wake up the kernel verbs handler blocked awaiting the reply.
 *
 *
 * The kernel verbs handlers do a "get" to put a 2nd reference on the
 * VQ Request object.  If the kernel verbs handler exits before the adapter
 * can respond, this extra reference will keep the VQ Request object around
 * until the adapter's reply can be processed.  The reason we need this is
 * because a pointer to this object is stuffed into the context field of
 * the verbs work request message, and reflected back in the reply message.
 * It is used in the interrupt handler (handle_vq()) to wake up the appropriate
 * kernel verb handler that is blocked awaiting the verb reply.
 * So handle_vq() will do a "put" on the object when it's done accessing it.
 * NOTE:  If we guarantee that the kernel verb handler will never bail before
 *        getting the reply, then we don't need these refcnts.
 *
 *
 * VQ Request objects are freed by the kernel verbs handlers only
 * after the verb has been processed, or when the adapter fails and
 * does not reply.
 *
 *
 * Verbs Reply Buffers:
 *
 * VQ Reply bufs are local host memory copies of a
 * outstanding Verb Request reply
 * message.  The are always allocated by the kernel verbs handlers, and _may_ be
 * freed by either the kernel verbs handler -or- the interrupt handler.  The
 * kernel verbs handler _must_ free the repbuf, then free the vq request object
 * in that order.
 */

int vq_init(struct c2_dev *c2dev)
{
	sprintf(c2dev->vq_cache_name, "c2-vq:dev%c",
		(char) ('0' + c2dev->devnum));
	c2dev->host_msg_cache =
	    kmem_cache_create(c2dev->vq_cache_name, c2dev->rep_vq.msg_size, 0,
			      SLAB_HWCACHE_ALIGN, NULL, NULL);
	if (c2dev->host_msg_cache == NULL) {
		return -ENOMEM;
	}
	return 0;
}

void vq_term(struct c2_dev *c2dev)
{
	kmem_cache_destroy(c2dev->host_msg_cache);
}

/* vq_req_alloc - allocate a VQ Request Object and initialize it.
 * The refcnt is set to 1.
 */
struct c2_vq_req *vq_req_alloc(struct c2_dev *c2dev)
{
	struct c2_vq_req *r;

	r = kmalloc(sizeof(struct c2_vq_req), GFP_KERNEL);
	if (r) {
		init_waitqueue_head(&r->wait_object);
		r->reply_msg = (u64) NULL;
		r->event = 0;
		r->cm_id = NULL;
		r->qp = NULL;
		atomic_set(&r->refcnt, 1);
		atomic_set(&r->reply_ready, 0);
	}
	return r;
}


/* vq_req_free - free the VQ Request Object.  It is assumed the verbs handler
 * has already free the VQ Reply Buffer if it existed.
 */
void vq_req_free(struct c2_dev *c2dev, struct c2_vq_req *r)
{
	r->reply_msg = (u64) NULL;
	if (atomic_dec_and_test(&r->refcnt)) {
		kfree(r);
	}
}

/* vq_req_get - reference a VQ Request Object.  Done
 * only in the kernel verbs handlers.
 */
void vq_req_get(struct c2_dev *c2dev, struct c2_vq_req *r)
{
	atomic_inc(&r->refcnt);
}


/* vq_req_put - dereference and potentially free a VQ Request Object.
 *
 * This is only called by handle_vq() on the
 * interrupt when it is done processing
 * a verb reply message.  If the associated
 * kernel verbs handler has already bailed,
 * then this put will actually free the VQ
 * Request object _and_ the VQ Reply Buffer
 * if it exists.
 */
void vq_req_put(struct c2_dev *c2dev, struct c2_vq_req *r)
{
	if (atomic_dec_and_test(&r->refcnt)) {
		if (r->reply_msg != (u64) NULL)
			vq_repbuf_free(c2dev,
				       (void *) (unsigned long) r->reply_msg);
		kfree(r);
	}
}


/*
 * vq_repbuf_alloc - allocate a VQ Reply Buffer.
 */
void *vq_repbuf_alloc(struct c2_dev *c2dev)
{
	return kmem_cache_alloc(c2dev->host_msg_cache, GFP_ATOMIC);
}

/*
 * vq_send_wr - post a verbs request message to the Verbs Request Queue.
 * If a message is not available in the MQ, then block until one is available.
 * NOTE: handle_mq() on the interrupt context will wake up threads blocked here.
 * When the adapter drains the Verbs Request Queue,
 * it inserts MQ index 0 in to the
 * adapter->host activity fifo and interrupts the host.
 */
int vq_send_wr(struct c2_dev *c2dev, union c2wr *wr)
{
	void *msg;
	wait_queue_t __wait;

	/*
	 * grab adapter vq lock
	 */
	spin_lock(&c2dev->vqlock);

	/*
	 * allocate msg
	 */
	msg = c2_mq_alloc(&c2dev->req_vq);

	/*
	 * If we cannot get a msg, then we'll wait
	 * When a messages are available, the int handler will wake_up()
	 * any waiters.
	 */
	while (msg == NULL) {
		pr_debug("%s:%d no available msg in VQ, waiting...\n",
		       __FUNCTION__, __LINE__);
		init_waitqueue_entry(&__wait, current);
		add_wait_queue(&c2dev->req_vq_wo, &__wait);
		spin_unlock(&c2dev->vqlock);
		for (;;) {
			set_current_state(TASK_INTERRUPTIBLE);
			if (!c2_mq_full(&c2dev->req_vq)) {
				break;
			}
			if (!signal_pending(current)) {
				schedule_timeout(1 * HZ);	/* 1 second... */
				continue;
			}
			set_current_state(TASK_RUNNING);
			remove_wait_queue(&c2dev->req_vq_wo, &__wait);
			return -EINTR;
		}
		set_current_state(TASK_RUNNING);
		remove_wait_queue(&c2dev->req_vq_wo, &__wait);
		spin_lock(&c2dev->vqlock);
		msg = c2_mq_alloc(&c2dev->req_vq);
	}

	/*
	 * copy wr into adapter msg
	 */
	memcpy(msg, wr, c2dev->req_vq.msg_size);

	/*
	 * post msg
	 */
	c2_mq_produce(&c2dev->req_vq);

	/*
	 * release adapter vq lock
	 */
	spin_unlock(&c2dev->vqlock);
	return 0;
}


/*
 * vq_wait_for_reply - block until the adapter posts a Verb Reply Message.
 */
int vq_wait_for_reply(struct c2_dev *c2dev, struct c2_vq_req *req)
{
	if (!wait_event_timeout(req->wait_object,
				atomic_read(&req->reply_ready),
				60*HZ))
		return -ETIMEDOUT;

	return 0;
}

/*
 * vq_repbuf_free - Free a Verbs Reply Buffer.
 */
void vq_repbuf_free(struct c2_dev *c2dev, void *reply)
{
	kmem_cache_free(c2dev->host_msg_cache, reply);
}
