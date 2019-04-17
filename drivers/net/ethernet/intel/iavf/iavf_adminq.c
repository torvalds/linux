// SPDX-License-Identifier: GPL-2.0
/* Copyright(c) 2013 - 2018 Intel Corporation. */

#include "iavf_status.h"
#include "iavf_type.h"
#include "iavf_register.h"
#include "iavf_adminq.h"
#include "iavf_prototype.h"

/**
 *  iavf_adminq_init_regs - Initialize AdminQ registers
 *  @hw: pointer to the hardware structure
 *
 *  This assumes the alloc_asq and alloc_arq functions have already been called
 **/
static void iavf_adminq_init_regs(struct iavf_hw *hw)
{
	/* set head and tail registers in our local struct */
	hw->aq.asq.tail = IAVF_VF_ATQT1;
	hw->aq.asq.head = IAVF_VF_ATQH1;
	hw->aq.asq.len  = IAVF_VF_ATQLEN1;
	hw->aq.asq.bal  = IAVF_VF_ATQBAL1;
	hw->aq.asq.bah  = IAVF_VF_ATQBAH1;
	hw->aq.arq.tail = IAVF_VF_ARQT1;
	hw->aq.arq.head = IAVF_VF_ARQH1;
	hw->aq.arq.len  = IAVF_VF_ARQLEN1;
	hw->aq.arq.bal  = IAVF_VF_ARQBAL1;
	hw->aq.arq.bah  = IAVF_VF_ARQBAH1;
}

/**
 *  iavf_alloc_adminq_asq_ring - Allocate Admin Queue send rings
 *  @hw: pointer to the hardware structure
 **/
static enum iavf_status iavf_alloc_adminq_asq_ring(struct iavf_hw *hw)
{
	enum iavf_status ret_code;

	ret_code = iavf_allocate_dma_mem(hw, &hw->aq.asq.desc_buf,
					 iavf_mem_atq_ring,
					 (hw->aq.num_asq_entries *
					 sizeof(struct iavf_aq_desc)),
					 IAVF_ADMINQ_DESC_ALIGNMENT);
	if (ret_code)
		return ret_code;

	ret_code = iavf_allocate_virt_mem(hw, &hw->aq.asq.cmd_buf,
					  (hw->aq.num_asq_entries *
					  sizeof(struct iavf_asq_cmd_details)));
	if (ret_code) {
		iavf_free_dma_mem(hw, &hw->aq.asq.desc_buf);
		return ret_code;
	}

	return ret_code;
}

/**
 *  iavf_alloc_adminq_arq_ring - Allocate Admin Queue receive rings
 *  @hw: pointer to the hardware structure
 **/
static enum iavf_status iavf_alloc_adminq_arq_ring(struct iavf_hw *hw)
{
	enum iavf_status ret_code;

	ret_code = iavf_allocate_dma_mem(hw, &hw->aq.arq.desc_buf,
					 iavf_mem_arq_ring,
					 (hw->aq.num_arq_entries *
					 sizeof(struct iavf_aq_desc)),
					 IAVF_ADMINQ_DESC_ALIGNMENT);

	return ret_code;
}

/**
 *  iavf_free_adminq_asq - Free Admin Queue send rings
 *  @hw: pointer to the hardware structure
 *
 *  This assumes the posted send buffers have already been cleaned
 *  and de-allocated
 **/
static void iavf_free_adminq_asq(struct iavf_hw *hw)
{
	iavf_free_dma_mem(hw, &hw->aq.asq.desc_buf);
}

/**
 *  iavf_free_adminq_arq - Free Admin Queue receive rings
 *  @hw: pointer to the hardware structure
 *
 *  This assumes the posted receive buffers have already been cleaned
 *  and de-allocated
 **/
static void iavf_free_adminq_arq(struct iavf_hw *hw)
{
	iavf_free_dma_mem(hw, &hw->aq.arq.desc_buf);
}

/**
 *  iavf_alloc_arq_bufs - Allocate pre-posted buffers for the receive queue
 *  @hw: pointer to the hardware structure
 **/
static enum iavf_status iavf_alloc_arq_bufs(struct iavf_hw *hw)
{
	struct iavf_aq_desc *desc;
	struct iavf_dma_mem *bi;
	enum iavf_status ret_code;
	int i;

	/* We'll be allocating the buffer info memory first, then we can
	 * allocate the mapped buffers for the event processing
	 */

