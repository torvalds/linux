/*
 * Intel MIC Platform Software Stack (MPSS)
 *
 * Copyright(c) 2015 Intel Corporation.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License, version 2, as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * General Public License for more details.
 *
 * Intel SCIF driver.
 *
 */

#include "scif_main.h"

/**
 * scif_recv_mark: Handle SCIF_MARK request
 * @msg:	Interrupt message
 *
 * The peer has requested a mark.
 */
void scif_recv_mark(struct scif_dev *scifdev, struct scifmsg *msg)
{
	struct scif_endpt *ep = (struct scif_endpt *)msg->payload[0];
	int mark = 0;
	int err;

	err = _scif_fence_mark(ep, &mark);
	if (err)
		msg->uop = SCIF_MARK_NACK;
	else
		msg->uop = SCIF_MARK_ACK;
	msg->payload[0] = ep->remote_ep;
	msg->payload[2] = mark;
	scif_nodeqp_send(ep->remote_dev, msg);
}

/**
 * scif_recv_mark_resp: Handle SCIF_MARK_(N)ACK messages.
 * @msg:	Interrupt message
 *
 * The peer has responded to a SCIF_MARK message.
 */
void scif_recv_mark_resp(struct scif_dev *scifdev, struct scifmsg *msg)
{
	struct scif_endpt *ep = (struct scif_endpt *)msg->payload[0];
	struct scif_fence_info *fence_req =
		(struct scif_fence_info *)msg->payload[1];

	mutex_lock(&ep->rma_info.rma_lock);
	if (msg->uop == SCIF_MARK_ACK) {
		fence_req->state = OP_COMPLETED;
		fence_req->dma_mark = (int)msg->payload[2];
	} else {
		fence_req->state = OP_FAILED;
	}
	mutex_unlock(&ep->rma_info.rma_lock);
	complete(&fence_req->comp);
}

/**
 * scif_recv_wait: Handle SCIF_WAIT request
 * @msg:	Interrupt message
 *
 * The peer has requested waiting on a fence.
 */
void scif_recv_wait(struct scif_dev *scifdev, struct scifmsg *msg)
{
	struct scif_endpt *ep = (struct scif_endpt *)msg->payload[0];
	struct scif_remote_fence_info *fence;

	/*
	 * Allocate structure for remote fence information and
	 * send a NACK if the allocation failed. The peer will
	 * return ENOMEM upon receiving a NACK.
	 */
	fence = kmalloc(sizeof(*fence), GFP_KERNEL);
	if (!fence) {
		msg->payload[0] = ep->remote_ep;
		msg->uop = SCIF_WAIT_NACK;
		scif_nodeqp_send(ep->remote_dev, msg);
		return;
	}

	/* Prepare the fence request */
	memcpy(&fence->msg, msg, sizeof(struct scifmsg));
	INIT_LIST_HEAD(&fence->list);

	/* Insert to the global remote fence request list */
	mutex_lock(&scif_info.fencelock);
	atomic_inc(&ep->rma_info.fence_refcount);
	list_add_tail(&fence->list, &scif_info.fence);
	mutex_unlock(&scif_info.fencelock);

	schedule_work(&scif_info.misc_work);
}

/**
 * scif_recv_wait_resp: Handle SCIF_WAIT_(N)ACK messages.
 * @msg:	Interrupt message
 *
 * The peer has responded to a SCIF_WAIT message.
 */
void scif_recv_wait_resp(struct scif_dev *scifdev, struct scifmsg *msg)
{
	struct scif_endpt *ep = (struct scif_endpt *)msg->payload[0];
	struct scif_fence_info *fence_req =
		(struct scif_fence_info *)msg->payload[1];

	mutex_lock(&ep->rma_info.rma_lock);
	if (msg->uop == SCIF_WAIT_ACK)
		fence_req->state = OP_COMPLETED;
	else
		fence_req->state = OP_FAILED;
	mutex_unlock(&ep->rma_info.rma_lock);
	complete(&fence_req->comp);
}

