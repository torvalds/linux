// SPDX-License-Identifier: GPL-2.0-only
/* Copyright (C) 2023 Intel Corporation */

#include "idpf_controlq.h"

/**
 * idpf_ctlq_setup_regs - initialize control queue registers
 * @cq: pointer to the specific control queue
 * @q_create_info: structs containing info for each queue to be initialized
 */
static void idpf_ctlq_setup_regs(struct idpf_ctlq_info *cq,
				 struct idpf_ctlq_create_info *q_create_info)
{
	/* set control queue registers in our local struct */
	cq->reg.head = q_create_info->reg.head;
	cq->reg.tail = q_create_info->reg.tail;
	cq->reg.len = q_create_info->reg.len;
	cq->reg.bah = q_create_info->reg.bah;
	cq->reg.bal = q_create_info->reg.bal;
	cq->reg.len_mask = q_create_info->reg.len_mask;
	cq->reg.len_ena_mask = q_create_info->reg.len_ena_mask;
	cq->reg.head_mask = q_create_info->reg.head_mask;
}

/**
 * idpf_ctlq_init_regs - Initialize control queue registers
 * @hw: pointer to hw struct
 * @cq: pointer to the specific Control queue
 * @is_rxq: true if receive control queue, false otherwise
 *
 * Initialize registers. The caller is expected to have already initialized the
 * descriptor ring memory and buffer memory
 */
static void idpf_ctlq_init_regs(struct idpf_hw *hw, struct idpf_ctlq_info *cq,
				bool is_rxq)
{
	/* Update tail to post pre-allocated buffers for rx queues */
	if (is_rxq)
		wr32(hw, cq->reg.tail, (u32)(cq->ring_size - 1));

	/* For non-Mailbox control queues only TAIL need to be set */
	if (cq->q_id != -1)
		return;

	/* Clear Head for both send or receive */
	wr32(hw, cq->reg.head, 0);

	/* set starting point */
	wr32(hw, cq->reg.bal, lower_32_bits(cq->desc_ring.pa));
	wr32(hw, cq->reg.bah, upper_32_bits(cq->desc_ring.pa));
	wr32(hw, cq->reg.len, (cq->ring_size | cq->reg.len_ena_mask));
}

/**
 * idpf_ctlq_init_rxq_bufs - populate receive queue descriptors with buf
 * @cq: pointer to the specific Control queue
 *
 * Record the address of the receive queue DMA buffers in the descriptors.
 * The buffers must have been previously allocated.
 */
static void idpf_ctlq_init_rxq_bufs(struct idpf_ctlq_info *cq)
{
	int i;

	for (i = 0; i < cq->ring_size; i++) {
		struct idpf_ctlq_desc *desc = IDPF_CTLQ_DESC(cq, i);
		struct idpf_dma_mem *bi = cq->bi.rx_buff[i];

		/* No buffer to post to descriptor, continue */
		if (!bi)
			continue;

		desc->flags =
			cpu_to_le16(IDPF_CTLQ_FLAG_BUF | IDPF_CTLQ_FLAG_RD);
		desc->opcode = 0;
		desc->datalen = cpu_to_le16(bi->size);
		desc->ret_val = 0;
		desc->v_opcode_dtype = 0;
		desc->v_retval = 0;
		desc->params.indirect.addr_high =
			cpu_to_le32(upper_32_bits(bi->pa));
		desc->params.indirect.addr_low =
			cpu_to_le32(lower_32_bits(bi->pa));
		desc->params.indirect.param0 = 0;
		desc->params.indirect.sw_cookie = 0;
		desc->params.indirect.v_flags = 0;
	}
}

/**
 * idpf_ctlq_shutdown - shutdown the CQ
 * @hw: pointer to hw struct
 * @cq: pointer to the specific Control queue
 *
 * The main shutdown routine for any controq queue
 */
static void idpf_ctlq_shutdown(struct idpf_hw *hw, struct idpf_ctlq_info *cq)
{
	mutex_lock(&cq->cq_lock);

	/* free ring buffers and the ring itself */
	idpf_ctlq_dealloc_ring_res(hw, cq);

	/* Set ring_size to 0 to indicate uninitialized queue */
	cq->ring_size = 0;

	mutex_unlock(&cq->cq_lock);
	mutex_destroy(&cq->cq_lock);
}