	/* buffer_info structures do not need alignment */
	ret_code = iavf_allocate_virt_mem(hw, &hw->aq.arq.dma_head,
					  (hw->aq.num_arq_entries *
					   sizeof(struct iavf_dma_mem)));
	if (ret_code)
		goto alloc_arq_bufs;
	hw->aq.arq.r.arq_bi = (struct iavf_dma_mem *)hw->aq.arq.dma_head.va;

	/* allocate the mapped buffers */
	for (i = 0; i < hw->aq.num_arq_entries; i++) {
		bi = &hw->aq.arq.r.arq_bi[i];
		ret_code = iavf_allocate_dma_mem(hw, bi,
						 iavf_mem_arq_buf,
						 hw->aq.arq_buf_size,
						 IAVF_ADMINQ_DESC_ALIGNMENT);
		if (ret_code)
			goto unwind_alloc_arq_bufs;

		/* now configure the descriptors for use */
		desc = IAVF_ADMINQ_DESC(hw->aq.arq, i);

		desc->flags = cpu_to_le16(IAVF_AQ_FLAG_BUF);
		if (hw->aq.arq_buf_size > IAVF_AQ_LARGE_BUF)
			desc->flags |= cpu_to_le16(IAVF_AQ_FLAG_LB);
		desc->opcode = 0;
		/* This is in accordance with Admin queue design, there is no
		 * register for buffer size configuration
		 */
		desc->datalen = cpu_to_le16((u16)bi->size);
		desc->retval = 0;
		desc->cookie_high = 0;
		desc->cookie_low = 0;
		desc->params.external.addr_high =
			cpu_to_le32(upper_32_bits(bi->pa));
		desc->params.external.addr_low =
			cpu_to_le32(lower_32_bits(bi->pa));
		desc->params.external.param0 = 0;
		desc->params.external.param1 = 0;
	}

alloc_arq_bufs:
	return ret_code;

unwind_alloc_arq_bufs:
	/* don't try to free the one that failed... */
	i--;
	for (; i >= 0; i--)
		iavf_free_dma_mem(hw, &hw->aq.arq.r.arq_bi[i]);
	iavf_free_virt_mem(hw, &hw->aq.arq.dma_head);

	return ret_code;
}

/**
 *  iavf_alloc_asq_bufs - Allocate empty buffer structs for the send queue
 *  @hw: pointer to the hardware structure
 **/
static enum iavf_status iavf_alloc_asq_bufs(struct iavf_hw *hw)
{
	struct iavf_dma_mem *bi;
	enum iavf_status ret_code;
	int i;

	/* No mapped memory needed yet, just the buffer info structures */
	ret_code = iavf_allocate_virt_mem(hw, &hw->aq.asq.dma_head,
					  (hw->aq.num_asq_entries *
					   sizeof(struct iavf_dma_mem)));
	if (ret_code)
		goto alloc_asq_bufs;
	hw->aq.asq.r.asq_bi = (struct iavf_dma_mem *)hw->aq.asq.dma_head.va;

	/* allocate the mapped buffers */
	for (i = 0; i < hw->aq.num_asq_entries; i++) {
		bi = &hw->aq.asq.r.asq_bi[i];
		ret_code = iavf_allocate_dma_mem(hw, bi,
						 iavf_mem_asq_buf,
						 hw->aq.asq_buf_size,
						 IAVF_ADMINQ_DESC_ALIGNMENT);
		if (ret_code)
			goto unwind_alloc_asq_bufs;
	}
alloc_asq_bufs:
	return ret_code;

unwind_alloc_asq_bufs:
	/* don't try to free the one that failed... */
	i--;
	for (; i >= 0; i--)
		iavf_free_dma_mem(hw, &hw->aq.asq.r.asq_bi[i]);
	iavf_free_virt_mem(hw, &hw->aq.asq.dma_head);

	return ret_code;
}

/**
 *  iavf_free_arq_bufs - Free receive queue buffer info elements
 *  @hw: pointer to the hardware structure
 **/
static void iavf_free_arq_bufs(struct iavf_hw *hw)
{
	int i;

	/* free descriptors */
	for (i = 0; i < hw->aq.num_arq_entries; i++)
		iavf_free_dma_mem(hw, &hw->aq.arq.r.arq_bi[i]);

	/* free the descriptor memory */
	iavf_free_dma_mem(hw, &hw->aq.arq.desc_buf);

	/* free the dma header */
	iavf_free_virt_mem(hw, &hw->aq.arq.dma_head);
}

