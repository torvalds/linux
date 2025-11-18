// SPDX-License-Identifier: GPL-2.0-only
/* Copyright (C) 2023 Intel Corporation */

#include <linux/export.h>
#include <net/libeth/rx.h>

#include "idpf.h"
#include "idpf_virtchnl.h"
#include "idpf_ptp.h"

/**
 * struct idpf_vc_xn_manager - Manager for tracking transactions
 * @ring: backing and lookup for transactions
 * @free_xn_bm: bitmap for free transactions
 * @xn_bm_lock: make bitmap access synchronous where necessary
 * @salt: used to make cookie unique every message
 */
struct idpf_vc_xn_manager {
	struct idpf_vc_xn ring[IDPF_VC_XN_RING_LEN];
	DECLARE_BITMAP(free_xn_bm, IDPF_VC_XN_RING_LEN);
	spinlock_t xn_bm_lock;
	u8 salt;
};

/**
 * idpf_vid_to_vport - Translate vport id to vport pointer
 * @adapter: private data struct
 * @v_id: vport id to translate
 *
 * Returns vport matching v_id, NULL if not found.
 */
static
struct idpf_vport *idpf_vid_to_vport(struct idpf_adapter *adapter, u32 v_id)
{
	u16 num_max_vports = idpf_get_max_vports(adapter);
	int i;

	for (i = 0; i < num_max_vports; i++)
		if (adapter->vport_ids[i] == v_id)
			return adapter->vports[i];

	return NULL;
}

/**
 * idpf_handle_event_link - Handle link event message
 * @adapter: private data struct
 * @v2e: virtchnl event message
 */
static void idpf_handle_event_link(struct idpf_adapter *adapter,
				   const struct virtchnl2_event *v2e)
{
	struct idpf_netdev_priv *np;
	struct idpf_vport *vport;

	vport = idpf_vid_to_vport(adapter, le32_to_cpu(v2e->vport_id));
	if (!vport) {
		dev_err_ratelimited(&adapter->pdev->dev, "Failed to find vport_id %d for link event\n",
				    v2e->vport_id);
		return;
	}
	np = netdev_priv(vport->netdev);

	np->link_speed_mbps = le32_to_cpu(v2e->link_speed);

	if (vport->link_up == v2e->link_status)
		return;

	vport->link_up = v2e->link_status;

	if (np->state != __IDPF_VPORT_UP)
		return;

	if (vport->link_up) {
		netif_tx_start_all_queues(vport->netdev);
		netif_carrier_on(vport->netdev);
	} else {
		netif_tx_stop_all_queues(vport->netdev);
		netif_carrier_off(vport->netdev);
	}
}

/**
 * idpf_recv_event_msg - Receive virtchnl event message
 * @adapter: Driver specific private structure
 * @ctlq_msg: message to copy from
 *
 * Receive virtchnl event message
 */
static void idpf_recv_event_msg(struct idpf_adapter *adapter,
				struct idpf_ctlq_msg *ctlq_msg)
{
	int payload_size = ctlq_msg->ctx.indirect.payload->size;
	struct virtchnl2_event *v2e;
	u32 event;

	if (payload_size < sizeof(*v2e)) {
		dev_err_ratelimited(&adapter->pdev->dev, "Failed to receive valid payload for event msg (op %d len %d)\n",
				    ctlq_msg->cookie.mbx.chnl_opcode,
				    payload_size);
		return;
	}

	v2e = (struct virtchnl2_event *)ctlq_msg->ctx.indirect.payload->va;
	event = le32_to_cpu(v2e->event);

	switch (event) {
	case VIRTCHNL2_EVENT_LINK_CHANGE:
		idpf_handle_event_link(adapter, v2e);
		return;
	default:
		dev_err(&adapter->pdev->dev,
			"Unknown event %d from PF\n", event);
		break;
	}
}

/**
 * idpf_mb_clean - Reclaim the send mailbox queue entries
 * @adapter: Driver specific private structure
 *
 * Reclaim the send mailbox queue entries to be used to send further messages
 *
 * Returns 0 on success, negative on failure
 */
static int idpf_mb_clean(struct idpf_adapter *adapter)
{
	u16 i, num_q_msg = IDPF_DFLT_MBX_Q_LEN;
	struct idpf_ctlq_msg **q_msg;
	struct idpf_dma_mem *dma_mem;
	int err;

	q_msg = kcalloc(num_q_msg, sizeof(struct idpf_ctlq_msg *), GFP_ATOMIC);
	if (!q_msg)
		return -ENOMEM;

	err = idpf_ctlq_clean_sq(adapter->hw.asq, &num_q_msg, q_msg);
	if (err)
		goto err_kfree;

	for (i = 0; i < num_q_msg; i++) {
		if (!q_msg[i])
			continue;
		dma_mem = q_msg[i]->ctx.indirect.payload;
		if (dma_mem)
			dma_free_coherent(&adapter->pdev->dev, dma_mem->size,
					  dma_mem->va, dma_mem->pa);
		kfree(q_msg[i]);
		kfree(dma_mem);
	}

err_kfree:
	kfree(q_msg);

	return err;
}

#if IS_ENABLED(CONFIG_PTP_1588_CLOCK)
/**
 * idpf_ptp_is_mb_msg - Check if the message is PTP-related
 * @op: virtchnl opcode
 *
 * Return: true if msg is PTP-related, false otherwise.
 */
static bool idpf_ptp_is_mb_msg(u32 op)
{
	switch (op) {
	case VIRTCHNL2_OP_PTP_GET_DEV_CLK_TIME:
	case VIRTCHNL2_OP_PTP_GET_CROSS_TIME:
	case VIRTCHNL2_OP_PTP_SET_DEV_CLK_TIME:
	case VIRTCHNL2_OP_PTP_ADJ_DEV_CLK_FINE:
	case VIRTCHNL2_OP_PTP_ADJ_DEV_CLK_TIME:
	case VIRTCHNL2_OP_PTP_GET_VPORT_TX_TSTAMP_CAPS:
	case VIRTCHNL2_OP_PTP_GET_VPORT_TX_TSTAMP:
		return true;
	default:
		return false;
	}
}

/**
 * idpf_prepare_ptp_mb_msg - Prepare PTP related message
 *
 * @adapter: Driver specific private structure
 * @op: virtchnl opcode
 * @ctlq_msg: Corresponding control queue message
 */
static void idpf_prepare_ptp_mb_msg(struct idpf_adapter *adapter, u32 op,
				    struct idpf_ctlq_msg *ctlq_msg)
{
	/* If the message is PTP-related and the secondary mailbox is available,
	 * send the message through the secondary mailbox.
	 */
	if (!idpf_ptp_is_mb_msg(op) || !adapter->ptp->secondary_mbx.valid)
		return;

	ctlq_msg->opcode = idpf_mbq_opc_send_msg_to_peer_drv;
	ctlq_msg->func_id = adapter->ptp->secondary_mbx.peer_mbx_q_id;
	ctlq_msg->host_id = adapter->ptp->secondary_mbx.peer_id;
}
#else /* !CONFIG_PTP_1588_CLOCK */
static void idpf_prepare_ptp_mb_msg(struct idpf_adapter *adapter, u32 op,
				    struct idpf_ctlq_msg *ctlq_msg)
{ }
#endif /* CONFIG_PTP_1588_CLOCK */

/**
 * idpf_send_mb_msg - Send message over mailbox
 * @adapter: Driver specific private structure
 * @op: virtchnl opcode
 * @msg_size: size of the payload
 * @msg: pointer to buffer holding the payload
 * @cookie: unique SW generated cookie per message
 *
 * Will prepare the control queue message and initiates the send api
 *
 * Returns 0 on success, negative on failure
 */
int idpf_send_mb_msg(struct idpf_adapter *adapter, u32 op,
		     u16 msg_size, u8 *msg, u16 cookie)
{
	struct idpf_ctlq_msg *ctlq_msg;
	struct idpf_dma_mem *dma_mem;
	int err;

	/* If we are here and a reset is detected nothing much can be
	 * done. This thread should silently abort and expected to
	 * be corrected with a new run either by user or driver
	 * flows after reset
	 */
	if (idpf_is_reset_detected(adapter))
		return 0;

	err = idpf_mb_clean(adapter);
	if (err)
		return err;

	ctlq_msg = kzalloc(sizeof(*ctlq_msg), GFP_ATOMIC);
	if (!ctlq_msg)
		return -ENOMEM;

	dma_mem = kzalloc(sizeof(*dma_mem), GFP_ATOMIC);
	if (!dma_mem) {
		err = -ENOMEM;
		goto dma_mem_error;
	}

	ctlq_msg->opcode = idpf_mbq_opc_send_msg_to_cp;
	ctlq_msg->func_id = 0;

	idpf_prepare_ptp_mb_msg(adapter, op, ctlq_msg);

	ctlq_msg->data_len = msg_size;
	ctlq_msg->cookie.mbx.chnl_opcode = op;
	ctlq_msg->cookie.mbx.chnl_retval = 0;
	dma_mem->size = IDPF_CTLQ_MAX_BUF_LEN;
	dma_mem->va = dma_alloc_coherent(&adapter->pdev->dev, dma_mem->size,
					 &dma_mem->pa, GFP_ATOMIC);
	if (!dma_mem->va) {
		err = -ENOMEM;
		goto dma_alloc_error;
	}

	/* It's possible we're just sending an opcode but no buffer */
	if (msg && msg_size)
		memcpy(dma_mem->va, msg, msg_size);
	ctlq_msg->ctx.indirect.payload = dma_mem;
	ctlq_msg->ctx.sw_cookie.data = cookie;

	err = idpf_ctlq_send(&adapter->hw, adapter->hw.asq, 1, ctlq_msg);
	if (err)
		goto send_error;

	return 0;

send_error:
	dma_free_coherent(&adapter->pdev->dev, dma_mem->size, dma_mem->va,
			  dma_mem->pa);
dma_alloc_error:
	kfree(dma_mem);
dma_mem_error:
	kfree(ctlq_msg);

	return err;
}

/* API for virtchnl "transaction" support ("xn" for short).
 *
 * We are reusing the completion lock to serialize the accesses to the
 * transaction state for simplicity, but it could be its own separate synchro
 * as well. For now, this API is only used from within a workqueue context;
 * raw_spin_lock() is enough.
 */
/**
 * idpf_vc_xn_lock - Request exclusive access to vc transaction
 * @xn: struct idpf_vc_xn* to access
 */
#define idpf_vc_xn_lock(xn)			\
	raw_spin_lock(&(xn)->completed.wait.lock)

/**
 * idpf_vc_xn_unlock - Release exclusive access to vc transaction
 * @xn: struct idpf_vc_xn* to access
 */
#define idpf_vc_xn_unlock(xn)		\
	raw_spin_unlock(&(xn)->completed.wait.lock)

/**
 * idpf_vc_xn_release_bufs - Release reference to reply buffer(s) and
 * reset the transaction state.
 * @xn: struct idpf_vc_xn to update
 */
static void idpf_vc_xn_release_bufs(struct idpf_vc_xn *xn)
{
	xn->reply.iov_base = NULL;
	xn->reply.iov_len = 0;

	if (xn->state != IDPF_VC_XN_SHUTDOWN)
		xn->state = IDPF_VC_XN_IDLE;
}

/**
 * idpf_vc_xn_init - Initialize virtchnl transaction object
 * @vcxn_mngr: pointer to vc transaction manager struct
 */
static void idpf_vc_xn_init(struct idpf_vc_xn_manager *vcxn_mngr)
{
	int i;

	spin_lock_init(&vcxn_mngr->xn_bm_lock);

	for (i = 0; i < ARRAY_SIZE(vcxn_mngr->ring); i++) {
		struct idpf_vc_xn *xn = &vcxn_mngr->ring[i];

		xn->state = IDPF_VC_XN_IDLE;
		xn->idx = i;
		idpf_vc_xn_release_bufs(xn);
		init_completion(&xn->completed);
	}

	bitmap_fill(vcxn_mngr->free_xn_bm, IDPF_VC_XN_RING_LEN);
}

/**
 * idpf_vc_xn_shutdown - Uninitialize virtchnl transaction object
 * @vcxn_mngr: pointer to vc transaction manager struct
 *
 * All waiting threads will be woken-up and their transaction aborted. Further
 * operations on that object will fail.
 */
void idpf_vc_xn_shutdown(struct idpf_vc_xn_manager *vcxn_mngr)
{
	int i;

	spin_lock_bh(&vcxn_mngr->xn_bm_lock);
	bitmap_zero(vcxn_mngr->free_xn_bm, IDPF_VC_XN_RING_LEN);
	spin_unlock_bh(&vcxn_mngr->xn_bm_lock);

	for (i = 0; i < ARRAY_SIZE(vcxn_mngr->ring); i++) {
		struct idpf_vc_xn *xn = &vcxn_mngr->ring[i];

		idpf_vc_xn_lock(xn);
		xn->state = IDPF_VC_XN_SHUTDOWN;
		idpf_vc_xn_release_bufs(xn);
		idpf_vc_xn_unlock(xn);
		complete_all(&xn->completed);
	}
}

/**
 * idpf_vc_xn_pop_free - Pop a free transaction from free list
 * @vcxn_mngr: transaction manager to pop from
 *
 * Returns NULL if no free transactions
 */
static
struct idpf_vc_xn *idpf_vc_xn_pop_free(struct idpf_vc_xn_manager *vcxn_mngr)
{
	struct idpf_vc_xn *xn = NULL;
	unsigned long free_idx;

	spin_lock_bh(&vcxn_mngr->xn_bm_lock);
	free_idx = find_first_bit(vcxn_mngr->free_xn_bm, IDPF_VC_XN_RING_LEN);
	if (free_idx == IDPF_VC_XN_RING_LEN)
		goto do_unlock;

	clear_bit(free_idx, vcxn_mngr->free_xn_bm);
	xn = &vcxn_mngr->ring[free_idx];
	xn->salt = vcxn_mngr->salt++;

do_unlock:
	spin_unlock_bh(&vcxn_mngr->xn_bm_lock);

	return xn;
}

/**
 * idpf_vc_xn_push_free - Push a free transaction to free list
 * @vcxn_mngr: transaction manager to push to
 * @xn: transaction to push
 */
static void idpf_vc_xn_push_free(struct idpf_vc_xn_manager *vcxn_mngr,
				 struct idpf_vc_xn *xn)
{
	idpf_vc_xn_release_bufs(xn);
	set_bit(xn->idx, vcxn_mngr->free_xn_bm);
}

/**
 * idpf_vc_xn_exec - Perform a send/recv virtchnl transaction
 * @adapter: driver specific private structure with vcxn_mngr
 * @params: parameters for this particular transaction including
 *   -vc_op: virtchannel operation to send
 *   -send_buf: kvec iov for send buf and len
 *   -recv_buf: kvec iov for recv buf and len (ignored if NULL)
 *   -timeout_ms: timeout waiting for a reply (milliseconds)
 *   -async: don't wait for message reply, will lose caller context
 *   -async_handler: callback to handle async replies
 *
 * @returns >= 0 for success, the size of the initial reply (may or may not be
 * >= @recv_buf.iov_len, but we never overflow @@recv_buf_iov_base). < 0 for
 * error.
 */
ssize_t idpf_vc_xn_exec(struct idpf_adapter *adapter,
			const struct idpf_vc_xn_params *params)
{
	const struct kvec *send_buf = &params->send_buf;
	struct idpf_vc_xn *xn;
	ssize_t retval;
	u16 cookie;

	xn = idpf_vc_xn_pop_free(adapter->vcxn_mngr);
	/* no free transactions available */
	if (!xn)
		return -ENOSPC;

	idpf_vc_xn_lock(xn);
	if (xn->state == IDPF_VC_XN_SHUTDOWN) {
		retval = -ENXIO;
		goto only_unlock;
	} else if (xn->state != IDPF_VC_XN_IDLE) {
		/* We're just going to clobber this transaction even though
		 * it's not IDLE. If we don't reuse it we could theoretically
		 * eventually leak all the free transactions and not be able to
		 * send any messages. At least this way we make an attempt to
		 * remain functional even though something really bad is
		 * happening that's corrupting what was supposed to be free
		 * transactions.
		 */
		WARN_ONCE(1, "There should only be idle transactions in free list (idx %d op %d)\n",
			  xn->idx, xn->vc_op);
	}

	xn->reply = params->recv_buf;
	xn->reply_sz = 0;
	xn->state = params->async ? IDPF_VC_XN_ASYNC : IDPF_VC_XN_WAITING;
	xn->vc_op = params->vc_op;
	xn->async_handler = params->async_handler;
	idpf_vc_xn_unlock(xn);

	if (!params->async)
		reinit_completion(&xn->completed);
	cookie = FIELD_PREP(IDPF_VC_XN_SALT_M, xn->salt) |
		 FIELD_PREP(IDPF_VC_XN_IDX_M, xn->idx);

	retval = idpf_send_mb_msg(adapter, params->vc_op,
				  send_buf->iov_len, send_buf->iov_base,
				  cookie);
	if (retval) {
		idpf_vc_xn_lock(xn);
		goto release_and_unlock;
	}

	if (params->async)
		return 0;

	wait_for_completion_timeout(&xn->completed,
				    msecs_to_jiffies(params->timeout_ms));

	/* No need to check the return value; we check the final state of the
	 * transaction below. It's possible the transaction actually gets more
	 * timeout than specified if we get preempted here but after
	 * wait_for_completion_timeout returns. This should be non-issue
	 * however.
	 */
	idpf_vc_xn_lock(xn);
	switch (xn->state) {
	case IDPF_VC_XN_SHUTDOWN:
		retval = -ENXIO;
		goto only_unlock;
	case IDPF_VC_XN_WAITING:
		dev_notice_ratelimited(&adapter->pdev->dev,
				       "Transaction timed-out (op:%d cookie:%04x vc_op:%d salt:%02x timeout:%dms)\n",
				       params->vc_op, cookie, xn->vc_op,
				       xn->salt, params->timeout_ms);
		retval = -ETIME;
		break;
	case IDPF_VC_XN_COMPLETED_SUCCESS:
		retval = xn->reply_sz;
		break;
	case IDPF_VC_XN_COMPLETED_FAILED:
		dev_notice_ratelimited(&adapter->pdev->dev, "Transaction failed (op %d)\n",
				       params->vc_op);
		retval = -EIO;
		break;
	default:
		/* Invalid state. */
		WARN_ON_ONCE(1);
		retval = -EIO;
		break;
	}

release_and_unlock:
	idpf_vc_xn_push_free(adapter->vcxn_mngr, xn);
	/* If we receive a VC reply after here, it will be dropped. */
only_unlock:
	idpf_vc_xn_unlock(xn);

	return retval;
}

/**
 * idpf_vc_xn_forward_async - Handle async reply receives
 * @adapter: private data struct
 * @xn: transaction to handle
 * @ctlq_msg: corresponding ctlq_msg
 *
 * For async sends we're going to lose the caller's context so, if an
 * async_handler was provided, it can deal with the reply, otherwise we'll just
 * check and report if there is an error.
 */
static int
idpf_vc_xn_forward_async(struct idpf_adapter *adapter, struct idpf_vc_xn *xn,
			 const struct idpf_ctlq_msg *ctlq_msg)
{
	int err = 0;

	if (ctlq_msg->cookie.mbx.chnl_opcode != xn->vc_op) {
		dev_err_ratelimited(&adapter->pdev->dev, "Async message opcode does not match transaction opcode (msg: %d) (xn: %d)\n",
				    ctlq_msg->cookie.mbx.chnl_opcode, xn->vc_op);
		xn->reply_sz = 0;
		err = -EINVAL;
		goto release_bufs;
	}

	if (xn->async_handler) {
		err = xn->async_handler(adapter, xn, ctlq_msg);
		goto release_bufs;
	}

	if (ctlq_msg->cookie.mbx.chnl_retval) {
		xn->reply_sz = 0;
		dev_err_ratelimited(&adapter->pdev->dev, "Async message failure (op %d)\n",
				    ctlq_msg->cookie.mbx.chnl_opcode);
		err = -EINVAL;
	}

release_bufs:
	idpf_vc_xn_push_free(adapter->vcxn_mngr, xn);

	return err;
}

/**
 * idpf_vc_xn_forward_reply - copy a reply back to receiving thread
 * @adapter: driver specific private structure with vcxn_mngr
 * @ctlq_msg: controlq message to send back to receiving thread
 */
