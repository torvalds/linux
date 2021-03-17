// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2018, Intel Corporation. */

#include "ice_common.h"

/**
 * ice_adminq_init_regs - Initialize AdminQ registers
 * @hw: pointer to the hardware structure
 *
 * This assumes the alloc_sq and alloc_rq functions have already been called
 */
static void ice_adminq_init_regs(struct ice_hw *hw)
{
	struct ice_ctl_q_info *cq = &hw->adminq;

	cq->sq.head = PF_FW_ATQH;
	cq->sq.tail = PF_FW_ATQT;
	cq->sq.len = PF_FW_ATQLEN;
	cq->sq.bah = PF_FW_ATQBAH;
	cq->sq.bal = PF_FW_ATQBAL;
	cq->sq.len_mask = PF_FW_ATQLEN_ATQLEN_M;
	cq->sq.len_ena_mask = PF_FW_ATQLEN_ATQENABLE_M;
	cq->sq.head_mask = PF_FW_ATQH_ATQH_M;

	cq->rq.head = PF_FW_ARQH;
	cq->rq.tail = PF_FW_ARQT;
	cq->rq.len = PF_FW_ARQLEN;
	cq->rq.bah = PF_FW_ARQBAH;
	cq->rq.bal = PF_FW_ARQBAL;
	cq->rq.len_mask = PF_FW_ARQLEN_ARQLEN_M;
	cq->rq.len_ena_mask = PF_FW_ARQLEN_ARQENABLE_M;
	cq->rq.head_mask = PF_FW_ARQH_ARQH_M;
}

/**
 * ice_check_sq_alive
 * @hw: pointer to the hw struct
 * @cq: pointer to the specific Control queue
 *
 * Returns true if Queue is enabled else false.
 */
bool ice_check_sq_alive(struct ice_hw *hw, struct ice_ctl_q_info *cq)
{
	/* check both queue-length and queue-enable fields */
	if (cq->sq.len && cq->sq.len_mask && cq->sq.len_ena_mask)
		return (rd32(hw, cq->sq.len) & (cq->sq.len_mask |
						cq->sq.len_ena_mask)) ==
			(cq->num_sq_entries | cq->sq.len_ena_mask);

	return false;
}

/**
 * ice_alloc_ctrlq_sq_ring - Allocate Control Transmit Queue (ATQ) rings
 * @hw: pointer to the hardware structure
 * @cq: pointer to the specific Control queue
 */
static enum ice_status
ice_alloc_ctrlq_sq_ring(struct ice_hw *hw, struct ice_ctl_q_info *cq)
{
	size_t size = cq->num_sq_entries * sizeof(struct ice_aq_desc);

	cq->sq.desc_buf.va = dmam_alloc_coherent(ice_hw_to_dev(hw), size,
						 &cq->sq.desc_buf.pa,
						 GFP_KERNEL | __GFP_ZERO);
	if (!cq->sq.desc_buf.va)
		return ICE_ERR_NO_MEMORY;
	cq->sq.desc_buf.size = size;

	cq->sq.cmd_buf = devm_kcalloc(ice_hw_to_dev(hw), cq->num_sq_entries,
				      sizeof(struct ice_sq_cd), GFP_KERNEL);
	if (!cq->sq.cmd_buf) {
		dmam_free_coherent(ice_hw_to_dev(hw), cq->sq.desc_buf.size,
				   cq->sq.desc_buf.va, cq->sq.desc_buf.pa);
		cq->sq.desc_buf.va = NULL;
		cq->sq.desc_buf.pa = 0;
		cq->sq.desc_buf.size = 0;
		return ICE_ERR_NO_MEMORY;
	}

	return 0;
}

/**
 * ice_alloc_ctrlq_rq_ring - Allocate Control Receive Queue (ARQ) rings
 * @hw: pointer to the hardware structure
 * @cq: pointer to the specific Control queue
 */
static enum ice_status
ice_alloc_ctrlq_rq_ring(struct ice_hw *hw, struct ice_ctl_q_info *cq)
{
	size_t size = cq->num_rq_entries * sizeof(struct ice_aq_desc);

	cq->rq.desc_buf.va = dmam_alloc_coherent(ice_hw_to_dev(hw), size,
						 &cq->rq.desc_buf.pa,
						 GFP_KERNEL | __GFP_ZERO);
	if (!cq->rq.desc_buf.va)
		return ICE_ERR_NO_MEMORY;
	cq->rq.desc_buf.size = size;
	return 0;
}

/**
 * ice_free_ctrlq_sq_ring - Free Control Transmit Queue (ATQ) rings
 * @hw: pointer to the hardware structure
 * @cq: pointer to the specific Control queue
 *
 * This assumes the posted send buffers have already been cleaned
 * and de-allocated
 */
static void ice_free_ctrlq_sq_ring(struct ice_hw *hw, struct ice_ctl_q_info *cq)
{
	dmam_free_coherent(ice_hw_to_dev(hw), cq->sq.desc_buf.size,
			   cq->sq.desc_buf.va, cq->sq.desc_buf.pa);
	cq->sq.desc_buf.va = NULL;
	cq->sq.desc_buf.pa = 0;
	cq->sq.desc_buf.size = 0;
}