/**
 *  iavf_free_asq_bufs - Free send queue buffer info elements
 *  @hw: pointer to the hardware structure
 **/
static void iavf_free_asq_bufs(struct iavf_hw *hw)
{
	int i;

	/* only unmap if the address is non-NULL */
	for (i = 0; i < hw->aq.num_asq_entries; i++)
		if (hw->aq.asq.r.asq_bi[i].pa)
			iavf_free_dma_mem(hw, &hw->aq.asq.r.asq_bi[i]);

	/* free the buffer info list */
	iavf_free_virt_mem(hw, &hw->aq.asq.cmd_buf);

	/* free the descriptor memory */
	iavf_free_dma_mem(hw, &hw->aq.asq.desc_buf);

	/* free the dma header */
	iavf_free_virt_mem(hw, &hw->aq.asq.dma_head);
}

/**
 *  iavf_config_asq_regs - configure ASQ registers
 *  @hw: pointer to the hardware structure
 *
 *  Configure base address and length registers for the transmit queue
 **/
static enum iavf_status iavf_config_asq_regs(struct iavf_hw *hw)
{
	enum iavf_status ret_code = 0;
	u32 reg = 0;

	/* Clear Head and Tail */
	wr32(hw, hw->aq.asq.head, 0);
	wr32(hw, hw->aq.asq.tail, 0);

	/* set starting point */
	wr32(hw, hw->aq.asq.len, (hw->aq.num_asq_entries |
				  IAVF_VF_ATQLEN1_ATQENABLE_MASK));
	wr32(hw, hw->aq.asq.bal, lower_32_bits(hw->aq.asq.desc_buf.pa));
	wr32(hw, hw->aq.asq.bah, upper_32_bits(hw->aq.asq.desc_buf.pa));

	/* Check one register to verify that config was applied */
	reg = rd32(hw, hw->aq.asq.bal);
	if (reg != lower_32_bits(hw->aq.asq.desc_buf.pa))
		ret_code = IAVF_ERR_ADMIN_QUEUE_ERROR;

	return ret_code;
}

/**
 *  iavf_config_arq_regs - ARQ register configuration
 *  @hw: pointer to the hardware structure
 *
 * Configure base address and length registers for the receive (event queue)
 **/
static enum iavf_status iavf_config_arq_regs(struct iavf_hw *hw)
{
	enum iavf_status ret_code = 0;
	u32 reg = 0;

	/* Clear Head and Tail */
	wr32(hw, hw->aq.arq.head, 0);
	wr32(hw, hw->aq.arq.tail, 0);

	/* set starting point */
	wr32(hw, hw->aq.arq.len, (hw->aq.num_arq_entries |
				  IAVF_VF_ARQLEN1_ARQENABLE_MASK));
	wr32(hw, hw->aq.arq.bal, lower_32_bits(hw->aq.arq.desc_buf.pa));
	wr32(hw, hw->aq.arq.bah, upper_32_bits(hw->aq.arq.desc_buf.pa));

	/* Update tail in the HW to post pre-allocated buffers */
	wr32(hw, hw->aq.arq.tail, hw->aq.num_arq_entries - 1);

	/* Check one register to verify that config was applied */
	reg = rd32(hw, hw->aq.arq.bal);
	if (reg != lower_32_bits(hw->aq.arq.desc_buf.pa))
		ret_code = IAVF_ERR_ADMIN_QUEUE_ERROR;

	return ret_code;
}

/**
 *  iavf_init_asq - main initialization routine for ASQ
 *  @hw: pointer to the hardware structure
 *
 *  This is the main initialization routine for the Admin Send Queue
 *  Prior to calling this function, drivers *MUST* set the following fields
 *  in the hw->aq structure:
 *     - hw->aq.num_asq_entries
 *     - hw->aq.arq_buf_size
 *
 *  Do *NOT* hold the lock when calling this as the memory allocation routines
 *  called are not going to be atomic context safe
 **/
static enum iavf_status iavf_init_asq(struct iavf_hw *hw)
{
	enum iavf_status ret_code = 0;

	if (hw->aq.asq.count > 0) {
		/* queue already initialized */
		ret_code = IAVF_ERR_NOT_READY;
		goto init_adminq_exit;
	}

	/* verify input for valid configuration */
	if ((hw->aq.num_asq_entries == 0) ||
	    (hw->aq.asq_buf_size == 0)) {
		ret_code = IAVF_ERR_CONFIG;
		goto init_adminq_exit;
	}