static int
idpf_vc_xn_forward_reply(struct idpf_adapter *adapter,
			 const struct idpf_ctlq_msg *ctlq_msg)
{
	const void *payload = NULL;
	size_t payload_size = 0;
	struct idpf_vc_xn *xn;
	u16 msg_info;
	int err = 0;
	u16 xn_idx;
	u16 salt;

	msg_info = ctlq_msg->ctx.sw_cookie.data;
	xn_idx = FIELD_GET(IDPF_VC_XN_IDX_M, msg_info);
	if (xn_idx >= ARRAY_SIZE(adapter->vcxn_mngr->ring)) {
		dev_err_ratelimited(&adapter->pdev->dev, "Out of bounds cookie received: %02x\n",
				    xn_idx);
		return -EINVAL;
	}
	xn = &adapter->vcxn_mngr->ring[xn_idx];
	idpf_vc_xn_lock(xn);
	salt = FIELD_GET(IDPF_VC_XN_SALT_M, msg_info);
	if (xn->salt != salt) {
		dev_err_ratelimited(&adapter->pdev->dev, "Transaction salt does not match (exp:%d@%02x(%d) != got:%d@%02x)\n",
				    xn->vc_op, xn->salt, xn->state,
				    ctlq_msg->cookie.mbx.chnl_opcode, salt);
		idpf_vc_xn_unlock(xn);
		return -EINVAL;
	}

	switch (xn->state) {
	case IDPF_VC_XN_WAITING:
		/* success */
		break;
	case IDPF_VC_XN_IDLE:
		dev_err_ratelimited(&adapter->pdev->dev, "Unexpected or belated VC reply (op %d)\n",
				    ctlq_msg->cookie.mbx.chnl_opcode);
		err = -EINVAL;
		goto out_unlock;
	case IDPF_VC_XN_SHUTDOWN:
		/* ENXIO is a bit special here as the recv msg loop uses that
		 * know if it should stop trying to clean the ring if we lost
		 * the virtchnl. We need to stop playing with registers and
		 * yield.
		 */
		err = -ENXIO;
		goto out_unlock;
	case IDPF_VC_XN_ASYNC:
		err = idpf_vc_xn_forward_async(adapter, xn, ctlq_msg);
		idpf_vc_xn_unlock(xn);
		return err;
	default:
		dev_err_ratelimited(&adapter->pdev->dev, "Overwriting VC reply (op %d)\n",
				    ctlq_msg->cookie.mbx.chnl_opcode);
		err = -EBUSY;
		goto out_unlock;
	}

	if (ctlq_msg->cookie.mbx.chnl_opcode != xn->vc_op) {
		dev_err_ratelimited(&adapter->pdev->dev, "Message opcode does not match transaction opcode (msg: %d) (xn: %d)\n",
				    ctlq_msg->cookie.mbx.chnl_opcode, xn->vc_op);
		xn->reply_sz = 0;
		xn->state = IDPF_VC_XN_COMPLETED_FAILED;
		err = -EINVAL;
		goto out_unlock;
	}

	if (ctlq_msg->cookie.mbx.chnl_retval) {
		xn->reply_sz = 0;
		xn->state = IDPF_VC_XN_COMPLETED_FAILED;
		err = -EINVAL;
		goto out_unlock;
	}

	if (ctlq_msg->data_len) {
		payload = ctlq_msg->ctx.indirect.payload->va;
		payload_size = ctlq_msg->data_len;
	}

	xn->reply_sz = payload_size;
	xn->state = IDPF_VC_XN_COMPLETED_SUCCESS;

	if (xn->reply.iov_base && xn->reply.iov_len && payload_size)
		memcpy(xn->reply.iov_base, payload,
		       min_t(size_t, xn->reply.iov_len, payload_size));

out_unlock:
	idpf_vc_xn_unlock(xn);
	/* we _cannot_ hold lock while calling complete */
	complete(&xn->completed);

	return err;
}

/**
 * idpf_recv_mb_msg - Receive message over mailbox
 * @adapter: Driver specific private structure
 *
 * Will receive control queue message and posts the receive buffer. Returns 0
 * on success and negative on failure.
 */
int idpf_recv_mb_msg(struct idpf_adapter *adapter)
{
	struct idpf_ctlq_msg ctlq_msg;
	struct idpf_dma_mem *dma_mem;
	int post_err, err;
	u16 num_recv;

	while (1) {
		/* This will get <= num_recv messages and output how many
		 * actually received on num_recv.
		 */
		num_recv = 1;
		err = idpf_ctlq_recv(adapter->hw.arq, &num_recv, &ctlq_msg);
		if (err || !num_recv)
			break;

		if (ctlq_msg.data_len) {
			dma_mem = ctlq_msg.ctx.indirect.payload;
		} else {
			dma_mem = NULL;
			num_recv = 0;
		}

		if (ctlq_msg.cookie.mbx.chnl_opcode == VIRTCHNL2_OP_EVENT)
			idpf_recv_event_msg(adapter, &ctlq_msg);
		else
			err = idpf_vc_xn_forward_reply(adapter, &ctlq_msg);

		post_err = idpf_ctlq_post_rx_buffs(&adapter->hw,
						   adapter->hw.arq,
						   &num_recv, &dma_mem);

		/* If post failed clear the only buffer we supplied */
		if (post_err) {
			if (dma_mem)
				dma_free_coherent(&adapter->pdev->dev,
						  dma_mem->size, dma_mem->va,
						  dma_mem->pa);
			break;
		}

		/* virtchnl trying to shutdown, stop cleaning */
		if (err == -ENXIO)
			break;
	}

	return err;
}

struct idpf_chunked_msg_params {
	u32			(*prepare_msg)(const struct idpf_vport *vport,
					       void *buf, const void *pos,
					       u32 num);

	const void		*chunks;
	u32			num_chunks;

	u32			chunk_sz;
	u32			config_sz;

	u32			vc_op;
};

struct idpf_queue_set *idpf_alloc_queue_set(struct idpf_vport *vport, u32 num)
{
	struct idpf_queue_set *qp;

	qp = kzalloc(struct_size(qp, qs, num), GFP_KERNEL);
	if (!qp)
		return NULL;

	qp->vport = vport;
	qp->num = num;

	return qp;
}

/**
 * idpf_send_chunked_msg - send VC message consisting of chunks
 * @vport: virtual port data structure
 * @params: message params
 *
 * Helper function for preparing a message describing queues to be enabled
 * or disabled.
 *
 * Return: the total size of the prepared message.
 */
static int idpf_send_chunked_msg(struct idpf_vport *vport,
				 const struct idpf_chunked_msg_params *params)
{
	struct idpf_vc_xn_params xn_params = {
		.vc_op		= params->vc_op,
		.timeout_ms	= IDPF_VC_XN_DEFAULT_TIMEOUT_MSEC,
	};
	const void *pos = params->chunks;
	u32 num_chunks, num_msgs, buf_sz;
	void *buf __free(kfree) = NULL;
	u32 totqs = params->num_chunks;

	num_chunks = min(IDPF_NUM_CHUNKS_PER_MSG(params->config_sz,
						 params->chunk_sz), totqs);
	num_msgs = DIV_ROUND_UP(totqs, num_chunks);

	buf_sz = params->config_sz + num_chunks * params->chunk_sz;
	buf = kzalloc(buf_sz, GFP_KERNEL);
	if (!buf)
		return -ENOMEM;

	xn_params.send_buf.iov_base = buf;

	for (u32 i = 0; i < num_msgs; i++) {
		ssize_t reply_sz;

		memset(buf, 0, buf_sz);
		xn_params.send_buf.iov_len = buf_sz;

		if (params->prepare_msg(vport, buf, pos, num_chunks) != buf_sz)
			return -EINVAL;

		reply_sz = idpf_vc_xn_exec(vport->adapter, &xn_params);
		if (reply_sz < 0)
			return reply_sz;

		pos += num_chunks * params->chunk_sz;
		totqs -= num_chunks;

		num_chunks = min(num_chunks, totqs);
		buf_sz = params->config_sz + num_chunks * params->chunk_sz;
	}

	return 0;
}

/**
 * idpf_wait_for_marker_event_set - wait for software marker response for
 *				    selected Tx queues
 * @qs: set of the Tx queues
 *
 * Return: 0 success, -errno on failure.
 */
static int idpf_wait_for_marker_event_set(const struct idpf_queue_set *qs)
{
	struct idpf_tx_queue *txq;
	bool markers_rcvd = true;

	for (u32 i = 0; i < qs->num; i++) {
		switch (qs->qs[i].type) {
		case VIRTCHNL2_QUEUE_TYPE_TX:
			txq = qs->qs[i].txq;

			idpf_queue_set(SW_MARKER, txq);
			idpf_wait_for_sw_marker_completion(txq);
			markers_rcvd &= !idpf_queue_has(SW_MARKER, txq);
			break;
		default:
			break;
		}
	}

	if (!markers_rcvd) {
		netdev_warn(qs->vport->netdev,
			    "Failed to receive marker packets\n");
		return -ETIMEDOUT;
	}

	return 0;
}

/**
 * idpf_wait_for_marker_event - wait for software marker response
 * @vport: virtual port data structure
 *
 * Return: 0 success, negative on failure.
 **/
static int idpf_wait_for_marker_event(struct idpf_vport *vport)
{
	struct idpf_queue_set *qs __free(kfree) = NULL;

	qs = idpf_alloc_queue_set(vport, vport->num_txq);
	if (!qs)
		return -ENOMEM;

	for (u32 i = 0; i < qs->num; i++) {
		qs->qs[i].type = VIRTCHNL2_QUEUE_TYPE_TX;
		qs->qs[i].txq = vport->txqs[i];
	}

	return idpf_wait_for_marker_event_set(qs);
}

/**
 * idpf_send_ver_msg - send virtchnl version message
 * @adapter: Driver specific private structure
 *
 * Send virtchnl version message.  Returns 0 on success, negative on failure.
 */
static int idpf_send_ver_msg(struct idpf_adapter *adapter)
{
	struct idpf_vc_xn_params xn_params = {};
	struct virtchnl2_version_info vvi;
	ssize_t reply_sz;
	u32 major, minor;
	int err = 0;

	if (adapter->virt_ver_maj) {
		vvi.major = cpu_to_le32(adapter->virt_ver_maj);
		vvi.minor = cpu_to_le32(adapter->virt_ver_min);
	} else {
		vvi.major = cpu_to_le32(IDPF_VIRTCHNL_VERSION_MAJOR);
		vvi.minor = cpu_to_le32(IDPF_VIRTCHNL_VERSION_MINOR);
	}

	xn_params.vc_op = VIRTCHNL2_OP_VERSION;
	xn_params.send_buf.iov_base = &vvi;
	xn_params.send_buf.iov_len = sizeof(vvi);
	xn_params.recv_buf = xn_params.send_buf;
	xn_params.timeout_ms = IDPF_VC_XN_DEFAULT_TIMEOUT_MSEC;

	reply_sz = idpf_vc_xn_exec(adapter, &xn_params);
	if (reply_sz < 0)
		return reply_sz;
	if (reply_sz < sizeof(vvi))
		return -EIO;

	major = le32_to_cpu(vvi.major);
	minor = le32_to_cpu(vvi.minor);

	if (major > IDPF_VIRTCHNL_VERSION_MAJOR) {
		dev_warn(&adapter->pdev->dev, "Virtchnl major version greater than supported\n");
		return -EINVAL;
	}

	if (major == IDPF_VIRTCHNL_VERSION_MAJOR &&
	    minor > IDPF_VIRTCHNL_VERSION_MINOR)
		dev_warn(&adapter->pdev->dev, "Virtchnl minor version didn't match\n");

	/* If we have a mismatch, resend version to update receiver on what
	 * version we will use.
	 */
	if (!adapter->virt_ver_maj &&
	    major != IDPF_VIRTCHNL_VERSION_MAJOR &&
	    minor != IDPF_VIRTCHNL_VERSION_MINOR)
		err = -EAGAIN;

	adapter->virt_ver_maj = major;
	adapter->virt_ver_min = minor;

	return err;
}

/**
 * idpf_send_get_caps_msg - Send virtchnl get capabilities message
 * @adapter: Driver specific private structure
 *
 * Send virtchl get capabilities message. Returns 0 on success, negative on
 * failure.
 */
static int idpf_send_get_caps_msg(struct idpf_adapter *adapter)
{
	struct virtchnl2_get_capabilities caps = {};
	struct idpf_vc_xn_params xn_params = {};
	ssize_t reply_sz;

	caps.csum_caps =
		cpu_to_le32(VIRTCHNL2_CAP_TX_CSUM_L3_IPV4	|
			    VIRTCHNL2_CAP_TX_CSUM_L4_IPV4_TCP	|
			    VIRTCHNL2_CAP_TX_CSUM_L4_IPV4_UDP	|
			    VIRTCHNL2_CAP_TX_CSUM_L4_IPV4_SCTP	|
			    VIRTCHNL2_CAP_TX_CSUM_L4_IPV6_TCP	|
			    VIRTCHNL2_CAP_TX_CSUM_L4_IPV6_UDP	|
			    VIRTCHNL2_CAP_TX_CSUM_L4_IPV6_SCTP	|
			    VIRTCHNL2_CAP_RX_CSUM_L3_IPV4	|
			    VIRTCHNL2_CAP_RX_CSUM_L4_IPV4_TCP	|
			    VIRTCHNL2_CAP_RX_CSUM_L4_IPV4_UDP	|
			    VIRTCHNL2_CAP_RX_CSUM_L4_IPV4_SCTP	|
			    VIRTCHNL2_CAP_RX_CSUM_L4_IPV6_TCP	|
			    VIRTCHNL2_CAP_RX_CSUM_L4_IPV6_UDP	|
			    VIRTCHNL2_CAP_RX_CSUM_L4_IPV6_SCTP	|
			    VIRTCHNL2_CAP_TX_CSUM_L3_SINGLE_TUNNEL |
			    VIRTCHNL2_CAP_RX_CSUM_L3_SINGLE_TUNNEL |
			    VIRTCHNL2_CAP_TX_CSUM_L4_SINGLE_TUNNEL |
			    VIRTCHNL2_CAP_RX_CSUM_L4_SINGLE_TUNNEL |
			    VIRTCHNL2_CAP_RX_CSUM_GENERIC);

	caps.seg_caps =
		cpu_to_le32(VIRTCHNL2_CAP_SEG_IPV4_TCP		|
			    VIRTCHNL2_CAP_SEG_IPV4_UDP		|
			    VIRTCHNL2_CAP_SEG_IPV4_SCTP		|
			    VIRTCHNL2_CAP_SEG_IPV6_TCP		|
			    VIRTCHNL2_CAP_SEG_IPV6_UDP		|
			    VIRTCHNL2_CAP_SEG_IPV6_SCTP		|
			    VIRTCHNL2_CAP_SEG_TX_SINGLE_TUNNEL);

	caps.rss_caps =
		cpu_to_le64(VIRTCHNL2_FLOW_IPV4_TCP		|
			    VIRTCHNL2_FLOW_IPV4_UDP		|
			    VIRTCHNL2_FLOW_IPV4_SCTP		|
			    VIRTCHNL2_FLOW_IPV4_OTHER		|
			    VIRTCHNL2_FLOW_IPV6_TCP		|
			    VIRTCHNL2_FLOW_IPV6_UDP		|
			    VIRTCHNL2_FLOW_IPV6_SCTP		|
			    VIRTCHNL2_FLOW_IPV6_OTHER);

	caps.hsplit_caps =
		cpu_to_le32(VIRTCHNL2_CAP_RX_HSPLIT_AT_L4V4	|
			    VIRTCHNL2_CAP_RX_HSPLIT_AT_L4V6);

	caps.rsc_caps =
		cpu_to_le32(VIRTCHNL2_CAP_RSC_IPV4_TCP		|
			    VIRTCHNL2_CAP_RSC_IPV6_TCP);

	caps.other_caps =
		cpu_to_le64(VIRTCHNL2_CAP_SRIOV			|
			    VIRTCHNL2_CAP_RDMA                  |
			    VIRTCHNL2_CAP_LAN_MEMORY_REGIONS	|
			    VIRTCHNL2_CAP_MACFILTER		|
			    VIRTCHNL2_CAP_SPLITQ_QSCHED		|
			    VIRTCHNL2_CAP_PROMISC		|
			    VIRTCHNL2_CAP_LOOPBACK		|
			    VIRTCHNL2_CAP_PTP);

	xn_params.vc_op = VIRTCHNL2_OP_GET_CAPS;
	xn_params.send_buf.iov_base = &caps;
	xn_params.send_buf.iov_len = sizeof(caps);
	xn_params.recv_buf.iov_base = &adapter->caps;
	xn_params.recv_buf.iov_len = sizeof(adapter->caps);
	xn_params.timeout_ms = IDPF_VC_XN_DEFAULT_TIMEOUT_MSEC;

	reply_sz = idpf_vc_xn_exec(adapter, &xn_params);
	if (reply_sz < 0)
		return reply_sz;
	if (reply_sz < sizeof(adapter->caps))
		return -EIO;

	return 0;
}

/**
 * idpf_send_get_lan_memory_regions - Send virtchnl get LAN memory regions msg
 * @adapter: Driver specific private struct
 *
 * Return: 0 on success or error code on failure.
 */
static int idpf_send_get_lan_memory_regions(struct idpf_adapter *adapter)
{
	struct virtchnl2_get_lan_memory_regions *rcvd_regions __free(kfree);
	struct idpf_vc_xn_params xn_params = {
		.vc_op = VIRTCHNL2_OP_GET_LAN_MEMORY_REGIONS,
		.recv_buf.iov_len = IDPF_CTLQ_MAX_BUF_LEN,
		.timeout_ms = IDPF_VC_XN_DEFAULT_TIMEOUT_MSEC,
	};
	int num_regions, size;
	struct idpf_hw *hw;
	ssize_t reply_sz;
	int err = 0;

	rcvd_regions = kzalloc(IDPF_CTLQ_MAX_BUF_LEN, GFP_KERNEL);
	if (!rcvd_regions)
		return -ENOMEM;

	xn_params.recv_buf.iov_base = rcvd_regions;
	reply_sz = idpf_vc_xn_exec(adapter, &xn_params);
	if (reply_sz < 0)
		return reply_sz;

	num_regions = le16_to_cpu(rcvd_regions->num_memory_regions);
	size = struct_size(rcvd_regions, mem_reg, num_regions);
	if (reply_sz < size)
		return -EIO;

	if (size > IDPF_CTLQ_MAX_BUF_LEN)
		return -EINVAL;

	hw = &adapter->hw;
	hw->lan_regs = kcalloc(num_regions, sizeof(*hw->lan_regs), GFP_KERNEL);
	if (!hw->lan_regs)
		return -ENOMEM;

	for (int i = 0; i < num_regions; i++) {
		hw->lan_regs[i].addr_len =
			le64_to_cpu(rcvd_regions->mem_reg[i].size);
		hw->lan_regs[i].addr_start =
			le64_to_cpu(rcvd_regions->mem_reg[i].start_offset);
	}
	hw->num_lan_regs = num_regions;

	return err;
}

/**
 * idpf_calc_remaining_mmio_regs - calculate MMIO regions outside mbx and rstat
 * @adapter: Driver specific private structure
 *
 * Called when idpf_send_get_lan_memory_regions is not supported. This will
 * calculate the offsets and sizes for the regions before, in between, and
 * after the mailbox and rstat MMIO mappings.
 *
 * Return: 0 on success or error code on failure.
 */
static int idpf_calc_remaining_mmio_regs(struct idpf_adapter *adapter)
{
	struct resource *rstat_reg = &adapter->dev_ops.static_reg_info[1];
	struct resource *mbx_reg = &adapter->dev_ops.static_reg_info[0];
	struct idpf_hw *hw = &adapter->hw;

	hw->num_lan_regs = IDPF_MMIO_MAP_FALLBACK_MAX_REMAINING;
	hw->lan_regs = kcalloc(hw->num_lan_regs, sizeof(*hw->lan_regs),
			       GFP_KERNEL);
	if (!hw->lan_regs)
		return -ENOMEM;

	/* Region preceding mailbox */
	hw->lan_regs[0].addr_start = 0;
	hw->lan_regs[0].addr_len = mbx_reg->start;
	/* Region between mailbox and rstat */
	hw->lan_regs[1].addr_start = mbx_reg->end + 1;
	hw->lan_regs[1].addr_len = rstat_reg->start -
					hw->lan_regs[1].addr_start;
	/* Region after rstat */
	hw->lan_regs[2].addr_start = rstat_reg->end + 1;
	hw->lan_regs[2].addr_len = pci_resource_len(adapter->pdev, 0) -
					hw->lan_regs[2].addr_start;

	return 0;
}

/**
 * idpf_map_lan_mmio_regs - map remaining LAN BAR regions
 * @adapter: Driver specific private structure
 *
 * Return: 0 on success or error code on failure.
 */