/**
 * idpf_ctlq_add - add one control queue
 * @hw: pointer to hardware struct
 * @qinfo: info for queue to be created
 * @cq_out: (output) double pointer to control queue to be created
 *
 * Allocate and initialize a control queue and add it to the control queue list.
 * The cq parameter will be allocated/initialized and passed back to the caller
 * if no errors occur.
 *
 * Note: idpf_ctlq_init must be called prior to any calls to idpf_ctlq_add
 */
int idpf_ctlq_add(struct idpf_hw *hw,
		  struct idpf_ctlq_create_info *qinfo,
		  struct idpf_ctlq_info **cq_out)
{
	struct idpf_ctlq_info *cq;
	bool is_rxq = false;
	int err;

	cq = kzalloc(sizeof(*cq), GFP_KERNEL);
	if (!cq)
		return -ENOMEM;

	cq->cq_type = qinfo->type;
	cq->q_id = qinfo->id;
	cq->buf_size = qinfo->buf_size;
	cq->ring_size = qinfo->len;

	cq->next_to_use = 0;
	cq->next_to_clean = 0;
	cq->next_to_post = cq->ring_size - 1;

	switch (qinfo->type) {
	case IDPF_CTLQ_TYPE_MAILBOX_RX:
		is_rxq = true;
		fallthrough;
	case IDPF_CTLQ_TYPE_MAILBOX_TX:
		err = idpf_ctlq_alloc_ring_res(hw, cq);
		break;
	default:
		err = -EBADR;
		break;
	}

	if (err)
		goto init_free_q;

	if (is_rxq) {
		idpf_ctlq_init_rxq_bufs(cq);
	} else {
		/* Allocate the array of msg pointers for TX queues */
		cq->bi.tx_msg = kcalloc(qinfo->len,
					sizeof(struct idpf_ctlq_msg *),
					GFP_KERNEL);
		if (!cq->bi.tx_msg) {
			err = -ENOMEM;
			goto init_dealloc_q_mem;
		}
	}

	idpf_ctlq_setup_regs(cq, qinfo);

	idpf_ctlq_init_regs(hw, cq, is_rxq);

	mutex_init(&cq->cq_lock);

	list_add(&cq->cq_list, &hw->cq_list_head);

	*cq_out = cq;

	return 0;

init_dealloc_q_mem:
	/* free ring buffers and the ring itself */
	idpf_ctlq_dealloc_ring_res(hw, cq);
init_free_q:
	kfree(cq);

	return err;
}

/**
 * idpf_ctlq_remove - deallocate and remove specified control queue
 * @hw: pointer to hardware struct
 * @cq: pointer to control queue to be removed
 */
void idpf_ctlq_remove(struct idpf_hw *hw,
		      struct idpf_ctlq_info *cq)
{
	list_del(&cq->cq_list);
	idpf_ctlq_shutdown(hw, cq);
	kfree(cq);
}

/**
 * idpf_ctlq_init - main initialization routine for all control queues
 * @hw: pointer to hardware struct
 * @num_q: number of queues to initialize
 * @q_info: array of structs containing info for each queue to be initialized
 *
 * This initializes any number and any type of control queues. This is an all
 * or nothing routine; if one fails, all previously allocated queues will be
 * destroyed. This must be called prior to using the individual add/remove
 * APIs.
 */
int idpf_ctlq_init(struct idpf_hw *hw, u8 num_q,
		   struct idpf_ctlq_create_info *q_info)
{
	struct idpf_ctlq_info *cq, *tmp;
	int err;
	int i;

	INIT_LIST_HEAD(&hw->cq_list_head);

	for (i = 0; i < num_q; i++) {
		struct idpf_ctlq_create_info *qinfo = q_info + i;

		err = idpf_ctlq_add(hw, qinfo, &cq);
		if (err)
			goto init_destroy_qs;
	}

	return 0;

init_destroy_qs:
	list_for_each_entry_safe(cq, tmp, &hw->cq_list_head, cq_list)
		idpf_ctlq_remove(hw, cq);

	return err;
}

/**
 * idpf_ctlq_deinit - destroy all control queues
 * @hw: pointer to hw struct
 */