	hw->aq.asq.next_to_use = 0;
	hw->aq.asq.next_to_clean = 0;

	/* allocate the ring memory */
	ret_code = iavf_alloc_adminq_asq_ring(hw);
	if (ret_code)
		goto init_adminq_exit;

	/* allocate buffers in the rings */
	ret_code = iavf_alloc_asq_bufs(hw);
	if (ret_code)
		goto init_adminq_free_rings;

	/* initialize base registers */
	ret_code = iavf_config_asq_regs(hw);
	if (ret_code)
		goto init_adminq_free_rings;

	/* success! */
	hw->aq.asq.count = hw->aq.num_asq_entries;
	goto init_adminq_exit;

init_adminq_free_rings:
	iavf_free_adminq_asq(hw);

init_adminq_exit:
	return ret_code;
}

/**
 *  iavf_init_arq - initialize ARQ
 *  @hw: pointer to the hardware structure
 *
 *  The main initialization routine for the Admin Receive (Event) Queue.
 *  Prior to calling this function, drivers *MUST* set the following fields
 *  in the hw->aq structure:
 *     - hw->aq.num_asq_entries
 *     - hw->aq.arq_buf_size
 *
 *  Do *NOT* hold the lock when calling this as the memory allocation routines
 *  called are not going to be atomic context safe
 **/
static enum iavf_status iavf_init_arq(struct iavf_hw *hw)
{
	enum iavf_status ret_code = 0;

	if (hw->aq.arq.count > 0) {
		/* queue already initialized */
		ret_code = IAVF_ERR_NOT_READY;
		goto init_adminq_exit;
	}

	/* verify input for valid configuration */
	if ((hw->aq.num_arq_entries == 0) ||
	    (hw->aq.arq_buf_size == 0)) {
		ret_code = IAVF_ERR_CONFIG;
		goto init_adminq_exit;
	}

	hw->aq.arq.next_to_use = 0;
	hw->aq.arq.next_to_clean = 0;

	/* allocate the ring memory */
	ret_code = iavf_alloc_adminq_arq_ring(hw);
	if (ret_code)
		goto init_adminq_exit;

	/* allocate buffers in the rings */
	ret_code = iavf_alloc_arq_bufs(hw);
	if (ret_code)
		goto init_adminq_free_rings;

	/* initialize base registers */
	ret_code = iavf_config_arq_regs(hw);
	if (ret_code)
		goto init_adminq_free_rings;

	/* success! */
	hw->aq.arq.count = hw->aq.num_arq_entries;
	goto init_adminq_exit;

init_adminq_free_rings:
	iavf_free_adminq_arq(hw);

init_adminq_exit:
	return ret_code;
}

/**
 *  iavf_shutdown_asq - shutdown the ASQ
 *  @hw: pointer to the hardware structure
 *
 *  The main shutdown routine for the Admin Send Queue
 **/
static enum iavf_status iavf_shutdown_asq(struct iavf_hw *hw)
{
	enum iavf_status ret_code = 0;

	mutex_lock(&hw->aq.asq_mutex);

	if (hw->aq.asq.count == 0) {
		ret_code = IAVF_ERR_NOT_READY;
		goto shutdown_asq_out;
	}

	/* Stop firmware AdminQ processing */
	wr32(hw, hw->aq.asq.head, 0);
	wr32(hw, hw->aq.asq.tail, 0);
	wr32(hw, hw->aq.asq.len, 0);
	wr32(hw, hw->aq.asq.bal, 0);
	wr32(hw, hw->aq.asq.bah, 0);

	hw->aq.asq.count = 0; /* to indicate uninitialized queue */

	/* free ring buffers */
	iavf_free_asq_bufs(hw);

shutdown_asq_out:
	mutex_unlock(&hw->aq.asq_mutex);
	return ret_code;
}

/**
 *  iavf_shutdown_arq - shutdown ARQ
 *  @hw: pointer to the hardware structure
 *
 *  The main shutdown routine for the Admin Receive Queue
 **/
static enum iavf_status iavf_shutdown_arq(struct iavf_hw *hw)
{
	enum iavf_status ret_code = 0;

	mutex_lock(&hw->aq.arq_mutex);

	if (hw->aq.arq.count == 0) {
		ret_code = IAVF_ERR_NOT_READY;
		goto shutdown_arq_out;
	}