/**
 * ice_free_ctrlq_rq_ring - Free Control Receive Queue (ARQ) rings
 * @hw: pointer to the hardware structure
 * @cq: pointer to the specific Control queue
 *
 * This assumes the posted receive buffers have already been cleaned
 * and de-allocated
 */
static void ice_free_ctrlq_rq_ring(struct ice_hw *hw, struct ice_ctl_q_info *cq)
{
	dmam_free_coherent(ice_hw_to_dev(hw), cq->rq.desc_buf.size,
			   cq->rq.desc_buf.va, cq->rq.desc_buf.pa);
	cq->rq.desc_buf.va = NULL;
	cq->rq.desc_buf.pa = 0;
	cq->rq.desc_buf.size = 0;
}

/**
 * ice_alloc_rq_bufs - Allocate pre-posted buffers for the ARQ
 * @hw: pointer to the hardware structure
 * @cq: pointer to the specific Control queue
 */
static enum ice_status
ice_alloc_rq_bufs(struct ice_hw *hw, struct ice_ctl_q_info *cq)
{
	int i;

	/* We'll be allocating the buffer info memory first, then we can
	 * allocate the mapped buffers for the event processing
	 */
	cq->rq.dma_head = devm_kcalloc(ice_hw_to_dev(hw), cq->num_rq_entries,
				       sizeof(cq->rq.desc_buf), GFP_KERNEL);
	if (!cq->rq.dma_head)
		return ICE_ERR_NO_MEMORY;
	cq->rq.r.rq_bi = (struct ice_dma_mem *)cq->rq.dma_head;

	/* allocate the mapped buffers */
	for (i = 0; i < cq->num_rq_entries; i++) {
		struct ice_aq_desc *desc;
		struct ice_dma_mem *bi;

		bi = &cq->rq.r.rq_bi[i];
		bi->va = dmam_alloc_coherent(ice_hw_to_dev(hw),
					     cq->rq_buf_size, &bi->pa,
					     GFP_KERNEL | __GFP_ZERO);
		if (!bi->va)
			goto unwind_alloc_rq_bufs;
		bi->size = cq->rq_buf_size;

		/* now configure the descriptors for use */
		desc = ICE_CTL_Q_DESC(cq->rq, i);

		desc->flags = cpu_to_le16(ICE_AQ_FLAG_BUF);
		if (cq->rq_buf_size > ICE_AQ_LG_BUF)
			desc->flags |= cpu_to_le16(ICE_AQ_FLAG_LB);
		desc->opcode = 0;
		/* This is in accordance with Admin queue design, there is no
		 * register for buffer size configuration
		 */
		desc->datalen = cpu_to_le16(bi->size);
		desc->retval = 0;
		desc->cookie_high = 0;
		desc->cookie_low = 0;
		desc->params.generic.addr_high =
			cpu_to_le32(upper_32_bits(bi->pa));
		desc->params.generic.addr_low =
			cpu_to_le32(lower_32_bits(bi->pa));
		desc->params.generic.param0 = 0;
		desc->params.generic.param1 = 0;
	}
	return 0;

unwind_alloc_rq_bufs:
	/* don't try to free the one that failed... */
	i--;
	for (; i >= 0; i--) {
		dmam_free_coherent(ice_hw_to_dev(hw), cq->rq.r.rq_bi[i].size,
				   cq->rq.r.rq_bi[i].va, cq->rq.r.rq_bi[i].pa);
		cq->rq.r.rq_bi[i].va = NULL;
		cq->rq.r.rq_bi[i].pa = 0;
		cq->rq.r.rq_bi[i].size = 0;
	}
	devm_kfree(ice_hw_to_dev(hw), cq->rq.dma_head);

	return ICE_ERR_NO_MEMORY;
}

/**
 * ice_alloc_sq_bufs - Allocate empty buffer structs for the ATQ
 * @hw: pointer to the hardware structure
 * @cq: pointer to the specific Control queue
 */
static enum ice_status
ice_alloc_sq_bufs(struct ice_hw *hw, struct ice_ctl_q_info *cq)
{
	int i;

	/* No mapped memory needed yet, just the buffer info structures */
	cq->sq.dma_head = devm_kcalloc(ice_hw_to_dev(hw), cq->num_sq_entries,
				       sizeof(cq->sq.desc_buf), GFP_KERNEL);
	if (!cq->sq.dma_head)
		return ICE_ERR_NO_MEMORY;
	cq->sq.r.sq_bi = (struct ice_dma_mem *)cq->sq.dma_head;

	/* allocate the mapped buffers */
	for (i = 0; i < cq->num_sq_entries; i++) {
		struct ice_dma_mem *bi;

		bi = &cq->sq.r.sq_bi[i];
		bi->va = dmam_alloc_coherent(ice_hw_to_dev(hw),
					     cq->sq_buf_size, &bi->pa,
					     GFP_KERNEL | __GFP_ZERO);
		if (!bi->va)
			goto unwind_alloc_sq_bufs;
		bi->size = cq->sq_buf_size;
	}
	return 0;

unwind_alloc_sq_bufs:
	/* don't try to free the one that failed... */
	i--;
	for (; i >= 0; i--) {
		dmam_free_coherent(ice_hw_to_dev(hw), cq->sq.r.sq_bi[i].size,
				   cq->sq.r.sq_bi[i].va, cq->sq.r.sq_bi[i].pa);
		cq->sq.r.sq_bi[i].va = NULL;
		cq->sq.r.sq_bi[i].pa = 0;
		cq->sq.r.sq_bi[i].size = 0;
	}
	devm_kfree(ice_hw_to_dev(hw), cq->sq.dma_head);

	return ICE_ERR_NO_MEMORY;
}