void idpf_ctlq_deinit(struct idpf_hw *hw)
{
	struct idpf_ctlq_info *cq, *tmp;

	list_for_each_entry_safe(cq, tmp, &hw->cq_list_head, cq_list)
		idpf_ctlq_remove(hw, cq);
}

/**
 * idpf_ctlq_send - send command to Control Queue (CTQ)
 * @hw: pointer to hw struct
 * @cq: handle to control queue struct to send on
 * @num_q_msg: number of messages to send on control queue
 * @q_msg: pointer to array of queue messages to be sent
 *
 * The caller is expected to allocate DMAable buffers and pass them to the
 * send routine via the q_msg struct / control queue specific data struct.
 * The control queue will hold a reference to each send message until
 * the completion for that message has been cleaned.
 */
int idpf_ctlq_send(struct idpf_hw *hw, struct idpf_ctlq_info *cq,
		   u16 num_q_msg, struct idpf_ctlq_msg q_msg[])
{
	struct idpf_ctlq_desc *desc;
	int num_desc_avail;
	int err = 0;
	int i;

	mutex_lock(&cq->cq_lock);

	/* Ensure there are enough descriptors to send all messages */
	num_desc_avail = IDPF_CTLQ_DESC_UNUSED(cq);
	if (num_desc_avail == 0 || num_desc_avail < num_q_msg) {
		err = -ENOSPC;
		goto err_unlock;
	}

	for (i = 0; i < num_q_msg; i++) {
		struct idpf_ctlq_msg *msg = &q_msg[i];

		desc = IDPF_CTLQ_DESC(cq, cq->next_to_use);

		desc->opcode = cpu_to_le16(msg->opcode);
		desc->pfid_vfid = cpu_to_le16(msg->func_id);

		desc->v_opcode_dtype = cpu_to_le32(msg->cookie.mbx.chnl_opcode);
		desc->v_retval = cpu_to_le32(msg->cookie.mbx.chnl_retval);

		desc->flags = cpu_to_le16((msg->host_id & IDPF_HOST_ID_MASK) <<
					  IDPF_CTLQ_FLAG_HOST_ID_S);
		if (msg->data_len) {
			struct idpf_dma_mem *buff = msg->ctx.indirect.payload;

			desc->datalen |= cpu_to_le16(msg->data_len);
			desc->flags |= cpu_to_le16(IDPF_CTLQ_FLAG_BUF);
			desc->flags |= cpu_to_le16(IDPF_CTLQ_FLAG_RD);

			/* Update the address values in the desc with the pa
			 * value for respective buffer
			 */
			desc->params.indirect.addr_high =
				cpu_to_le32(upper_32_bits(buff->pa));
			desc->params.indirect.addr_low =
				cpu_to_le32(lower_32_bits(buff->pa));

			memcpy(&desc->params, msg->ctx.indirect.context,
			       IDPF_INDIRECT_CTX_SIZE);
		} else {
			memcpy(&desc->params, msg->ctx.direct,
			       IDPF_DIRECT_CTX_SIZE);
		}

		/* Store buffer info */
		cq->bi.tx_msg[cq->next_to_use] = msg;

		(cq->next_to_use)++;
		if (cq->next_to_use == cq->ring_size)
			cq->next_to_use = 0;
	}

	/* Force memory write to complete before letting hardware
	 * know that there are new descriptors to fetch.
	 */
	dma_wmb();

	wr32(hw, cq->reg.tail, cq->next_to_use);

err_unlock:
	mutex_unlock(&cq->cq_lock);

	return err;
}

/**
 * idpf_ctlq_clean_sq - reclaim send descriptors on HW write back for the
 * requested queue
 * @cq: pointer to the specific Control queue
 * @clean_count: (input|output) number of descriptors to clean as input, and
 * number of descriptors actually cleaned as output
 * @msg_status: (output) pointer to msg pointer array to be populated; needs
 * to be allocated by caller
 *
 * Returns an array of message pointers associated with the cleaned
 * descriptors. The pointers are to the original ctlq_msgs sent on the cleaned
 * descriptors.  The status will be returned for each; any messages that failed
 * to send will have a non-zero status. The caller is expected to free original
 * ctlq_msgs and free or reuse the DMA buffers.
 */
int idpf_ctlq_clean_sq(struct idpf_ctlq_info *cq, u16 *clean_count,
		       struct idpf_ctlq_msg *msg_status[])
{
	struct idpf_ctlq_desc *desc;
	u16 i, num_to_clean;
	u16 ntc, desc_err;