/**
 * scif_recv_sig_local: Handle SCIF_SIG_LOCAL request
 * @msg:	Interrupt message
 *
 * The peer has requested a signal on a local offset.
 */
void scif_recv_sig_local(struct scif_dev *scifdev, struct scifmsg *msg)
{
	struct scif_endpt *ep = (struct scif_endpt *)msg->payload[0];
	int err;

	err = scif_prog_signal(ep, msg->payload[1], msg->payload[2],
			       SCIF_WINDOW_SELF);
	if (err)
		msg->uop = SCIF_SIG_NACK;
	else
		msg->uop = SCIF_SIG_ACK;
	msg->payload[0] = ep->remote_ep;
	scif_nodeqp_send(ep->remote_dev, msg);
}

/**
 * scif_recv_sig_remote: Handle SCIF_SIGNAL_REMOTE request
 * @msg:	Interrupt message
 *
 * The peer has requested a signal on a remote offset.
 */
void scif_recv_sig_remote(struct scif_dev *scifdev, struct scifmsg *msg)
{
	struct scif_endpt *ep = (struct scif_endpt *)msg->payload[0];
	int err;

	err = scif_prog_signal(ep, msg->payload[1], msg->payload[2],
			       SCIF_WINDOW_PEER);
	if (err)
		msg->uop = SCIF_SIG_NACK;
	else
		msg->uop = SCIF_SIG_ACK;
	msg->payload[0] = ep->remote_ep;
	scif_nodeqp_send(ep->remote_dev, msg);
}

/**
 * scif_recv_sig_resp: Handle SCIF_SIG_(N)ACK messages.
 * @msg:	Interrupt message
 *
 * The peer has responded to a signal request.
 */
void scif_recv_sig_resp(struct scif_dev *scifdev, struct scifmsg *msg)
{
	struct scif_endpt *ep = (struct scif_endpt *)msg->payload[0];
	struct scif_fence_info *fence_req =
		(struct scif_fence_info *)msg->payload[3];

	mutex_lock(&ep->rma_info.rma_lock);
	if (msg->uop == SCIF_SIG_ACK)
		fence_req->state = OP_COMPLETED;
	else
		fence_req->state = OP_FAILED;
	mutex_unlock(&ep->rma_info.rma_lock);
	complete(&fence_req->comp);
}

static inline void *scif_get_local_va(off_t off, struct scif_window *window)
{
	struct page **pages = window->pinned_pages->pages;
	int page_nr = (off - window->offset) >> PAGE_SHIFT;
	off_t page_off = off & ~PAGE_MASK;

	return page_address(pages[page_nr]) + page_off;
}

static void scif_prog_signal_cb(void *arg)
{
	struct scif_cb_arg *cb_arg = arg;

	dma_pool_free(cb_arg->ep->remote_dev->signal_pool, cb_arg->status,
		      cb_arg->src_dma_addr);
	kfree(cb_arg);
}