/**
 * ice_free_rq_bufs - Free ARQ buffer info elements
 * @hw: pointer to the hardware structure
 * @cq: pointer to the specific Control queue
 */
static void ice_free_rq_bufs(struct ice_hw *hw, struct ice_ctl_q_info *cq)
{
	int i;

	/* free descriptors */
	for (i = 0; i < cq->num_rq_entries; i++) {
		dmam_free_coherent(ice_hw_to_dev(hw), cq->rq.r.rq_bi[i].size,
				   cq->rq.r.rq_bi[i].va, cq->rq.r.rq_bi[i].pa);
		cq->rq.r.rq_bi[i].va = NULL;
		cq->rq.r.rq_bi[i].pa = 0;
		cq->rq.r.rq_bi[i].size = 0;
	}

	/* free the dma header */
	devm_kfree(ice_hw_to_dev(hw), cq->rq.dma_head);
}

/**
 * ice_free_sq_bufs - Free ATQ buffer info elements
 * @hw: pointer to the hardware structure
 * @cq: pointer to the specific Control queue
 */
static void ice_free_sq_bufs(struct ice_hw *hw, struct ice_ctl_q_info *cq)
{
	int i;

	/* only unmap if the address is non-NULL */
	for (i = 0; i < cq->num_sq_entries; i++)
		if (cq->sq.r.sq_bi[i].pa) {
			dmam_free_coherent(ice_hw_to_dev(hw),
					   cq->sq.r.sq_bi[i].size,
					   cq->sq.r.sq_bi[i].va,
					   cq->sq.r.sq_bi[i].pa);
			cq->sq.r.sq_bi[i].va = NULL;
			cq->sq.r.sq_bi[i].pa = 0;
			cq->sq.r.sq_bi[i].size = 0;
		}

	/* free the buffer info list */
	devm_kfree(ice_hw_to_dev(hw), cq->sq.cmd_buf);

	/* free the dma header */
	devm_kfree(ice_hw_to_dev(hw), cq->sq.dma_head);
}

/**
 * ice_cfg_sq_regs - configure Control ATQ registers
 * @hw: pointer to the hardware structure
 * @cq: pointer to the specific Control queue
 *
 * Configure base address and length registers for the transmit queue
 */
static enum ice_status
ice_cfg_sq_regs(struct ice_hw *hw, struct ice_ctl_q_info *cq)
{
	u32 reg = 0;

	/* Clear Head and Tail */
	wr32(hw, cq->sq.head, 0);
	wr32(hw, cq->sq.tail, 0);

	/* set starting point */
	wr32(hw, cq->sq.len, (cq->num_sq_entries | cq->sq.len_ena_mask));
	wr32(hw, cq->sq.bal, lower_32_bits(cq->sq.desc_buf.pa));
	wr32(hw, cq->sq.bah, upper_32_bits(cq->sq.desc_buf.pa));

	/* Check one register to verify that config was applied */
	reg = rd32(hw, cq->sq.bal);
	if (reg != lower_32_bits(cq->sq.desc_buf.pa))
		return ICE_ERR_AQ_ERROR;

	return 0;
}

/**
 * ice_cfg_rq_regs - configure Control ARQ register
 * @hw: pointer to the hardware structure
 * @cq: pointer to the specific Control queue
 *
 * Configure base address and length registers for the receive (event q)
 */
static enum ice_status
ice_cfg_rq_regs(struct ice_hw *hw, struct ice_ctl_q_info *cq)
{
	u32 reg = 0;

	/* Clear Head and Tail */
	wr32(hw, cq->rq.head, 0);
	wr32(hw, cq->rq.tail, 0);

	/* set starting point */
	wr32(hw, cq->rq.len, (cq->num_rq_entries | cq->rq.len_ena_mask));
	wr32(hw, cq->rq.bal, lower_32_bits(cq->rq.desc_buf.pa));
	wr32(hw, cq->rq.bah, upper_32_bits(cq->rq.desc_buf.pa));

	/* Update tail in the HW to post pre-allocated buffers */
	wr32(hw, cq->rq.tail, (u32)(cq->num_rq_entries - 1));

	/* Check one register to verify that config was applied */
	reg = rd32(hw, cq->rq.bal);
	if (reg != lower_32_bits(cq->rq.desc_buf.pa))
		return ICE_ERR_AQ_ERROR;

	return 0;
}

