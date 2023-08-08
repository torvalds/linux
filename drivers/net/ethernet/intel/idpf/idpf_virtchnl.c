// SPDX-License-Identifier: GPL-2.0-only
/* Copyright (C) 2023 Intel Corporation */

#include "idpf.h"

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

/**
 * idpf_send_mb_msg - Send message over mailbox
 * @adapter: Driver specific private structure
 * @op: virtchnl opcode
 * @msg_size: size of the payload
 * @msg: pointer to buffer holding the payload
 *
 * Will prepare the control queue message and initiates the send api
 *
 * Returns 0 on success, negative on failure
 */
int idpf_send_mb_msg(struct idpf_adapter *adapter, u32 op,
		     u16 msg_size, u8 *msg)
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
	memcpy(dma_mem->va, msg, msg_size);
	ctlq_msg->ctx.indirect.payload = dma_mem;

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

/**
 * idpf_copy_data_to_vc_buf - Copy the virtchnl response data into the buffer.
 * @adapter: driver specific private structure
 * @vport: virtual port structure
 * @ctlq_msg: msg to copy from
 * @err_enum: err bit to set on error
 *
 * Copies the payload from ctlq_msg into virtchnl buffer. Returns 0 on success,
 * negative on failure.
 */
static int idpf_copy_data_to_vc_buf(struct idpf_adapter *adapter,
				    struct idpf_vport *vport,
				    struct idpf_ctlq_msg *ctlq_msg,
				    enum idpf_vport_vc_state err_enum)
{
	if (ctlq_msg->cookie.mbx.chnl_retval) {
		set_bit(err_enum, adapter->vc_state);

		return -EINVAL;
	}

	memcpy(adapter->vc_msg, ctlq_msg->ctx.indirect.payload->va,
	       min_t(int, ctlq_msg->ctx.indirect.payload->size,
		     IDPF_CTLQ_MAX_BUF_LEN));

	return 0;
}

/**
 * idpf_recv_vchnl_op - helper function with common logic when handling the
 * reception of VIRTCHNL OPs.
 * @adapter: driver specific private structure
 * @vport: virtual port structure
 * @ctlq_msg: msg to copy from
 * @state: state bit used on timeout check
 * @err_state: err bit to set on error
 */
static void idpf_recv_vchnl_op(struct idpf_adapter *adapter,
			       struct idpf_vport *vport,
			       struct idpf_ctlq_msg *ctlq_msg,
			       enum idpf_vport_vc_state state,
			       enum idpf_vport_vc_state err_state)
{
	wait_queue_head_t *vchnl_wq = &adapter->vchnl_wq;
	int err;

	err = idpf_copy_data_to_vc_buf(adapter, vport, ctlq_msg, err_state);
	if (wq_has_sleeper(vchnl_wq)) {
		set_bit(state, adapter->vc_state);

		wake_up(vchnl_wq);
	} else {
		if (!err)
			dev_warn(&adapter->pdev->dev, "opcode %d received without waiting thread\n",
				 ctlq_msg->cookie.mbx.chnl_opcode);
		else
			/* Clear the errors since there is no sleeper to pass
			 * them on
			 */
			clear_bit(err_state, adapter->vc_state);
	}
}

/**
 * idpf_recv_mb_msg - Receive message over mailbox
 * @adapter: Driver specific private structure
 * @op: virtchannel operation code
 * @msg: Received message holding buffer
 * @msg_size: message size
 *
 * Will receive control queue message and posts the receive buffer. Returns 0
 * on success and negative on failure.
 */