static int _scif_prog_signal(scif_epd_t epd, dma_addr_t dst, u64 val)
{
	struct scif_endpt *ep = (struct scif_endpt *)epd;
	struct dma_chan *chan = ep->rma_info.dma_chan;
	struct dma_device *ddev = chan->device;
	bool x100 = !is_dma_copy_aligned(chan->device, 1, 1, 1);
	struct dma_async_tx_descriptor *tx;
	struct scif_status *status = NULL;
	struct scif_cb_arg *cb_arg = NULL;
	dma_addr_t src;
	dma_cookie_t cookie;
	int err;

	tx = ddev->device_prep_dma_memcpy(chan, 0, 0, 0, DMA_PREP_FENCE);
	if (!tx) {
		err = -ENOMEM;
		dev_err(&ep->remote_dev->sdev->dev, "%s %d err %d\n",
			__func__, __LINE__, err);
		goto alloc_fail;
	}
	cookie = tx->tx_submit(tx);
	if (dma_submit_error(cookie)) {
		err = (int)cookie;
		dev_err(&ep->remote_dev->sdev->dev, "%s %d err %d\n",
			__func__, __LINE__, err);
		goto alloc_fail;
	}
	dma_async_issue_pending(chan);
	if (x100) {
		/*
		 * For X100 use the status descriptor to write the value to
		 * the destination.
		 */
		tx = ddev->device_prep_dma_imm_data(chan, dst, val, 0);
	} else {
		status = dma_pool_alloc(ep->remote_dev->signal_pool, GFP_KERNEL,
					&src);
		if (!status) {
			err = -ENOMEM;
			dev_err(&ep->remote_dev->sdev->dev, "%s %d err %d\n",
				__func__, __LINE__, err);
			goto alloc_fail;
		}
		status->val = val;
		status->src_dma_addr = src;
		status->ep = ep;
		src += offsetof(struct scif_status, val);
		tx = ddev->device_prep_dma_memcpy(chan, dst, src, sizeof(val),
						  DMA_PREP_INTERRUPT);
	}
	if (!tx) {
		err = -ENOMEM;
		dev_err(&ep->remote_dev->sdev->dev, "%s %d err %d\n",
			__func__, __LINE__, err);
		goto dma_fail;
	}
	if (!x100) {
		cb_arg = kmalloc(sizeof(*cb_arg), GFP_KERNEL);
		if (!cb_arg) {
			err = -ENOMEM;
			goto dma_fail;
		}
		cb_arg->src_dma_addr = src;
		cb_arg->status = status;
		cb_arg->ep = ep;
		tx->callback = scif_prog_signal_cb;
		tx->callback_param = cb_arg;
	}
	cookie = tx->tx_submit(tx);
	if (dma_submit_error(cookie)) {
		err = -EIO;
		dev_err(&ep->remote_dev->sdev->dev, "%s %d err %d\n",
			__func__, __LINE__, err);
		goto dma_fail;
	}
	dma_async_issue_pending(chan);
	return 0;
dma_fail:
	if (!x100) {
		dma_pool_free(ep->remote_dev->signal_pool, status,
			      src - offsetof(struct scif_status, val));
		kfree(cb_arg);
	}
alloc_fail:
	return err;
}

/*
 * scif_prog_signal:
 * @epd - Endpoint Descriptor
 * @offset - registered address to write @val to
 * @val - Value to be written at @offset
 * @type - Type of the window.
 *
 * Arrange to write a value to the registered offset after ensuring that the
 * offset provided is indeed valid.
 */
int scif_prog_signal(scif_epd_t epd, off_t offset, u64 val,
		     enum scif_window_type type)
{
	struct scif_endpt *ep = (struct scif_endpt *)epd;
	struct scif_window *window = NULL;
	struct scif_rma_req req;
	dma_addr_t dst_dma_addr;
	int err;

	mutex_lock(&ep->rma_info.rma_lock);
	req.out_window = &window;
	req.offset = offset;
	req.nr_bytes = sizeof(u64);
	req.prot = SCIF_PROT_WRITE;
	req.type = SCIF_WINDOW_SINGLE;
	if (type == SCIF_WINDOW_SELF)
		req.head = &ep->rma_info.reg_list;
	else
		req.head = &ep->rma_info.remote_reg_list;
	/* Does a valid window exist? */
	err = scif_query_window(&req);
	if (err) {
		dev_err(scif_info.mdev.this_device,
			"%s %d err %d\n", __func__, __LINE__, err);
		goto unlock_ret;
	}

	if (scif_is_mgmt_node() && scifdev_self(ep->remote_dev)) {
		u64 *dst_virt;

		if (type == SCIF_WINDOW_SELF)
			dst_virt = scif_get_local_va(offset, window);
		else
			dst_virt =
			scif_get_local_va(offset, (struct scif_window *)
					  window->peer_window);
		*dst_virt = val;
	} else {
		dst_dma_addr = __scif_off_to_dma_addr(window, offset);
		err = _scif_prog_signal(epd, dst_dma_addr, val);
	}
unlock_ret:
	mutex_unlock(&ep->rma_info.rma_lock);
	return err;
}