/**
 * ice_init_sq - main initialization routine for Control ATQ
 * @hw: pointer to the hardware structure
 * @cq: pointer to the specific Control queue
 *
 * This is the main initialization routine for the Control Send Queue
 * Prior to calling this function, drivers *MUST* set the following fields
 * in the cq->structure:
 *     - cq->num_sq_entries
 *     - cq->sq_buf_size
 *
 * Do *NOT* hold the lock when calling this as the memory allocation routines
 * called are not going to be atomic context safe
 */
static enum ice_status ice_init_sq(struct ice_hw *hw, struct ice_ctl_q_info *cq)
{
	enum ice_status ret_code;

	if (cq->sq.count > 0) {
		/* queue already initialized */
		ret_code = ICE_ERR_NOT_READY;
		goto init_ctrlq_exit;
	}

	/* verify input for valid configuration */
	if (!cq->num_sq_entries || !cq->sq_buf_size) {
		ret_code = ICE_ERR_CFG;
		goto init_ctrlq_exit;
	}

	cq->sq.next_to_use = 0;
	cq->sq.next_to_clean = 0;

	/* allocate the ring memory */
	ret_code = ice_alloc_ctrlq_sq_ring(hw, cq);
	if (ret_code)
		goto init_ctrlq_exit;

	/* allocate buffers in the rings */
	ret_code = ice_alloc_sq_bufs(hw, cq);
	if (ret_code)
		goto init_ctrlq_free_rings;

	/* initialize base registers */
	ret_code = ice_cfg_sq_regs(hw, cq);
	if (ret_code)
		goto init_ctrlq_free_rings;

	/* success! */
	cq->sq.count = cq->num_sq_entries;
	goto init_ctrlq_exit;

init_ctrlq_free_rings:
	ice_free_ctrlq_sq_ring(hw, cq);

init_ctrlq_exit:
	return ret_code;
}

/**
 * ice_init_rq - initialize ARQ
 * @hw: pointer to the hardware structure
 * @cq: pointer to the specific Control queue
 *
 * The main initialization routine for the Admin Receive (Event) Queue.
 * Prior to calling this function, drivers *MUST* set the following fields
 * in the cq->structure:
 *     - cq->num_rq_entries
 *     - cq->rq_buf_size
 *
 * Do *NOT* hold the lock when calling this as the memory allocation routines
 * called are not going to be atomic context safe
 */
static enum ice_status ice_init_rq(struct ice_hw *hw, struct ice_ctl_q_info *cq)
{
	enum ice_status ret_code;

	if (cq->rq.count > 0) {
		/* queue already initialized */
		ret_code = ICE_ERR_NOT_READY;
		goto init_ctrlq_exit;
	}

	/* verify input for valid configuration */
	if (!cq->num_rq_entries || !cq->rq_buf_size) {
		ret_code = ICE_ERR_CFG;
		goto init_ctrlq_exit;
	}

	cq->rq.next_to_use = 0;
	cq->rq.next_to_clean = 0;

	/* allocate the ring memory */
	ret_code = ice_alloc_ctrlq_rq_ring(hw, cq);
	if (ret_code)
		goto init_ctrlq_exit;

	/* allocate buffers in the rings */
	ret_code = ice_alloc_rq_bufs(hw, cq);
	if (ret_code)
		goto init_ctrlq_free_rings;

	/* initialize base registers */
	ret_code = ice_cfg_rq_regs(hw, cq);
	if (ret_code)
		goto init_ctrlq_free_rings;

	/* success! */
	cq->rq.count = cq->num_rq_entries;
	goto init_ctrlq_exit;

init_ctrlq_free_rings:
	ice_free_ctrlq_rq_ring(hw, cq);

init_ctrlq_exit:
	return ret_code;
}

/**
 * ice_shutdown_sq - shutdown the Control ATQ
 * @hw: pointer to the hardware structure
 * @cq: pointer to the specific Control queue
 *
 * The main shutdown routine for the Control Transmit Queue
 */
static enum ice_status
ice_shutdown_sq(struct ice_hw *hw, struct ice_ctl_q_info *cq)
{
	enum ice_status ret_code = 0;

	mutex_lock(&cq->sq_lock);

	if (!cq->sq.count) {
		ret_code = ICE_ERR_NOT_READY;
		goto shutdown_sq_out;
	}

	/* Stop firmware AdminQ processing */
	wr32(hw, cq->sq.head, 0);
	wr32(hw, cq->sq.tail, 0);
	wr32(hw, cq->sq.len, 0);
	wr32(hw, cq->sq.bal, 0);
	wr32(hw, cq->sq.bah, 0);

	cq->sq.count = 0;	/* to indicate uninitialized queue */

	/* free ring buffers and the ring itself */
	ice_free_sq_bufs(hw, cq);
	ice_free_ctrlq_sq_ring(hw, cq);

shutdown_sq_out:
	mutex_unlock(&cq->sq_lock);
	return ret_code;
}