int idpf_recv_mb_msg(struct idpf_adapter *adapter, u32 op,
		     void *msg, int msg_size)
{
	struct idpf_ctlq_msg ctlq_msg;
	struct idpf_dma_mem *dma_mem;
	bool work_done = false;
	int num_retry = 2000;
	u16 num_q_msg;
	int err;

	while (1) {
		int payload_size = 0;

		/* Try to get one message */
		num_q_msg = 1;
		dma_mem = NULL;
		err = idpf_ctlq_recv(adapter->hw.arq, &num_q_msg, &ctlq_msg);
		/* If no message then decide if we have to retry based on
		 * opcode
		 */
		if (err || !num_q_msg) {
			/* Increasing num_retry to consider the delayed
			 * responses because of large number of VF's mailbox
			 * messages. If the mailbox message is received from
			 * the other side, we come out of the sleep cycle
			 * immediately else we wait for more time.
			 */
			if (!op || !num_retry--)
				break;
			if (test_bit(IDPF_REMOVE_IN_PROG, adapter->flags)) {
				err = -EIO;
				break;
			}
			msleep(20);
			continue;
		}

		/* If we are here a message is received. Check if we are looking
		 * for a specific message based on opcode. If it is different
		 * ignore and post buffers
		 */
		if (op && ctlq_msg.cookie.mbx.chnl_opcode != op)
			goto post_buffs;

		if (ctlq_msg.data_len)
			payload_size = ctlq_msg.ctx.indirect.payload->size;

		/* All conditions are met. Either a message requested is
		 * received or we received a message to be processed
		 */
		switch (ctlq_msg.cookie.mbx.chnl_opcode) {
		case VIRTCHNL2_OP_VERSION:
		case VIRTCHNL2_OP_GET_CAPS:
			if (ctlq_msg.cookie.mbx.chnl_retval) {
				dev_err(&adapter->pdev->dev, "Failure initializing, vc op: %u retval: %u\n",
					ctlq_msg.cookie.mbx.chnl_opcode,
					ctlq_msg.cookie.mbx.chnl_retval);
				err = -EBADMSG;
			} else if (msg) {
				memcpy(msg, ctlq_msg.ctx.indirect.payload->va,
				       min_t(int, payload_size, msg_size));
			}
			work_done = true;
			break;
		case VIRTCHNL2_OP_ALLOC_VECTORS:
			idpf_recv_vchnl_op(adapter, NULL, &ctlq_msg,
					   IDPF_VC_ALLOC_VECTORS,
					   IDPF_VC_ALLOC_VECTORS_ERR);
			break;
		case VIRTCHNL2_OP_DEALLOC_VECTORS:
			idpf_recv_vchnl_op(adapter, NULL, &ctlq_msg,
					   IDPF_VC_DEALLOC_VECTORS,
					   IDPF_VC_DEALLOC_VECTORS_ERR);
			break;
		default:
			dev_warn(&adapter->pdev->dev,
				 "Unhandled virtchnl response %d\n",
				 ctlq_msg.cookie.mbx.chnl_opcode);
			break;
		}

post_buffs:
		if (ctlq_msg.data_len)
			dma_mem = ctlq_msg.ctx.indirect.payload;
		else
			num_q_msg = 0;

		err = idpf_ctlq_post_rx_buffs(&adapter->hw, adapter->hw.arq,
					      &num_q_msg, &dma_mem);
		/* If post failed clear the only buffer we supplied */
		if (err && dma_mem)
			dma_free_coherent(&adapter->pdev->dev, dma_mem->size,
					  dma_mem->va, dma_mem->pa);

		/* Applies only if we are looking for a specific opcode */
		if (work_done)
			break;
	}

	return err;
}

/**
 * __idpf_wait_for_event - wrapper function for wait on virtchannel response
 * @adapter: Driver private data structure
 * @vport: virtual port structure
 * @state: check on state upon timeout
 * @err_check: check if this specific error bit is set
 * @timeout: Max time to wait
 *
 * Checks if state is set upon expiry of timeout.  Returns 0 on success,
 * negative on failure.
 */