static int _scif_fence_wait(scif_epd_t epd, int mark)
{
	struct scif_endpt *ep = (struct scif_endpt *)epd;
	dma_cookie_t cookie = mark & ~SCIF_REMOTE_FENCE;
	int err;

	/* Wait for DMA callback in scif_fence_mark_cb(..) */
	err = wait_event_interruptible_timeout(ep->rma_info.markwq,
					       dma_async_is_tx_complete(
					       ep->rma_info.dma_chan,
					       cookie, NULL, NULL) ==
					       DMA_COMPLETE,
					       SCIF_NODE_ALIVE_TIMEOUT);
	if (!err)
		err = -ETIMEDOUT;
	else if (err > 0)
		err = 0;
	return err;
}

/**
 * scif_rma_handle_remote_fences:
 *
 * This routine services remote fence requests.
 */
void scif_rma_handle_remote_fences(void)
{
	struct list_head *item, *tmp;
	struct scif_remote_fence_info *fence;
	struct scif_endpt *ep;
	int mark, err;

	might_sleep();
	mutex_lock(&scif_info.fencelock);
	list_for_each_safe(item, tmp, &scif_info.fence) {
		fence = list_entry(item, struct scif_remote_fence_info,
				   list);
		/* Remove fence from global list */
		list_del(&fence->list);

		/* Initiate the fence operation */
		ep = (struct scif_endpt *)fence->msg.payload[0];
		mark = fence->msg.payload[2];
		err = _scif_fence_wait(ep, mark);
		if (err)
			fence->msg.uop = SCIF_WAIT_NACK;
		else
			fence->msg.uop = SCIF_WAIT_ACK;
		fence->msg.payload[0] = ep->remote_ep;
		scif_nodeqp_send(ep->remote_dev, &fence->msg);
		kfree(fence);
		if (!atomic_sub_return(1, &ep->rma_info.fence_refcount))
			schedule_work(&scif_info.misc_work);
	}
	mutex_unlock(&scif_info.fencelock);
}

static int _scif_send_fence(scif_epd_t epd, int uop, int mark, int *out_mark)
{
	int err;
	struct scifmsg msg;
	struct scif_fence_info *fence_req;
	struct scif_endpt *ep = (struct scif_endpt *)epd;

	fence_req = kmalloc(sizeof(*fence_req), GFP_KERNEL);
	if (!fence_req) {
		err = -ENOMEM;
		goto error;
	}

	fence_req->state = OP_IN_PROGRESS;
	init_completion(&fence_req->comp);

	msg.src = ep->port;
	msg.uop = uop;
	msg.payload[0] = ep->remote_ep;
	msg.payload[1] = (u64)fence_req;
	if (uop == SCIF_WAIT)
		msg.payload[2] = mark;
	spin_lock(&ep->lock);
	if (ep->state == SCIFEP_CONNECTED)
		err = scif_nodeqp_send(ep->remote_dev, &msg);
	else
		err = -ENOTCONN;
	spin_unlock(&ep->lock);
	if (err)
		goto error_free;
retry:
	/* Wait for a SCIF_WAIT_(N)ACK message */
	err = wait_for_completion_timeout(&fence_req->comp,
					  SCIF_NODE_ALIVE_TIMEOUT);
	if (!err && scifdev_alive(ep))
		goto retry;
	if (!err)
		err = -ENODEV;
	if (err > 0)
		err = 0;
	mutex_lock(&ep->rma_info.rma_lock);
	if (err < 0) {
		if (fence_req->state == OP_IN_PROGRESS)
			fence_req->state = OP_FAILED;
	}
	if (fence_req->state == OP_FAILED && !err)
		err = -ENOMEM;
	if (uop == SCIF_MARK && fence_req->state == OP_COMPLETED)
		*out_mark = SCIF_REMOTE_FENCE | fence_req->dma_mark;
	mutex_unlock(&ep->rma_info.rma_lock);
error_free:
	kfree(fence_req);
error:
	return err;
}