static int idpf_map_lan_mmio_regs(struct idpf_adapter *adapter)
{
	struct pci_dev *pdev = adapter->pdev;
	struct idpf_hw *hw = &adapter->hw;
	resource_size_t res_start;

	res_start = pci_resource_start(pdev, 0);

	for (int i = 0; i < hw->num_lan_regs; i++) {
		resource_size_t start;
		long len;

		len = hw->lan_regs[i].addr_len;
		if (!len)
			continue;
		start = hw->lan_regs[i].addr_start + res_start;

		hw->lan_regs[i].vaddr = devm_ioremap(&pdev->dev, start, len);
		if (!hw->lan_regs[i].vaddr) {
			pci_err(pdev, "failed to allocate BAR0 region\n");
			return -ENOMEM;
		}
	}

	return 0;
}

/**
 * idpf_add_del_fsteer_filters - Send virtchnl add/del Flow Steering message
 * @adapter: adapter info struct
 * @rule: Flow steering rule to add/delete
 * @opcode: VIRTCHNL2_OP_ADD_FLOW_RULE to add filter, or
 *          VIRTCHNL2_OP_DEL_FLOW_RULE to delete. All other values are invalid.
 *
 * Send ADD/DELETE flow steering virtchnl message and receive the result.
 *
 * Return: 0 on success, negative on failure.
 */
int idpf_add_del_fsteer_filters(struct idpf_adapter *adapter,
				struct virtchnl2_flow_rule_add_del *rule,
				enum virtchnl2_op opcode)
{
	int rule_count = le32_to_cpu(rule->count);
	struct idpf_vc_xn_params xn_params = {};
	ssize_t reply_sz;

	if (opcode != VIRTCHNL2_OP_ADD_FLOW_RULE &&
	    opcode != VIRTCHNL2_OP_DEL_FLOW_RULE)
		return -EINVAL;

	xn_params.vc_op = opcode;
	xn_params.timeout_ms = IDPF_VC_XN_DEFAULT_TIMEOUT_MSEC;
	xn_params.async = false;
	xn_params.send_buf.iov_base = rule;
	xn_params.send_buf.iov_len = struct_size(rule, rule_info, rule_count);
	xn_params.recv_buf.iov_base = rule;
	xn_params.recv_buf.iov_len = struct_size(rule, rule_info, rule_count);

	reply_sz = idpf_vc_xn_exec(adapter, &xn_params);
	return reply_sz < 0 ? reply_sz : 0;
}

/**
 * idpf_vport_alloc_max_qs - Allocate max queues for a vport
 * @adapter: Driver specific private structure
 * @max_q: vport max queue structure
 */
int idpf_vport_alloc_max_qs(struct idpf_adapter *adapter,
			    struct idpf_vport_max_q *max_q)
{
	struct idpf_avail_queue_info *avail_queues = &adapter->avail_queues;
	struct virtchnl2_get_capabilities *caps = &adapter->caps;
	u16 default_vports = idpf_get_default_vports(adapter);
	u32 max_rx_q, max_tx_q, max_buf_q, max_compl_q;

	mutex_lock(&adapter->queue_lock);

	/* Caps are device-wide. Give each vport an equal piece */
	max_rx_q = le16_to_cpu(caps->max_rx_q) / default_vports;
	max_tx_q = le16_to_cpu(caps->max_tx_q) / default_vports;
	max_buf_q = le16_to_cpu(caps->max_rx_bufq) / default_vports;
	max_compl_q = le16_to_cpu(caps->max_tx_complq) / default_vports;

	if (adapter->num_alloc_vports >= default_vports) {
		max_rx_q = IDPF_MIN_Q;
		max_tx_q = IDPF_MIN_Q;
	}

	/*
	 * Harmonize the numbers. The current implementation always creates
	 * `IDPF_MAX_BUFQS_PER_RXQ_GRP` buffer queues for each Rx queue and
	 * one completion queue for each Tx queue for best performance.
	 * If less buffer or completion queues is available, cap the number
	 * of the corresponding Rx/Tx queues.
	 */
	max_rx_q = min(max_rx_q, max_buf_q / IDPF_MAX_BUFQS_PER_RXQ_GRP);
	max_tx_q = min(max_tx_q, max_compl_q);

	max_q->max_rxq = max_rx_q;
	max_q->max_txq = max_tx_q;
	max_q->max_bufq = max_rx_q * IDPF_MAX_BUFQS_PER_RXQ_GRP;
	max_q->max_complq = max_tx_q;

	if (avail_queues->avail_rxq < max_q->max_rxq ||
	    avail_queues->avail_txq < max_q->max_txq ||
	    avail_queues->avail_bufq < max_q->max_bufq ||
	    avail_queues->avail_complq < max_q->max_complq) {
		mutex_unlock(&adapter->queue_lock);

		return -EINVAL;
	}

	avail_queues->avail_rxq -= max_q->max_rxq;
	avail_queues->avail_txq -= max_q->max_txq;
	avail_queues->avail_bufq -= max_q->max_bufq;
	avail_queues->avail_complq -= max_q->max_complq;

	mutex_unlock(&adapter->queue_lock);

	return 0;
}

/**
 * idpf_vport_dealloc_max_qs - Deallocate max queues of a vport
 * @adapter: Driver specific private structure
 * @max_q: vport max queue structure
 */
void idpf_vport_dealloc_max_qs(struct idpf_adapter *adapter,
			       struct idpf_vport_max_q *max_q)
{
	struct idpf_avail_queue_info *avail_queues;

	mutex_lock(&adapter->queue_lock);
	avail_queues = &adapter->avail_queues;

	avail_queues->avail_rxq += max_q->max_rxq;
	avail_queues->avail_txq += max_q->max_txq;
	avail_queues->avail_bufq += max_q->max_bufq;
	avail_queues->avail_complq += max_q->max_complq;

	mutex_unlock(&adapter->queue_lock);
}

/**
 * idpf_init_avail_queues - Initialize available queues on the device
 * @adapter: Driver specific private structure
 */
static void idpf_init_avail_queues(struct idpf_adapter *adapter)
{
	struct idpf_avail_queue_info *avail_queues = &adapter->avail_queues;
	struct virtchnl2_get_capabilities *caps = &adapter->caps;

	avail_queues->avail_rxq = le16_to_cpu(caps->max_rx_q);
	avail_queues->avail_txq = le16_to_cpu(caps->max_tx_q);
	avail_queues->avail_bufq = le16_to_cpu(caps->max_rx_bufq);
	avail_queues->avail_complq = le16_to_cpu(caps->max_tx_complq);
}

/**
 * idpf_get_reg_intr_vecs - Get vector queue register offset
 * @vport: virtual port structure
 * @reg_vals: Register offsets to store in
 *
 * Returns number of registers that got populated
 */
int idpf_get_reg_intr_vecs(struct idpf_vport *vport,
			   struct idpf_vec_regs *reg_vals)
{
	struct virtchnl2_vector_chunks *chunks;
	struct idpf_vec_regs reg_val;
	u16 num_vchunks, num_vec;
	int num_regs = 0, i, j;

	chunks = &vport->adapter->req_vec_chunks->vchunks;
	num_vchunks = le16_to_cpu(chunks->num_vchunks);

	for (j = 0; j < num_vchunks; j++) {
		struct virtchnl2_vector_chunk *chunk;
		u32 dynctl_reg_spacing;
		u32 itrn_reg_spacing;

		chunk = &chunks->vchunks[j];
		num_vec = le16_to_cpu(chunk->num_vectors);
		reg_val.dyn_ctl_reg = le32_to_cpu(chunk->dynctl_reg_start);
		reg_val.itrn_reg = le32_to_cpu(chunk->itrn_reg_start);
		reg_val.itrn_index_spacing = le32_to_cpu(chunk->itrn_index_spacing);

		dynctl_reg_spacing = le32_to_cpu(chunk->dynctl_reg_spacing);
		itrn_reg_spacing = le32_to_cpu(chunk->itrn_reg_spacing);

		for (i = 0; i < num_vec; i++) {
			reg_vals[num_regs].dyn_ctl_reg = reg_val.dyn_ctl_reg;
			reg_vals[num_regs].itrn_reg = reg_val.itrn_reg;
			reg_vals[num_regs].itrn_index_spacing =
						reg_val.itrn_index_spacing;

			reg_val.dyn_ctl_reg += dynctl_reg_spacing;
			reg_val.itrn_reg += itrn_reg_spacing;
			num_regs++;
		}
	}

	return num_regs;
}

/**
 * idpf_vport_get_q_reg - Get the queue registers for the vport
 * @reg_vals: register values needing to be set
 * @num_regs: amount we expect to fill
 * @q_type: queue model
 * @chunks: queue regs received over mailbox
 *
 * This function parses the queue register offsets from the queue register
 * chunk information, with a specific queue type and stores it into the array
 * passed as an argument. It returns the actual number of queue registers that
 * are filled.
 */
static int idpf_vport_get_q_reg(u32 *reg_vals, int num_regs, u32 q_type,
				struct virtchnl2_queue_reg_chunks *chunks)
{
	u16 num_chunks = le16_to_cpu(chunks->num_chunks);
	int reg_filled = 0, i;
	u32 reg_val;

	while (num_chunks--) {
		struct virtchnl2_queue_reg_chunk *chunk;
		u16 num_q;

		chunk = &chunks->chunks[num_chunks];
		if (le32_to_cpu(chunk->type) != q_type)
			continue;

		num_q = le32_to_cpu(chunk->num_queues);
		reg_val = le64_to_cpu(chunk->qtail_reg_start);
		for (i = 0; i < num_q && reg_filled < num_regs ; i++) {
			reg_vals[reg_filled++] = reg_val;
			reg_val += le32_to_cpu(chunk->qtail_reg_spacing);
		}
	}

	return reg_filled;
}

/**
 * __idpf_queue_reg_init - initialize queue registers
 * @vport: virtual port structure
 * @reg_vals: registers we are initializing
 * @num_regs: how many registers there are in total
 * @q_type: queue model
 *
 * Return number of queues that are initialized
 */
static int __idpf_queue_reg_init(struct idpf_vport *vport, u32 *reg_vals,
				 int num_regs, u32 q_type)
{
	struct idpf_adapter *adapter = vport->adapter;
	int i, j, k = 0;

	switch (q_type) {
	case VIRTCHNL2_QUEUE_TYPE_TX:
		for (i = 0; i < vport->num_txq_grp; i++) {
			struct idpf_txq_group *tx_qgrp = &vport->txq_grps[i];

			for (j = 0; j < tx_qgrp->num_txq && k < num_regs; j++, k++)
				tx_qgrp->txqs[j]->tail =
					idpf_get_reg_addr(adapter, reg_vals[k]);
		}
		break;
	case VIRTCHNL2_QUEUE_TYPE_RX:
		for (i = 0; i < vport->num_rxq_grp; i++) {
			struct idpf_rxq_group *rx_qgrp = &vport->rxq_grps[i];
			u16 num_rxq = rx_qgrp->singleq.num_rxq;

			for (j = 0; j < num_rxq && k < num_regs; j++, k++) {
				struct idpf_rx_queue *q;

				q = rx_qgrp->singleq.rxqs[j];
				q->tail = idpf_get_reg_addr(adapter,
							    reg_vals[k]);
			}
		}
		break;
	case VIRTCHNL2_QUEUE_TYPE_RX_BUFFER:
		for (i = 0; i < vport->num_rxq_grp; i++) {
			struct idpf_rxq_group *rx_qgrp = &vport->rxq_grps[i];
			u8 num_bufqs = vport->num_bufqs_per_qgrp;

			for (j = 0; j < num_bufqs && k < num_regs; j++, k++) {
				struct idpf_buf_queue *q;

				q = &rx_qgrp->splitq.bufq_sets[j].bufq;
				q->tail = idpf_get_reg_addr(adapter,
							    reg_vals[k]);
			}
		}
		break;
	default:
		break;
	}

	return k;
}

/**
 * idpf_queue_reg_init - initialize queue registers
 * @vport: virtual port structure
 *
 * Return 0 on success, negative on failure
 */
int idpf_queue_reg_init(struct idpf_vport *vport)
{
	struct virtchnl2_create_vport *vport_params;
	struct virtchnl2_queue_reg_chunks *chunks;
	struct idpf_vport_config *vport_config;
	u16 vport_idx = vport->idx;
	int num_regs, ret = 0;
	u32 *reg_vals;

	/* We may never deal with more than 256 same type of queues */
	reg_vals = kzalloc(sizeof(void *) * IDPF_LARGE_MAX_Q, GFP_KERNEL);
	if (!reg_vals)
		return -ENOMEM;

	vport_config = vport->adapter->vport_config[vport_idx];
	if (vport_config->req_qs_chunks) {
		struct virtchnl2_add_queues *vc_aq =
		  (struct virtchnl2_add_queues *)vport_config->req_qs_chunks;
		chunks = &vc_aq->chunks;
	} else {
		vport_params = vport->adapter->vport_params_recvd[vport_idx];
		chunks = &vport_params->chunks;
	}

	/* Initialize Tx queue tail register address */
	num_regs = idpf_vport_get_q_reg(reg_vals, IDPF_LARGE_MAX_Q,
					VIRTCHNL2_QUEUE_TYPE_TX,
					chunks);
	if (num_regs < vport->num_txq) {
		ret = -EINVAL;
		goto free_reg_vals;
	}

	num_regs = __idpf_queue_reg_init(vport, reg_vals, num_regs,
					 VIRTCHNL2_QUEUE_TYPE_TX);
	if (num_regs < vport->num_txq) {
		ret = -EINVAL;
		goto free_reg_vals;
	}

	/* Initialize Rx/buffer queue tail register address based on Rx queue
	 * model
	 */
	if (idpf_is_queue_model_split(vport->rxq_model)) {
		num_regs = idpf_vport_get_q_reg(reg_vals, IDPF_LARGE_MAX_Q,
						VIRTCHNL2_QUEUE_TYPE_RX_BUFFER,
						chunks);
		if (num_regs < vport->num_bufq) {
			ret = -EINVAL;
			goto free_reg_vals;
		}

		num_regs = __idpf_queue_reg_init(vport, reg_vals, num_regs,
						 VIRTCHNL2_QUEUE_TYPE_RX_BUFFER);
		if (num_regs < vport->num_bufq) {
			ret = -EINVAL;
			goto free_reg_vals;
		}
	} else {
		num_regs = idpf_vport_get_q_reg(reg_vals, IDPF_LARGE_MAX_Q,
						VIRTCHNL2_QUEUE_TYPE_RX,
						chunks);
		if (num_regs < vport->num_rxq) {
			ret = -EINVAL;
			goto free_reg_vals;
		}

		num_regs = __idpf_queue_reg_init(vport, reg_vals, num_regs,
						 VIRTCHNL2_QUEUE_TYPE_RX);
		if (num_regs < vport->num_rxq) {
			ret = -EINVAL;
			goto free_reg_vals;
		}
	}

free_reg_vals:
	kfree(reg_vals);

	return ret;
}

/**
 * idpf_send_create_vport_msg - Send virtchnl create vport message
 * @adapter: Driver specific private structure
 * @max_q: vport max queue info
 *
 * send virtchnl creae vport message
 *
 * Returns 0 on success, negative on failure
 */
int idpf_send_create_vport_msg(struct idpf_adapter *adapter,
			       struct idpf_vport_max_q *max_q)
{
	struct virtchnl2_create_vport *vport_msg;
	struct idpf_vc_xn_params xn_params = {};
	u16 idx = adapter->next_vport;
	int err, buf_size;
	ssize_t reply_sz;

	buf_size = sizeof(struct virtchnl2_create_vport);
	if (!adapter->vport_params_reqd[idx]) {
		adapter->vport_params_reqd[idx] = kzalloc(buf_size,
							  GFP_KERNEL);
		if (!adapter->vport_params_reqd[idx])
			return -ENOMEM;
	}

	vport_msg = adapter->vport_params_reqd[idx];
	vport_msg->vport_type = cpu_to_le16(VIRTCHNL2_VPORT_TYPE_DEFAULT);
	vport_msg->vport_index = cpu_to_le16(idx);

	if (adapter->req_tx_splitq || !IS_ENABLED(CONFIG_IDPF_SINGLEQ))
		vport_msg->txq_model = cpu_to_le16(VIRTCHNL2_QUEUE_MODEL_SPLIT);
	else
		vport_msg->txq_model = cpu_to_le16(VIRTCHNL2_QUEUE_MODEL_SINGLE);

	if (adapter->req_rx_splitq || !IS_ENABLED(CONFIG_IDPF_SINGLEQ))
		vport_msg->rxq_model = cpu_to_le16(VIRTCHNL2_QUEUE_MODEL_SPLIT);
	else
		vport_msg->rxq_model = cpu_to_le16(VIRTCHNL2_QUEUE_MODEL_SINGLE);

	err = idpf_vport_calc_total_qs(adapter, idx, vport_msg, max_q);
	if (err) {
		dev_err(&adapter->pdev->dev, "Enough queues are not available");

		return err;
	}

	if (!adapter->vport_params_recvd[idx]) {
		adapter->vport_params_recvd[idx] = kzalloc(IDPF_CTLQ_MAX_BUF_LEN,
							   GFP_KERNEL);
		if (!adapter->vport_params_recvd[idx]) {
			err = -ENOMEM;
			goto free_vport_params;
		}
	}

	xn_params.vc_op = VIRTCHNL2_OP_CREATE_VPORT;
	xn_params.send_buf.iov_base = vport_msg;
	xn_params.send_buf.iov_len = buf_size;
	xn_params.recv_buf.iov_base = adapter->vport_params_recvd[idx];
	xn_params.recv_buf.iov_len = IDPF_CTLQ_MAX_BUF_LEN;
	xn_params.timeout_ms = IDPF_VC_XN_DEFAULT_TIMEOUT_MSEC;
	reply_sz = idpf_vc_xn_exec(adapter, &xn_params);
	if (reply_sz < 0) {
		err = reply_sz;
		goto free_vport_params;
	}

	return 0;

free_vport_params:
	kfree(adapter->vport_params_recvd[idx]);
	adapter->vport_params_recvd[idx] = NULL;
	kfree(adapter->vport_params_reqd[idx]);
	adapter->vport_params_reqd[idx] = NULL;

	return err;
}

/**
 * idpf_check_supported_desc_ids - Verify we have required descriptor support
 * @vport: virtual port structure
 *
 * Return 0 on success, error on failure
 */
int idpf_check_supported_desc_ids(struct idpf_vport *vport)
{
	struct idpf_adapter *adapter = vport->adapter;
	struct virtchnl2_create_vport *vport_msg;
	u64 rx_desc_ids, tx_desc_ids;

	vport_msg = adapter->vport_params_recvd[vport->idx];

	if (!IS_ENABLED(CONFIG_IDPF_SINGLEQ) &&
	    (vport_msg->rxq_model == VIRTCHNL2_QUEUE_MODEL_SINGLE ||
	     vport_msg->txq_model == VIRTCHNL2_QUEUE_MODEL_SINGLE)) {
		pci_err(adapter->pdev, "singleq mode requested, but not compiled-in\n");
		return -EOPNOTSUPP;
	}

	rx_desc_ids = le64_to_cpu(vport_msg->rx_desc_ids);
	tx_desc_ids = le64_to_cpu(vport_msg->tx_desc_ids);

	if (idpf_is_queue_model_split(vport->rxq_model)) {
		if (!(rx_desc_ids & VIRTCHNL2_RXDID_2_FLEX_SPLITQ_M)) {
			dev_info(&adapter->pdev->dev, "Minimum RX descriptor support not provided, using the default\n");
			vport_msg->rx_desc_ids = cpu_to_le64(VIRTCHNL2_RXDID_2_FLEX_SPLITQ_M);
		}
	} else {
		if (!(rx_desc_ids & VIRTCHNL2_RXDID_2_FLEX_SQ_NIC_M))
			vport->base_rxd = true;
	}

	if (!idpf_is_queue_model_split(vport->txq_model))
		return 0;

	if ((tx_desc_ids & MIN_SUPPORT_TXDID) != MIN_SUPPORT_TXDID) {
		dev_info(&adapter->pdev->dev, "Minimum TX descriptor support not provided, using the default\n");
		vport_msg->tx_desc_ids = cpu_to_le64(MIN_SUPPORT_TXDID);
	}

	return 0;
}

/**
 * idpf_send_destroy_vport_msg - Send virtchnl destroy vport message
 * @vport: virtual port data structure
 *
 * Send virtchnl destroy vport message.  Returns 0 on success, negative on
 * failure.
 */
int idpf_send_destroy_vport_msg(struct idpf_vport *vport)
{
	struct idpf_vc_xn_params xn_params = {};
	struct virtchnl2_vport v_id;
	ssize_t reply_sz;

	v_id.vport_id = cpu_to_le32(vport->vport_id);

	xn_params.vc_op = VIRTCHNL2_OP_DESTROY_VPORT;
	xn_params.send_buf.iov_base = &v_id;
	xn_params.send_buf.iov_len = sizeof(v_id);
	xn_params.timeout_ms = IDPF_VC_XN_DEFAULT_TIMEOUT_MSEC;
	reply_sz = idpf_vc_xn_exec(vport->adapter, &xn_params);

	return reply_sz < 0 ? reply_sz : 0;
}