static int __idpf_wait_for_event(struct idpf_adapter *adapter,
				 struct idpf_vport *vport,
				 enum idpf_vport_vc_state state,
				 enum idpf_vport_vc_state err_check,
				 int timeout)
{
	int time_to_wait, num_waits;
	wait_queue_head_t *vchnl_wq;
	unsigned long *vc_state;

	time_to_wait = ((timeout <= IDPF_MAX_WAIT) ? timeout : IDPF_MAX_WAIT);
	num_waits = ((timeout <= IDPF_MAX_WAIT) ? 1 : timeout / IDPF_MAX_WAIT);

	vchnl_wq = &adapter->vchnl_wq;
	vc_state = adapter->vc_state;

	while (num_waits) {
		int event;

		/* If we are here and a reset is detected do not wait but
		 * return. Reset timing is out of drivers control. So
		 * while we are cleaning resources as part of reset if the
		 * underlying HW mailbox is gone, wait on mailbox messages
		 * is not meaningful
		 */
		if (idpf_is_reset_detected(adapter))
			return 0;

		event = wait_event_timeout(*vchnl_wq,
					   test_and_clear_bit(state, vc_state),
					   msecs_to_jiffies(time_to_wait));
		if (event) {
			if (test_and_clear_bit(err_check, vc_state)) {
				dev_err(&adapter->pdev->dev, "VC response error %s\n",
					idpf_vport_vc_state_str[err_check]);

				return -EINVAL;
			}

			return 0;
		}
		num_waits--;
	}

	/* Timeout occurred */
	dev_err(&adapter->pdev->dev, "VC timeout, state = %s\n",
		idpf_vport_vc_state_str[state]);

	return -ETIMEDOUT;
}

/**
 * idpf_min_wait_for_event - wait for virtchannel response
 * @adapter: Driver private data structure
 * @vport: virtual port structure
 * @state: check on state upon timeout
 * @err_check: check if this specific error bit is set
 *
 * Returns 0 on success, negative on failure.
 */
static int idpf_min_wait_for_event(struct idpf_adapter *adapter,
				   struct idpf_vport *vport,
				   enum idpf_vport_vc_state state,
				   enum idpf_vport_vc_state err_check)
{
	return __idpf_wait_for_event(adapter, vport, state, err_check,
				     IDPF_WAIT_FOR_EVENT_TIMEO_MIN);
}

/**
 * idpf_wait_for_event - wait for virtchannel response
 * @adapter: Driver private data structure
 * @vport: virtual port structure
 * @state: check on state upon timeout after 500ms
 * @err_check: check if this specific error bit is set
 *
 * Returns 0 on success, negative on failure.
 */
static int idpf_wait_for_event(struct idpf_adapter *adapter,
			       struct idpf_vport *vport,
			       enum idpf_vport_vc_state state,
			       enum idpf_vport_vc_state err_check)
{
	/* Increasing the timeout in __IDPF_INIT_SW flow to consider large
	 * number of VF's mailbox message responses. When a message is received
	 * on mailbox, this thread is woken up by the idpf_recv_mb_msg before
	 * the timeout expires. Only in the error case i.e. if no message is
	 * received on mailbox, we wait for the complete timeout which is
	 * less likely to happen.
	 */
	return __idpf_wait_for_event(adapter, vport, state, err_check,
				     IDPF_WAIT_FOR_EVENT_TIMEO);
}

/**
 * idpf_send_ver_msg - send virtchnl version message
 * @adapter: Driver specific private structure
 *
 * Send virtchnl version message.  Returns 0 on success, negative on failure.
 */
static int idpf_send_ver_msg(struct idpf_adapter *adapter)
{
	struct virtchnl2_version_info vvi;

	if (adapter->virt_ver_maj) {
		vvi.major = cpu_to_le32(adapter->virt_ver_maj);
		vvi.minor = cpu_to_le32(adapter->virt_ver_min);
	} else {
		vvi.major = cpu_to_le32(IDPF_VIRTCHNL_VERSION_MAJOR);
		vvi.minor = cpu_to_le32(IDPF_VIRTCHNL_VERSION_MINOR);
	}

	return idpf_send_mb_msg(adapter, VIRTCHNL2_OP_VERSION, sizeof(vvi),
				(u8 *)&vvi);
}

/**
 * idpf_recv_ver_msg - Receive virtchnl version message
 * @adapter: Driver specific private structure
 *
 * Receive virtchnl version message. Returns 0 on success, -EAGAIN if we need
 * to send version message again, otherwise negative on failure.
 */