/**
 * scif_send_fence_mark:
 * @epd: end point descriptor.
 * @out_mark: Output DMA mark reported by peer.
 *
 * Send a remote fence mark request.
 */
static int scif_send_fence_mark(scif_epd_t epd, int *out_mark)
{
	return _scif_send_fence(epd, SCIF_MARK, 0, out_mark);
}

/**
 * scif_send_fence_wait:
 * @epd: end point descriptor.
 * @mark: DMA mark to wait for.
 *
 * Send a remote fence wait request.
 */
static int scif_send_fence_wait(scif_epd_t epd, int mark)
{
	return _scif_send_fence(epd, SCIF_WAIT, mark, NULL);
}

static int _scif_send_fence_signal_wait(struct scif_endpt *ep,
					struct scif_fence_info *fence_req)
{
	int err;

retry:
	/* Wait for a SCIF_SIG_(N)ACK message */
	err = wait_for_completion_timeout(&fence_req->comp,
					  SCIF_NODE_ALIVE_TIMEOUT);
	if (!err && scifdev_alive(ep))
		goto retry;
	if (!err)
		err = -ENODEV;
	if (err > 0)
		err = 0;
	if (err < 0) {
		mutex_lock(&ep->rma_info.rma_lock);
		if (fence_req->state == OP_IN_PROGRESS)
			fence_req->state = OP_FAILED;
		mutex_unlock(&ep->rma_info.rma_lock);
	}
	if (fence_req->state == OP_FAILED && !err)
		err = -ENXIO;
	return err;
}

/**
 * scif_send_fence_signal:
 * @epd - endpoint descriptor
 * @loff - local offset
 * @lval - local value to write to loffset
 * @roff - remote offset
 * @rval - remote value to write to roffset
 * @flags - flags
 *
 * Sends a remote fence signal request
 */
static int scif_send_fence_signal(scif_epd_t epd, off_t roff, u64 rval,
				  off_t loff, u64 lval, int flags)
{
	int err = 0;
	struct scifmsg msg;
	struct scif_fence_info *fence_req;
	struct scif_endpt *ep = (struct scif_endpt *)epd;

	fence_req = kmalloc(sizeof(*fence_req), GFP_KERNEL);
	if (!fence_req) {
		err = -ENOMEM;
		goto error;
	}

	fence_req->state = OP_IN_PROGRESS;
	init_completion(&fence_req->comp);
	msg.src = ep->port;
	if (flags & SCIF_SIGNAL_LOCAL) {
		msg.uop = SCIF_SIG_LOCAL;
		msg.payload[0] = ep->remote_ep;
		msg.payload[1] = roff;
		msg.payload[2] = rval;
		msg.payload[3] = (u64)fence_req;
		spin_lock(&ep->lock);
		if (ep->state == SCIFEP_CONNECTED)
			err = scif_nodeqp_send(ep->remote_dev, &msg);
		else
			err = -ENOTCONN;
		spin_unlock(&ep->lock);
		if (err)
			goto error_free;
		err = _scif_send_fence_signal_wait(ep, fence_req);
		if (err)
			goto error_free;
	}
	fence_req->state = OP_IN_PROGRESS;

	if (flags & SCIF_SIGNAL_REMOTE) {
		msg.uop = SCIF_SIG_REMOTE;
		msg.payload[0] = ep->remote_ep;
		msg.payload[1] = loff;
		msg.payload[2] = lval;
		msg.payload[3] = (u64)fence_req;
		spin_lock(&ep->lock);
		if (ep->state == SCIFEP_CONNECTED)
			err = scif_nodeqp_send(ep->remote_dev, &msg);
		else
			err = -ENOTCONN;
		spin_unlock(&ep->lock);
		if (err)
			goto error_free;
		err = _scif_send_fence_signal_wait(ep, fence_req);
	}
error_free:
	kfree(fence_req);
error:
	return err;
}