/**
 * idpf_send_enable_vport_msg - Send virtchnl enable vport message
 * @vport: virtual port data structure
 *
 * Send enable vport virtchnl message.  Returns 0 on success, negative on
 * failure.
 */
int idpf_send_enable_vport_msg(struct idpf_vport *vport)
{
	struct idpf_vc_xn_params xn_params = {};
	struct virtchnl2_vport v_id;
	ssize_t reply_sz;

	v_id.vport_id = cpu_to_le32(vport->vport_id);

	xn_params.vc_op = VIRTCHNL2_OP_ENABLE_VPORT;
	xn_params.send_buf.iov_base = &v_id;
	xn_params.send_buf.iov_len = sizeof(v_id);
	xn_params.timeout_ms = IDPF_VC_XN_DEFAULT_TIMEOUT_MSEC;
	reply_sz = idpf_vc_xn_exec(vport->adapter, &xn_params);

	return reply_sz < 0 ? reply_sz : 0;
}

/**
 * idpf_send_disable_vport_msg - Send virtchnl disable vport message
 * @vport: virtual port data structure
 *
 * Send disable vport virtchnl message.  Returns 0 on success, negative on
 * failure.
 */
int idpf_send_disable_vport_msg(struct idpf_vport *vport)
{
	struct idpf_vc_xn_params xn_params = {};
	struct virtchnl2_vport v_id;
	ssize_t reply_sz;

	v_id.vport_id = cpu_to_le32(vport->vport_id);

	xn_params.vc_op = VIRTCHNL2_OP_DISABLE_VPORT;
	xn_params.send_buf.iov_base = &v_id;
	xn_params.send_buf.iov_len = sizeof(v_id);
	xn_params.timeout_ms = IDPF_VC_XN_DEFAULT_TIMEOUT_MSEC;
	reply_sz = idpf_vc_xn_exec(vport->adapter, &xn_params);

	return reply_sz < 0 ? reply_sz : 0;
}

/**
 * idpf_fill_txq_config_chunk - fill chunk describing the Tx queue
 * @vport: virtual port data structure
 * @q: Tx queue to be inserted into VC chunk
 * @qi: pointer to the buffer containing the VC chunk
 */
static void idpf_fill_txq_config_chunk(const struct idpf_vport *vport,
				       const struct idpf_tx_queue *q,
				       struct virtchnl2_txq_info *qi)
{
	u32 val;

	qi->queue_id = cpu_to_le32(q->q_id);
	qi->model = cpu_to_le16(vport->txq_model);
	qi->type = cpu_to_le32(VIRTCHNL2_QUEUE_TYPE_TX);
	qi->ring_len = cpu_to_le16(q->desc_count);
	qi->dma_ring_addr = cpu_to_le64(q->dma);
	qi->relative_queue_id = cpu_to_le16(q->rel_q_id);

	if (!idpf_is_queue_model_split(vport->txq_model)) {
		qi->sched_mode = cpu_to_le16(VIRTCHNL2_TXQ_SCHED_MODE_QUEUE);
		return;
	}

	if (idpf_queue_has(XDP, q))
		val = q->complq->q_id;
	else
		val = q->txq_grp->complq->q_id;

	qi->tx_compl_queue_id = cpu_to_le16(val);

	if (idpf_queue_has(FLOW_SCH_EN, q))
		val = VIRTCHNL2_TXQ_SCHED_MODE_FLOW;
	else
		val = VIRTCHNL2_TXQ_SCHED_MODE_QUEUE;

	qi->sched_mode = cpu_to_le16(val);
}

/**
 * idpf_fill_complq_config_chunk - fill chunk describing the completion queue
 * @vport: virtual port data structure
 * @q: completion queue to be inserted into VC chunk
 * @qi: pointer to the buffer containing the VC chunk
 */
static void idpf_fill_complq_config_chunk(const struct idpf_vport *vport,
					  const struct idpf_compl_queue *q,
					  struct virtchnl2_txq_info *qi)
{
	u32 val;

	qi->queue_id = cpu_to_le32(q->q_id);
	qi->model = cpu_to_le16(vport->txq_model);
	qi->type = cpu_to_le32(VIRTCHNL2_QUEUE_TYPE_TX_COMPLETION);
	qi->ring_len = cpu_to_le16(q->desc_count);
	qi->dma_ring_addr = cpu_to_le64(q->dma);

	if (idpf_queue_has(FLOW_SCH_EN, q))
		val = VIRTCHNL2_TXQ_SCHED_MODE_FLOW;
	else
		val = VIRTCHNL2_TXQ_SCHED_MODE_QUEUE;

	qi->sched_mode = cpu_to_le16(val);
}

/**
 * idpf_prepare_cfg_txqs_msg - prepare message to configure selected Tx queues
 * @vport: virtual port data structure
 * @buf: buffer containing the message
 * @pos: pointer to the first chunk describing the tx queue
 * @num_chunks: number of chunks in the message
 *
 * Helper function for preparing the message describing configuration of
 * Tx queues.
 *
 * Return: the total size of the prepared message.
 */
static u32 idpf_prepare_cfg_txqs_msg(const struct idpf_vport *vport,
				     void *buf, const void *pos,
				     u32 num_chunks)
{
	struct virtchnl2_config_tx_queues *ctq = buf;

	ctq->vport_id = cpu_to_le32(vport->vport_id);
	ctq->num_qinfo = cpu_to_le16(num_chunks);
	memcpy(ctq->qinfo, pos, num_chunks * sizeof(*ctq->qinfo));

	return struct_size(ctq, qinfo, num_chunks);
}

/**
 * idpf_send_config_tx_queue_set_msg - send virtchnl config Tx queues
 *				       message for selected queues
 * @qs: set of the Tx queues to configure
 *
 * Send config queues virtchnl message for queues contained in the @qs array.
 * The @qs array can contain Tx queues (or completion queues) only.
 *
 * Return: 0 on success, -errno on failure.
 */
static int idpf_send_config_tx_queue_set_msg(const struct idpf_queue_set *qs)
{
	struct virtchnl2_txq_info *qi __free(kfree) = NULL;
	struct idpf_chunked_msg_params params = {
		.vc_op		= VIRTCHNL2_OP_CONFIG_TX_QUEUES,
		.prepare_msg	= idpf_prepare_cfg_txqs_msg,
		.config_sz	= sizeof(struct virtchnl2_config_tx_queues),
		.chunk_sz	= sizeof(*qi),
	};

	qi = kcalloc(qs->num, sizeof(*qi), GFP_KERNEL);
	if (!qi)
		return -ENOMEM;

	params.chunks = qi;

	for (u32 i = 0; i < qs->num; i++) {
		if (qs->qs[i].type == VIRTCHNL2_QUEUE_TYPE_TX)
			idpf_fill_txq_config_chunk(qs->vport, qs->qs[i].txq,
						   &qi[params.num_chunks++]);
		else if (qs->qs[i].type == VIRTCHNL2_QUEUE_TYPE_TX_COMPLETION)
			idpf_fill_complq_config_chunk(qs->vport,
						      qs->qs[i].complq,
						      &qi[params.num_chunks++]);
	}

	return idpf_send_chunked_msg(qs->vport, &params);
}

/**
 * idpf_send_config_tx_queues_msg - send virtchnl config Tx queues message
 * @vport: virtual port data structure
 *
 * Return: 0 on success, -errno on failure.
 */
static int idpf_send_config_tx_queues_msg(struct idpf_vport *vport)
{
	struct idpf_queue_set *qs __free(kfree) = NULL;
	u32 totqs = vport->num_txq + vport->num_complq;
	u32 k = 0;

	qs = idpf_alloc_queue_set(vport, totqs);
	if (!qs)
		return -ENOMEM;

	/* Populate the queue info buffer with all queue context info */
	for (u32 i = 0; i < vport->num_txq_grp; i++) {
		const struct idpf_txq_group *tx_qgrp = &vport->txq_grps[i];

		for (u32 j = 0; j < tx_qgrp->num_txq; j++) {
			qs->qs[k].type = VIRTCHNL2_QUEUE_TYPE_TX;
			qs->qs[k++].txq = tx_qgrp->txqs[j];
		}

		if (idpf_is_queue_model_split(vport->txq_model)) {
			qs->qs[k].type = VIRTCHNL2_QUEUE_TYPE_TX_COMPLETION;
			qs->qs[k++].complq = tx_qgrp->complq;
		}
	}

	/* Make sure accounting agrees */
	if (k != totqs)
		return -EINVAL;

	return idpf_send_config_tx_queue_set_msg(qs);
}

/**
 * idpf_fill_rxq_config_chunk - fill chunk describing the Rx queue
 * @vport: virtual port data structure
 * @q: Rx queue to be inserted into VC chunk
 * @qi: pointer to the buffer containing the VC chunk
 */
static void idpf_fill_rxq_config_chunk(const struct idpf_vport *vport,
				       struct idpf_rx_queue *q,
				       struct virtchnl2_rxq_info *qi)
{
	const struct idpf_bufq_set *sets;

	qi->queue_id = cpu_to_le32(q->q_id);
	qi->model = cpu_to_le16(vport->rxq_model);
	qi->type = cpu_to_le32(VIRTCHNL2_QUEUE_TYPE_RX);
	qi->ring_len = cpu_to_le16(q->desc_count);
	qi->dma_ring_addr = cpu_to_le64(q->dma);
	qi->max_pkt_size = cpu_to_le32(q->rx_max_pkt_size);
	qi->rx_buffer_low_watermark = cpu_to_le16(q->rx_buffer_low_watermark);
	qi->qflags = cpu_to_le16(VIRTCHNL2_RX_DESC_SIZE_32BYTE);
	if (idpf_is_feature_ena(vport, NETIF_F_GRO_HW))
		qi->qflags |= cpu_to_le16(VIRTCHNL2_RXQ_RSC);

	if (!idpf_is_queue_model_split(vport->rxq_model)) {
		qi->data_buffer_size = cpu_to_le32(q->rx_buf_size);
		qi->desc_ids = cpu_to_le64(q->rxdids);

		return;
	}

	sets = q->bufq_sets;

	/*
	 * In splitq mode, RxQ buffer size should be set to that of the first
	 * buffer queue associated with this RxQ.
	 */
	q->rx_buf_size = sets[0].bufq.rx_buf_size;
	qi->data_buffer_size = cpu_to_le32(q->rx_buf_size);

	qi->rx_bufq1_id = cpu_to_le16(sets[0].bufq.q_id);
	if (vport->num_bufqs_per_qgrp > IDPF_SINGLE_BUFQ_PER_RXQ_GRP) {
		qi->bufq2_ena = IDPF_BUFQ2_ENA;
		qi->rx_bufq2_id = cpu_to_le16(sets[1].bufq.q_id);
	}

	q->rx_hbuf_size = sets[0].bufq.rx_hbuf_size;

	if (idpf_queue_has(HSPLIT_EN, q)) {
		qi->qflags |= cpu_to_le16(VIRTCHNL2_RXQ_HDR_SPLIT);
		qi->hdr_buffer_size = cpu_to_le16(q->rx_hbuf_size);
	}

	qi->desc_ids = cpu_to_le64(VIRTCHNL2_RXDID_2_FLEX_SPLITQ_M);
}

/**
 * idpf_fill_bufq_config_chunk - fill chunk describing the buffer queue
 * @vport: virtual port data structure
 * @q: buffer queue to be inserted into VC chunk
 * @qi: pointer to the buffer containing the VC chunk
 */
static void idpf_fill_bufq_config_chunk(const struct idpf_vport *vport,
					const struct idpf_buf_queue *q,
					struct virtchnl2_rxq_info *qi)
{
	qi->queue_id = cpu_to_le32(q->q_id);
	qi->model = cpu_to_le16(vport->rxq_model);
	qi->type = cpu_to_le32(VIRTCHNL2_QUEUE_TYPE_RX_BUFFER);
	qi->ring_len = cpu_to_le16(q->desc_count);
	qi->dma_ring_addr = cpu_to_le64(q->dma);
	qi->data_buffer_size = cpu_to_le32(q->rx_buf_size);
	qi->rx_buffer_low_watermark = cpu_to_le16(q->rx_buffer_low_watermark);
	qi->desc_ids = cpu_to_le64(VIRTCHNL2_RXDID_2_FLEX_SPLITQ_M);
	qi->buffer_notif_stride = IDPF_RX_BUF_STRIDE;
	if (idpf_is_feature_ena(vport, NETIF_F_GRO_HW))
		qi->qflags = cpu_to_le16(VIRTCHNL2_RXQ_RSC);

	if (idpf_queue_has(HSPLIT_EN, q)) {
		qi->qflags |= cpu_to_le16(VIRTCHNL2_RXQ_HDR_SPLIT);
		qi->hdr_buffer_size = cpu_to_le16(q->rx_hbuf_size);
	}
}

/**
 * idpf_prepare_cfg_rxqs_msg - prepare message to configure selected Rx queues
 * @vport: virtual port data structure
 * @buf: buffer containing the message
 * @pos: pointer to the first chunk describing the rx queue
 * @num_chunks: number of chunks in the message
 *
 * Helper function for preparing the message describing configuration of
 * Rx queues.
 *
 * Return: the total size of the prepared message.
 */
static u32 idpf_prepare_cfg_rxqs_msg(const struct idpf_vport *vport,
				     void *buf, const void *pos,
				     u32 num_chunks)
{
	struct virtchnl2_config_rx_queues *crq = buf;

	crq->vport_id = cpu_to_le32(vport->vport_id);
	crq->num_qinfo = cpu_to_le16(num_chunks);
	memcpy(crq->qinfo, pos, num_chunks * sizeof(*crq->qinfo));

	return struct_size(crq, qinfo, num_chunks);
}

/**
 * idpf_send_config_rx_queue_set_msg - send virtchnl config Rx queues message
 *				       for selected queues.
 * @qs: set of the Rx queues to configure
 *
 * Send config queues virtchnl message for queues contained in the @qs array.
 * The @qs array can contain Rx queues (or buffer queues) only.
 *
 * Return: 0 on success, -errno on failure.
 */
static int idpf_send_config_rx_queue_set_msg(const struct idpf_queue_set *qs)
{
	struct virtchnl2_rxq_info *qi __free(kfree) = NULL;
	struct idpf_chunked_msg_params params = {
		.vc_op		= VIRTCHNL2_OP_CONFIG_RX_QUEUES,
		.prepare_msg	= idpf_prepare_cfg_rxqs_msg,
		.config_sz	= sizeof(struct virtchnl2_config_rx_queues),
		.chunk_sz	= sizeof(*qi),
	};

	qi = kcalloc(qs->num, sizeof(*qi), GFP_KERNEL);
	if (!qi)
		return -ENOMEM;

	params.chunks = qi;

	for (u32 i = 0; i < qs->num; i++) {
		if (qs->qs[i].type == VIRTCHNL2_QUEUE_TYPE_RX)
			idpf_fill_rxq_config_chunk(qs->vport, qs->qs[i].rxq,
						   &qi[params.num_chunks++]);
		else if (qs->qs[i].type == VIRTCHNL2_QUEUE_TYPE_RX_BUFFER)
			idpf_fill_bufq_config_chunk(qs->vport, qs->qs[i].bufq,
						    &qi[params.num_chunks++]);
	}

	return idpf_send_chunked_msg(qs->vport, &params);
}

/**
 * idpf_send_config_rx_queues_msg - send virtchnl config Rx queues message
 * @vport: virtual port data structure
 *
 * Return: 0 on success, -errno on failure.
 */
static int idpf_send_config_rx_queues_msg(struct idpf_vport *vport)
{
	bool splitq = idpf_is_queue_model_split(vport->rxq_model);
	struct idpf_queue_set *qs __free(kfree) = NULL;
	u32 totqs = vport->num_rxq + vport->num_bufq;
	u32 k = 0;

	qs = idpf_alloc_queue_set(vport, totqs);
	if (!qs)
		return -ENOMEM;

	/* Populate the queue info buffer with all queue context info */
	for (u32 i = 0; i < vport->num_rxq_grp; i++) {
		const struct idpf_rxq_group *rx_qgrp = &vport->rxq_grps[i];
		u32 num_rxq;

		if (!splitq) {
			num_rxq = rx_qgrp->singleq.num_rxq;
			goto rxq;
		}

		for (u32 j = 0; j < vport->num_bufqs_per_qgrp; j++) {
			qs->qs[k].type = VIRTCHNL2_QUEUE_TYPE_RX_BUFFER;
			qs->qs[k++].bufq = &rx_qgrp->splitq.bufq_sets[j].bufq;
		}

		num_rxq = rx_qgrp->splitq.num_rxq_sets;

rxq:
		for (u32 j = 0; j < num_rxq; j++) {
			qs->qs[k].type = VIRTCHNL2_QUEUE_TYPE_RX;

			if (splitq)
				qs->qs[k++].rxq =
					&rx_qgrp->splitq.rxq_sets[j]->rxq;
			else
				qs->qs[k++].rxq = rx_qgrp->singleq.rxqs[j];
		}
	}

	/* Make sure accounting agrees */
	if (k != totqs)
		return -EINVAL;

	return idpf_send_config_rx_queue_set_msg(qs);
}

/**
 * idpf_prepare_ena_dis_qs_msg - prepare message to enable/disable selected
 *				 queues
 * @vport: virtual port data structure
 * @buf: buffer containing the message
 * @pos: pointer to the first chunk describing the queue
 * @num_chunks: number of chunks in the message
 *
 * Helper function for preparing the message describing queues to be enabled
 * or disabled.
 *
 * Return: the total size of the prepared message.
 */
static u32 idpf_prepare_ena_dis_qs_msg(const struct idpf_vport *vport,
				       void *buf, const void *pos,
				       u32 num_chunks)
{
	struct virtchnl2_del_ena_dis_queues *eq = buf;

	eq->vport_id = cpu_to_le32(vport->vport_id);
	eq->chunks.num_chunks = cpu_to_le16(num_chunks);
	memcpy(eq->chunks.chunks, pos,
	       num_chunks * sizeof(*eq->chunks.chunks));

	return struct_size(eq, chunks.chunks, num_chunks);
}

/**
 * idpf_send_ena_dis_queue_set_msg - send virtchnl enable or disable queues
 *				     message for selected queues
 * @qs: set of the queues to enable or disable
 * @en: whether to enable or disable queues
 *
 * Send enable or disable queues virtchnl message for queues contained
 * in the @qs array.
 * The @qs array can contain pointers to both Rx and Tx queues.
 *
 * Return: 0 on success, -errno on failure.
 */
static int idpf_send_ena_dis_queue_set_msg(const struct idpf_queue_set *qs,
					   bool en)
{
	struct virtchnl2_queue_chunk *qc __free(kfree) = NULL;
	struct idpf_chunked_msg_params params = {
		.vc_op		= en ? VIRTCHNL2_OP_ENABLE_QUEUES :
				       VIRTCHNL2_OP_DISABLE_QUEUES,
		.prepare_msg	= idpf_prepare_ena_dis_qs_msg,
		.config_sz	= sizeof(struct virtchnl2_del_ena_dis_queues),
		.chunk_sz	= sizeof(*qc),
		.num_chunks	= qs->num,
	};

	qc = kcalloc(qs->num, sizeof(*qc), GFP_KERNEL);
	if (!qc)
		return -ENOMEM;

	params.chunks = qc;

	for (u32 i = 0; i < qs->num; i++) {
		const struct idpf_queue_ptr *q = &qs->qs[i];
		u32 qid;

		qc[i].type = cpu_to_le32(q->type);
		qc[i].num_queues = cpu_to_le32(IDPF_NUMQ_PER_CHUNK);

		switch (q->type) {
		case VIRTCHNL2_QUEUE_TYPE_RX:
			qid = q->rxq->q_id;
			break;
		case VIRTCHNL2_QUEUE_TYPE_TX:
			qid = q->txq->q_id;
			break;
		case VIRTCHNL2_QUEUE_TYPE_RX_BUFFER:
			qid = q->bufq->q_id;
			break;
		case VIRTCHNL2_QUEUE_TYPE_TX_COMPLETION:
			qid = q->complq->q_id;
			break;
		default:
			return -EINVAL;
		}

		qc[i].start_queue_id = cpu_to_le32(qid);
	}

	return idpf_send_chunked_msg(qs->vport, &params);
}

/**
 * idpf_send_ena_dis_queues_msg - send virtchnl enable or disable queues
 *				  message
 * @vport: virtual port data structure
 * @en: whether to enable or disable queues
 *
 * Return: 0 on success, -errno on failure.
 */