static int idpf_recv_ver_msg(struct idpf_adapter *adapter)
{
	struct virtchnl2_version_info vvi;
	u32 major, minor;
	int err;

	err = idpf_recv_mb_msg(adapter, VIRTCHNL2_OP_VERSION, &vvi,
			       sizeof(vvi));
	if (err)
		return err;

	major = le32_to_cpu(vvi.major);
	minor = le32_to_cpu(vvi.minor);

	if (major > IDPF_VIRTCHNL_VERSION_MAJOR) {
		dev_warn(&adapter->pdev->dev,
			 "Virtchnl major version (%d) greater than supported\n",
			 major);

		return -EINVAL;
	}

	if (major == IDPF_VIRTCHNL_VERSION_MAJOR &&
	    minor > IDPF_VIRTCHNL_VERSION_MINOR)
		dev_warn(&adapter->pdev->dev,
			 "Virtchnl minor version (%d) didn't match\n", minor);

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
	struct virtchnl2_get_capabilities caps = { };

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
		cpu_to_le64(VIRTCHNL2_CAP_RSS_IPV4_TCP		|
			    VIRTCHNL2_CAP_RSS_IPV4_UDP		|
			    VIRTCHNL2_CAP_RSS_IPV4_SCTP		|
			    VIRTCHNL2_CAP_RSS_IPV4_OTHER	|
			    VIRTCHNL2_CAP_RSS_IPV6_TCP		|
			    VIRTCHNL2_CAP_RSS_IPV6_UDP		|
			    VIRTCHNL2_CAP_RSS_IPV6_SCTP		|
			    VIRTCHNL2_CAP_RSS_IPV6_OTHER);

	caps.hsplit_caps =
		cpu_to_le32(VIRTCHNL2_CAP_RX_HSPLIT_AT_L4V4	|
			    VIRTCHNL2_CAP_RX_HSPLIT_AT_L4V6);

	caps.rsc_caps =
		cpu_to_le32(VIRTCHNL2_CAP_RSC_IPV4_TCP		|
			    VIRTCHNL2_CAP_RSC_IPV6_TCP);

	caps.other_caps =
		cpu_to_le64(VIRTCHNL2_CAP_SRIOV			|
			    VIRTCHNL2_CAP_MACFILTER		|
			    VIRTCHNL2_CAP_SPLITQ_QSCHED		|
			    VIRTCHNL2_CAP_PROMISC		|
			    VIRTCHNL2_CAP_LOOPBACK);

	return idpf_send_mb_msg(adapter, VIRTCHNL2_OP_GET_CAPS, sizeof(caps),
				(u8 *)&caps);
}

/**
 * idpf_recv_get_caps_msg - Receive virtchnl get capabilities message
 * @adapter: Driver specific private structure
 *
 * Receive virtchnl get capabilities message. Returns 0 on success, negative on
 * failure.
 */