/**
 * ice_aq_ver_check - Check the reported AQ API version.
 * @fw_branch: The "branch" of FW, typically describes the device type
 * @fw_major: The major version of the FW API
 * @fw_minor: The minor version increment of the FW API
 *
 * Checks if the driver should load on a given AQ API version.
 *
 * Return: 'true' iff the driver should attempt to load. 'false' otherwise.
 */
static bool ice_aq_ver_check(u8 fw_branch, u8 fw_major, u8 fw_minor)
{
	if (fw_branch != EXP_FW_API_VER_BRANCH)
		return false;
	if (fw_major != EXP_FW_API_VER_MAJOR)
		return false;
	if (fw_minor != EXP_FW_API_VER_MINOR)
		return false;
	return true;
}

/**
 * ice_shutdown_rq - shutdown Control ARQ
 * @hw: pointer to the hardware structure
 * @cq: pointer to the specific Control queue
 *
 * The main shutdown routine for the Control Receive Queue
 */
static enum ice_status
ice_shutdown_rq(struct ice_hw *hw, struct ice_ctl_q_info *cq)
{
	enum ice_status ret_code = 0;

	mutex_lock(&cq->rq_lock);

	if (!cq->rq.count) {
		ret_code = ICE_ERR_NOT_READY;
		goto shutdown_rq_out;
	}

	/* Stop Control Queue processing */
	wr32(hw, cq->rq.head, 0);
	wr32(hw, cq->rq.tail, 0);
	wr32(hw, cq->rq.len, 0);
	wr32(hw, cq->rq.bal, 0);
	wr32(hw, cq->rq.bah, 0);

	/* set rq.count to 0 to indicate uninitialized queue */
	cq->rq.count = 0;

	/* free ring buffers and the ring itself */
	ice_free_rq_bufs(hw, cq);
	ice_free_ctrlq_rq_ring(hw, cq);

shutdown_rq_out:
	mutex_unlock(&cq->rq_lock);
	return ret_code;
}

/**
 * ice_init_check_adminq - Check version for Admin Queue to know if its alive
 * @hw: pointer to the hardware structure
 */
static enum ice_status ice_init_check_adminq(struct ice_hw *hw)
{
	struct ice_ctl_q_info *cq = &hw->adminq;
	enum ice_status status;

	status = ice_aq_get_fw_ver(hw, NULL);
	if (status)
		goto init_ctrlq_free_rq;

	if (!ice_aq_ver_check(hw->api_branch, hw->api_maj_ver,
			      hw->api_min_ver)) {
		status = ICE_ERR_FW_API_VER;
		goto init_ctrlq_free_rq;
	}

	return 0;

init_ctrlq_free_rq:
	if (cq->rq.head) {
		ice_shutdown_rq(hw, cq);
		mutex_destroy(&cq->rq_lock);
	}
	if (cq->sq.head) {
		ice_shutdown_sq(hw, cq);
		mutex_destroy(&cq->sq_lock);
	}
	return status;
}

/**
 * ice_init_ctrlq - main initialization routine for any control Queue
 * @hw: pointer to the hardware structure
 * @q_type: specific Control queue type
 *
 * Prior to calling this function, drivers *MUST* set the following fields
 * in the cq->structure:
 *     - cq->num_sq_entries
 *     - cq->num_rq_entries
 *     - cq->rq_buf_size
 *     - cq->sq_buf_size
 *
 */
static enum ice_status ice_init_ctrlq(struct ice_hw *hw, enum ice_ctl_q q_type)
{
	struct ice_ctl_q_info *cq;
	enum ice_status ret_code;

	switch (q_type) {
	case ICE_CTL_Q_ADMIN:
		ice_adminq_init_regs(hw);
		cq = &hw->adminq;
		break;
	default:
		return ICE_ERR_PARAM;
	}
	cq->qtype = q_type;

	/* verify input for valid configuration */
	if (!cq->num_rq_entries || !cq->num_sq_entries ||
	    !cq->rq_buf_size || !cq->sq_buf_size) {
		return ICE_ERR_CFG;
	}
	mutex_init(&cq->sq_lock);
	mutex_init(&cq->rq_lock);

	/* setup SQ command write back timeout */
	cq->sq_cmd_timeout = ICE_CTL_Q_SQ_CMD_TIMEOUT;

	/* allocate the ATQ */
	ret_code = ice_init_sq(hw, cq);
	if (ret_code)
		goto init_ctrlq_destroy_locks;

	/* allocate the ARQ */
	ret_code = ice_init_rq(hw, cq);
	if (ret_code)
		goto init_ctrlq_free_sq;

	/* success! */
	return 0;

init_ctrlq_free_sq:
	ice_shutdown_sq(hw, cq);
init_ctrlq_destroy_locks:
	mutex_destroy(&cq->sq_lock);
	mutex_destroy(&cq->rq_lock);
	return ret_code;
}