static int idpf_send_ena_dis_queues_msg(struct idpf_vport *vport, bool en)
{
	struct idpf_queue_set *qs __free(kfree) = NULL;
	u32 num_txq, num_q, k = 0;
	bool split;

	num_txq = vport->num_txq + vport->num_complq;
	num_q = num_txq + vport->num_rxq + vport->num_bufq;

	qs = idpf_alloc_queue_set(vport, num_q);
	if (!qs)
		return -ENOMEM;

	split = idpf_is_queue_model_split(vport->txq_model);

	for (u32 i = 0; i < vport->num_txq_grp; i++) {
		const struct idpf_txq_group *tx_qgrp = &vport->txq_grps[i];

		for (u32 j = 0; j < tx_qgrp->num_txq; j++) {
			qs->qs[k].type = VIRTCHNL2_QUEUE_TYPE_TX;
			qs->qs[k++].txq = tx_qgrp->txqs[j];
		}

		if (!split)
			continue;

		qs->qs[k].type = VIRTCHNL2_QUEUE_TYPE_TX_COMPLETION;
		qs->qs[k++].complq = tx_qgrp->complq;
	}

	if (k != num_txq)
		return -EINVAL;

	split = idpf_is_queue_model_split(vport->rxq_model);

	for (u32 i = 0; i < vport->num_rxq_grp; i++) {
		const struct idpf_rxq_group *rx_qgrp = &vport->rxq_grps[i];
		u32 num_rxq;

		if (split)
			num_rxq = rx_qgrp->splitq.num_rxq_sets;
		else
			num_rxq = rx_qgrp->singleq.num_rxq;

		for (u32 j = 0; j < num_rxq; j++) {
			qs->qs[k].type = VIRTCHNL2_QUEUE_TYPE_RX;

			if (split)
				qs->qs[k++].rxq =
					&rx_qgrp->splitq.rxq_sets[j]->rxq;
			else
				qs->qs[k++].rxq = rx_qgrp->singleq.rxqs[j];
		}

		if (!split)
			continue;

		for (u32 j = 0; j < vport->num_bufqs_per_qgrp; j++) {
			qs->qs[k].type = VIRTCHNL2_QUEUE_TYPE_RX_BUFFER;
			qs->qs[k++].bufq = &rx_qgrp->splitq.bufq_sets[j].bufq;
		}
	}

	if (k != num_q)
		return -EINVAL;

	return idpf_send_ena_dis_queue_set_msg(qs, en);
}

/**
 * idpf_prep_map_unmap_queue_set_vector_msg - prepare message to map or unmap
 *					      queue set to the interrupt vector
 * @vport: virtual port data structure
 * @buf: buffer containing the message
 * @pos: pointer to the first chunk describing the vector mapping
 * @num_chunks: number of chunks in the message
 *
 * Helper function for preparing the message describing mapping queues to
 * q_vectors.
 *
 * Return: the total size of the prepared message.
 */
static u32
idpf_prep_map_unmap_queue_set_vector_msg(const struct idpf_vport *vport,
					 void *buf, const void *pos,
					 u32 num_chunks)
{
	struct virtchnl2_queue_vector_maps *vqvm = buf;

	vqvm->vport_id = cpu_to_le32(vport->vport_id);
	vqvm->num_qv_maps = cpu_to_le16(num_chunks);
	memcpy(vqvm->qv_maps, pos, num_chunks * sizeof(*vqvm->qv_maps));

	return struct_size(vqvm, qv_maps, num_chunks);
}

/**
 * idpf_send_map_unmap_queue_set_vector_msg - send virtchnl map or unmap
 *					      queue set vector message
 * @qs: set of the queues to map or unmap
 * @map: true for map and false for unmap
 *
 * Return: 0 on success, -errno on failure.
 */
static int
idpf_send_map_unmap_queue_set_vector_msg(const struct idpf_queue_set *qs,
					 bool map)
{
	struct virtchnl2_queue_vector *vqv __free(kfree) = NULL;
	struct idpf_chunked_msg_params params = {
		.vc_op		= map ? VIRTCHNL2_OP_MAP_QUEUE_VECTOR :
					VIRTCHNL2_OP_UNMAP_QUEUE_VECTOR,
		.prepare_msg	= idpf_prep_map_unmap_queue_set_vector_msg,
		.config_sz	= sizeof(struct virtchnl2_queue_vector_maps),
		.chunk_sz	= sizeof(*vqv),
		.num_chunks	= qs->num,
	};
	bool split;

	vqv = kcalloc(qs->num, sizeof(*vqv), GFP_KERNEL);
	if (!vqv)
		return -ENOMEM;

	params.chunks = vqv;

	split = idpf_is_queue_model_split(qs->vport->txq_model);

	for (u32 i = 0; i < qs->num; i++) {
		const struct idpf_queue_ptr *q = &qs->qs[i];
		const struct idpf_q_vector *vec;
		u32 qid, v_idx, itr_idx;

		vqv[i].queue_type = cpu_to_le32(q->type);

		switch (q->type) {
		case VIRTCHNL2_QUEUE_TYPE_RX:
			qid = q->rxq->q_id;

			if (idpf_queue_has(NOIRQ, q->rxq))
				vec = NULL;
			else
				vec = q->rxq->q_vector;

			if (vec) {
				v_idx = vec->v_idx;
				itr_idx = vec->rx_itr_idx;
			} else {
				v_idx = qs->vport->noirq_v_idx;
				itr_idx = VIRTCHNL2_ITR_IDX_0;
			}
			break;
		case VIRTCHNL2_QUEUE_TYPE_TX:
			qid = q->txq->q_id;

			if (idpf_queue_has(NOIRQ, q->txq))
				vec = NULL;
			else if (idpf_queue_has(XDP, q->txq))
				vec = q->txq->complq->q_vector;
			else if (split)
				vec = q->txq->txq_grp->complq->q_vector;
			else
				vec = q->txq->q_vector;

			if (vec) {
				v_idx = vec->v_idx;
				itr_idx = vec->tx_itr_idx;
			} else {
				v_idx = qs->vport->noirq_v_idx;
				itr_idx = VIRTCHNL2_ITR_IDX_1;
			}
			break;
		default:
			return -EINVAL;
		}

		vqv[i].queue_id = cpu_to_le32(qid);
		vqv[i].vector_id = cpu_to_le16(v_idx);
		vqv[i].itr_idx = cpu_to_le32(itr_idx);
	}

	return idpf_send_chunked_msg(qs->vport, &params);
}

/**
 * idpf_send_map_unmap_queue_vector_msg - send virtchnl map or unmap queue
 *					  vector message
 * @vport: virtual port data structure
 * @map: true for map and false for unmap
 *
 * Return: 0 on success, -errno on failure.
 */
int idpf_send_map_unmap_queue_vector_msg(struct idpf_vport *vport, bool map)
{
	struct idpf_queue_set *qs __free(kfree) = NULL;
	u32 num_q = vport->num_txq + vport->num_rxq;
	u32 k = 0;

	qs = idpf_alloc_queue_set(vport, num_q);
	if (!qs)
		return -ENOMEM;

	for (u32 i = 0; i < vport->num_txq_grp; i++) {
		const struct idpf_txq_group *tx_qgrp = &vport->txq_grps[i];

		for (u32 j = 0; j < tx_qgrp->num_txq; j++) {
			qs->qs[k].type = VIRTCHNL2_QUEUE_TYPE_TX;
			qs->qs[k++].txq = tx_qgrp->txqs[j];
		}
	}

	if (k != vport->num_txq)
		return -EINVAL;

	for (u32 i = 0; i < vport->num_rxq_grp; i++) {
		const struct idpf_rxq_group *rx_qgrp = &vport->rxq_grps[i];
		u32 num_rxq;

		if (idpf_is_queue_model_split(vport->rxq_model))
			num_rxq = rx_qgrp->splitq.num_rxq_sets;
		else
			num_rxq = rx_qgrp->singleq.num_rxq;

		for (u32 j = 0; j < num_rxq; j++) {
			qs->qs[k].type = VIRTCHNL2_QUEUE_TYPE_RX;

			if (idpf_is_queue_model_split(vport->rxq_model))
				qs->qs[k++].rxq =
					&rx_qgrp->splitq.rxq_sets[j]->rxq;
			else
				qs->qs[k++].rxq = rx_qgrp->singleq.rxqs[j];
		}
	}

	if (k != num_q)
		return -EINVAL;

	return idpf_send_map_unmap_queue_set_vector_msg(qs, map);
}

/**
 * idpf_send_enable_queue_set_msg - send enable queues virtchnl message for
 *				    selected queues
 * @qs: set of the queues
 *
 * Send enable queues virtchnl message for queues contained in the @qs array.
 *
 * Return: 0 on success, -errno on failure.
 */
int idpf_send_enable_queue_set_msg(const struct idpf_queue_set *qs)
{
	return idpf_send_ena_dis_queue_set_msg(qs, true);
}

/**
 * idpf_send_disable_queue_set_msg - send disable queues virtchnl message for
 *				     selected queues
 * @qs: set of the queues
 *
 * Return: 0 on success, -errno on failure.
 */
int idpf_send_disable_queue_set_msg(const struct idpf_queue_set *qs)
{
	int err;

	err = idpf_send_ena_dis_queue_set_msg(qs, false);
	if (err)
		return err;

	return idpf_wait_for_marker_event_set(qs);
}

/**
 * idpf_send_config_queue_set_msg - send virtchnl config queues message for
 *				    selected queues
 * @qs: set of the queues
 *
 * Send config queues virtchnl message for queues contained in the @qs array.
 * The @qs array can contain both Rx or Tx queues.
 *
 * Return: 0 on success, -errno on failure.
 */
int idpf_send_config_queue_set_msg(const struct idpf_queue_set *qs)
{
	int err;

	err = idpf_send_config_tx_queue_set_msg(qs);
	if (err)
		return err;

	return idpf_send_config_rx_queue_set_msg(qs);
}

/**
 * idpf_send_enable_queues_msg - send enable queues virtchnl message
 * @vport: Virtual port private data structure
 *
 * Will send enable queues virtchnl message.  Returns 0 on success, negative on
 * failure.
 */
int idpf_send_enable_queues_msg(struct idpf_vport *vport)
{
	return idpf_send_ena_dis_queues_msg(vport, true);
}

/**
 * idpf_send_disable_queues_msg - send disable queues virtchnl message
 * @vport: Virtual port private data structure
 *
 * Will send disable queues virtchnl message.  Returns 0 on success, negative
 * on failure.
 */
int idpf_send_disable_queues_msg(struct idpf_vport *vport)
{
	int err;

	err = idpf_send_ena_dis_queues_msg(vport, false);
	if (err)
		return err;

	return idpf_wait_for_marker_event(vport);
}

/**
 * idpf_convert_reg_to_queue_chunks - Copy queue chunk information to the right
 * structure
 * @dchunks: Destination chunks to store data to
 * @schunks: Source chunks to copy data from
 * @num_chunks: number of chunks to copy
 */
static void idpf_convert_reg_to_queue_chunks(struct virtchnl2_queue_chunk *dchunks,
					     struct virtchnl2_queue_reg_chunk *schunks,
					     u16 num_chunks)
{
	u16 i;

	for (i = 0; i < num_chunks; i++) {
		dchunks[i].type = schunks[i].type;
		dchunks[i].start_queue_id = schunks[i].start_queue_id;
		dchunks[i].num_queues = schunks[i].num_queues;
	}
}

/**
 * idpf_send_delete_queues_msg - send delete queues virtchnl message
 * @vport: Virtual port private data structure
 *
 * Will send delete queues virtchnl message. Return 0 on success, negative on
 * failure.
 */
int idpf_send_delete_queues_msg(struct idpf_vport *vport)
{
	struct virtchnl2_del_ena_dis_queues *eq __free(kfree) = NULL;
	struct virtchnl2_create_vport *vport_params;
	struct virtchnl2_queue_reg_chunks *chunks;
	struct idpf_vc_xn_params xn_params = {};
	struct idpf_vport_config *vport_config;
	u16 vport_idx = vport->idx;
	ssize_t reply_sz;
	u16 num_chunks;
	int buf_size;

	vport_config = vport->adapter->vport_config[vport_idx];
	if (vport_config->req_qs_chunks) {
		chunks = &vport_config->req_qs_chunks->chunks;
	} else {
		vport_params = vport->adapter->vport_params_recvd[vport_idx];
		chunks = &vport_params->chunks;
	}

	num_chunks = le16_to_cpu(chunks->num_chunks);
	buf_size = struct_size(eq, chunks.chunks, num_chunks);

	eq = kzalloc(buf_size, GFP_KERNEL);
	if (!eq)
		return -ENOMEM;

	eq->vport_id = cpu_to_le32(vport->vport_id);
	eq->chunks.num_chunks = cpu_to_le16(num_chunks);

	idpf_convert_reg_to_queue_chunks(eq->chunks.chunks, chunks->chunks,
					 num_chunks);

	xn_params.vc_op = VIRTCHNL2_OP_DEL_QUEUES;
	xn_params.timeout_ms = IDPF_VC_XN_DEFAULT_TIMEOUT_MSEC;
	xn_params.send_buf.iov_base = eq;
	xn_params.send_buf.iov_len = buf_size;
	reply_sz = idpf_vc_xn_exec(vport->adapter, &xn_params);

	return reply_sz < 0 ? reply_sz : 0;
}

/**
 * idpf_send_config_queues_msg - Send config queues virtchnl message
 * @vport: Virtual port private data structure
 *
 * Will send config queues virtchnl message. Returns 0 on success, negative on
 * failure.
 */
int idpf_send_config_queues_msg(struct idpf_vport *vport)
{
	int err;

	err = idpf_send_config_tx_queues_msg(vport);
	if (err)
		return err;

	return idpf_send_config_rx_queues_msg(vport);
}

/**
 * idpf_send_add_queues_msg - Send virtchnl add queues message
 * @vport: Virtual port private data structure
 * @num_tx_q: number of transmit queues
 * @num_complq: number of transmit completion queues
 * @num_rx_q: number of receive queues
 * @num_rx_bufq: number of receive buffer queues
 *
 * Returns 0 on success, negative on failure. vport _MUST_ be const here as
 * we should not change any fields within vport itself in this function.
 */
int idpf_send_add_queues_msg(const struct idpf_vport *vport, u16 num_tx_q,
			     u16 num_complq, u16 num_rx_q, u16 num_rx_bufq)
{
	struct virtchnl2_add_queues *vc_msg __free(kfree) = NULL;
	struct idpf_vc_xn_params xn_params = {};
	struct idpf_vport_config *vport_config;
	struct virtchnl2_add_queues aq = {};
	u16 vport_idx = vport->idx;
	ssize_t reply_sz;
	int size;

	vc_msg = kzalloc(IDPF_CTLQ_MAX_BUF_LEN, GFP_KERNEL);
	if (!vc_msg)
		return -ENOMEM;

	vport_config = vport->adapter->vport_config[vport_idx];
	kfree(vport_config->req_qs_chunks);
	vport_config->req_qs_chunks = NULL;

	aq.vport_id = cpu_to_le32(vport->vport_id);
	aq.num_tx_q = cpu_to_le16(num_tx_q);
	aq.num_tx_complq = cpu_to_le16(num_complq);
	aq.num_rx_q = cpu_to_le16(num_rx_q);
	aq.num_rx_bufq = cpu_to_le16(num_rx_bufq);

	xn_params.vc_op = VIRTCHNL2_OP_ADD_QUEUES;
	xn_params.timeout_ms = IDPF_VC_XN_DEFAULT_TIMEOUT_MSEC;
	xn_params.send_buf.iov_base = &aq;
	xn_params.send_buf.iov_len = sizeof(aq);
	xn_params.recv_buf.iov_base = vc_msg;
	xn_params.recv_buf.iov_len = IDPF_CTLQ_MAX_BUF_LEN;
	reply_sz = idpf_vc_xn_exec(vport->adapter, &xn_params);
	if (reply_sz < 0)
		return reply_sz;

	/* compare vc_msg num queues with vport num queues */
	if (le16_to_cpu(vc_msg->num_tx_q) != num_tx_q ||
	    le16_to_cpu(vc_msg->num_rx_q) != num_rx_q ||
	    le16_to_cpu(vc_msg->num_tx_complq) != num_complq ||
	    le16_to_cpu(vc_msg->num_rx_bufq) != num_rx_bufq)
		return -EINVAL;

	size = struct_size(vc_msg, chunks.chunks,
			   le16_to_cpu(vc_msg->chunks.num_chunks));
	if (reply_sz < size)
		return -EIO;

	vport_config->req_qs_chunks = kmemdup(vc_msg, size, GFP_KERNEL);
	if (!vport_config->req_qs_chunks)
		return -ENOMEM;

	return 0;
}

/**
 * idpf_send_alloc_vectors_msg - Send virtchnl alloc vectors message
 * @adapter: Driver specific private structure
 * @num_vectors: number of vectors to be allocated
 *
 * Returns 0 on success, negative on failure.
 */
int idpf_send_alloc_vectors_msg(struct idpf_adapter *adapter, u16 num_vectors)
{
	struct virtchnl2_alloc_vectors *rcvd_vec __free(kfree) = NULL;
	struct idpf_vc_xn_params xn_params = {};
	struct virtchnl2_alloc_vectors ac = {};
	ssize_t reply_sz;
	u16 num_vchunks;
	int size;

	ac.num_vectors = cpu_to_le16(num_vectors);

	rcvd_vec = kzalloc(IDPF_CTLQ_MAX_BUF_LEN, GFP_KERNEL);
	if (!rcvd_vec)
		return -ENOMEM;

	xn_params.vc_op = VIRTCHNL2_OP_ALLOC_VECTORS;
	xn_params.send_buf.iov_base = &ac;
	xn_params.send_buf.iov_len = sizeof(ac);
	xn_params.recv_buf.iov_base = rcvd_vec;
	xn_params.recv_buf.iov_len = IDPF_CTLQ_MAX_BUF_LEN;
	xn_params.timeout_ms = IDPF_VC_XN_DEFAULT_TIMEOUT_MSEC;
	reply_sz = idpf_vc_xn_exec(adapter, &xn_params);
	if (reply_sz < 0)
		return reply_sz;

	num_vchunks = le16_to_cpu(rcvd_vec->vchunks.num_vchunks);
	size = struct_size(rcvd_vec, vchunks.vchunks, num_vchunks);
	if (reply_sz < size)
		return -EIO;

	if (size > IDPF_CTLQ_MAX_BUF_LEN)
		return -EINVAL;

	kfree(adapter->req_vec_chunks);
	adapter->req_vec_chunks = kmemdup(rcvd_vec, size, GFP_KERNEL);
	if (!adapter->req_vec_chunks)
		return -ENOMEM;

	if (le16_to_cpu(adapter->req_vec_chunks->num_vectors) < num_vectors) {
		kfree(adapter->req_vec_chunks);
		adapter->req_vec_chunks = NULL;
		return -EINVAL;
	}

	return 0;
}

/**
 * idpf_send_dealloc_vectors_msg - Send virtchnl de allocate vectors message
 * @adapter: Driver specific private structure
 *
 * Returns 0 on success, negative on failure.
 */
int idpf_send_dealloc_vectors_msg(struct idpf_adapter *adapter)
{
	struct virtchnl2_alloc_vectors *ac = adapter->req_vec_chunks;
	struct virtchnl2_vector_chunks *vcs = &ac->vchunks;
	struct idpf_vc_xn_params xn_params = {};
	ssize_t reply_sz;
	int buf_size;

	buf_size = struct_size(vcs, vchunks, le16_to_cpu(vcs->num_vchunks));

	xn_params.vc_op = VIRTCHNL2_OP_DEALLOC_VECTORS;
	xn_params.send_buf.iov_base = vcs;
	xn_params.send_buf.iov_len = buf_size;
	xn_params.timeout_ms = IDPF_VC_XN_DEFAULT_TIMEOUT_MSEC;
	reply_sz = idpf_vc_xn_exec(adapter, &xn_params);
	if (reply_sz < 0)
		return reply_sz;

	kfree(adapter->req_vec_chunks);
	adapter->req_vec_chunks = NULL;

	return 0;
}

/**
 * idpf_get_max_vfs - Get max number of vfs supported
 * @adapter: Driver specific private structure
 *
 * Returns max number of VFs
 */
static int idpf_get_max_vfs(struct idpf_adapter *adapter)
{
	return le16_to_cpu(adapter->caps.max_sriov_vfs);
}

/**
 * idpf_send_set_sriov_vfs_msg - Send virtchnl set sriov vfs message
 * @adapter: Driver specific private structure
 * @num_vfs: number of virtual functions to be created
 *
 * Returns 0 on success, negative on failure.
 */