	if (*clean_count == 0)
		return 0;
	if (*clean_count > cq->ring_size)
		return -EBADR;

	mutex_lock(&cq->cq_lock);

	ntc = cq->next_to_clean;

	num_to_clean = *clean_count;

	for (i = 0; i < num_to_clean; i++) {
		/* Fetch next descriptor and check if marked as done */
		desc = IDPF_CTLQ_DESC(cq, ntc);
		if (!(le16_to_cpu(desc->flags) & IDPF_CTLQ_FLAG_DD))
			break;

		/* Ensure no other fields are read until DD flag is checked */
		dma_rmb();

		/* strip off FW internal code */
		desc_err = le16_to_cpu(desc->ret_val) & 0xff;

		msg_status[i] = cq->bi.tx_msg[ntc];
		msg_status[i]->status = desc_err;

		cq->bi.tx_msg[ntc] = NULL;

		/* Zero out any stale data */
		memset(desc, 0, sizeof(*desc));

		ntc++;
		if (ntc == cq->ring_size)
			ntc = 0;
	}

	cq->next_to_clean = ntc;

	mutex_unlock(&cq->cq_lock);

	/* Return number of descriptors actually cleaned */
	*clean_count = i;

	return 0;
}

/**
 * idpf_ctlq_post_rx_buffs - post buffers to descriptor ring
 * @hw: pointer to hw struct
 * @cq: pointer to control queue handle
 * @buff_count: (input|output) input is number of buffers caller is trying to
 * return; output is number of buffers that were not posted
 * @buffs: array of pointers to dma mem structs to be given to hardware
 *
 * Caller uses this function to return DMA buffers to the descriptor ring after
 * consuming them; buff_count will be the number of buffers.
 *
 * Note: this function needs to be called after a receive call even
 * if there are no DMA buffers to be returned, i.e. buff_count = 0,
 * buffs = NULL to support direct commands
 */
int idpf_ctlq_post_rx_buffs(struct idpf_hw *hw, struct idpf_ctlq_info *cq,
			    u16 *buff_count, struct idpf_dma_mem **buffs)
{
	struct idpf_ctlq_desc *desc;
	u16 ntp = cq->next_to_post;
	bool buffs_avail = false;
	u16 tbp = ntp + 1;
	int i = 0;

	if (*buff_count > cq->ring_size)
		return -EBADR;

	if (*buff_count > 0)
		buffs_avail = true;

	mutex_lock(&cq->cq_lock);

	if (tbp >= cq->ring_size)
		tbp = 0;

	if (tbp == cq->next_to_clean)
		/* Nothing to do */
		goto post_buffs_out;

	/* Post buffers for as many as provided or up until the last one used */
	while (ntp != cq->next_to_clean) {
		desc = IDPF_CTLQ_DESC(cq, ntp);

		if (cq->bi.rx_buff[ntp])
			goto fill_desc;
		if (!buffs_avail) {
			/* If the caller hasn't given us any buffers or
			 * there are none left, search the ring itself
			 * for an available buffer to move to this
			 * entry starting at the next entry in the ring
			 */
			tbp = ntp + 1;

			/* Wrap ring if necessary */
			if (tbp >= cq->ring_size)
				tbp = 0;

			while (tbp != cq->next_to_clean) {
				if (cq->bi.rx_buff[tbp]) {
					cq->bi.rx_buff[ntp] =
						cq->bi.rx_buff[tbp];
					cq->bi.rx_buff[tbp] = NULL;

					/* Found a buffer, no need to
					 * search anymore
					 */
					break;
				}

				/* Wrap ring if necessary */
				tbp++;
				if (tbp >= cq->ring_size)
					tbp = 0;
			}

			if (tbp == cq->next_to_clean)
				goto post_buffs_out;
		} else {
			/* Give back pointer to DMA buffer */
			cq->bi.rx_buff[ntp] = buffs[i];
			i++;

			if (i >= *buff_count)
				buffs_avail = false;
		}

fill_desc:
		desc->flags =
			cpu_to_le16(IDPF_CTLQ_FLAG_BUF | IDPF_CTLQ_FLAG_RD);

		/* Post buffers to descriptor */
		desc->datalen = cpu_to_le16(cq->bi.rx_buff[ntp]->size);
		desc->params.indirect.addr_high =
			cpu_to_le32(upper_32_bits(cq->bi.rx_buff[ntp]->pa));
		desc->params.indirect.addr_low =
			cpu_to_le32(lower_32_bits(cq->bi.rx_buff[ntp]->pa));

		ntp++;
		if (ntp == cq->ring_size)
			ntp = 0;
	}

post_buffs_out:
	/* Only update tail if buffers were actually posted */
	if (cq->next_to_post != ntp) {
		if (ntp)
			/* Update next_to_post to ntp - 1 since current ntp
			 * will not have a buffer
			 */
			cq->next_to_post = ntp - 1;
		else
			/* Wrap to end of end ring since current ntp is 0 */
			cq->next_to_post = cq->ring_size - 1;

		dma_wmb();

		wr32(hw, cq->reg.tail, cq->next_to_post);
	}

	mutex_unlock(&cq->cq_lock);

	/* return the number of buffers that were not posted */
	*buff_count = *buff_count - i;

	return 0;
}