/**
 * ice_init_all_ctrlq - main initialization routine for all control queues
 * @hw: pointer to the hardware structure
 *
 * Prior to calling this function, drivers *MUST* set the following fields
 * in the cq->structure for all control queues:
 *     - cq->num_sq_entries
 *     - cq->num_rq_entries
 *     - cq->rq_buf_size
 *     - cq->sq_buf_size
 */
enum ice_status ice_init_all_ctrlq(struct ice_hw *hw)
{
	enum ice_status ret_code;

	/* Init FW admin queue */
	ret_code = ice_init_ctrlq(hw, ICE_CTL_Q_ADMIN);
	if (ret_code)
		return ret_code;

	return ice_init_check_adminq(hw);
}

/**
 * ice_shutdown_ctrlq - shutdown routine for any control queue
 * @hw: pointer to the hardware structure
 * @q_type: specific Control queue type
 */
static void ice_shutdown_ctrlq(struct ice_hw *hw, enum ice_ctl_q q_type)
{
	struct ice_ctl_q_info *cq;

	switch (q_type) {
	case ICE_CTL_Q_ADMIN:
		cq = &hw->adminq;
		if (ice_check_sq_alive(hw, cq))
			ice_aq_q_shutdown(hw, true);
		break;
	default:
		return;
	}

	if (cq->sq.head) {
		ice_shutdown_sq(hw, cq);
		mutex_destroy(&cq->sq_lock);
	}
	if (cq->rq.head) {
		ice_shutdown_rq(hw, cq);
		mutex_destroy(&cq->rq_lock);
	}
}

/**
 * ice_shutdown_all_ctrlq - shutdown routine for all control queues
 * @hw: pointer to the hardware structure
 */
void ice_shutdown_all_ctrlq(struct ice_hw *hw)
{
	/* Shutdown FW admin queue */
	ice_shutdown_ctrlq(hw, ICE_CTL_Q_ADMIN);
}

/**
 * ice_clean_sq - cleans Admin send queue (ATQ)
 * @hw: pointer to the hardware structure
 * @cq: pointer to the specific Control queue
 *
 * returns the number of free desc
 */
static u16 ice_clean_sq(struct ice_hw *hw, struct ice_ctl_q_info *cq)
{
	struct ice_ctl_q_ring *sq = &cq->sq;
	u16 ntc = sq->next_to_clean;
	struct ice_sq_cd *details;
	struct ice_aq_desc *desc;

	desc = ICE_CTL_Q_DESC(*sq, ntc);
	details = ICE_CTL_Q_DETAILS(*sq, ntc);

	while (rd32(hw, cq->sq.head) != ntc) {
		ice_debug(hw, ICE_DBG_AQ_MSG,
			  "ntc %d head %d.\n", ntc, rd32(hw, cq->sq.head));
		memset(desc, 0, sizeof(*desc));
		memset(details, 0, sizeof(*details));
		ntc++;
		if (ntc == sq->count)
			ntc = 0;
		desc = ICE_CTL_Q_DESC(*sq, ntc);
		details = ICE_CTL_Q_DETAILS(*sq, ntc);
	}

	sq->next_to_clean = ntc;

	return ICE_CTL_Q_DESC_UNUSED(sq);
}

/**
 * ice_sq_done - check if FW has processed the Admin Send Queue (ATQ)
 * @hw: pointer to the hw struct
 * @cq: pointer to the specific Control queue
 *
 * Returns true if the firmware has processed all descriptors on the
 * admin send queue. Returns false if there are still requests pending.
 */
static bool ice_sq_done(struct ice_hw *hw, struct ice_ctl_q_info *cq)
{
	/* AQ designers suggest use of head for better
	 * timing reliability than DD bit
	 */
	return rd32(hw, cq->sq.head) == cq->sq.next_to_use;
}

/**
 * ice_sq_send_cmd - send command to Control Queue (ATQ)
 * @hw: pointer to the hw struct
 * @cq: pointer to the specific Control queue
 * @desc: prefilled descriptor describing the command (non DMA mem)
 * @buf: buffer to use for indirect commands (or NULL for direct commands)
 * @buf_size: size of buffer for indirect commands (or 0 for direct commands)
 * @cd: pointer to command details structure
 *
 * This is the main send command routine for the ATQ.  It runs the q,
 * cleans the queue, etc.
 */
enum ice_status
ice_sq_send_cmd(struct ice_hw *hw, struct ice_ctl_q_info *cq,
		struct ice_aq_desc *desc, void *buf, u16 buf_size,
		struct ice_sq_cd *cd)
{
	struct ice_dma_mem *dma_buf = NULL;
	struct ice_aq_desc *desc_on_ring;
	bool cmd_completed = false;
	enum ice_status status = 0;
	struct ice_sq_cd *details;
	u32 total_delay = 0;
	u16 retval = 0;
	u32 val = 0;

	mutex_lock(&cq->sq_lock);

	cq->sq_last_status = ICE_AQ_RC_OK;

	if (!cq->sq.count) {
		ice_debug(hw, ICE_DBG_AQ_MSG,
			  "Control Send queue not initialized.\n");
		status = ICE_ERR_AQ_EMPTY;
		goto sq_send_command_error;
	}