int idpf_send_set_sriov_vfs_msg(struct idpf_adapter *adapter, u16 num_vfs)
{
	struct virtchnl2_sriov_vfs_info svi = {};
	struct idpf_vc_xn_params xn_params = {};
	ssize_t reply_sz;

	svi.num_vfs = cpu_to_le16(num_vfs);
	xn_params.vc_op = VIRTCHNL2_OP_SET_SRIOV_VFS;
	xn_params.timeout_ms = IDPF_VC_XN_DEFAULT_TIMEOUT_MSEC;
	xn_params.send_buf.iov_base = &svi;
	xn_params.send_buf.iov_len = sizeof(svi);
	reply_sz = idpf_vc_xn_exec(adapter, &xn_params);

	return reply_sz < 0 ? reply_sz : 0;
}

/**
 * idpf_send_get_stats_msg - Send virtchnl get statistics message
 * @vport: vport to get stats for
 *
 * Returns 0 on success, negative on failure.
 */
int idpf_send_get_stats_msg(struct idpf_vport *vport)
{
	struct idpf_netdev_priv *np = netdev_priv(vport->netdev);
	struct rtnl_link_stats64 *netstats = &np->netstats;
	struct virtchnl2_vport_stats stats_msg = {};
	struct idpf_vc_xn_params xn_params = {};
	ssize_t reply_sz;


	/* Don't send get_stats message if the link is down */
	if (np->state <= __IDPF_VPORT_DOWN)
		return 0;

	stats_msg.vport_id = cpu_to_le32(vport->vport_id);

	xn_params.vc_op = VIRTCHNL2_OP_GET_STATS;
	xn_params.send_buf.iov_base = &stats_msg;
	xn_params.send_buf.iov_len = sizeof(stats_msg);
	xn_params.recv_buf = xn_params.send_buf;
	xn_params.timeout_ms = IDPF_VC_XN_DEFAULT_TIMEOUT_MSEC;

	reply_sz = idpf_vc_xn_exec(vport->adapter, &xn_params);
	if (reply_sz < 0)
		return reply_sz;
	if (reply_sz < sizeof(stats_msg))
		return -EIO;

	spin_lock_bh(&np->stats_lock);

	netstats->rx_packets = le64_to_cpu(stats_msg.rx_unicast) +
			       le64_to_cpu(stats_msg.rx_multicast) +
			       le64_to_cpu(stats_msg.rx_broadcast);
	netstats->tx_packets = le64_to_cpu(stats_msg.tx_unicast) +
			       le64_to_cpu(stats_msg.tx_multicast) +
			       le64_to_cpu(stats_msg.tx_broadcast);
	netstats->rx_bytes = le64_to_cpu(stats_msg.rx_bytes);
	netstats->tx_bytes = le64_to_cpu(stats_msg.tx_bytes);
	netstats->rx_errors = le64_to_cpu(stats_msg.rx_errors);
	netstats->tx_errors = le64_to_cpu(stats_msg.tx_errors);
	netstats->rx_dropped = le64_to_cpu(stats_msg.rx_discards);
	netstats->tx_dropped = le64_to_cpu(stats_msg.tx_discards);

	vport->port_stats.vport_stats = stats_msg;

	spin_unlock_bh(&np->stats_lock);

	return 0;
}

/**
 * idpf_send_get_set_rss_lut_msg - Send virtchnl get or set rss lut message
 * @vport: virtual port data structure
 * @get: flag to set or get rss look up table
 *
 * Returns 0 on success, negative on failure.
 */
int idpf_send_get_set_rss_lut_msg(struct idpf_vport *vport, bool get)
{
	struct virtchnl2_rss_lut *recv_rl __free(kfree) = NULL;
	struct virtchnl2_rss_lut *rl __free(kfree) = NULL;
	struct idpf_vc_xn_params xn_params = {};
	struct idpf_rss_data *rss_data;
	int buf_size, lut_buf_size;
	ssize_t reply_sz;
	int i;

	rss_data =
		&vport->adapter->vport_config[vport->idx]->user_config.rss_data;
	buf_size = struct_size(rl, lut, rss_data->rss_lut_size);
	rl = kzalloc(buf_size, GFP_KERNEL);
	if (!rl)
		return -ENOMEM;

	rl->vport_id = cpu_to_le32(vport->vport_id);

	xn_params.timeout_ms = IDPF_VC_XN_DEFAULT_TIMEOUT_MSEC;
	xn_params.send_buf.iov_base = rl;
	xn_params.send_buf.iov_len = buf_size;

	if (get) {
		recv_rl = kzalloc(IDPF_CTLQ_MAX_BUF_LEN, GFP_KERNEL);
		if (!recv_rl)
			return -ENOMEM;
		xn_params.vc_op = VIRTCHNL2_OP_GET_RSS_LUT;
		xn_params.recv_buf.iov_base = recv_rl;
		xn_params.recv_buf.iov_len = IDPF_CTLQ_MAX_BUF_LEN;
	} else {
		rl->lut_entries = cpu_to_le16(rss_data->rss_lut_size);
		for (i = 0; i < rss_data->rss_lut_size; i++)
			rl->lut[i] = cpu_to_le32(rss_data->rss_lut[i]);

		xn_params.vc_op = VIRTCHNL2_OP_SET_RSS_LUT;
	}
	reply_sz = idpf_vc_xn_exec(vport->adapter, &xn_params);
	if (reply_sz < 0)
		return reply_sz;
	if (!get)
		return 0;
	if (reply_sz < sizeof(struct virtchnl2_rss_lut))
		return -EIO;

	lut_buf_size = le16_to_cpu(recv_rl->lut_entries) * sizeof(u32);
	if (reply_sz < lut_buf_size)
		return -EIO;

	/* size didn't change, we can reuse existing lut buf */
	if (rss_data->rss_lut_size == le16_to_cpu(recv_rl->lut_entries))
		goto do_memcpy;

	rss_data->rss_lut_size = le16_to_cpu(recv_rl->lut_entries);
	kfree(rss_data->rss_lut);

	rss_data->rss_lut = kzalloc(lut_buf_size, GFP_KERNEL);
	if (!rss_data->rss_lut) {
		rss_data->rss_lut_size = 0;
		return -ENOMEM;
	}

do_memcpy:
	memcpy(rss_data->rss_lut, recv_rl->lut, rss_data->rss_lut_size);

	return 0;
}

/**
 * idpf_send_get_set_rss_key_msg - Send virtchnl get or set rss key message
 * @vport: virtual port data structure
 * @get: flag to set or get rss look up table
 *
 * Returns 0 on success, negative on failure
 */
int idpf_send_get_set_rss_key_msg(struct idpf_vport *vport, bool get)
{
	struct virtchnl2_rss_key *recv_rk __free(kfree) = NULL;
	struct virtchnl2_rss_key *rk __free(kfree) = NULL;
	struct idpf_vc_xn_params xn_params = {};
	struct idpf_rss_data *rss_data;
	ssize_t reply_sz;
	int i, buf_size;
	u16 key_size;

	rss_data =
		&vport->adapter->vport_config[vport->idx]->user_config.rss_data;
	buf_size = struct_size(rk, key_flex, rss_data->rss_key_size);
	rk = kzalloc(buf_size, GFP_KERNEL);
	if (!rk)
		return -ENOMEM;

	rk->vport_id = cpu_to_le32(vport->vport_id);
	xn_params.send_buf.iov_base = rk;
	xn_params.send_buf.iov_len = buf_size;
	xn_params.timeout_ms = IDPF_VC_XN_DEFAULT_TIMEOUT_MSEC;
	if (get) {
		recv_rk = kzalloc(IDPF_CTLQ_MAX_BUF_LEN, GFP_KERNEL);
		if (!recv_rk)
			return -ENOMEM;

		xn_params.vc_op = VIRTCHNL2_OP_GET_RSS_KEY;
		xn_params.recv_buf.iov_base = recv_rk;
		xn_params.recv_buf.iov_len = IDPF_CTLQ_MAX_BUF_LEN;
	} else {
		rk->key_len = cpu_to_le16(rss_data->rss_key_size);
		for (i = 0; i < rss_data->rss_key_size; i++)
			rk->key_flex[i] = rss_data->rss_key[i];

		xn_params.vc_op = VIRTCHNL2_OP_SET_RSS_KEY;
	}

	reply_sz = idpf_vc_xn_exec(vport->adapter, &xn_params);
	if (reply_sz < 0)
		return reply_sz;
	if (!get)
		return 0;
	if (reply_sz < sizeof(struct virtchnl2_rss_key))
		return -EIO;

	key_size = min_t(u16, NETDEV_RSS_KEY_LEN,
			 le16_to_cpu(recv_rk->key_len));
	if (reply_sz < key_size)
		return -EIO;

	/* key len didn't change, reuse existing buf */
	if (rss_data->rss_key_size == key_size)
		goto do_memcpy;

	rss_data->rss_key_size = key_size;
	kfree(rss_data->rss_key);
	rss_data->rss_key = kzalloc(key_size, GFP_KERNEL);
	if (!rss_data->rss_key) {
		rss_data->rss_key_size = 0;
		return -ENOMEM;
	}

do_memcpy:
	memcpy(rss_data->rss_key, recv_rk->key_flex, rss_data->rss_key_size);

	return 0;
}

/**
 * idpf_fill_ptype_lookup - Fill L3 specific fields in ptype lookup table
 * @ptype: ptype lookup table
 * @pstate: state machine for ptype lookup table
 * @ipv4: ipv4 or ipv6
 * @frag: fragmentation allowed
 *
 */
static void idpf_fill_ptype_lookup(struct libeth_rx_pt *ptype,
				   struct idpf_ptype_state *pstate,
				   bool ipv4, bool frag)
{
	if (!pstate->outer_ip || !pstate->outer_frag) {
		pstate->outer_ip = true;

		if (ipv4)
			ptype->outer_ip = LIBETH_RX_PT_OUTER_IPV4;
		else
			ptype->outer_ip = LIBETH_RX_PT_OUTER_IPV6;

		if (frag) {
			ptype->outer_frag = LIBETH_RX_PT_FRAG;
			pstate->outer_frag = true;
		}
	} else {
		ptype->tunnel_type = LIBETH_RX_PT_TUNNEL_IP_IP;
		pstate->tunnel_state = IDPF_PTYPE_TUNNEL_IP;

		if (ipv4)
			ptype->tunnel_end_prot = LIBETH_RX_PT_TUNNEL_END_IPV4;
		else
			ptype->tunnel_end_prot = LIBETH_RX_PT_TUNNEL_END_IPV6;

		if (frag)
			ptype->tunnel_end_frag = LIBETH_RX_PT_FRAG;
	}
}

static void idpf_finalize_ptype_lookup(struct libeth_rx_pt *ptype)
{
	if (ptype->payload_layer == LIBETH_RX_PT_PAYLOAD_L2 &&
	    ptype->inner_prot)
		ptype->payload_layer = LIBETH_RX_PT_PAYLOAD_L4;
	else if (ptype->payload_layer == LIBETH_RX_PT_PAYLOAD_L2 &&
		 ptype->outer_ip)
		ptype->payload_layer = LIBETH_RX_PT_PAYLOAD_L3;
	else if (ptype->outer_ip == LIBETH_RX_PT_OUTER_L2)
		ptype->payload_layer = LIBETH_RX_PT_PAYLOAD_L2;
	else
		ptype->payload_layer = LIBETH_RX_PT_PAYLOAD_NONE;

	libeth_rx_pt_gen_hash_type(ptype);
}

/**
 * idpf_send_get_rx_ptype_msg - Send virtchnl for ptype info
 * @vport: virtual port data structure
 *
 * Returns 0 on success, negative on failure.
 */
int idpf_send_get_rx_ptype_msg(struct idpf_vport *vport)
{
	struct virtchnl2_get_ptype_info *get_ptype_info __free(kfree) = NULL;
	struct virtchnl2_get_ptype_info *ptype_info __free(kfree) = NULL;
	struct libeth_rx_pt *ptype_lkup __free(kfree) = NULL;
	int max_ptype, ptypes_recvd = 0, ptype_offset;
	struct idpf_adapter *adapter = vport->adapter;
	struct idpf_vc_xn_params xn_params = {};
	u16 next_ptype_id = 0;
	ssize_t reply_sz;
	int i, j, k;

	if (vport->rx_ptype_lkup)
		return 0;

	if (idpf_is_queue_model_split(vport->rxq_model))
		max_ptype = IDPF_RX_MAX_PTYPE;
	else
		max_ptype = IDPF_RX_MAX_BASE_PTYPE;

	ptype_lkup = kcalloc(max_ptype, sizeof(*ptype_lkup), GFP_KERNEL);
	if (!ptype_lkup)
		return -ENOMEM;

	get_ptype_info = kzalloc(sizeof(*get_ptype_info), GFP_KERNEL);
	if (!get_ptype_info)
		return -ENOMEM;

	ptype_info = kzalloc(IDPF_CTLQ_MAX_BUF_LEN, GFP_KERNEL);
	if (!ptype_info)
		return -ENOMEM;

	xn_params.vc_op = VIRTCHNL2_OP_GET_PTYPE_INFO;
	xn_params.send_buf.iov_base = get_ptype_info;
	xn_params.send_buf.iov_len = sizeof(*get_ptype_info);
	xn_params.recv_buf.iov_base = ptype_info;
	xn_params.recv_buf.iov_len = IDPF_CTLQ_MAX_BUF_LEN;
	xn_params.timeout_ms = IDPF_VC_XN_DEFAULT_TIMEOUT_MSEC;

	while (next_ptype_id < max_ptype) {
		get_ptype_info->start_ptype_id = cpu_to_le16(next_ptype_id);

		if ((next_ptype_id + IDPF_RX_MAX_PTYPES_PER_BUF) > max_ptype)
			get_ptype_info->num_ptypes =
				cpu_to_le16(max_ptype - next_ptype_id);
		else
			get_ptype_info->num_ptypes =
				cpu_to_le16(IDPF_RX_MAX_PTYPES_PER_BUF);

		reply_sz = idpf_vc_xn_exec(adapter, &xn_params);
		if (reply_sz < 0)
			return reply_sz;

		ptypes_recvd += le16_to_cpu(ptype_info->num_ptypes);
		if (ptypes_recvd > max_ptype)
			return -EINVAL;

		next_ptype_id = le16_to_cpu(get_ptype_info->start_ptype_id) +
				le16_to_cpu(get_ptype_info->num_ptypes);

		ptype_offset = IDPF_RX_PTYPE_HDR_SZ;

		for (i = 0; i < le16_to_cpu(ptype_info->num_ptypes); i++) {
			struct idpf_ptype_state pstate = { };
			struct virtchnl2_ptype *ptype;
			u16 id;

			ptype = (struct virtchnl2_ptype *)
					((u8 *)ptype_info + ptype_offset);

			ptype_offset += IDPF_GET_PTYPE_SIZE(ptype);
			if (ptype_offset > IDPF_CTLQ_MAX_BUF_LEN)
				return -EINVAL;

			/* 0xFFFF indicates end of ptypes */
			if (le16_to_cpu(ptype->ptype_id_10) ==
							IDPF_INVALID_PTYPE_ID)
				goto out;

			if (idpf_is_queue_model_split(vport->rxq_model))
				k = le16_to_cpu(ptype->ptype_id_10);
			else
				k = ptype->ptype_id_8;

			for (j = 0; j < ptype->proto_id_count; j++) {
				id = le16_to_cpu(ptype->proto_id[j]);
				switch (id) {
				case VIRTCHNL2_PROTO_HDR_GRE:
					if (pstate.tunnel_state ==
							IDPF_PTYPE_TUNNEL_IP) {
						ptype_lkup[k].tunnel_type =
						LIBETH_RX_PT_TUNNEL_IP_GRENAT;
						pstate.tunnel_state |=
						IDPF_PTYPE_TUNNEL_IP_GRENAT;
					}
					break;
				case VIRTCHNL2_PROTO_HDR_MAC:
					ptype_lkup[k].outer_ip =
						LIBETH_RX_PT_OUTER_L2;
					if (pstate.tunnel_state ==
							IDPF_TUN_IP_GRE) {
						ptype_lkup[k].tunnel_type =
						LIBETH_RX_PT_TUNNEL_IP_GRENAT_MAC;
						pstate.tunnel_state |=
						IDPF_PTYPE_TUNNEL_IP_GRENAT_MAC;
					}
					break;
				case VIRTCHNL2_PROTO_HDR_IPV4:
					idpf_fill_ptype_lookup(&ptype_lkup[k],
							       &pstate, true,
							       false);
					break;
				case VIRTCHNL2_PROTO_HDR_IPV6:
					idpf_fill_ptype_lookup(&ptype_lkup[k],
							       &pstate, false,
							       false);
					break;
				case VIRTCHNL2_PROTO_HDR_IPV4_FRAG:
					idpf_fill_ptype_lookup(&ptype_lkup[k],
							       &pstate, true,
							       true);
					break;
				case VIRTCHNL2_PROTO_HDR_IPV6_FRAG:
					idpf_fill_ptype_lookup(&ptype_lkup[k],
							       &pstate, false,
							       true);
					break;
				case VIRTCHNL2_PROTO_HDR_UDP:
					ptype_lkup[k].inner_prot =
					LIBETH_RX_PT_INNER_UDP;
					break;
				case VIRTCHNL2_PROTO_HDR_TCP:
					ptype_lkup[k].inner_prot =
					LIBETH_RX_PT_INNER_TCP;
					break;
				case VIRTCHNL2_PROTO_HDR_SCTP:
					ptype_lkup[k].inner_prot =
					LIBETH_RX_PT_INNER_SCTP;
					break;
				case VIRTCHNL2_PROTO_HDR_ICMP:
					ptype_lkup[k].inner_prot =
					LIBETH_RX_PT_INNER_ICMP;
					break;
				case VIRTCHNL2_PROTO_HDR_PAY:
					ptype_lkup[k].payload_layer =
						LIBETH_RX_PT_PAYLOAD_L2;
					break;
				case VIRTCHNL2_PROTO_HDR_ICMPV6:
				case VIRTCHNL2_PROTO_HDR_IPV6_EH:
				case VIRTCHNL2_PROTO_HDR_PRE_MAC:
				case VIRTCHNL2_PROTO_HDR_POST_MAC:
				case VIRTCHNL2_PROTO_HDR_ETHERTYPE:
				case VIRTCHNL2_PROTO_HDR_SVLAN:
				case VIRTCHNL2_PROTO_HDR_CVLAN:
				case VIRTCHNL2_PROTO_HDR_MPLS:
				case VIRTCHNL2_PROTO_HDR_MMPLS:
				case VIRTCHNL2_PROTO_HDR_PTP:
				case VIRTCHNL2_PROTO_HDR_CTRL:
				case VIRTCHNL2_PROTO_HDR_LLDP:
				case VIRTCHNL2_PROTO_HDR_ARP:
				case VIRTCHNL2_PROTO_HDR_ECP:
				case VIRTCHNL2_PROTO_HDR_EAPOL:
				case VIRTCHNL2_PROTO_HDR_PPPOD:
				case VIRTCHNL2_PROTO_HDR_PPPOE:
				case VIRTCHNL2_PROTO_HDR_IGMP:
				case VIRTCHNL2_PROTO_HDR_AH:
				case VIRTCHNL2_PROTO_HDR_ESP:
				case VIRTCHNL2_PROTO_HDR_IKE:
				case VIRTCHNL2_PROTO_HDR_NATT_KEEP:
				case VIRTCHNL2_PROTO_HDR_L2TPV2:
				case VIRTCHNL2_PROTO_HDR_L2TPV2_CONTROL:
				case VIRTCHNL2_PROTO_HDR_L2TPV3:
				case VIRTCHNL2_PROTO_HDR_GTP:
				case VIRTCHNL2_PROTO_HDR_GTP_EH:
				case VIRTCHNL2_PROTO_HDR_GTPCV2:
				case VIRTCHNL2_PROTO_HDR_GTPC_TEID:
				case VIRTCHNL2_PROTO_HDR_GTPU:
				case VIRTCHNL2_PROTO_HDR_GTPU_UL:
				case VIRTCHNL2_PROTO_HDR_GTPU_DL:
				case VIRTCHNL2_PROTO_HDR_ECPRI:
				case VIRTCHNL2_PROTO_HDR_VRRP:
				case VIRTCHNL2_PROTO_HDR_OSPF:
				case VIRTCHNL2_PROTO_HDR_TUN:
				case VIRTCHNL2_PROTO_HDR_NVGRE:
				case VIRTCHNL2_PROTO_HDR_VXLAN:
				case VIRTCHNL2_PROTO_HDR_VXLAN_GPE:
				case VIRTCHNL2_PROTO_HDR_GENEVE:
				case VIRTCHNL2_PROTO_HDR_NSH:
				case VIRTCHNL2_PROTO_HDR_QUIC:
				case VIRTCHNL2_PROTO_HDR_PFCP:
				case VIRTCHNL2_PROTO_HDR_PFCP_NODE:
				case VIRTCHNL2_PROTO_HDR_PFCP_SESSION:
				case VIRTCHNL2_PROTO_HDR_RTP:
				case VIRTCHNL2_PROTO_HDR_NO_PROTO:
					break;
				default:
					break;
				}
			}

			idpf_finalize_ptype_lookup(&ptype_lkup[k]);
		}
	}

out:
	vport->rx_ptype_lkup = no_free_ptr(ptype_lkup);

	return 0;
}