static void scif_fence_mark_cb(void *arg)
{
	struct scif_endpt *ep = (struct scif_endpt *)arg;

	wake_up_interruptible(&ep->rma_info.markwq);
	atomic_dec(&ep->rma_info.fence_refcount);
}

/*
 * _scif_fence_mark:
 *
 * @epd - endpoint descriptor
 * Set up a mark for this endpoint and return the value of the mark.
 */
int _scif_fence_mark(scif_epd_t epd, int *mark)
{
	struct scif_endpt *ep = (struct scif_endpt *)epd;
	struct dma_chan *chan = ep->rma_info.dma_chan;
	struct dma_device *ddev = chan->device;
	struct dma_async_tx_descriptor *tx;
	dma_cookie_t cookie;
	int err;

	tx = ddev->device_prep_dma_memcpy(chan, 0, 0, 0, DMA_PREP_FENCE);
	if (!tx) {
		err = -ENOMEM;
		dev_err(&ep->remote_dev->sdev->dev, "%s %d err %d\n",
			__func__, __LINE__, err);
		return err;
	}
	cookie = tx->tx_submit(tx);
	if (dma_submit_error(cookie)) {
		err = (int)cookie;
		dev_err(&ep->remote_dev->sdev->dev, "%s %d err %d\n",
			__func__, __LINE__, err);
		return err;
	}
	dma_async_issue_pending(chan);
	tx = ddev->device_prep_dma_interrupt(chan, DMA_PREP_INTERRUPT);
	if (!tx) {
		err = -ENOMEM;
		dev_err(&ep->remote_dev->sdev->dev, "%s %d err %d\n",
			__func__, __LINE__, err);
		return err;
	}
	tx->callback = scif_fence_mark_cb;
	tx->callback_param = ep;
	*mark = cookie = tx->tx_submit(tx);
	if (dma_submit_error(cookie)) {
		err = (int)cookie;
		dev_err(&ep->remote_dev->sdev->dev, "%s %d err %d\n",
			__func__, __LINE__, err);
		return err;
	}
	atomic_inc(&ep->rma_info.fence_refcount);
	dma_async_issue_pending(chan);
	return 0;
}

#define SCIF_LOOPB_MAGIC_MARK 0xdead

int scif_fence_mark(scif_epd_t epd, int flags, int *mark)
{
	struct scif_endpt *ep = (struct scif_endpt *)epd;
	int err = 0;

	dev_dbg(scif_info.mdev.this_device,
		"SCIFAPI fence_mark: ep %p flags 0x%x mark 0x%x\n",
		ep, flags, *mark);
	err = scif_verify_epd(ep);
	if (err)
		return err;

	/* Invalid flags? */
	if (flags & ~(SCIF_FENCE_INIT_SELF | SCIF_FENCE_INIT_PEER))
		return -EINVAL;

	/* At least one of init self or peer RMA should be set */
	if (!(flags & (SCIF_FENCE_INIT_SELF | SCIF_FENCE_INIT_PEER)))
		return -EINVAL;

	/* Exactly one of init self or peer RMA should be set but not both */
	if ((flags & SCIF_FENCE_INIT_SELF) && (flags & SCIF_FENCE_INIT_PEER))
		return -EINVAL;

	/*
	 * Management node loopback does not need to use DMA.
	 * Return a valid mark to be symmetric.
	 */
	if (scifdev_self(ep->remote_dev) && scif_is_mgmt_node()) {
		*mark = SCIF_LOOPB_MAGIC_MARK;
		return 0;
	}

	if (flags & SCIF_FENCE_INIT_SELF)
		err = _scif_fence_mark(epd, mark);
	else
		err = scif_send_fence_mark(ep, mark);

	if (err)
		dev_err(scif_info.mdev.this_device,
			"%s %d err %d\n", __func__, __LINE__, err);
	dev_dbg(scif_info.mdev.this_device,
		"SCIFAPI fence_mark: ep %p flags 0x%x mark 0x%x err %d\n",
		ep, flags, *mark, err);
	return err;
}
EXPORT_SYMBOL_GPL(scif_fence_mark);