	if ((buf && !buf_size) || (!buf && buf_size)) {
		status = ICE_ERR_PARAM;
		goto sq_send_command_error;
	}

	if (buf) {
		if (buf_size > cq->sq_buf_size) {
			ice_debug(hw, ICE_DBG_AQ_MSG,
				  "Invalid buffer size for Control Send queue: %d.\n",
				  buf_size);
			status = ICE_ERR_INVAL_SIZE;
			goto sq_send_command_error;
		}

		desc->flags |= cpu_to_le16(ICE_AQ_FLAG_BUF);
		if (buf_size > ICE_AQ_LG_BUF)
			desc->flags |= cpu_to_le16(ICE_AQ_FLAG_LB);
	}

	val = rd32(hw, cq->sq.head);
	if (val >= cq->num_sq_entries) {
		ice_debug(hw, ICE_DBG_AQ_MSG,
			  "head overrun at %d in the Control Send Queue ring\n",
			  val);
		status = ICE_ERR_AQ_EMPTY;
		goto sq_send_command_error;
	}

	details = ICE_CTL_Q_DETAILS(cq->sq, cq->sq.next_to_use);
	if (cd)
		memcpy(details, cd, sizeof(*details));
	else
		memset(details, 0, sizeof(*details));

	/* Call clean and check queue available function to reclaim the
	 * descriptors that were processed by FW/MBX; the function returns the
	 * number of desc available. The clean function called here could be
	 * called in a separate thread in case of asynchronous completions.
	 */
	if (ice_clean_sq(hw, cq) == 0) {
		ice_debug(hw, ICE_DBG_AQ_MSG,
			  "Error: Control Send Queue is full.\n");
		status = ICE_ERR_AQ_FULL;
		goto sq_send_command_error;
	}

	/* initialize the temp desc pointer with the right desc */
	desc_on_ring = ICE_CTL_Q_DESC(cq->sq, cq->sq.next_to_use);

	/* if the desc is available copy the temp desc to the right place */
	memcpy(desc_on_ring, desc, sizeof(*desc_on_ring));

	/* if buf is not NULL assume indirect command */
	if (buf) {
		dma_buf = &cq->sq.r.sq_bi[cq->sq.next_to_use];
		/* copy the user buf into the respective DMA buf */
		memcpy(dma_buf->va, buf, buf_size);
		desc_on_ring->datalen = cpu_to_le16(buf_size);

		/* Update the address values in the desc with the pa value
		 * for respective buffer
		 */
		desc_on_ring->params.generic.addr_high =
			cpu_to_le32(upper_32_bits(dma_buf->pa));
		desc_on_ring->params.generic.addr_low =
			cpu_to_le32(lower_32_bits(dma_buf->pa));
	}

	/* Debug desc and buffer */
	ice_debug(hw, ICE_DBG_AQ_MSG,
		  "ATQ: Control Send queue desc and buffer:\n");

	ice_debug_cq(hw, ICE_DBG_AQ_CMD, (void *)desc_on_ring, buf, buf_size);

	(cq->sq.next_to_use)++;
	if (cq->sq.next_to_use == cq->sq.count)
		cq->sq.next_to_use = 0;
	wr32(hw, cq->sq.tail, cq->sq.next_to_use);

	do {
		if (ice_sq_done(hw, cq))
			break;

		mdelay(1);
		total_delay++;
	} while (total_delay < cq->sq_cmd_timeout);

	/* if ready, copy the desc back to temp */
	if (ice_sq_done(hw, cq)) {
		memcpy(desc, desc_on_ring, sizeof(*desc));
		if (buf) {
			/* get returned length to copy */
			u16 copy_size = le16_to_cpu(desc->datalen);

			if (copy_size > buf_size) {
				ice_debug(hw, ICE_DBG_AQ_MSG,
					  "Return len %d > than buf len %d\n",
					  copy_size, buf_size);
				status = ICE_ERR_AQ_ERROR;
			} else {
				memcpy(buf, dma_buf->va, copy_size);
			}
		}
		retval = le16_to_cpu(desc->retval);
		if (retval) {
			ice_debug(hw, ICE_DBG_AQ_MSG,
				  "Control Send Queue command completed with error 0x%x\n",
				  retval);

			/* strip off FW internal code */
			retval &= 0xff;
		}
		cmd_completed = true;
		if (!status && retval != ICE_AQ_RC_OK)
			status = ICE_ERR_AQ_ERROR;
		cq->sq_last_status = (enum ice_aq_err)retval;
	}

	ice_debug(hw, ICE_DBG_AQ_MSG,
		  "ATQ: desc and buffer writeback:\n");

	ice_debug_cq(hw, ICE_DBG_AQ_CMD, (void *)desc, buf, buf_size);

	/* save writeback AQ if requested */
	if (details->wb_desc)
		memcpy(details->wb_desc, desc_on_ring,
		       sizeof(*details->wb_desc));