/**
 * idpf_ctlq_recv - receive control queue message call back
 * @cq: pointer to control queue handle to receive on
 * @num_q_msg: (input|output) input number of messages that should be received;
 * output number of messages actually received
 * @q_msg: (output) array of received control queue messages on this q;
 * needs to be pre-allocated by caller for as many messages as requested
 *
 * Called by interrupt handler or polling mechanism. Caller is expected
 * to free buffers
 */
int idpf_ctlq_recv(struct idpf_ctlq_info *cq, u16 *num_q_msg,
		   struct idpf_ctlq_msg *q_msg)
{
	u16 num_to_clean, ntc, flags;
	struct idpf_ctlq_desc *desc;
	int err = 0;
	u16 i;

	/* take the lock before we start messing with the ring */
	mutex_lock(&cq->cq_lock);

	ntc = cq->next_to_clean;

	num_to_clean = *num_q_msg;

	for (i = 0; i < num_to_clean; i++) {
		/* Fetch next descriptor and check if marked as done */
		desc = IDPF_CTLQ_DESC(cq, ntc);
		flags = le16_to_cpu(desc->flags);

		if (!(flags & IDPF_CTLQ_FLAG_DD))
			break;

		/* Ensure no other fields are read until DD flag is checked */
		dma_rmb();

		q_msg[i].vmvf_type = (flags &
				      (IDPF_CTLQ_FLAG_FTYPE_VM |
				       IDPF_CTLQ_FLAG_FTYPE_PF)) >>
				       IDPF_CTLQ_FLAG_FTYPE_S;

		if (flags & IDPF_CTLQ_FLAG_ERR)
			err  = -EBADMSG;

		q_msg[i].cookie.mbx.chnl_opcode =
				le32_to_cpu(desc->v_opcode_dtype);
		q_msg[i].cookie.mbx.chnl_retval =
				le32_to_cpu(desc->v_retval);

		q_msg[i].opcode = le16_to_cpu(desc->opcode);
		q_msg[i].data_len = le16_to_cpu(desc->datalen);
		q_msg[i].status = le16_to_cpu(desc->ret_val);

		if (desc->datalen) {
			memcpy(q_msg[i].ctx.indirect.context,
			       &desc->params.indirect, IDPF_INDIRECT_CTX_SIZE);

			/* Assign pointer to dma buffer to ctlq_msg array
			 * to be given to upper layer
			 */
			q_msg[i].ctx.indirect.payload = cq->bi.rx_buff[ntc];

			/* Zero out pointer to DMA buffer info;
			 * will be repopulated by post buffers API
			 */
			cq->bi.rx_buff[ntc] = NULL;
		} else {
			memcpy(q_msg[i].ctx.direct, desc->params.raw,
			       IDPF_DIRECT_CTX_SIZE);
		}

		/* Zero out stale data in descriptor */
		memset(desc, 0, sizeof(struct idpf_ctlq_desc));

		ntc++;
		if (ntc == cq->ring_size)
			ntc = 0;
	}

	cq->next_to_clean = ntc;

	mutex_unlock(&cq->cq_lock);

	*num_q_msg = i;
	if (*num_q_msg == 0)
		err = -ENOMSG;

	return err;
}