int scif_fence_wait(scif_epd_t epd, int mark)
{
	struct scif_endpt *ep = (struct scif_endpt *)epd;
	int err = 0;

	dev_dbg(scif_info.mdev.this_device,
		"SCIFAPI fence_wait: ep %p mark 0x%x\n",
		ep, mark);
	err = scif_verify_epd(ep);
	if (err)
		return err;
	/*
	 * Management node loopback does not need to use DMA.
	 * The only valid mark provided is 0 so simply
	 * return success if the mark is valid.
	 */
	if (scifdev_self(ep->remote_dev) && scif_is_mgmt_node()) {
		if (mark == SCIF_LOOPB_MAGIC_MARK)
			return 0;
		else
			return -EINVAL;
	}
	if (mark & SCIF_REMOTE_FENCE)
		err = scif_send_fence_wait(epd, mark);
	else
		err = _scif_fence_wait(epd, mark);
	if (err < 0)
		dev_err(scif_info.mdev.this_device,
			"%s %d err %d\n", __func__, __LINE__, err);
	return err;
}
EXPORT_SYMBOL_GPL(scif_fence_wait);

int scif_fence_signal(scif_epd_t epd, off_t loff, u64 lval,
		      off_t roff, u64 rval, int flags)
{
	struct scif_endpt *ep = (struct scif_endpt *)epd;
	int err = 0;

	dev_dbg(scif_info.mdev.this_device,
		"SCIFAPI fence_signal: ep %p loff 0x%lx lval 0x%llx roff 0x%lx rval 0x%llx flags 0x%x\n",
		ep, loff, lval, roff, rval, flags);
	err = scif_verify_epd(ep);
	if (err)
		return err;

	/* Invalid flags? */
	if (flags & ~(SCIF_FENCE_INIT_SELF | SCIF_FENCE_INIT_PEER |
			SCIF_SIGNAL_LOCAL | SCIF_SIGNAL_REMOTE))
		return -EINVAL;

	/* At least one of init self or peer RMA should be set */
	if (!(flags & (SCIF_FENCE_INIT_SELF | SCIF_FENCE_INIT_PEER)))
		return -EINVAL;

	/* Exactly one of init self or peer RMA should be set but not both */
	if ((flags & SCIF_FENCE_INIT_SELF) && (flags & SCIF_FENCE_INIT_PEER))
		return -EINVAL;

	/* At least one of SCIF_SIGNAL_LOCAL or SCIF_SIGNAL_REMOTE required */
	if (!(flags & (SCIF_SIGNAL_LOCAL | SCIF_SIGNAL_REMOTE)))
		return -EINVAL;

	/* Only Dword offsets allowed */
	if ((flags & SCIF_SIGNAL_LOCAL) && (loff & (sizeof(u32) - 1)))
		return -EINVAL;

	/* Only Dword aligned offsets allowed */
	if ((flags & SCIF_SIGNAL_REMOTE) && (roff & (sizeof(u32) - 1)))
		return -EINVAL;

	if (flags & SCIF_FENCE_INIT_PEER) {
		err = scif_send_fence_signal(epd, roff, rval, loff,
					     lval, flags);
	} else {
		/* Local Signal in Local RAS */
		if (flags & SCIF_SIGNAL_LOCAL) {
			err = scif_prog_signal(epd, loff, lval,
					       SCIF_WINDOW_SELF);
			if (err)
				goto error_ret;
		}

		/* Signal in Remote RAS */
		if (flags & SCIF_SIGNAL_REMOTE)
			err = scif_prog_signal(epd, roff,
					       rval, SCIF_WINDOW_PEER);
	}
error_ret:
	if (err)
		dev_err(scif_info.mdev.this_device,
			"%s %d err %d\n", __func__, __LINE__, err);
	return err;
}
EXPORT_SYMBOL_GPL(scif_fence_signal);