	/* Stop firmware AdminQ processing */
	wr32(hw, hw->aq.arq.head, 0);
	wr32(hw, hw->aq.arq.tail, 0);
	wr32(hw, hw->aq.arq.len, 0);
	wr32(hw, hw->aq.arq.bal, 0);
	wr32(hw, hw->aq.arq.bah, 0);

	hw->aq.arq.count = 0; /* to indicate uninitialized queue */

	/* free ring buffers */
	iavf_free_arq_bufs(hw);

shutdown_arq_out:
	mutex_unlock(&hw->aq.arq_mutex);
	return ret_code;
}

/**
 *  iavf_init_adminq - main initialization routine for Admin Queue
 *  @hw: pointer to the hardware structure
 *
 *  Prior to calling this function, drivers *MUST* set the following fields
 *  in the hw->aq structure:
 *     - hw->aq.num_asq_entries
 *     - hw->aq.num_arq_entries
 *     - hw->aq.arq_buf_size
 *     - hw->aq.asq_buf_size
 **/
enum iavf_status iavf_init_adminq(struct iavf_hw *hw)
{
	enum iavf_status ret_code;

	/* verify input for valid configuration */
	if ((hw->aq.num_arq_entries == 0) ||
	    (hw->aq.num_asq_entries == 0) ||
	    (hw->aq.arq_buf_size == 0) ||
	    (hw->aq.asq_buf_size == 0)) {
		ret_code = IAVF_ERR_CONFIG;
		goto init_adminq_exit;
	}

	/* Set up register offsets */
	iavf_adminq_init_regs(hw);

	/* setup ASQ command write back timeout */
	hw->aq.asq_cmd_timeout = IAVF_ASQ_CMD_TIMEOUT;

	/* allocate the ASQ */
	ret_code = iavf_init_asq(hw);
	if (ret_code)
		goto init_adminq_destroy_locks;

	/* allocate the ARQ */
	ret_code = iavf_init_arq(hw);
	if (ret_code)
		goto init_adminq_free_asq;

	/* success! */
	goto init_adminq_exit;

init_adminq_free_asq:
	iavf_shutdown_asq(hw);
init_adminq_destroy_locks:

init_adminq_exit:
	return ret_code;
}

/**
 *  iavf_shutdown_adminq - shutdown routine for the Admin Queue
 *  @hw: pointer to the hardware structure
 **/
enum iavf_status iavf_shutdown_adminq(struct iavf_hw *hw)
{
	enum iavf_status ret_code = 0;

	if (iavf_check_asq_alive(hw))
		iavf_aq_queue_shutdown(hw, true);

	iavf_shutdown_asq(hw);
	iavf_shutdown_arq(hw);

	return ret_code;
}

/**
 *  iavf_clean_asq - cleans Admin send queue
 *  @hw: pointer to the hardware structure
 *
 *  returns the number of free desc
 **/
static u16 iavf_clean_asq(struct iavf_hw *hw)
{
	struct iavf_adminq_ring *asq = &hw->aq.asq;
	struct iavf_asq_cmd_details *details;
	u16 ntc = asq->next_to_clean;
	struct iavf_aq_desc desc_cb;
	struct iavf_aq_desc *desc;

	desc = IAVF_ADMINQ_DESC(*asq, ntc);
	details = IAVF_ADMINQ_DETAILS(*asq, ntc);
	while (rd32(hw, hw->aq.asq.head) != ntc) {
		iavf_debug(hw, IAVF_DEBUG_AQ_MESSAGE,
			   "ntc %d head %d.\n", ntc, rd32(hw, hw->aq.asq.head));

		if (details->callback) {
			IAVF_ADMINQ_CALLBACK cb_func =
					(IAVF_ADMINQ_CALLBACK)details->callback;
			desc_cb = *desc;
			cb_func(hw, &desc_cb);
		}
		memset((void *)desc, 0, sizeof(struct iavf_aq_desc));
		memset((void *)details, 0,
		       sizeof(struct iavf_asq_cmd_details));
		ntc++;
		if (ntc == asq->count)
			ntc = 0;
		desc = IAVF_ADMINQ_DESC(*asq, ntc);
		details = IAVF_ADMINQ_DETAILS(*asq, ntc);
	}

	asq->next_to_clean = ntc;

	return IAVF_DESC_UNUSED(asq);
}

/**
 *  iavf_asq_done - check if FW has processed the Admin Send Queue
 *  @hw: pointer to the hw struct
 *
 *  Returns true if the firmware has processed all descriptors on the
 *  admin send queue. Returns false if there are still requests pending.
 **/