/**
 * idpf_send_ena_dis_loopback_msg - Send virtchnl enable/disable loopback
 *				    message
 * @vport: virtual port data structure
 *
 * Returns 0 on success, negative on failure.
 */
int idpf_send_ena_dis_loopback_msg(struct idpf_vport *vport)
{
	struct idpf_vc_xn_params xn_params = {};
	struct virtchnl2_loopback loopback;
	ssize_t reply_sz;

	loopback.vport_id = cpu_to_le32(vport->vport_id);
	loopback.enable = idpf_is_feature_ena(vport, NETIF_F_LOOPBACK);

	xn_params.vc_op = VIRTCHNL2_OP_LOOPBACK;
	xn_params.timeout_ms = IDPF_VC_XN_DEFAULT_TIMEOUT_MSEC;
	xn_params.send_buf.iov_base = &loopback;
	xn_params.send_buf.iov_len = sizeof(loopback);
	reply_sz = idpf_vc_xn_exec(vport->adapter, &xn_params);

	return reply_sz < 0 ? reply_sz : 0;
}

/**
 * idpf_find_ctlq - Given a type and id, find ctlq info
 * @hw: hardware struct
 * @type: type of ctrlq to find
 * @id: ctlq id to find
 *
 * Returns pointer to found ctlq info struct, NULL otherwise.
 */
static struct idpf_ctlq_info *idpf_find_ctlq(struct idpf_hw *hw,
					     enum idpf_ctlq_type type, int id)
{
	struct idpf_ctlq_info *cq, *tmp;

	list_for_each_entry_safe(cq, tmp, &hw->cq_list_head, cq_list)
		if (cq->q_id == id && cq->cq_type == type)
			return cq;

	return NULL;
}

/**
 * idpf_init_dflt_mbx - Setup default mailbox parameters and make request
 * @adapter: adapter info struct
 *
 * Returns 0 on success, negative otherwise
 */
int idpf_init_dflt_mbx(struct idpf_adapter *adapter)
{
	struct idpf_ctlq_create_info ctlq_info[] = {
		{
			.type = IDPF_CTLQ_TYPE_MAILBOX_TX,
			.id = IDPF_DFLT_MBX_ID,
			.len = IDPF_DFLT_MBX_Q_LEN,
			.buf_size = IDPF_CTLQ_MAX_BUF_LEN
		},
		{
			.type = IDPF_CTLQ_TYPE_MAILBOX_RX,
			.id = IDPF_DFLT_MBX_ID,
			.len = IDPF_DFLT_MBX_Q_LEN,
			.buf_size = IDPF_CTLQ_MAX_BUF_LEN
		}
	};
	struct idpf_hw *hw = &adapter->hw;
	int err;

	adapter->dev_ops.reg_ops.ctlq_reg_init(adapter, ctlq_info);

	err = idpf_ctlq_init(hw, IDPF_NUM_DFLT_MBX_Q, ctlq_info);
	if (err)
		return err;

	hw->asq = idpf_find_ctlq(hw, IDPF_CTLQ_TYPE_MAILBOX_TX,
				 IDPF_DFLT_MBX_ID);
	hw->arq = idpf_find_ctlq(hw, IDPF_CTLQ_TYPE_MAILBOX_RX,
				 IDPF_DFLT_MBX_ID);

	if (!hw->asq || !hw->arq) {
		idpf_ctlq_deinit(hw);

		return -ENOENT;
	}

	adapter->state = __IDPF_VER_CHECK;

	return 0;
}

/**
 * idpf_deinit_dflt_mbx - Free up ctlqs setup
 * @adapter: Driver specific private data structure
 */
void idpf_deinit_dflt_mbx(struct idpf_adapter *adapter)
{
	if (adapter->hw.arq && adapter->hw.asq) {
		idpf_mb_clean(adapter);
		idpf_ctlq_deinit(&adapter->hw);
	}
	adapter->hw.arq = NULL;
	adapter->hw.asq = NULL;
}

/**
 * idpf_vport_params_buf_rel - Release memory for MailBox resources
 * @adapter: Driver specific private data structure
 *
 * Will release memory to hold the vport parameters received on MailBox
 */
static void idpf_vport_params_buf_rel(struct idpf_adapter *adapter)
{
	kfree(adapter->vport_params_recvd);
	adapter->vport_params_recvd = NULL;
	kfree(adapter->vport_params_reqd);
	adapter->vport_params_reqd = NULL;
	kfree(adapter->vport_ids);
	adapter->vport_ids = NULL;
}

/**
 * idpf_vport_params_buf_alloc - Allocate memory for MailBox resources
 * @adapter: Driver specific private data structure
 *
 * Will alloc memory to hold the vport parameters received on MailBox
 */
static int idpf_vport_params_buf_alloc(struct idpf_adapter *adapter)
{
	u16 num_max_vports = idpf_get_max_vports(adapter);

	adapter->vport_params_reqd = kcalloc(num_max_vports,
					     sizeof(*adapter->vport_params_reqd),
					     GFP_KERNEL);
	if (!adapter->vport_params_reqd)
		return -ENOMEM;

	adapter->vport_params_recvd = kcalloc(num_max_vports,
					      sizeof(*adapter->vport_params_recvd),
					      GFP_KERNEL);
	if (!adapter->vport_params_recvd)
		goto err_mem;

	adapter->vport_ids = kcalloc(num_max_vports, sizeof(u32), GFP_KERNEL);
	if (!adapter->vport_ids)
		goto err_mem;

	if (adapter->vport_config)
		return 0;

	adapter->vport_config = kcalloc(num_max_vports,
					sizeof(*adapter->vport_config),
					GFP_KERNEL);
	if (!adapter->vport_config)
		goto err_mem;

	return 0;

err_mem:
	idpf_vport_params_buf_rel(adapter);

	return -ENOMEM;
}

/**
 * idpf_vc_core_init - Initialize state machine and get driver specific
 * resources
 * @adapter: Driver specific private structure
 *
 * This function will initialize the state machine and request all necessary
 * resources required by the device driver. Once the state machine is
 * initialized, allocate memory to store vport specific information and also
 * requests required interrupts.
 *
 * Returns 0 on success, -EAGAIN function will get called again,
 * otherwise negative on failure.
 */
int idpf_vc_core_init(struct idpf_adapter *adapter)
{
	int task_delay = 30;
	u16 num_max_vports;
	int err = 0;

	if (!adapter->vcxn_mngr) {
		adapter->vcxn_mngr = kzalloc(sizeof(*adapter->vcxn_mngr), GFP_KERNEL);
		if (!adapter->vcxn_mngr) {
			err = -ENOMEM;
			goto init_failed;
		}
	}
	idpf_vc_xn_init(adapter->vcxn_mngr);

	while (adapter->state != __IDPF_INIT_SW) {
		switch (adapter->state) {
		case __IDPF_VER_CHECK:
			err = idpf_send_ver_msg(adapter);
			switch (err) {
			case 0:
				/* success, move state machine forward */
				adapter->state = __IDPF_GET_CAPS;
				fallthrough;
			case -EAGAIN:
				goto restart;
			default:
				/* Something bad happened, try again but only a
				 * few times.
				 */
				goto init_failed;
			}
		case __IDPF_GET_CAPS:
			err = idpf_send_get_caps_msg(adapter);
			if (err)
				goto init_failed;
			adapter->state = __IDPF_INIT_SW;
			break;
		default:
			dev_err(&adapter->pdev->dev, "Device is in bad state: %d\n",
				adapter->state);
			err = -EINVAL;
			goto init_failed;
		}
		break;
restart:
		/* Give enough time before proceeding further with
		 * state machine
		 */
		msleep(task_delay);
	}

	if (idpf_is_cap_ena(adapter, IDPF_OTHER_CAPS, VIRTCHNL2_CAP_LAN_MEMORY_REGIONS)) {
		err = idpf_send_get_lan_memory_regions(adapter);
		if (err) {
			dev_err(&adapter->pdev->dev, "Failed to get LAN memory regions: %d\n",
				err);
			return -EINVAL;
		}
	} else {
		/* Fallback to mapping the remaining regions of the entire BAR */
		err = idpf_calc_remaining_mmio_regs(adapter);
		if (err) {
			dev_err(&adapter->pdev->dev, "Failed to allocate BAR0 region(s): %d\n",
				err);
			return -ENOMEM;
		}
	}

	err = idpf_map_lan_mmio_regs(adapter);
	if (err) {
		dev_err(&adapter->pdev->dev, "Failed to map BAR0 region(s): %d\n",
			err);
		return -ENOMEM;
	}

	pci_sriov_set_totalvfs(adapter->pdev, idpf_get_max_vfs(adapter));
	num_max_vports = idpf_get_max_vports(adapter);
	adapter->max_vports = num_max_vports;
	adapter->vports = kcalloc(num_max_vports, sizeof(*adapter->vports),
				  GFP_KERNEL);
	if (!adapter->vports)
		return -ENOMEM;

	if (!adapter->netdevs) {
		adapter->netdevs = kcalloc(num_max_vports,
					   sizeof(struct net_device *),
					   GFP_KERNEL);
		if (!adapter->netdevs) {
			err = -ENOMEM;
			goto err_netdev_alloc;
		}
	}

	err = idpf_vport_params_buf_alloc(adapter);
	if (err) {
		dev_err(&adapter->pdev->dev, "Failed to alloc vport params buffer: %d\n",
			err);
		goto err_netdev_alloc;
	}

	/* Start the mailbox task before requesting vectors. This will ensure
	 * vector information response from mailbox is handled
	 */
	queue_delayed_work(adapter->mbx_wq, &adapter->mbx_task, 0);

	queue_delayed_work(adapter->serv_wq, &adapter->serv_task,
			   msecs_to_jiffies(5 * (adapter->pdev->devfn & 0x07)));

	err = idpf_intr_req(adapter);
	if (err) {
		dev_err(&adapter->pdev->dev, "failed to enable interrupt vectors: %d\n",
			err);
		goto err_intr_req;
	}

	err = idpf_ptp_init(adapter);
	if (err)
		pci_err(adapter->pdev, "PTP init failed, err=%pe\n",
			ERR_PTR(err));

	idpf_init_avail_queues(adapter);

	/* Skew the delay for init tasks for each function based on fn number
	 * to prevent every function from making the same call simultaneously.
	 */
	queue_delayed_work(adapter->init_wq, &adapter->init_task,
			   msecs_to_jiffies(5 * (adapter->pdev->devfn & 0x07)));

	set_bit(IDPF_VC_CORE_INIT, adapter->flags);

	return 0;

err_intr_req:
	cancel_delayed_work_sync(&adapter->serv_task);
	cancel_delayed_work_sync(&adapter->mbx_task);
	idpf_vport_params_buf_rel(adapter);
err_netdev_alloc:
	kfree(adapter->vports);
	adapter->vports = NULL;
	return err;

init_failed:
	/* Don't retry if we're trying to go down, just bail. */
	if (test_bit(IDPF_REMOVE_IN_PROG, adapter->flags))
		return err;

	if (++adapter->mb_wait_count > IDPF_MB_MAX_ERR) {
		dev_err(&adapter->pdev->dev, "Failed to establish mailbox communications with hardware\n");

		return -EFAULT;
	}
	/* If it reached here, it is possible that mailbox queue initialization
	 * register writes might not have taken effect. Retry to initialize
	 * the mailbox again
	 */
	adapter->state = __IDPF_VER_CHECK;
	if (adapter->vcxn_mngr)
		idpf_vc_xn_shutdown(adapter->vcxn_mngr);
	set_bit(IDPF_HR_DRV_LOAD, adapter->flags);
	queue_delayed_work(adapter->vc_event_wq, &adapter->vc_event_task,
			   msecs_to_jiffies(task_delay));

	return -EAGAIN;
}

/**
 * idpf_vc_core_deinit - Device deinit routine
 * @adapter: Driver specific private structure
 *
 */
void idpf_vc_core_deinit(struct idpf_adapter *adapter)
{
	bool remove_in_prog;

	if (!test_bit(IDPF_VC_CORE_INIT, adapter->flags))
		return;

	/* Avoid transaction timeouts when called during reset */
	remove_in_prog = test_bit(IDPF_REMOVE_IN_PROG, adapter->flags);
	if (!remove_in_prog)
		idpf_vc_xn_shutdown(adapter->vcxn_mngr);

	idpf_ptp_release(adapter);
	idpf_deinit_task(adapter);
	idpf_idc_deinit_core_aux_device(adapter->cdev_info);
	idpf_intr_rel(adapter);

	if (remove_in_prog)
		idpf_vc_xn_shutdown(adapter->vcxn_mngr);

	cancel_delayed_work_sync(&adapter->serv_task);
	cancel_delayed_work_sync(&adapter->mbx_task);

	idpf_vport_params_buf_rel(adapter);

	kfree(adapter->vports);
	adapter->vports = NULL;

	clear_bit(IDPF_VC_CORE_INIT, adapter->flags);
}

/**
 * idpf_vport_alloc_vec_indexes - Get relative vector indexes
 * @vport: virtual port data struct
 *
 * This function requests the vector information required for the vport and
 * stores the vector indexes received from the 'global vector distribution'
 * in the vport's queue vectors array.
 *
 * Return 0 on success, error on failure
 */
int idpf_vport_alloc_vec_indexes(struct idpf_vport *vport)
{
	struct idpf_vector_info vec_info;
	int num_alloc_vecs;
	u32 req;

	vec_info.num_curr_vecs = vport->num_q_vectors;
	if (vec_info.num_curr_vecs)
		vec_info.num_curr_vecs += IDPF_RESERVED_VECS;

	/* XDPSQs are all bound to the NOIRQ vector from IDPF_RESERVED_VECS */
	req = max(vport->num_txq - vport->num_xdp_txq, vport->num_rxq) +
	      IDPF_RESERVED_VECS;
	vec_info.num_req_vecs = req;

	vec_info.default_vport = vport->default_vport;
	vec_info.index = vport->idx;

	num_alloc_vecs = idpf_req_rel_vector_indexes(vport->adapter,
						     vport->q_vector_idxs,
						     &vec_info);
	if (num_alloc_vecs <= 0) {
		dev_err(&vport->adapter->pdev->dev, "Vector distribution failed: %d\n",
			num_alloc_vecs);
		return -EINVAL;
	}

	vport->num_q_vectors = num_alloc_vecs - IDPF_RESERVED_VECS;

	return 0;
}

/**
 * idpf_vport_init - Initialize virtual port
 * @vport: virtual port to be initialized
 * @max_q: vport max queue info
 *
 * Will initialize vport with the info received through MB earlier
 */
void idpf_vport_init(struct idpf_vport *vport, struct idpf_vport_max_q *max_q)
{
	struct idpf_adapter *adapter = vport->adapter;
	struct virtchnl2_create_vport *vport_msg;
	struct idpf_vport_config *vport_config;
	u16 tx_itr[] = {2, 8, 64, 128, 256};
	u16 rx_itr[] = {2, 8, 32, 96, 128};
	struct idpf_rss_data *rss_data;
	u16 idx = vport->idx;
	int err;

	vport_config = adapter->vport_config[idx];
	rss_data = &vport_config->user_config.rss_data;
	vport_msg = adapter->vport_params_recvd[idx];

	vport_config->max_q.max_txq = max_q->max_txq;
	vport_config->max_q.max_rxq = max_q->max_rxq;
	vport_config->max_q.max_complq = max_q->max_complq;
	vport_config->max_q.max_bufq = max_q->max_bufq;

	vport->txq_model = le16_to_cpu(vport_msg->txq_model);
	vport->rxq_model = le16_to_cpu(vport_msg->rxq_model);
	vport->vport_type = le16_to_cpu(vport_msg->vport_type);
	vport->vport_id = le32_to_cpu(vport_msg->vport_id);

	rss_data->rss_key_size = min_t(u16, NETDEV_RSS_KEY_LEN,
				       le16_to_cpu(vport_msg->rss_key_size));
	rss_data->rss_lut_size = le16_to_cpu(vport_msg->rss_lut_size);

	ether_addr_copy(vport->default_mac_addr, vport_msg->default_mac_addr);
	vport->max_mtu = le16_to_cpu(vport_msg->max_mtu) - LIBETH_RX_LL_LEN;

	/* Initialize Tx and Rx profiles for Dynamic Interrupt Moderation */
	memcpy(vport->rx_itr_profile, rx_itr, IDPF_DIM_PROFILE_SLOTS);
	memcpy(vport->tx_itr_profile, tx_itr, IDPF_DIM_PROFILE_SLOTS);

	idpf_vport_set_hsplit(vport, ETHTOOL_TCP_DATA_SPLIT_ENABLED);

	idpf_vport_init_num_qs(vport, vport_msg);
	idpf_vport_calc_num_q_desc(vport);
	idpf_vport_calc_num_q_groups(vport);
	idpf_vport_alloc_vec_indexes(vport);

	vport->crc_enable = adapter->crc_enable;

	if (!(vport_msg->vport_flags &
	      cpu_to_le16(VIRTCHNL2_VPORT_UPLINK_PORT)))
		return;

	err = idpf_ptp_get_vport_tstamps_caps(vport);
	if (err) {
		pci_dbg(vport->adapter->pdev, "Tx timestamping not supported\n");
		return;
	}

	INIT_WORK(&vport->tstamp_task, idpf_tstamp_task);
}

/**
 * idpf_get_vec_ids - Initialize vector id from Mailbox parameters
 * @adapter: adapter structure to get the mailbox vector id
 * @vecids: Array of vector ids
 * @num_vecids: number of vector ids
 * @chunks: vector ids received over mailbox
 *
 * Will initialize the mailbox vector id which is received from the
 * get capabilities and data queue vector ids with ids received as
 * mailbox parameters.
 * Returns number of ids filled
 */
int idpf_get_vec_ids(struct idpf_adapter *adapter,
		     u16 *vecids, int num_vecids,
		     struct virtchnl2_vector_chunks *chunks)
{
	u16 num_chunks = le16_to_cpu(chunks->num_vchunks);
	int num_vecid_filled = 0;
	int i, j;

	vecids[num_vecid_filled] = adapter->mb_vector.v_idx;
	num_vecid_filled++;

	for (j = 0; j < num_chunks; j++) {
		struct virtchnl2_vector_chunk *chunk;
		u16 start_vecid, num_vec;

		chunk = &chunks->vchunks[j];
		num_vec = le16_to_cpu(chunk->num_vectors);
		start_vecid = le16_to_cpu(chunk->start_vector_id);

		for (i = 0; i < num_vec; i++) {
			if ((num_vecid_filled + i) < num_vecids) {
				vecids[num_vecid_filled + i] = start_vecid;
				start_vecid++;
			} else {
				break;
			}
		}
		num_vecid_filled = num_vecid_filled + i;
	}

	return num_vecid_filled;
}

/**
 * idpf_vport_get_queue_ids - Initialize queue id from Mailbox parameters
 * @qids: Array of queue ids
 * @num_qids: number of queue ids
 * @q_type: queue model
 * @chunks: queue ids received over mailbox
 *
 * Will initialize all queue ids with ids received as mailbox parameters
 * Returns number of ids filled
 */
static int idpf_vport_get_queue_ids(u32 *qids, int num_qids, u16 q_type,
				    struct virtchnl2_queue_reg_chunks *chunks)
{
	u16 num_chunks = le16_to_cpu(chunks->num_chunks);
	u32 num_q_id_filled = 0, i;
	u32 start_q_id, num_q;

	while (num_chunks--) {
		struct virtchnl2_queue_reg_chunk *chunk;

		chunk = &chunks->chunks[num_chunks];
		if (le32_to_cpu(chunk->type) != q_type)
			continue;

		num_q = le32_to_cpu(chunk->num_queues);
		start_q_id = le32_to_cpu(chunk->start_queue_id);

		for (i = 0; i < num_q; i++) {
			if ((num_q_id_filled + i) < num_qids) {
				qids[num_q_id_filled + i] = start_q_id;
				start_q_id++;
			} else {
				break;
			}
		}
		num_q_id_filled = num_q_id_filled + i;
	}

	return num_q_id_filled;
}

/**
 * __idpf_vport_queue_ids_init - Initialize queue ids from Mailbox parameters
 * @vport: virtual port for which the queues ids are initialized
 * @qids: queue ids
 * @num_qids: number of queue ids
 * @q_type: type of queue
 *
 * Will initialize all queue ids with ids received as mailbox
 * parameters. Returns number of queue ids initialized.
 */