static int idpf_recv_get_caps_msg(struct idpf_adapter *adapter)
{
	return idpf_recv_mb_msg(adapter, VIRTCHNL2_OP_GET_CAPS, &adapter->caps,
				sizeof(struct virtchnl2_get_capabilities));
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
	struct virtchnl2_alloc_vectors *alloc_vec, *rcvd_vec;
	struct virtchnl2_alloc_vectors ac = { };
	u16 num_vchunks;
	int size, err;

	ac.num_vectors = cpu_to_le16(num_vectors);

	mutex_lock(&adapter->vc_buf_lock);

	err = idpf_send_mb_msg(adapter, VIRTCHNL2_OP_ALLOC_VECTORS,
			       sizeof(ac), (u8 *)&ac);
	if (err)
		goto error;

	err = idpf_wait_for_event(adapter, NULL, IDPF_VC_ALLOC_VECTORS,
				  IDPF_VC_ALLOC_VECTORS_ERR);
	if (err)
		goto error;

	rcvd_vec = (struct virtchnl2_alloc_vectors *)adapter->vc_msg;
	num_vchunks = le16_to_cpu(rcvd_vec->vchunks.num_vchunks);

	size = struct_size(rcvd_vec, vchunks.vchunks, num_vchunks);
	if (size > sizeof(adapter->vc_msg)) {
		err = -EINVAL;
		goto error;
	}

	kfree(adapter->req_vec_chunks);
	adapter->req_vec_chunks = NULL;
	adapter->req_vec_chunks = kmemdup(adapter->vc_msg, size, GFP_KERNEL);
	if (!adapter->req_vec_chunks) {
		err = -ENOMEM;
		goto error;
	}

	alloc_vec = adapter->req_vec_chunks;
	if (le16_to_cpu(alloc_vec->num_vectors) < num_vectors) {
		kfree(adapter->req_vec_chunks);
		adapter->req_vec_chunks = NULL;
		err = -EINVAL;
	}

error:
	mutex_unlock(&adapter->vc_buf_lock);

	return err;
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
	int buf_size, err;

	buf_size = struct_size(vcs, vchunks, le16_to_cpu(vcs->num_vchunks));

	mutex_lock(&adapter->vc_buf_lock);

	err = idpf_send_mb_msg(adapter, VIRTCHNL2_OP_DEALLOC_VECTORS, buf_size,
			       (u8 *)vcs);
	if (err)
		goto error;

	err = idpf_min_wait_for_event(adapter, NULL, IDPF_VC_DEALLOC_VECTORS,
				      IDPF_VC_DEALLOC_VECTORS_ERR);
	if (err)
		goto error;

	kfree(adapter->req_vec_chunks);
	adapter->req_vec_chunks = NULL;

error:
	mutex_unlock(&adapter->vc_buf_lock);

	return err;
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

	adapter->dev_ops.reg_ops.ctlq_reg_init(ctlq_info);

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

	adapter->state = __IDPF_STARTUP;

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
	int err = 0;

	while (adapter->state != __IDPF_INIT_SW) {
		switch (adapter->state) {
		case __IDPF_STARTUP:
			if (idpf_send_ver_msg(adapter))
				goto init_failed;
			adapter->state = __IDPF_VER_CHECK;
			goto restart;
		case __IDPF_VER_CHECK:
			err = idpf_recv_ver_msg(adapter);
			if (err == -EIO) {
				return err;
			} else if (err == -EAGAIN) {
				adapter->state = __IDPF_STARTUP;
				goto restart;
			} else if (err) {
				goto init_failed;
			}
			if (idpf_send_get_caps_msg(adapter))
				goto init_failed;
			adapter->state = __IDPF_GET_CAPS;
			goto restart;
		case __IDPF_GET_CAPS:
			if (idpf_recv_get_caps_msg(adapter))
				goto init_failed;
			adapter->state = __IDPF_INIT_SW;
			break;
		default:
			dev_err(&adapter->pdev->dev, "Device is in bad state: %d\n",
				adapter->state);
			goto init_failed;
		}
		break;
restart:
		/* Give enough time before proceeding further with
		 * state machine
		 */
		msleep(task_delay);
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

	goto no_err;

err_intr_req:
	cancel_delayed_work_sync(&adapter->serv_task);
no_err:
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
	adapter->state = __IDPF_STARTUP;
	idpf_deinit_dflt_mbx(adapter);
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
	int i;

	idpf_intr_rel(adapter);
	/* Set all bits as we dont know on which vc_state the vhnl_wq is
	 * waiting on and wakeup the virtchnl workqueue even if it is waiting
	 * for the response as we are going down
	 */
	for (i = 0; i < IDPF_VC_NBITS; i++)
		set_bit(i, adapter->vc_state);
	wake_up(&adapter->vchnl_wq);

	cancel_delayed_work_sync(&adapter->serv_task);
	cancel_delayed_work_sync(&adapter->mbx_task);
	/* Clear all the bits */
	for (i = 0; i < IDPF_VC_NBITS; i++)
		clear_bit(i, adapter->vc_state);
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