bool iavf_asq_done(struct iavf_hw *hw)
{
	/* AQ designers suggest use of head for better
	 * timing reliability than DD bit
	 */
	return rd32(hw, hw->aq.asq.head) == hw->aq.asq.next_to_use;
}

/**
 *  iavf_asq_send_command - send command to Admin Queue
 *  @hw: pointer to the hw struct
 *  @desc: prefilled descriptor describing the command (non DMA mem)
 *  @buff: buffer to use for indirect commands
 *  @buff_size: size of buffer for indirect commands
 *  @cmd_details: pointer to command details structure
 *
 *  This is the main send command driver routine for the Admin Queue send
 *  queue.  It runs the queue, cleans the queue, etc
 **/
enum iavf_status iavf_asq_send_command(struct iavf_hw *hw,
				       struct iavf_aq_desc *desc,
				       void *buff, /* can be NULL */
				       u16  buff_size,
				       struct iavf_asq_cmd_details *cmd_details)
{
	struct iavf_dma_mem *dma_buff = NULL;
	struct iavf_asq_cmd_details *details;
	struct iavf_aq_desc *desc_on_ring;
	bool cmd_completed = false;
	enum iavf_status status = 0;
	u16  retval = 0;
	u32  val = 0;

	mutex_lock(&hw->aq.asq_mutex);

	if (hw->aq.asq.count == 0) {
		iavf_debug(hw, IAVF_DEBUG_AQ_MESSAGE,
			   "AQTX: Admin queue not initialized.\n");
		status = IAVF_ERR_QUEUE_EMPTY;
		goto asq_send_command_error;
	}

	hw->aq.asq_last_status = IAVF_AQ_RC_OK;

	val = rd32(hw, hw->aq.asq.head);
	if (val >= hw->aq.num_asq_entries) {
		iavf_debug(hw, IAVF_DEBUG_AQ_MESSAGE,
			   "AQTX: head overrun at %d\n", val);
		status = IAVF_ERR_QUEUE_EMPTY;
		goto asq_send_command_error;
	}

	details = IAVF_ADMINQ_DETAILS(hw->aq.asq, hw->aq.asq.next_to_use);
	if (cmd_details) {
		*details = *cmd_details;

		/* If the cmd_details are defined copy the cookie.  The
		 * cpu_to_le32 is not needed here because the data is ignored
		 * by the FW, only used by the driver
		 */
		if (details->cookie) {
			desc->cookie_high =
				cpu_to_le32(upper_32_bits(details->cookie));
			desc->cookie_low =
				cpu_to_le32(lower_32_bits(details->cookie));
		}
	} else {
		memset(details, 0, sizeof(struct iavf_asq_cmd_details));
	}

	/* clear requested flags and then set additional flags if defined */
	desc->flags &= ~cpu_to_le16(details->flags_dis);
	desc->flags |= cpu_to_le16(details->flags_ena);

	if (buff_size > hw->aq.asq_buf_size) {
		iavf_debug(hw,
			   IAVF_DEBUG_AQ_MESSAGE,
			   "AQTX: Invalid buffer size: %d.\n",
			   buff_size);
		status = IAVF_ERR_INVALID_SIZE;
		goto asq_send_command_error;
	}

	if (details->postpone && !details->async) {
		iavf_debug(hw,
			   IAVF_DEBUG_AQ_MESSAGE,
			   "AQTX: Async flag not set along with postpone flag");
		status = IAVF_ERR_PARAM;
		goto asq_send_command_error;
	}

	/* call clean and check queue available function to reclaim the
	 * descriptors that were processed by FW, the function returns the
	 * number of desc available
	 */
	/* the clean function called here could be called in a separate thread
	 * in case of asynchronous completions
	 */
	if (iavf_clean_asq(hw) == 0) {
		iavf_debug(hw,
			   IAVF_DEBUG_AQ_MESSAGE,
			   "AQTX: Error queue is full.\n");
		status = IAVF_ERR_ADMIN_QUEUE_FULL;
		goto asq_send_command_error;
	}

	/* initialize the temp desc pointer with the right desc */
	desc_on_ring = IAVF_ADMINQ_DESC(hw->aq.asq, hw->aq.asq.next_to_use);

	/* if the desc is available copy the temp desc to the right place */
	*desc_on_ring = *desc;