static int __idpf_vport_queue_ids_init(struct idpf_vport *vport,
				       const u32 *qids,
				       int num_qids,
				       u32 q_type)
{
	int i, j, k = 0;

	switch (q_type) {
	case VIRTCHNL2_QUEUE_TYPE_TX:
		for (i = 0; i < vport->num_txq_grp; i++) {
			struct idpf_txq_group *tx_qgrp = &vport->txq_grps[i];

			for (j = 0; j < tx_qgrp->num_txq && k < num_qids; j++, k++)
				tx_qgrp->txqs[j]->q_id = qids[k];
		}
		break;
	case VIRTCHNL2_QUEUE_TYPE_RX:
		for (i = 0; i < vport->num_rxq_grp; i++) {
			struct idpf_rxq_group *rx_qgrp = &vport->rxq_grps[i];
			u16 num_rxq;

			if (idpf_is_queue_model_split(vport->rxq_model))
				num_rxq = rx_qgrp->splitq.num_rxq_sets;
			else
				num_rxq = rx_qgrp->singleq.num_rxq;

			for (j = 0; j < num_rxq && k < num_qids; j++, k++) {
				struct idpf_rx_queue *q;

				if (idpf_is_queue_model_split(vport->rxq_model))
					q = &rx_qgrp->splitq.rxq_sets[j]->rxq;
				else
					q = rx_qgrp->singleq.rxqs[j];
				q->q_id = qids[k];
			}
		}
		break;
	case VIRTCHNL2_QUEUE_TYPE_TX_COMPLETION:
		for (i = 0; i < vport->num_txq_grp && k < num_qids; i++, k++) {
			struct idpf_txq_group *tx_qgrp = &vport->txq_grps[i];

			tx_qgrp->complq->q_id = qids[k];
		}
		break;
	case VIRTCHNL2_QUEUE_TYPE_RX_BUFFER:
		for (i = 0; i < vport->num_rxq_grp; i++) {
			struct idpf_rxq_group *rx_qgrp = &vport->rxq_grps[i];
			u8 num_bufqs = vport->num_bufqs_per_qgrp;

			for (j = 0; j < num_bufqs && k < num_qids; j++, k++) {
				struct idpf_buf_queue *q;

				q = &rx_qgrp->splitq.bufq_sets[j].bufq;
				q->q_id = qids[k];
			}
		}
		break;
	default:
		break;
	}

	return k;
}

/**
 * idpf_vport_queue_ids_init - Initialize queue ids from Mailbox parameters
 * @vport: virtual port for which the queues ids are initialized
 *
 * Will initialize all queue ids with ids received as mailbox parameters.
 * Returns 0 on success, negative if all the queues are not initialized.
 */
int idpf_vport_queue_ids_init(struct idpf_vport *vport)
{
	struct virtchnl2_create_vport *vport_params;
	struct virtchnl2_queue_reg_chunks *chunks;
	struct idpf_vport_config *vport_config;
	u16 vport_idx = vport->idx;
	int num_ids, err = 0;
	u16 q_type;
	u32 *qids;

	vport_config = vport->adapter->vport_config[vport_idx];
	if (vport_config->req_qs_chunks) {
		struct virtchnl2_add_queues *vc_aq =
			(struct virtchnl2_add_queues *)vport_config->req_qs_chunks;
		chunks = &vc_aq->chunks;
	} else {
		vport_params = vport->adapter->vport_params_recvd[vport_idx];
		chunks = &vport_params->chunks;
	}

	qids = kcalloc(IDPF_MAX_QIDS, sizeof(u32), GFP_KERNEL);
	if (!qids)
		return -ENOMEM;

	num_ids = idpf_vport_get_queue_ids(qids, IDPF_MAX_QIDS,
					   VIRTCHNL2_QUEUE_TYPE_TX,
					   chunks);
	if (num_ids < vport->num_txq) {
		err = -EINVAL;
		goto mem_rel;
	}
	num_ids = __idpf_vport_queue_ids_init(vport, qids, num_ids,
					      VIRTCHNL2_QUEUE_TYPE_TX);
	if (num_ids < vport->num_txq) {
		err = -EINVAL;
		goto mem_rel;
	}

	num_ids = idpf_vport_get_queue_ids(qids, IDPF_MAX_QIDS,
					   VIRTCHNL2_QUEUE_TYPE_RX,
					   chunks);
	if (num_ids < vport->num_rxq) {
		err = -EINVAL;
		goto mem_rel;
	}
	num_ids = __idpf_vport_queue_ids_init(vport, qids, num_ids,
					      VIRTCHNL2_QUEUE_TYPE_RX);
	if (num_ids < vport->num_rxq) {
		err = -EINVAL;
		goto mem_rel;
	}

	if (!idpf_is_queue_model_split(vport->txq_model))
		goto check_rxq;

	q_type = VIRTCHNL2_QUEUE_TYPE_TX_COMPLETION;
	num_ids = idpf_vport_get_queue_ids(qids, IDPF_MAX_QIDS, q_type, chunks);
	if (num_ids < vport->num_complq) {
		err = -EINVAL;
		goto mem_rel;
	}
	num_ids = __idpf_vport_queue_ids_init(vport, qids, num_ids, q_type);
	if (num_ids < vport->num_complq) {
		err = -EINVAL;
		goto mem_rel;
	}

check_rxq:
	if (!idpf_is_queue_model_split(vport->rxq_model))
		goto mem_rel;

	q_type = VIRTCHNL2_QUEUE_TYPE_RX_BUFFER;
	num_ids = idpf_vport_get_queue_ids(qids, IDPF_MAX_QIDS, q_type, chunks);
	if (num_ids < vport->num_bufq) {
		err = -EINVAL;
		goto mem_rel;
	}
	num_ids = __idpf_vport_queue_ids_init(vport, qids, num_ids, q_type);
	if (num_ids < vport->num_bufq)
		err = -EINVAL;

mem_rel:
	kfree(qids);

	return err;
}

/**
 * idpf_vport_adjust_qs - Adjust to new requested queues
 * @vport: virtual port data struct
 *
 * Renegotiate queues.  Returns 0 on success, negative on failure.
 */
int idpf_vport_adjust_qs(struct idpf_vport *vport)
{
	struct virtchnl2_create_vport vport_msg;
	int err;

	vport_msg.txq_model = cpu_to_le16(vport->txq_model);
	vport_msg.rxq_model = cpu_to_le16(vport->rxq_model);
	err = idpf_vport_calc_total_qs(vport->adapter, vport->idx, &vport_msg,
				       NULL);
	if (err)
		return err;

	idpf_vport_init_num_qs(vport, &vport_msg);
	idpf_vport_calc_num_q_groups(vport);

	return 0;
}

/**
 * idpf_is_capability_ena - Default implementation of capability checking
 * @adapter: Private data struct
 * @all: all or one flag
 * @field: caps field to check for flags
 * @flag: flag to check
 *
 * Return true if all capabilities are supported, false otherwise
 */
bool idpf_is_capability_ena(struct idpf_adapter *adapter, bool all,
			    enum idpf_cap_field field, u64 flag)
{
	u8 *caps = (u8 *)&adapter->caps;
	u32 *cap_field;

	if (!caps)
		return false;

	if (field == IDPF_BASE_CAPS)
		return false;

	cap_field = (u32 *)(caps + field);

	if (all)
		return (*cap_field & flag) == flag;
	else
		return !!(*cap_field & flag);
}

/**
 * idpf_vport_is_cap_ena - Check if vport capability is enabled
 * @vport: Private data struct
 * @flag: flag(s) to check
 *
 * Return: true if the capability is supported, false otherwise
 */
bool idpf_vport_is_cap_ena(struct idpf_vport *vport, u16 flag)
{
	struct virtchnl2_create_vport *vport_msg;

	vport_msg = vport->adapter->vport_params_recvd[vport->idx];

	return !!(le16_to_cpu(vport_msg->vport_flags) & flag);
}

/**
 * idpf_sideband_flow_type_ena - Check if steering is enabled for flow type
 * @vport: Private data struct
 * @flow_type: flow type to check (from ethtool.h)
 *
 * Return: true if sideband filters are allowed for @flow_type, false otherwise
 */
bool idpf_sideband_flow_type_ena(struct idpf_vport *vport, u32 flow_type)
{
	struct virtchnl2_create_vport *vport_msg;
	__le64 caps;

	vport_msg = vport->adapter->vport_params_recvd[vport->idx];
	caps = vport_msg->sideband_flow_caps;

	switch (flow_type) {
	case TCP_V4_FLOW:
		return !!(caps & cpu_to_le64(VIRTCHNL2_FLOW_IPV4_TCP));
	case UDP_V4_FLOW:
		return !!(caps & cpu_to_le64(VIRTCHNL2_FLOW_IPV4_UDP));
	default:
		return false;
	}
}

/**
 * idpf_sideband_action_ena - Check if steering is enabled for action
 * @vport: Private data struct
 * @fsp: flow spec
 *
 * Return: true if sideband filters are allowed for @fsp, false otherwise
 */
bool idpf_sideband_action_ena(struct idpf_vport *vport,
			      struct ethtool_rx_flow_spec *fsp)
{
	struct virtchnl2_create_vport *vport_msg;
	unsigned int supp_actions;

	vport_msg = vport->adapter->vport_params_recvd[vport->idx];
	supp_actions = le32_to_cpu(vport_msg->sideband_flow_actions);

	/* Actions Drop/Wake are not supported */
	if (fsp->ring_cookie == RX_CLS_FLOW_DISC ||
	    fsp->ring_cookie == RX_CLS_FLOW_WAKE)
		return false;

	return !!(supp_actions & VIRTCHNL2_ACTION_QUEUE);
}

unsigned int idpf_fsteer_max_rules(struct idpf_vport *vport)
{
	struct virtchnl2_create_vport *vport_msg;

	vport_msg = vport->adapter->vport_params_recvd[vport->idx];
	return le32_to_cpu(vport_msg->flow_steer_max_rules);
}

/**
 * idpf_get_vport_id: Get vport id
 * @vport: virtual port structure
 *
 * Return vport id from the adapter persistent data
 */
u32 idpf_get_vport_id(struct idpf_vport *vport)
{
	struct virtchnl2_create_vport *vport_msg;

	vport_msg = vport->adapter->vport_params_recvd[vport->idx];

	return le32_to_cpu(vport_msg->vport_id);
}

static void idpf_set_mac_type(struct idpf_vport *vport,
			      struct virtchnl2_mac_addr *mac_addr)
{
	bool is_primary;

	is_primary = ether_addr_equal(vport->default_mac_addr, mac_addr->addr);
	mac_addr->type = is_primary ? VIRTCHNL2_MAC_ADDR_PRIMARY :
				      VIRTCHNL2_MAC_ADDR_EXTRA;
}

/**
 * idpf_mac_filter_async_handler - Async callback for mac filters
 * @adapter: private data struct
 * @xn: transaction for message
 * @ctlq_msg: received message
 *
 * In some scenarios driver can't sleep and wait for a reply (e.g.: stack is
 * holding rtnl_lock) when adding a new mac filter. It puts us in a difficult
 * situation to deal with errors returned on the reply. The best we can
 * ultimately do is remove it from our list of mac filters and report the
 * error.
 */
static int idpf_mac_filter_async_handler(struct idpf_adapter *adapter,
					 struct idpf_vc_xn *xn,
					 const struct idpf_ctlq_msg *ctlq_msg)
{
	struct virtchnl2_mac_addr_list *ma_list;
	struct idpf_vport_config *vport_config;
	struct virtchnl2_mac_addr *mac_addr;
	struct idpf_mac_filter *f, *tmp;
	struct list_head *ma_list_head;
	struct idpf_vport *vport;
	u16 num_entries;
	int i;

	/* if success we're done, we're only here if something bad happened */
	if (!ctlq_msg->cookie.mbx.chnl_retval)
		return 0;

	/* make sure at least struct is there */
	if (xn->reply_sz < sizeof(*ma_list))
		goto invalid_payload;

	ma_list = ctlq_msg->ctx.indirect.payload->va;
	mac_addr = ma_list->mac_addr_list;
	num_entries = le16_to_cpu(ma_list->num_mac_addr);
	/* we should have received a buffer at least this big */
	if (xn->reply_sz < struct_size(ma_list, mac_addr_list, num_entries))
		goto invalid_payload;

	vport = idpf_vid_to_vport(adapter, le32_to_cpu(ma_list->vport_id));
	if (!vport)
		goto invalid_payload;

	vport_config = adapter->vport_config[le32_to_cpu(ma_list->vport_id)];
	ma_list_head = &vport_config->user_config.mac_filter_list;

	/* We can't do much to reconcile bad filters at this point, however we
	 * should at least remove them from our list one way or the other so we
	 * have some idea what good filters we have.
	 */
	spin_lock_bh(&vport_config->mac_filter_list_lock);
	list_for_each_entry_safe(f, tmp, ma_list_head, list)
		for (i = 0; i < num_entries; i++)
			if (ether_addr_equal(mac_addr[i].addr, f->macaddr))
				list_del(&f->list);
	spin_unlock_bh(&vport_config->mac_filter_list_lock);
	dev_err_ratelimited(&adapter->pdev->dev, "Received error sending MAC filter request (op %d)\n",
			    xn->vc_op);

	return 0;

invalid_payload:
	dev_err_ratelimited(&adapter->pdev->dev, "Received invalid MAC filter payload (op %d) (len %zd)\n",
			    xn->vc_op, xn->reply_sz);

	return -EINVAL;
}

/**
 * idpf_add_del_mac_filters - Add/del mac filters
 * @vport: Virtual port data structure
 * @np: Netdev private structure
 * @add: Add or delete flag
 * @async: Don't wait for return message
 *
 * Returns 0 on success, error on failure.
 **/
int idpf_add_del_mac_filters(struct idpf_vport *vport,
			     struct idpf_netdev_priv *np,
			     bool add, bool async)
{
	struct virtchnl2_mac_addr_list *ma_list __free(kfree) = NULL;
	struct virtchnl2_mac_addr *mac_addr __free(kfree) = NULL;
	struct idpf_adapter *adapter = np->adapter;
	struct idpf_vc_xn_params xn_params = {};
	struct idpf_vport_config *vport_config;
	u32 num_msgs, total_filters = 0;
	struct idpf_mac_filter *f;
	ssize_t reply_sz;
	int i = 0, k;

	xn_params.vc_op = add ? VIRTCHNL2_OP_ADD_MAC_ADDR :
				VIRTCHNL2_OP_DEL_MAC_ADDR;
	xn_params.timeout_ms = IDPF_VC_XN_DEFAULT_TIMEOUT_MSEC;
	xn_params.async = async;
	xn_params.async_handler = idpf_mac_filter_async_handler;

	vport_config = adapter->vport_config[np->vport_idx];
	spin_lock_bh(&vport_config->mac_filter_list_lock);

	/* Find the number of newly added filters */
	list_for_each_entry(f, &vport_config->user_config.mac_filter_list,
			    list) {
		if (add && f->add)
			total_filters++;
		else if (!add && f->remove)
			total_filters++;
	}

	if (!total_filters) {
		spin_unlock_bh(&vport_config->mac_filter_list_lock);

		return 0;
	}

	/* Fill all the new filters into virtchannel message */
	mac_addr = kcalloc(total_filters, sizeof(struct virtchnl2_mac_addr),
			   GFP_ATOMIC);
	if (!mac_addr) {
		spin_unlock_bh(&vport_config->mac_filter_list_lock);

		return -ENOMEM;
	}

	list_for_each_entry(f, &vport_config->user_config.mac_filter_list,
			    list) {
		if (add && f->add) {
			ether_addr_copy(mac_addr[i].addr, f->macaddr);
			idpf_set_mac_type(vport, &mac_addr[i]);
			i++;
			f->add = false;
			if (i == total_filters)
				break;
		}
		if (!add && f->remove) {
			ether_addr_copy(mac_addr[i].addr, f->macaddr);
			idpf_set_mac_type(vport, &mac_addr[i]);
			i++;
			f->remove = false;
			if (i == total_filters)
				break;
		}
	}

	spin_unlock_bh(&vport_config->mac_filter_list_lock);

	/* Chunk up the filters into multiple messages to avoid
	 * sending a control queue message buffer that is too large
	 */
	num_msgs = DIV_ROUND_UP(total_filters, IDPF_NUM_FILTERS_PER_MSG);

	for (i = 0, k = 0; i < num_msgs; i++) {
		u32 entries_size, buf_size, num_entries;

		num_entries = min_t(u32, total_filters,
				    IDPF_NUM_FILTERS_PER_MSG);
		entries_size = sizeof(struct virtchnl2_mac_addr) * num_entries;
		buf_size = struct_size(ma_list, mac_addr_list, num_entries);

		if (!ma_list || num_entries != IDPF_NUM_FILTERS_PER_MSG) {
			kfree(ma_list);
			ma_list = kzalloc(buf_size, GFP_ATOMIC);
			if (!ma_list)
				return -ENOMEM;
		} else {
			memset(ma_list, 0, buf_size);
		}

		ma_list->vport_id = cpu_to_le32(np->vport_id);
		ma_list->num_mac_addr = cpu_to_le16(num_entries);
		memcpy(ma_list->mac_addr_list, &mac_addr[k], entries_size);

		xn_params.send_buf.iov_base = ma_list;
		xn_params.send_buf.iov_len = buf_size;
		reply_sz = idpf_vc_xn_exec(adapter, &xn_params);
		if (reply_sz < 0)
			return reply_sz;

		k += num_entries;
		total_filters -= num_entries;
	}

	return 0;
}

/**
 * idpf_set_promiscuous - set promiscuous and send message to mailbox
 * @adapter: Driver specific private structure
 * @config_data: Vport specific config data
 * @vport_id: Vport identifier
 *
 * Request to enable promiscuous mode for the vport. Message is sent
 * asynchronously and won't wait for response.  Returns 0 on success, negative
 * on failure;
 */
int idpf_set_promiscuous(struct idpf_adapter *adapter,
			 struct idpf_vport_user_config_data *config_data,
			 u32 vport_id)
{
	struct idpf_vc_xn_params xn_params = {};
	struct virtchnl2_promisc_info vpi;
	ssize_t reply_sz;
	u16 flags = 0;

	if (test_bit(__IDPF_PROMISC_UC, config_data->user_flags))
		flags |= VIRTCHNL2_UNICAST_PROMISC;
	if (test_bit(__IDPF_PROMISC_MC, config_data->user_flags))
		flags |= VIRTCHNL2_MULTICAST_PROMISC;

	vpi.vport_id = cpu_to_le32(vport_id);
	vpi.flags = cpu_to_le16(flags);

	xn_params.vc_op = VIRTCHNL2_OP_CONFIG_PROMISCUOUS_MODE;
	xn_params.timeout_ms = IDPF_VC_XN_DEFAULT_TIMEOUT_MSEC;
	xn_params.send_buf.iov_base = &vpi;
	xn_params.send_buf.iov_len = sizeof(vpi);
	/* setting promiscuous is only ever done asynchronously */
	xn_params.async = true;
	reply_sz = idpf_vc_xn_exec(adapter, &xn_params);

	return reply_sz < 0 ? reply_sz : 0;
}

/**
 * idpf_idc_rdma_vc_send_sync - virtchnl send callback for IDC registered drivers
 * @cdev_info: IDC core device info pointer
 * @send_msg: message to send
 * @msg_size: size of message to send
 * @recv_msg: message to populate on reception of response
 * @recv_len: length of message copied into recv_msg or 0 on error
 *
 * Return: 0 on success or error code on failure.
 */
int idpf_idc_rdma_vc_send_sync(struct iidc_rdma_core_dev_info *cdev_info,
			       u8 *send_msg, u16 msg_size,
			       u8 *recv_msg, u16 *recv_len)
{
	struct idpf_adapter *adapter = pci_get_drvdata(cdev_info->pdev);
	struct idpf_vc_xn_params xn_params = { };
	ssize_t reply_sz;
	u16 recv_size;

	if (!recv_msg || !recv_len || msg_size > IDPF_CTLQ_MAX_BUF_LEN)
		return -EINVAL;

	recv_size = min_t(u16, *recv_len, IDPF_CTLQ_MAX_BUF_LEN);
	*recv_len = 0;
	xn_params.vc_op = VIRTCHNL2_OP_RDMA;
	xn_params.timeout_ms = IDPF_VC_XN_DEFAULT_TIMEOUT_MSEC;
	xn_params.send_buf.iov_base = send_msg;
	xn_params.send_buf.iov_len = msg_size;
	xn_params.recv_buf.iov_base = recv_msg;
	xn_params.recv_buf.iov_len = recv_size;
	reply_sz = idpf_vc_xn_exec(adapter, &xn_params);
	if (reply_sz < 0)
		return reply_sz;
	*recv_len = reply_sz;

	return 0;
}
EXPORT_SYMBOL_GPL(idpf_idc_rdma_vc_send_sync);