	/* update the error if time out occurred */
	if (!cmd_completed) {
		ice_debug(hw, ICE_DBG_AQ_MSG,
			  "Control Send Queue Writeback timeout.\n");
		status = ICE_ERR_AQ_TIMEOUT;
	}

sq_send_command_error:
	mutex_unlock(&cq->sq_lock);
	return status;
}

/**
 * ice_fill_dflt_direct_cmd_desc - AQ descriptor helper function
 * @desc: pointer to the temp descriptor (non DMA mem)
 * @opcode: the opcode can be used to decide which flags to turn off or on
 *
 * Fill the desc with default values
 */
void ice_fill_dflt_direct_cmd_desc(struct ice_aq_desc *desc, u16 opcode)
{
	/* zero out the desc */
	memset(desc, 0, sizeof(*desc));
	desc->opcode = cpu_to_le16(opcode);
	desc->flags = cpu_to_le16(ICE_AQ_FLAG_SI);
}

/**
 * ice_clean_rq_elem
 * @hw: pointer to the hw struct
 * @cq: pointer to the specific Control queue
 * @e: event info from the receive descriptor, includes any buffers
 * @pending: number of events that could be left to process
 *
 * This function cleans one Admin Receive Queue element and returns
 * the contents through e.  It can also return how many events are
 * left to process through 'pending'.
 */
enum ice_status
ice_clean_rq_elem(struct ice_hw *hw, struct ice_ctl_q_info *cq,
		  struct ice_rq_event_info *e, u16 *pending)
{
	u16 ntc = cq->rq.next_to_clean;
	enum ice_status ret_code = 0;
	struct ice_aq_desc *desc;
	struct ice_dma_mem *bi;
	u16 desc_idx;
	u16 datalen;
	u16 flags;
	u16 ntu;

	/* pre-clean the event info */
	memset(&e->desc, 0, sizeof(e->desc));

	/* take the lock before we start messing with the ring */
	mutex_lock(&cq->rq_lock);

	if (!cq->rq.count) {
		ice_debug(hw, ICE_DBG_AQ_MSG,
			  "Control Receive queue not initialized.\n");
		ret_code = ICE_ERR_AQ_EMPTY;
		goto clean_rq_elem_err;
	}

	/* set next_to_use to head */
	ntu = (u16)(rd32(hw, cq->rq.head) & cq->rq.head_mask);

	if (ntu == ntc) {
		/* nothing to do - shouldn't need to update ring's values */
		ret_code = ICE_ERR_AQ_NO_WORK;
		goto clean_rq_elem_out;
	}

	/* now clean the next descriptor */
	desc = ICE_CTL_Q_DESC(cq->rq, ntc);
	desc_idx = ntc;

	cq->rq_last_status = (enum ice_aq_err)le16_to_cpu(desc->retval);
	flags = le16_to_cpu(desc->flags);
	if (flags & ICE_AQ_FLAG_ERR) {
		ret_code = ICE_ERR_AQ_ERROR;
		ice_debug(hw, ICE_DBG_AQ_MSG,
			  "Control Receive Queue Event received with error 0x%x\n",
			  cq->rq_last_status);
	}
	memcpy(&e->desc, desc, sizeof(e->desc));
	datalen = le16_to_cpu(desc->datalen);
	e->msg_len = min(datalen, e->buf_len);
	if (e->msg_buf && e->msg_len)
		memcpy(e->msg_buf, cq->rq.r.rq_bi[desc_idx].va, e->msg_len);

	ice_debug(hw, ICE_DBG_AQ_MSG, "ARQ: desc and buffer:\n");

	ice_debug_cq(hw, ICE_DBG_AQ_CMD, (void *)desc, e->msg_buf,
		     cq->rq_buf_size);

	/* Restore the original datalen and buffer address in the desc,
	 * FW updates datalen to indicate the event message size
	 */
	bi = &cq->rq.r.rq_bi[ntc];
	memset(desc, 0, sizeof(*desc));

	desc->flags = cpu_to_le16(ICE_AQ_FLAG_BUF);
	if (cq->rq_buf_size > ICE_AQ_LG_BUF)
		desc->flags |= cpu_to_le16(ICE_AQ_FLAG_LB);
	desc->datalen = cpu_to_le16(bi->size);
	desc->params.generic.addr_high = cpu_to_le32(upper_32_bits(bi->pa));
	desc->params.generic.addr_low = cpu_to_le32(lower_32_bits(bi->pa));

	/* set tail = the last cleaned desc index. */
	wr32(hw, cq->rq.tail, ntc);
	/* ntc is updated to tail + 1 */
	ntc++;
	if (ntc == cq->num_rq_entries)
		ntc = 0;
	cq->rq.next_to_clean = ntc;
	cq->rq.next_to_use = ntu;

clean_rq_elem_out:
	/* Set pending if needed, unlock and return */
	if (pending) {
		/* re-read HW head to calculate actual pending messages */
		ntu = (u16)(rd32(hw, cq->rq.head) & cq->rq.head_mask);
		*pending = (u16)((ntc > ntu ? cq->rq.count : 0) + (ntu - ntc));
	}
clean_rq_elem_err:
	mutex_unlock(&cq->rq_lock);

	return ret_code;
}