	/* if buff is not NULL assume indirect command */
	if (buff) {
		dma_buff = &hw->aq.asq.r.asq_bi[hw->aq.asq.next_to_use];
		/* copy the user buff into the respective DMA buff */
		memcpy(dma_buff->va, buff, buff_size);
		desc_on_ring->datalen = cpu_to_le16(buff_size);

		/* Update the address values in the desc with the pa value
		 * for respective buffer
		 */
		desc_on_ring->params.external.addr_high =
				cpu_to_le32(upper_32_bits(dma_buff->pa));
		desc_on_ring->params.external.addr_low =
				cpu_to_le32(lower_32_bits(dma_buff->pa));
	}

	/* bump the tail */
	iavf_debug(hw, IAVF_DEBUG_AQ_MESSAGE, "AQTX: desc and buffer:\n");
	iavf_debug_aq(hw, IAVF_DEBUG_AQ_COMMAND, (void *)desc_on_ring,
		      buff, buff_size);
	(hw->aq.asq.next_to_use)++;
	if (hw->aq.asq.next_to_use == hw->aq.asq.count)
		hw->aq.asq.next_to_use = 0;
	if (!details->postpone)
		wr32(hw, hw->aq.asq.tail, hw->aq.asq.next_to_use);

	/* if cmd_details are not defined or async flag is not set,
	 * we need to wait for desc write back
	 */
	if (!details->async && !details->postpone) {
		u32 total_delay = 0;

		do {
			/* AQ designers suggest use of head for better
			 * timing reliability than DD bit
			 */
			if (iavf_asq_done(hw))
				break;
			udelay(50);
			total_delay += 50;
		} while (total_delay < hw->aq.asq_cmd_timeout);
	}

	/* if ready, copy the desc back to temp */
	if (iavf_asq_done(hw)) {
		*desc = *desc_on_ring;
		if (buff)
			memcpy(buff, dma_buff->va, buff_size);
		retval = le16_to_cpu(desc->retval);
		if (retval != 0) {
			iavf_debug(hw,
				   IAVF_DEBUG_AQ_MESSAGE,
				   "AQTX: Command completed with error 0x%X.\n",
				   retval);

			/* strip off FW internal code */
			retval &= 0xff;
		}
		cmd_completed = true;
		if ((enum iavf_admin_queue_err)retval == IAVF_AQ_RC_OK)
			status = 0;
		else if ((enum iavf_admin_queue_err)retval == IAVF_AQ_RC_EBUSY)
			status = IAVF_ERR_NOT_READY;
		else
			status = IAVF_ERR_ADMIN_QUEUE_ERROR;
		hw->aq.asq_last_status = (enum iavf_admin_queue_err)retval;
	}

	iavf_debug(hw, IAVF_DEBUG_AQ_MESSAGE,
		   "AQTX: desc and buffer writeback:\n");
	iavf_debug_aq(hw, IAVF_DEBUG_AQ_COMMAND, (void *)desc, buff, buff_size);

	/* save writeback aq if requested */
	if (details->wb_desc)
		*details->wb_desc = *desc_on_ring;

	/* update the error if time out occurred */
	if ((!cmd_completed) &&
	    (!details->async && !details->postpone)) {
		if (rd32(hw, hw->aq.asq.len) & IAVF_VF_ATQLEN1_ATQCRIT_MASK) {
			iavf_debug(hw, IAVF_DEBUG_AQ_MESSAGE,
				   "AQTX: AQ Critical error.\n");
			status = IAVF_ERR_ADMIN_QUEUE_CRITICAL_ERROR;
		} else {
			iavf_debug(hw, IAVF_DEBUG_AQ_MESSAGE,
				   "AQTX: Writeback timeout.\n");
			status = IAVF_ERR_ADMIN_QUEUE_TIMEOUT;
		}
	}

asq_send_command_error:
	mutex_unlock(&hw->aq.asq_mutex);
	return status;
}

/**
 *  iavf_fill_default_direct_cmd_desc - AQ descriptor helper function
 *  @desc:     pointer to the temp descriptor (non DMA mem)
 *  @opcode:   the opcode can be used to decide which flags to turn off or on
 *
 *  Fill the desc with default values
 **/
void iavf_fill_default_direct_cmd_desc(struct iavf_aq_desc *desc, u16 opcode)
{
	/* zero out the desc */
	memset((void *)desc, 0, sizeof(struct iavf_aq_desc));
	desc->opcode = cpu_to_le16(opcode);
	desc->flags = cpu_to_le16(IAVF_AQ_FLAG_SI);
}

/**
 *  iavf_clean_arq_element
 *  @hw: pointer to the hw struct
 *  @e: event info from the receive descriptor, includes any buffers
 *  @pending: number of events that could be left to process
 *
 *  This function cleans one Admin Receive Queue element and returns
 *  the contents through e.  It can also return how many events are
 *  left to process through 'pending'
 **/
enum iavf_status iavf_clean_arq_element(struct iavf_hw *hw,
					struct iavf_arq_event_info *e,
					u16 *pending)
{
	u16 ntc = hw->aq.arq.next_to_clean;
	struct iavf_aq_desc *desc;
	enum iavf_status ret_code = 0;
	struct iavf_dma_mem *bi;
	u16 desc_idx;
	u16 datalen;
	u16 flags;
	u16 ntu;

	/* pre-clean the event info */
	memset(&e->desc, 0, sizeof(e->desc));

	/* take the lock before we start messing with the ring */
	mutex_lock(&hw->aq.arq_mutex);

	if (hw->aq.arq.count == 0) {
		iavf_debug(hw, IAVF_DEBUG_AQ_MESSAGE,
			   "AQRX: Admin queue not initialized.\n");
		ret_code = IAVF_ERR_QUEUE_EMPTY;
		goto clean_arq_element_err;
	}

	/* set next_to_use to head */
	ntu = rd32(hw, hw->aq.arq.head) & IAVF_VF_ARQH1_ARQH_MASK;
	if (ntu == ntc) {
		/* nothing to do - shouldn't need to update ring's values */
		ret_code = IAVF_ERR_ADMIN_QUEUE_NO_WORK;
		goto clean_arq_element_out;
	}

	/* now clean the next descriptor */
	desc = IAVF_ADMINQ_DESC(hw->aq.arq, ntc);
	desc_idx = ntc;

	hw->aq.arq_last_status =
		(enum iavf_admin_queue_err)le16_to_cpu(desc->retval);
	flags = le16_to_cpu(desc->flags);
	if (flags & IAVF_AQ_FLAG_ERR) {
		ret_code = IAVF_ERR_ADMIN_QUEUE_ERROR;
		iavf_debug(hw,
			   IAVF_DEBUG_AQ_MESSAGE,
			   "AQRX: Event received with error 0x%X.\n",
			   hw->aq.arq_last_status);
	}

	e->desc = *desc;
	datalen = le16_to_cpu(desc->datalen);
	e->msg_len = min(datalen, e->buf_len);
	if (e->msg_buf && (e->msg_len != 0))
		memcpy(e->msg_buf, hw->aq.arq.r.arq_bi[desc_idx].va,
		       e->msg_len);

	iavf_debug(hw, IAVF_DEBUG_AQ_MESSAGE, "AQRX: desc and buffer:\n");
	iavf_debug_aq(hw, IAVF_DEBUG_AQ_COMMAND, (void *)desc, e->msg_buf,
		      hw->aq.arq_buf_size);

	/* Restore the original datalen and buffer address in the desc,
	 * FW updates datalen to indicate the event message
	 * size
	 */
	bi = &hw->aq.arq.r.arq_bi[ntc];
	memset((void *)desc, 0, sizeof(struct iavf_aq_desc));

	desc->flags = cpu_to_le16(IAVF_AQ_FLAG_BUF);
	if (hw->aq.arq_buf_size > IAVF_AQ_LARGE_BUF)
		desc->flags |= cpu_to_le16(IAVF_AQ_FLAG_LB);
	desc->datalen = cpu_to_le16((u16)bi->size);
	desc->params.external.addr_high = cpu_to_le32(upper_32_bits(bi->pa));
	desc->params.external.addr_low = cpu_to_le32(lower_32_bits(bi->pa));

	/* set tail = the last cleaned desc index. */
	wr32(hw, hw->aq.arq.tail, ntc);
	/* ntc is updated to tail + 1 */
	ntc++;
	if (ntc == hw->aq.num_arq_entries)
		ntc = 0;
	hw->aq.arq.next_to_clean = ntc;
	hw->aq.arq.next_to_use = ntu;

clean_arq_element_out:
	/* Set pending if needed, unlock and return */
	if (pending)
		*pending = (ntc > ntu ? hw->aq.arq.count : 0) + (ntu - ntc);

clean_arq_element_err:
	mutex_unlock(&hw->aq.arq_mutex);

	return ret_code;
}
