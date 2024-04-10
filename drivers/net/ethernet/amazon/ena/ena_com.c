// SPDX-License-Identifier: GPL-2.0 OR Linux-OpenIB
/*
 * Copyright 2015-2020 Amazon.com, Inc. or its affiliates. All rights reserved.
 */

#include "ena_com.h"

/*****************************************************************************/
/*****************************************************************************/

/* Timeout in micro-sec */
#define ADMIN_CMD_TIMEOUT_US (3000000)

#define ENA_ASYNC_QUEUE_DEPTH 16
#define ENA_ADMIN_QUEUE_DEPTH 32


#define ENA_CTRL_MAJOR		0
#define ENA_CTRL_MINOR		0
#define ENA_CTRL_SUB_MINOR	1

#define MIN_ENA_CTRL_VER \
	(((ENA_CTRL_MAJOR) << \
	(ENA_REGS_CONTROLLER_VERSION_MAJOR_VERSION_SHIFT)) | \
	((ENA_CTRL_MINOR) << \
	(ENA_REGS_CONTROLLER_VERSION_MINOR_VERSION_SHIFT)) | \
	(ENA_CTRL_SUB_MINOR))

#define ENA_DMA_ADDR_TO_UINT32_LOW(x)	((u32)((u64)(x)))
#define ENA_DMA_ADDR_TO_UINT32_HIGH(x)	((u32)(((u64)(x)) >> 32))

#define ENA_MMIO_READ_TIMEOUT 0xFFFFFFFF

#define ENA_COM_BOUNCE_BUFFER_CNTRL_CNT	4

#define ENA_REGS_ADMIN_INTR_MASK 1

#define ENA_MAX_BACKOFF_DELAY_EXP 16U

#define ENA_MIN_ADMIN_POLL_US 100

#define ENA_MAX_ADMIN_POLL_US 5000

/*****************************************************************************/
/*****************************************************************************/
/*****************************************************************************/

enum ena_cmd_status {
	ENA_CMD_SUBMITTED,
	ENA_CMD_COMPLETED,
	/* Abort - canceled by the driver */
	ENA_CMD_ABORTED,
};

struct ena_comp_ctx {
	struct completion wait_event;
	struct ena_admin_acq_entry *user_cqe;
	u32 comp_size;
	enum ena_cmd_status status;
	/* status from the device */
	u8 comp_status;
	u8 cmd_opcode;
	bool occupied;
};

struct ena_com_stats_ctx {
	struct ena_admin_aq_get_stats_cmd get_cmd;
	struct ena_admin_acq_get_stats_resp get_resp;
};

static int ena_com_mem_addr_set(struct ena_com_dev *ena_dev,
				       struct ena_common_mem_addr *ena_addr,
				       dma_addr_t addr)
{
	if ((addr & GENMASK_ULL(ena_dev->dma_addr_bits - 1, 0)) != addr) {
		netdev_err(ena_dev->net_device,
			   "DMA address has more bits that the device supports\n");
		return -EINVAL;
	}

	ena_addr->mem_addr_low = lower_32_bits(addr);
	ena_addr->mem_addr_high = (u16)upper_32_bits(addr);

	return 0;
}

static int ena_com_admin_init_sq(struct ena_com_admin_queue *admin_queue)
{
	struct ena_com_dev *ena_dev = admin_queue->ena_dev;
	struct ena_com_admin_sq *sq = &admin_queue->sq;
	u16 size = ADMIN_SQ_SIZE(admin_queue->q_depth);

	sq->entries = dma_alloc_coherent(admin_queue->q_dmadev, size,
					 &sq->dma_addr, GFP_KERNEL);

	if (!sq->entries) {
		netdev_err(ena_dev->net_device, "Memory allocation failed\n");
		return -ENOMEM;
	}

	sq->head = 0;
	sq->tail = 0;
	sq->phase = 1;

	sq->db_addr = NULL;

	return 0;
}

static int ena_com_admin_init_cq(struct ena_com_admin_queue *admin_queue)
{
	struct ena_com_dev *ena_dev = admin_queue->ena_dev;
	struct ena_com_admin_cq *cq = &admin_queue->cq;
	u16 size = ADMIN_CQ_SIZE(admin_queue->q_depth);

	cq->entries = dma_alloc_coherent(admin_queue->q_dmadev, size,
					 &cq->dma_addr, GFP_KERNEL);

	if (!cq->entries) {
		netdev_err(ena_dev->net_device, "Memory allocation failed\n");
		return -ENOMEM;
	}

	cq->head = 0;
	cq->phase = 1;

	return 0;
}

static int ena_com_admin_init_aenq(struct ena_com_dev *ena_dev,
				   struct ena_aenq_handlers *aenq_handlers)
{
	struct ena_com_aenq *aenq = &ena_dev->aenq;
	u32 addr_low, addr_high, aenq_caps;
	u16 size;

	ena_dev->aenq.q_depth = ENA_ASYNC_QUEUE_DEPTH;
	size = ADMIN_AENQ_SIZE(ENA_ASYNC_QUEUE_DEPTH);
	aenq->entries = dma_alloc_coherent(ena_dev->dmadev, size,
					   &aenq->dma_addr, GFP_KERNEL);

	if (!aenq->entries) {
		netdev_err(ena_dev->net_device, "Memory allocation failed\n");
		return -ENOMEM;
	}

	aenq->head = aenq->q_depth;
	aenq->phase = 1;

	addr_low = ENA_DMA_ADDR_TO_UINT32_LOW(aenq->dma_addr);
	addr_high = ENA_DMA_ADDR_TO_UINT32_HIGH(aenq->dma_addr);

	writel(addr_low, ena_dev->reg_bar + ENA_REGS_AENQ_BASE_LO_OFF);
	writel(addr_high, ena_dev->reg_bar + ENA_REGS_AENQ_BASE_HI_OFF);

	aenq_caps = 0;
	aenq_caps |= ena_dev->aenq.q_depth & ENA_REGS_AENQ_CAPS_AENQ_DEPTH_MASK;
	aenq_caps |= (sizeof(struct ena_admin_aenq_entry)
		      << ENA_REGS_AENQ_CAPS_AENQ_ENTRY_SIZE_SHIFT) &
		     ENA_REGS_AENQ_CAPS_AENQ_ENTRY_SIZE_MASK;
	writel(aenq_caps, ena_dev->reg_bar + ENA_REGS_AENQ_CAPS_OFF);

	if (unlikely(!aenq_handlers)) {
		netdev_err(ena_dev->net_device,
			   "AENQ handlers pointer is NULL\n");
		return -EINVAL;
	}

	aenq->aenq_handlers = aenq_handlers;

	return 0;
}

static void comp_ctxt_release(struct ena_com_admin_queue *queue,
				     struct ena_comp_ctx *comp_ctx)
{
	comp_ctx->occupied = false;
	atomic_dec(&queue->outstanding_cmds);
}

static struct ena_comp_ctx *get_comp_ctxt(struct ena_com_admin_queue *admin_queue,
					  u16 command_id, bool capture)
{
	if (unlikely(command_id >= admin_queue->q_depth)) {
		netdev_err(admin_queue->ena_dev->net_device,
			   "Command id is larger than the queue size. cmd_id: %u queue size %d\n",
			   command_id, admin_queue->q_depth);
		return NULL;
	}

	if (unlikely(!admin_queue->comp_ctx)) {
		netdev_err(admin_queue->ena_dev->net_device,
			   "Completion context is NULL\n");
		return NULL;
	}

	if (unlikely(admin_queue->comp_ctx[command_id].occupied && capture)) {
		netdev_err(admin_queue->ena_dev->net_device,
			   "Completion context is occupied\n");
		return NULL;
	}

	if (capture) {
		atomic_inc(&admin_queue->outstanding_cmds);
		admin_queue->comp_ctx[command_id].occupied = true;
	}

	return &admin_queue->comp_ctx[command_id];
}

static struct ena_comp_ctx *__ena_com_submit_admin_cmd(struct ena_com_admin_queue *admin_queue,
						       struct ena_admin_aq_entry *cmd,
						       size_t cmd_size_in_bytes,
						       struct ena_admin_acq_entry *comp,
						       size_t comp_size_in_bytes)
{
	struct ena_comp_ctx *comp_ctx;
	u16 tail_masked, cmd_id;
	u16 queue_size_mask;
	u16 cnt;

	queue_size_mask = admin_queue->q_depth - 1;

	tail_masked = admin_queue->sq.tail & queue_size_mask;

	/* In case of queue FULL */
	cnt = (u16)atomic_read(&admin_queue->outstanding_cmds);
	if (cnt >= admin_queue->q_depth) {
		netdev_dbg(admin_queue->ena_dev->net_device,
			   "Admin queue is full.\n");
		admin_queue->stats.out_of_space++;
		return ERR_PTR(-ENOSPC);
	}

	cmd_id = admin_queue->curr_cmd_id;

	cmd->aq_common_descriptor.flags |= admin_queue->sq.phase &
		ENA_ADMIN_AQ_COMMON_DESC_PHASE_MASK;

	cmd->aq_common_descriptor.command_id |= cmd_id &
		ENA_ADMIN_AQ_COMMON_DESC_COMMAND_ID_MASK;

	comp_ctx = get_comp_ctxt(admin_queue, cmd_id, true);
	if (unlikely(!comp_ctx))
		return ERR_PTR(-EINVAL);

	comp_ctx->status = ENA_CMD_SUBMITTED;
	comp_ctx->comp_size = (u32)comp_size_in_bytes;
	comp_ctx->user_cqe = comp;
	comp_ctx->cmd_opcode = cmd->aq_common_descriptor.opcode;

	reinit_completion(&comp_ctx->wait_event);

	memcpy(&admin_queue->sq.entries[tail_masked], cmd, cmd_size_in_bytes);

	admin_queue->curr_cmd_id = (admin_queue->curr_cmd_id + 1) &
		queue_size_mask;

	admin_queue->sq.tail++;
	admin_queue->stats.submitted_cmd++;

	if (unlikely((admin_queue->sq.tail & queue_size_mask) == 0))
		admin_queue->sq.phase = !admin_queue->sq.phase;

	writel(admin_queue->sq.tail, admin_queue->sq.db_addr);

	return comp_ctx;
}

static int ena_com_init_comp_ctxt(struct ena_com_admin_queue *admin_queue)
{
	struct ena_com_dev *ena_dev = admin_queue->ena_dev;
	size_t size = admin_queue->q_depth * sizeof(struct ena_comp_ctx);
	struct ena_comp_ctx *comp_ctx;
	u16 i;

	admin_queue->comp_ctx =
		devm_kzalloc(admin_queue->q_dmadev, size, GFP_KERNEL);
	if (unlikely(!admin_queue->comp_ctx)) {
		netdev_err(ena_dev->net_device, "Memory allocation failed\n");
		return -ENOMEM;
	}

	for (i = 0; i < admin_queue->q_depth; i++) {
		comp_ctx = get_comp_ctxt(admin_queue, i, false);
		if (comp_ctx)
			init_completion(&comp_ctx->wait_event);
	}

	return 0;
}

static struct ena_comp_ctx *ena_com_submit_admin_cmd(struct ena_com_admin_queue *admin_queue,
						     struct ena_admin_aq_entry *cmd,
						     size_t cmd_size_in_bytes,
						     struct ena_admin_acq_entry *comp,
						     size_t comp_size_in_bytes)
{
	unsigned long flags = 0;
	struct ena_comp_ctx *comp_ctx;

	spin_lock_irqsave(&admin_queue->q_lock, flags);
	if (unlikely(!admin_queue->running_state)) {
		spin_unlock_irqrestore(&admin_queue->q_lock, flags);
		return ERR_PTR(-ENODEV);
	}
	comp_ctx = __ena_com_submit_admin_cmd(admin_queue, cmd,
					      cmd_size_in_bytes,
					      comp,
					      comp_size_in_bytes);
	if (IS_ERR(comp_ctx))
		admin_queue->running_state = false;
	spin_unlock_irqrestore(&admin_queue->q_lock, flags);

	return comp_ctx;
}

static int ena_com_init_io_sq(struct ena_com_dev *ena_dev,
			      struct ena_com_create_io_ctx *ctx,
			      struct ena_com_io_sq *io_sq)
{
	size_t size;
	int dev_node = 0;

	memset(&io_sq->desc_addr, 0x0, sizeof(io_sq->desc_addr));

	io_sq->dma_addr_bits = (u8)ena_dev->dma_addr_bits;
	io_sq->desc_entry_size =
		(io_sq->direction == ENA_COM_IO_QUEUE_DIRECTION_TX) ?
		sizeof(struct ena_eth_io_tx_desc) :
		sizeof(struct ena_eth_io_rx_desc);

	size = io_sq->desc_entry_size * io_sq->q_depth;

	if (io_sq->mem_queue_type == ENA_ADMIN_PLACEMENT_POLICY_HOST) {
		dev_node = dev_to_node(ena_dev->dmadev);
		set_dev_node(ena_dev->dmadev, ctx->numa_node);
		io_sq->desc_addr.virt_addr =
			dma_alloc_coherent(ena_dev->dmadev, size,
					   &io_sq->desc_addr.phys_addr,
					   GFP_KERNEL);
		set_dev_node(ena_dev->dmadev, dev_node);
		if (!io_sq->desc_addr.virt_addr) {
			io_sq->desc_addr.virt_addr =
				dma_alloc_coherent(ena_dev->dmadev, size,
						   &io_sq->desc_addr.phys_addr,
						   GFP_KERNEL);
		}

		if (!io_sq->desc_addr.virt_addr) {
			netdev_err(ena_dev->net_device,
				   "Memory allocation failed\n");
			return -ENOMEM;
		}
	}

	if (io_sq->mem_queue_type == ENA_ADMIN_PLACEMENT_POLICY_DEV) {
		/* Allocate bounce buffers */
		io_sq->bounce_buf_ctrl.buffer_size =
			ena_dev->llq_info.desc_list_entry_size;
		io_sq->bounce_buf_ctrl.buffers_num =
			ENA_COM_BOUNCE_BUFFER_CNTRL_CNT;
		io_sq->bounce_buf_ctrl.next_to_use = 0;

		size = (size_t)io_sq->bounce_buf_ctrl.buffer_size *
			io_sq->bounce_buf_ctrl.buffers_num;

		dev_node = dev_to_node(ena_dev->dmadev);
		set_dev_node(ena_dev->dmadev, ctx->numa_node);
		io_sq->bounce_buf_ctrl.base_buffer =
			devm_kzalloc(ena_dev->dmadev, size, GFP_KERNEL);
		set_dev_node(ena_dev->dmadev, dev_node);
		if (!io_sq->bounce_buf_ctrl.base_buffer)
			io_sq->bounce_buf_ctrl.base_buffer =
				devm_kzalloc(ena_dev->dmadev, size, GFP_KERNEL);

		if (!io_sq->bounce_buf_ctrl.base_buffer) {
			netdev_err(ena_dev->net_device,
				   "Bounce buffer memory allocation failed\n");
			return -ENOMEM;
		}

		memcpy(&io_sq->llq_info, &ena_dev->llq_info,
		       sizeof(io_sq->llq_info));

		/* Initiate the first bounce buffer */
		io_sq->llq_buf_ctrl.curr_bounce_buf =
			ena_com_get_next_bounce_buffer(&io_sq->bounce_buf_ctrl);
		memset(io_sq->llq_buf_ctrl.curr_bounce_buf,
		       0x0, io_sq->llq_info.desc_list_entry_size);
		io_sq->llq_buf_ctrl.descs_left_in_line =
			io_sq->llq_info.descs_num_before_header;
		io_sq->disable_meta_caching =
			io_sq->llq_info.disable_meta_caching;

		if (io_sq->llq_info.max_entries_in_tx_burst > 0)
			io_sq->entries_in_tx_burst_left =
				io_sq->llq_info.max_entries_in_tx_burst;
	}

	io_sq->tail = 0;
	io_sq->next_to_comp = 0;
	io_sq->phase = 1;

	return 0;
}

static int ena_com_init_io_cq(struct ena_com_dev *ena_dev,
			      struct ena_com_create_io_ctx *ctx,
			      struct ena_com_io_cq *io_cq)
{
	size_t size;
	int prev_node = 0;

	memset(&io_cq->cdesc_addr, 0x0, sizeof(io_cq->cdesc_addr));

	/* Use the basic completion descriptor for Rx */
	io_cq->cdesc_entry_size_in_bytes =
		(io_cq->direction == ENA_COM_IO_QUEUE_DIRECTION_TX) ?
		sizeof(struct ena_eth_io_tx_cdesc) :
		sizeof(struct ena_eth_io_rx_cdesc_base);

	size = io_cq->cdesc_entry_size_in_bytes * io_cq->q_depth;

	prev_node = dev_to_node(ena_dev->dmadev);
	set_dev_node(ena_dev->dmadev, ctx->numa_node);
	io_cq->cdesc_addr.virt_addr =
		dma_alloc_coherent(ena_dev->dmadev, size,
				   &io_cq->cdesc_addr.phys_addr, GFP_KERNEL);
	set_dev_node(ena_dev->dmadev, prev_node);
	if (!io_cq->cdesc_addr.virt_addr) {
		io_cq->cdesc_addr.virt_addr =
			dma_alloc_coherent(ena_dev->dmadev, size,
					   &io_cq->cdesc_addr.phys_addr,
					   GFP_KERNEL);
	}

	if (!io_cq->cdesc_addr.virt_addr) {
		netdev_err(ena_dev->net_device, "Memory allocation failed\n");
		return -ENOMEM;
	}

	io_cq->phase = 1;
	io_cq->head = 0;

	return 0;
}

static void ena_com_handle_single_admin_completion(struct ena_com_admin_queue *admin_queue,
						   struct ena_admin_acq_entry *cqe)
{
	struct ena_comp_ctx *comp_ctx;
	u16 cmd_id;

	cmd_id = cqe->acq_common_descriptor.command &
		ENA_ADMIN_ACQ_COMMON_DESC_COMMAND_ID_MASK;

	comp_ctx = get_comp_ctxt(admin_queue, cmd_id, false);
	if (unlikely(!comp_ctx)) {
		netdev_err(admin_queue->ena_dev->net_device,
			   "comp_ctx is NULL. Changing the admin queue running state\n");
		admin_queue->running_state = false;
		return;
	}

	comp_ctx->status = ENA_CMD_COMPLETED;
	comp_ctx->comp_status = cqe->acq_common_descriptor.status;

	if (comp_ctx->user_cqe)
		memcpy(comp_ctx->user_cqe, (void *)cqe, comp_ctx->comp_size);

	if (!admin_queue->polling)
		complete(&comp_ctx->wait_event);
}

static void ena_com_handle_admin_completion(struct ena_com_admin_queue *admin_queue)
{
	struct ena_admin_acq_entry *cqe = NULL;
	u16 comp_num = 0;
	u16 head_masked;
	u8 phase;

	head_masked = admin_queue->cq.head & (admin_queue->q_depth - 1);
	phase = admin_queue->cq.phase;

	cqe = &admin_queue->cq.entries[head_masked];

	/* Go over all the completions */
	while ((READ_ONCE(cqe->acq_common_descriptor.flags) &
		ENA_ADMIN_ACQ_COMMON_DESC_PHASE_MASK) == phase) {
		/* Do not read the rest of the completion entry before the
		 * phase bit was validated
		 */
		dma_rmb();
		ena_com_handle_single_admin_completion(admin_queue, cqe);

		head_masked++;
		comp_num++;
		if (unlikely(head_masked == admin_queue->q_depth)) {
			head_masked = 0;
			phase = !phase;
		}

		cqe = &admin_queue->cq.entries[head_masked];
	}

	admin_queue->cq.head += comp_num;
	admin_queue->cq.phase = phase;
	admin_queue->sq.head += comp_num;
	admin_queue->stats.completed_cmd += comp_num;
}

static int ena_com_comp_status_to_errno(struct ena_com_admin_queue *admin_queue,
					u8 comp_status)
{
	if (unlikely(comp_status != 0))
		netdev_err(admin_queue->ena_dev->net_device,
			   "Admin command failed[%u]\n", comp_status);

	switch (comp_status) {
	case ENA_ADMIN_SUCCESS:
		return 0;
	case ENA_ADMIN_RESOURCE_ALLOCATION_FAILURE:
		return -ENOMEM;
	case ENA_ADMIN_UNSUPPORTED_OPCODE:
		return -EOPNOTSUPP;
	case ENA_ADMIN_BAD_OPCODE:
	case ENA_ADMIN_MALFORMED_REQUEST:
	case ENA_ADMIN_ILLEGAL_PARAMETER:
	case ENA_ADMIN_UNKNOWN_ERROR:
		return -EINVAL;
	case ENA_ADMIN_RESOURCE_BUSY:
		return -EAGAIN;
	}

	return -EINVAL;
}

static void ena_delay_exponential_backoff_us(u32 exp, u32 delay_us)
{
	exp = min_t(u32, exp, ENA_MAX_BACKOFF_DELAY_EXP);
	delay_us = max_t(u32, ENA_MIN_ADMIN_POLL_US, delay_us);
	delay_us = min_t(u32, delay_us * (1U << exp), ENA_MAX_ADMIN_POLL_US);
	usleep_range(delay_us, 2 * delay_us);
}

static int ena_com_wait_and_process_admin_cq_polling(struct ena_comp_ctx *comp_ctx,
						     struct ena_com_admin_queue *admin_queue)
{
	unsigned long flags = 0;
	unsigned long timeout;
	int ret;
	u32 exp = 0;

	timeout = jiffies + usecs_to_jiffies(admin_queue->completion_timeout);

	while (1) {
		spin_lock_irqsave(&admin_queue->q_lock, flags);
		ena_com_handle_admin_completion(admin_queue);
		spin_unlock_irqrestore(&admin_queue->q_lock, flags);

		if (comp_ctx->status != ENA_CMD_SUBMITTED)
			break;

		if (time_is_before_jiffies(timeout)) {
			netdev_err(admin_queue->ena_dev->net_device,
				   "Wait for completion (polling) timeout\n");
			/* ENA didn't have any completion */
			spin_lock_irqsave(&admin_queue->q_lock, flags);
			admin_queue->stats.no_completion++;
			admin_queue->running_state = false;
			spin_unlock_irqrestore(&admin_queue->q_lock, flags);

			ret = -ETIME;
			goto err;
		}

		ena_delay_exponential_backoff_us(exp++,
						 admin_queue->ena_dev->ena_min_poll_delay_us);
	}

	if (unlikely(comp_ctx->status == ENA_CMD_ABORTED)) {
		netdev_err(admin_queue->ena_dev->net_device,
			   "Command was aborted\n");
		spin_lock_irqsave(&admin_queue->q_lock, flags);
		admin_queue->stats.aborted_cmd++;
		spin_unlock_irqrestore(&admin_queue->q_lock, flags);
		ret = -ENODEV;
		goto err;
	}

	WARN(comp_ctx->status != ENA_CMD_COMPLETED, "Invalid comp status %d\n",
	     comp_ctx->status);

	ret = ena_com_comp_status_to_errno(admin_queue, comp_ctx->comp_status);
err:
	comp_ctxt_release(admin_queue, comp_ctx);
	return ret;
}

/*
 * Set the LLQ configurations of the firmware
 *
 * The driver provides only the enabled feature values to the device,
 * which in turn, checks if they are supported.
 */
static int ena_com_set_llq(struct ena_com_dev *ena_dev)
{
	struct ena_com_admin_queue *admin_queue;
	struct ena_admin_set_feat_cmd cmd;
	struct ena_admin_set_feat_resp resp;
	struct ena_com_llq_info *llq_info = &ena_dev->llq_info;
	int ret;

	memset(&cmd, 0x0, sizeof(cmd));
	admin_queue = &ena_dev->admin_queue;

	cmd.aq_common_descriptor.opcode = ENA_ADMIN_SET_FEATURE;
	cmd.feat_common.feature_id = ENA_ADMIN_LLQ;

	cmd.u.llq.header_location_ctrl_enabled = llq_info->header_location_ctrl;
	cmd.u.llq.entry_size_ctrl_enabled = llq_info->desc_list_entry_size_ctrl;
	cmd.u.llq.desc_num_before_header_enabled = llq_info->descs_num_before_header;
	cmd.u.llq.descriptors_stride_ctrl_enabled = llq_info->desc_stride_ctrl;

	cmd.u.llq.accel_mode.u.set.enabled_flags =
		BIT(ENA_ADMIN_DISABLE_META_CACHING) |
		BIT(ENA_ADMIN_LIMIT_TX_BURST);

	ret = ena_com_execute_admin_command(admin_queue,
					    (struct ena_admin_aq_entry *)&cmd,
					    sizeof(cmd),
					    (struct ena_admin_acq_entry *)&resp,
					    sizeof(resp));

	if (unlikely(ret))
		netdev_err(ena_dev->net_device,
			   "Failed to set LLQ configurations: %d\n", ret);

	return ret;
}

static int ena_com_config_llq_info(struct ena_com_dev *ena_dev,
				   struct ena_admin_feature_llq_desc *llq_features,
				   struct ena_llq_configurations *llq_default_cfg)
{
	struct ena_com_llq_info *llq_info = &ena_dev->llq_info;
	struct ena_admin_accel_mode_get llq_accel_mode_get;
	u16 supported_feat;
	int rc;

	memset(llq_info, 0, sizeof(*llq_info));

	supported_feat = llq_features->header_location_ctrl_supported;

	if (likely(supported_feat & llq_default_cfg->llq_header_location)) {
		llq_info->header_location_ctrl =
			llq_default_cfg->llq_header_location;
	} else {
		netdev_err(ena_dev->net_device,
			   "Invalid header location control, supported: 0x%x\n",
			   supported_feat);
		return -EINVAL;
	}

	if (likely(llq_info->header_location_ctrl == ENA_ADMIN_INLINE_HEADER)) {
		supported_feat = llq_features->descriptors_stride_ctrl_supported;
		if (likely(supported_feat & llq_default_cfg->llq_stride_ctrl)) {
			llq_info->desc_stride_ctrl = llq_default_cfg->llq_stride_ctrl;
		} else	{
			if (supported_feat & ENA_ADMIN_MULTIPLE_DESCS_PER_ENTRY) {
				llq_info->desc_stride_ctrl = ENA_ADMIN_MULTIPLE_DESCS_PER_ENTRY;
			} else if (supported_feat & ENA_ADMIN_SINGLE_DESC_PER_ENTRY) {
				llq_info->desc_stride_ctrl = ENA_ADMIN_SINGLE_DESC_PER_ENTRY;
			} else {
				netdev_err(ena_dev->net_device,
					   "Invalid desc_stride_ctrl, supported: 0x%x\n",
					   supported_feat);
				return -EINVAL;
			}

			netdev_err(ena_dev->net_device,
				   "Default llq stride ctrl is not supported, performing fallback, default: 0x%x, supported: 0x%x, used: 0x%x\n",
				   llq_default_cfg->llq_stride_ctrl,
				   supported_feat, llq_info->desc_stride_ctrl);
		}
	} else {
		llq_info->desc_stride_ctrl = 0;
	}

	supported_feat = llq_features->entry_size_ctrl_supported;
	if (likely(supported_feat & llq_default_cfg->llq_ring_entry_size)) {
		llq_info->desc_list_entry_size_ctrl = llq_default_cfg->llq_ring_entry_size;
		llq_info->desc_list_entry_size = llq_default_cfg->llq_ring_entry_size_value;
	} else {
		if (supported_feat & ENA_ADMIN_LIST_ENTRY_SIZE_128B) {
			llq_info->desc_list_entry_size_ctrl = ENA_ADMIN_LIST_ENTRY_SIZE_128B;
			llq_info->desc_list_entry_size = 128;
		} else if (supported_feat & ENA_ADMIN_LIST_ENTRY_SIZE_192B) {
			llq_info->desc_list_entry_size_ctrl = ENA_ADMIN_LIST_ENTRY_SIZE_192B;
			llq_info->desc_list_entry_size = 192;
		} else if (supported_feat & ENA_ADMIN_LIST_ENTRY_SIZE_256B) {
			llq_info->desc_list_entry_size_ctrl = ENA_ADMIN_LIST_ENTRY_SIZE_256B;
			llq_info->desc_list_entry_size = 256;
		} else {
			netdev_err(ena_dev->net_device,
				   "Invalid entry_size_ctrl, supported: 0x%x\n",
				   supported_feat);
			return -EINVAL;
		}

		netdev_err(ena_dev->net_device,
			   "Default llq ring entry size is not supported, performing fallback, default: 0x%x, supported: 0x%x, used: 0x%x\n",
			   llq_default_cfg->llq_ring_entry_size, supported_feat,
			   llq_info->desc_list_entry_size);
	}
	if (unlikely(llq_info->desc_list_entry_size & 0x7)) {
		/* The desc list entry size should be whole multiply of 8
		 * This requirement comes from __iowrite64_copy()
		 */
		netdev_err(ena_dev->net_device, "Illegal entry size %d\n",
			   llq_info->desc_list_entry_size);
		return -EINVAL;
	}

	if (llq_info->desc_stride_ctrl == ENA_ADMIN_MULTIPLE_DESCS_PER_ENTRY)
		llq_info->descs_per_entry = llq_info->desc_list_entry_size /
			sizeof(struct ena_eth_io_tx_desc);
	else
		llq_info->descs_per_entry = 1;

	supported_feat = llq_features->desc_num_before_header_supported;
	if (likely(supported_feat & llq_default_cfg->llq_num_decs_before_header)) {
		llq_info->descs_num_before_header = llq_default_cfg->llq_num_decs_before_header;
	} else {
		if (supported_feat & ENA_ADMIN_LLQ_NUM_DESCS_BEFORE_HEADER_2) {
			llq_info->descs_num_before_header = ENA_ADMIN_LLQ_NUM_DESCS_BEFORE_HEADER_2;
		} else if (supported_feat & ENA_ADMIN_LLQ_NUM_DESCS_BEFORE_HEADER_1) {
			llq_info->descs_num_before_header = ENA_ADMIN_LLQ_NUM_DESCS_BEFORE_HEADER_1;
		} else if (supported_feat & ENA_ADMIN_LLQ_NUM_DESCS_BEFORE_HEADER_4) {
			llq_info->descs_num_before_header = ENA_ADMIN_LLQ_NUM_DESCS_BEFORE_HEADER_4;
		} else if (supported_feat & ENA_ADMIN_LLQ_NUM_DESCS_BEFORE_HEADER_8) {
			llq_info->descs_num_before_header = ENA_ADMIN_LLQ_NUM_DESCS_BEFORE_HEADER_8;
		} else {
			netdev_err(ena_dev->net_device,
				   "Invalid descs_num_before_header, supported: 0x%x\n",
				   supported_feat);
			return -EINVAL;
		}

		netdev_err(ena_dev->net_device,
			   "Default llq num descs before header is not supported, performing fallback, default: 0x%x, supported: 0x%x, used: 0x%x\n",
			   llq_default_cfg->llq_num_decs_before_header,
			   supported_feat, llq_info->descs_num_before_header);
	}
	/* Check for accelerated queue supported */
	llq_accel_mode_get = llq_features->accel_mode.u.get;

	llq_info->disable_meta_caching =
		!!(llq_accel_mode_get.supported_flags &
		   BIT(ENA_ADMIN_DISABLE_META_CACHING));

	if (llq_accel_mode_get.supported_flags & BIT(ENA_ADMIN_LIMIT_TX_BURST))
		llq_info->max_entries_in_tx_burst =
			llq_accel_mode_get.max_tx_burst_size /
			llq_default_cfg->llq_ring_entry_size_value;

	rc = ena_com_set_llq(ena_dev);
	if (rc)
		netdev_err(ena_dev->net_device,
			   "Cannot set LLQ configuration: %d\n", rc);

	return rc;
}

static int ena_com_wait_and_process_admin_cq_interrupts(struct ena_comp_ctx *comp_ctx,
							struct ena_com_admin_queue *admin_queue)
{
	unsigned long flags = 0;
	int ret;

	wait_for_completion_timeout(&comp_ctx->wait_event,
				    usecs_to_jiffies(
					    admin_queue->completion_timeout));

	/* In case the command wasn't completed find out the root cause.
	 * There might be 2 kinds of errors
	 * 1) No completion (timeout reached)
	 * 2) There is completion but the device didn't get any msi-x interrupt.
	 */
	if (unlikely(comp_ctx->status == ENA_CMD_SUBMITTED)) {
		spin_lock_irqsave(&admin_queue->q_lock, flags);
		ena_com_handle_admin_completion(admin_queue);
		admin_queue->stats.no_completion++;
		spin_unlock_irqrestore(&admin_queue->q_lock, flags);

		if (comp_ctx->status == ENA_CMD_COMPLETED) {
			netdev_err(admin_queue->ena_dev->net_device,
				   "The ena device sent a completion but the driver didn't receive a MSI-X interrupt (cmd %d), autopolling mode is %s\n",
				   comp_ctx->cmd_opcode,
				   admin_queue->auto_polling ? "ON" : "OFF");
			/* Check if fallback to polling is enabled */
			if (admin_queue->auto_polling)
				admin_queue->polling = true;
		} else {
			netdev_err(admin_queue->ena_dev->net_device,
				   "The ena device didn't send a completion for the admin cmd %d status %d\n",
				   comp_ctx->cmd_opcode, comp_ctx->status);
		}
		/* Check if shifted to polling mode.
		 * This will happen if there is a completion without an interrupt
		 * and autopolling mode is enabled. Continuing normal execution in such case
		 */
		if (!admin_queue->polling) {
			admin_queue->running_state = false;
			ret = -ETIME;
			goto err;
		}
	}

	ret = ena_com_comp_status_to_errno(admin_queue, comp_ctx->comp_status);
err:
	comp_ctxt_release(admin_queue, comp_ctx);
	return ret;
}

/* This method read the hardware device register through posting writes
 * and waiting for response
 * On timeout the function will return ENA_MMIO_READ_TIMEOUT
 */
static u32 ena_com_reg_bar_read32(struct ena_com_dev *ena_dev, u16 offset)
{
	struct ena_com_mmio_read *mmio_read = &ena_dev->mmio_read;
	volatile struct ena_admin_ena_mmio_req_read_less_resp *read_resp =
		mmio_read->read_resp;
	u32 mmio_read_reg, ret, i;
	unsigned long flags = 0;
	u32 timeout = mmio_read->reg_read_to;

	might_sleep();

	if (timeout == 0)
		timeout = ENA_REG_READ_TIMEOUT;

	/* If readless is disabled, perform regular read */
	if (!mmio_read->readless_supported)
		return readl(ena_dev->reg_bar + offset);

	spin_lock_irqsave(&mmio_read->lock, flags);
	mmio_read->seq_num++;

	read_resp->req_id = mmio_read->seq_num + 0xDEAD;
	mmio_read_reg = (offset << ENA_REGS_MMIO_REG_READ_REG_OFF_SHIFT) &
			ENA_REGS_MMIO_REG_READ_REG_OFF_MASK;
	mmio_read_reg |= mmio_read->seq_num &
			ENA_REGS_MMIO_REG_READ_REQ_ID_MASK;

	writel(mmio_read_reg, ena_dev->reg_bar + ENA_REGS_MMIO_REG_READ_OFF);

	for (i = 0; i < timeout; i++) {
		if (READ_ONCE(read_resp->req_id) == mmio_read->seq_num)
			break;

		udelay(1);
	}

	if (unlikely(i == timeout)) {
		netdev_err(ena_dev->net_device,
			   "Reading reg failed for timeout. expected: req id[%u] offset[%u] actual: req id[%u] offset[%u]\n",
			   mmio_read->seq_num, offset, read_resp->req_id,
			   read_resp->reg_off);
		ret = ENA_MMIO_READ_TIMEOUT;
		goto err;
	}

	if (read_resp->reg_off != offset) {
		netdev_err(ena_dev->net_device,
			   "Read failure: wrong offset provided\n");
		ret = ENA_MMIO_READ_TIMEOUT;
	} else {
		ret = read_resp->reg_val;
	}
err:
	spin_unlock_irqrestore(&mmio_read->lock, flags);

	return ret;
}

/* There are two types to wait for completion.
 * Polling mode - wait until the completion is available.
 * Async mode - wait on wait queue until the completion is ready
 * (or the timeout expired).
 * It is expected that the IRQ called ena_com_handle_admin_completion
 * to mark the completions.
 */
static int ena_com_wait_and_process_admin_cq(struct ena_comp_ctx *comp_ctx,
					     struct ena_com_admin_queue *admin_queue)
{
	if (admin_queue->polling)
		return ena_com_wait_and_process_admin_cq_polling(comp_ctx,
								 admin_queue);

	return ena_com_wait_and_process_admin_cq_interrupts(comp_ctx,
							    admin_queue);
}

static int ena_com_destroy_io_sq(struct ena_com_dev *ena_dev,
				 struct ena_com_io_sq *io_sq)
{
	struct ena_com_admin_queue *admin_queue = &ena_dev->admin_queue;
	struct ena_admin_aq_destroy_sq_cmd destroy_cmd;
	struct ena_admin_acq_destroy_sq_resp_desc destroy_resp;
	u8 direction;
	int ret;

	memset(&destroy_cmd, 0x0, sizeof(destroy_cmd));

	if (io_sq->direction == ENA_COM_IO_QUEUE_DIRECTION_TX)
		direction = ENA_ADMIN_SQ_DIRECTION_TX;
	else
		direction = ENA_ADMIN_SQ_DIRECTION_RX;

	destroy_cmd.sq.sq_identity |= (direction <<
		ENA_ADMIN_SQ_SQ_DIRECTION_SHIFT) &
		ENA_ADMIN_SQ_SQ_DIRECTION_MASK;

	destroy_cmd.sq.sq_idx = io_sq->idx;
	destroy_cmd.aq_common_descriptor.opcode = ENA_ADMIN_DESTROY_SQ;

	ret = ena_com_execute_admin_command(admin_queue,
					    (struct ena_admin_aq_entry *)&destroy_cmd,
					    sizeof(destroy_cmd),
					    (struct ena_admin_acq_entry *)&destroy_resp,
					    sizeof(destroy_resp));

	if (unlikely(ret && (ret != -ENODEV)))
		netdev_err(ena_dev->net_device,
			   "Failed to destroy io sq error: %d\n", ret);

	return ret;
}

static void ena_com_io_queue_free(struct ena_com_dev *ena_dev,
				  struct ena_com_io_sq *io_sq,
				  struct ena_com_io_cq *io_cq)
{
	size_t size;

	if (io_cq->cdesc_addr.virt_addr) {
		size = io_cq->cdesc_entry_size_in_bytes * io_cq->q_depth;

		dma_free_coherent(ena_dev->dmadev, size,
				  io_cq->cdesc_addr.virt_addr,
				  io_cq->cdesc_addr.phys_addr);

		io_cq->cdesc_addr.virt_addr = NULL;
	}

	if (io_sq->desc_addr.virt_addr) {
		size = io_sq->desc_entry_size * io_sq->q_depth;

		dma_free_coherent(ena_dev->dmadev, size,
				  io_sq->desc_addr.virt_addr,
				  io_sq->desc_addr.phys_addr);

		io_sq->desc_addr.virt_addr = NULL;
	}

	if (io_sq->bounce_buf_ctrl.base_buffer) {
		devm_kfree(ena_dev->dmadev, io_sq->bounce_buf_ctrl.base_buffer);
		io_sq->bounce_buf_ctrl.base_buffer = NULL;
	}
}

static int wait_for_reset_state(struct ena_com_dev *ena_dev, u32 timeout,
				u16 exp_state)
{
	u32 val, exp = 0;
	unsigned long timeout_stamp;

	/* Convert timeout from resolution of 100ms to us resolution. */
	timeout_stamp = jiffies + usecs_to_jiffies(100 * 1000 * timeout);

	while (1) {
		val = ena_com_reg_bar_read32(ena_dev, ENA_REGS_DEV_STS_OFF);

		if (unlikely(val == ENA_MMIO_READ_TIMEOUT)) {
			netdev_err(ena_dev->net_device,
				   "Reg read timeout occurred\n");
			return -ETIME;
		}

		if ((val & ENA_REGS_DEV_STS_RESET_IN_PROGRESS_MASK) ==
			exp_state)
			return 0;

		if (time_is_before_jiffies(timeout_stamp))
			return -ETIME;

		ena_delay_exponential_backoff_us(exp++, ena_dev->ena_min_poll_delay_us);
	}
}

static bool ena_com_check_supported_feature_id(struct ena_com_dev *ena_dev,
					       enum ena_admin_aq_feature_id feature_id)
{
	u32 feature_mask = 1 << feature_id;

	/* Device attributes is always supported */
	if ((feature_id != ENA_ADMIN_DEVICE_ATTRIBUTES) &&
	    !(ena_dev->supported_features & feature_mask))
		return false;

	return true;
}

static int ena_com_get_feature_ex(struct ena_com_dev *ena_dev,
				  struct ena_admin_get_feat_resp *get_resp,
				  enum ena_admin_aq_feature_id feature_id,
				  dma_addr_t control_buf_dma_addr,
				  u32 control_buff_size,
				  u8 feature_ver)
{
	struct ena_com_admin_queue *admin_queue;
	struct ena_admin_get_feat_cmd get_cmd;
	int ret;

	if (!ena_com_check_supported_feature_id(ena_dev, feature_id)) {
		netdev_dbg(ena_dev->net_device, "Feature %d isn't supported\n",
			   feature_id);
		return -EOPNOTSUPP;
	}

	memset(&get_cmd, 0x0, sizeof(get_cmd));
	admin_queue = &ena_dev->admin_queue;

	get_cmd.aq_common_descriptor.opcode = ENA_ADMIN_GET_FEATURE;

	if (control_buff_size)
		get_cmd.aq_common_descriptor.flags =
			ENA_ADMIN_AQ_COMMON_DESC_CTRL_DATA_INDIRECT_MASK;
	else
		get_cmd.aq_common_descriptor.flags = 0;

	ret = ena_com_mem_addr_set(ena_dev,
				   &get_cmd.control_buffer.address,
				   control_buf_dma_addr);
	if (unlikely(ret)) {
		netdev_err(ena_dev->net_device, "Memory address set failed\n");
		return ret;
	}

	get_cmd.control_buffer.length = control_buff_size;
	get_cmd.feat_common.feature_version = feature_ver;
	get_cmd.feat_common.feature_id = feature_id;

	ret = ena_com_execute_admin_command(admin_queue,
					    (struct ena_admin_aq_entry *)
					    &get_cmd,
					    sizeof(get_cmd),
					    (struct ena_admin_acq_entry *)
					    get_resp,
					    sizeof(*get_resp));

	if (unlikely(ret))
		netdev_err(ena_dev->net_device,
			   "Failed to submit get_feature command %d error: %d\n",
			   feature_id, ret);

	return ret;
}

static int ena_com_get_feature(struct ena_com_dev *ena_dev,
			       struct ena_admin_get_feat_resp *get_resp,
			       enum ena_admin_aq_feature_id feature_id,
			       u8 feature_ver)
{
	return ena_com_get_feature_ex(ena_dev,
				      get_resp,
				      feature_id,
				      0,
				      0,
				      feature_ver);
}

int ena_com_get_current_hash_function(struct ena_com_dev *ena_dev)
{
	return ena_dev->rss.hash_func;
}

static void ena_com_hash_key_fill_default_key(struct ena_com_dev *ena_dev)
{
	struct ena_admin_feature_rss_flow_hash_control *hash_key =
		(ena_dev->rss).hash_key;

	netdev_rss_key_fill(&hash_key->key, sizeof(hash_key->key));
	/* The key buffer is stored in the device in an array of
	 * uint32 elements.
	 */
	hash_key->key_parts = ENA_ADMIN_RSS_KEY_PARTS;
}

static int ena_com_hash_key_allocate(struct ena_com_dev *ena_dev)
{
	struct ena_rss *rss = &ena_dev->rss;

	if (!ena_com_check_supported_feature_id(ena_dev,
						ENA_ADMIN_RSS_HASH_FUNCTION))
		return -EOPNOTSUPP;

	rss->hash_key =
		dma_alloc_coherent(ena_dev->dmadev, sizeof(*rss->hash_key),
				   &rss->hash_key_dma_addr, GFP_KERNEL);

	if (unlikely(!rss->hash_key))
		return -ENOMEM;

	return 0;
}

static void ena_com_hash_key_destroy(struct ena_com_dev *ena_dev)
{
	struct ena_rss *rss = &ena_dev->rss;

	if (rss->hash_key)
		dma_free_coherent(ena_dev->dmadev, sizeof(*rss->hash_key),
				  rss->hash_key, rss->hash_key_dma_addr);
	rss->hash_key = NULL;
}

static int ena_com_hash_ctrl_init(struct ena_com_dev *ena_dev)
{
	struct ena_rss *rss = &ena_dev->rss;

	rss->hash_ctrl =
		dma_alloc_coherent(ena_dev->dmadev, sizeof(*rss->hash_ctrl),
				   &rss->hash_ctrl_dma_addr, GFP_KERNEL);

	if (unlikely(!rss->hash_ctrl))
		return -ENOMEM;

	return 0;
}

static void ena_com_hash_ctrl_destroy(struct ena_com_dev *ena_dev)
{
	struct ena_rss *rss = &ena_dev->rss;

	if (rss->hash_ctrl)
		dma_free_coherent(ena_dev->dmadev, sizeof(*rss->hash_ctrl),
				  rss->hash_ctrl, rss->hash_ctrl_dma_addr);
	rss->hash_ctrl = NULL;
}

static int ena_com_indirect_table_allocate(struct ena_com_dev *ena_dev,
					   u16 log_size)
{
	struct ena_rss *rss = &ena_dev->rss;
	struct ena_admin_get_feat_resp get_resp;
	size_t tbl_size;
	int ret;

	ret = ena_com_get_feature(ena_dev, &get_resp,
				  ENA_ADMIN_RSS_INDIRECTION_TABLE_CONFIG, 0);
	if (unlikely(ret))
		return ret;

	if ((get_resp.u.ind_table.min_size > log_size) ||
	    (get_resp.u.ind_table.max_size < log_size)) {
		netdev_err(ena_dev->net_device,
			   "Indirect table size doesn't fit. requested size: %d while min is:%d and max %d\n",
			   1 << log_size, 1 << get_resp.u.ind_table.min_size,
			   1 << get_resp.u.ind_table.max_size);
		return -EINVAL;
	}

	tbl_size = (1ULL << log_size) *
		sizeof(struct ena_admin_rss_ind_table_entry);

	rss->rss_ind_tbl =
		dma_alloc_coherent(ena_dev->dmadev, tbl_size,
				   &rss->rss_ind_tbl_dma_addr, GFP_KERNEL);
	if (unlikely(!rss->rss_ind_tbl))
		goto mem_err1;

	tbl_size = (1ULL << log_size) * sizeof(u16);
	rss->host_rss_ind_tbl =
		devm_kzalloc(ena_dev->dmadev, tbl_size, GFP_KERNEL);
	if (unlikely(!rss->host_rss_ind_tbl))
		goto mem_err2;

	rss->tbl_log_size = log_size;

	return 0;

mem_err2:
	tbl_size = (1ULL << log_size) *
		sizeof(struct ena_admin_rss_ind_table_entry);

	dma_free_coherent(ena_dev->dmadev, tbl_size, rss->rss_ind_tbl,
			  rss->rss_ind_tbl_dma_addr);
	rss->rss_ind_tbl = NULL;
mem_err1:
	rss->tbl_log_size = 0;
	return -ENOMEM;
}

static void ena_com_indirect_table_destroy(struct ena_com_dev *ena_dev)
{
	struct ena_rss *rss = &ena_dev->rss;
	size_t tbl_size = (1ULL << rss->tbl_log_size) *
		sizeof(struct ena_admin_rss_ind_table_entry);

	if (rss->rss_ind_tbl)
		dma_free_coherent(ena_dev->dmadev, tbl_size, rss->rss_ind_tbl,
				  rss->rss_ind_tbl_dma_addr);
	rss->rss_ind_tbl = NULL;

	if (rss->host_rss_ind_tbl)
		devm_kfree(ena_dev->dmadev, rss->host_rss_ind_tbl);
	rss->host_rss_ind_tbl = NULL;
}

static int ena_com_create_io_sq(struct ena_com_dev *ena_dev,
				struct ena_com_io_sq *io_sq, u16 cq_idx)
{
	struct ena_com_admin_queue *admin_queue = &ena_dev->admin_queue;
	struct ena_admin_aq_create_sq_cmd create_cmd;
	struct ena_admin_acq_create_sq_resp_desc cmd_completion;
	u8 direction;
	int ret;

	memset(&create_cmd, 0x0, sizeof(create_cmd));

	create_cmd.aq_common_descriptor.opcode = ENA_ADMIN_CREATE_SQ;

	if (io_sq->direction == ENA_COM_IO_QUEUE_DIRECTION_TX)
		direction = ENA_ADMIN_SQ_DIRECTION_TX;
	else
		direction = ENA_ADMIN_SQ_DIRECTION_RX;

	create_cmd.sq_identity |= (direction <<
		ENA_ADMIN_AQ_CREATE_SQ_CMD_SQ_DIRECTION_SHIFT) &
		ENA_ADMIN_AQ_CREATE_SQ_CMD_SQ_DIRECTION_MASK;

	create_cmd.sq_caps_2 |= io_sq->mem_queue_type &
		ENA_ADMIN_AQ_CREATE_SQ_CMD_PLACEMENT_POLICY_MASK;

	create_cmd.sq_caps_2 |= (ENA_ADMIN_COMPLETION_POLICY_DESC <<
		ENA_ADMIN_AQ_CREATE_SQ_CMD_COMPLETION_POLICY_SHIFT) &
		ENA_ADMIN_AQ_CREATE_SQ_CMD_COMPLETION_POLICY_MASK;

	create_cmd.sq_caps_3 |=
		ENA_ADMIN_AQ_CREATE_SQ_CMD_IS_PHYSICALLY_CONTIGUOUS_MASK;

	create_cmd.cq_idx = cq_idx;
	create_cmd.sq_depth = io_sq->q_depth;

	if (io_sq->mem_queue_type == ENA_ADMIN_PLACEMENT_POLICY_HOST) {
		ret = ena_com_mem_addr_set(ena_dev,
					   &create_cmd.sq_ba,
					   io_sq->desc_addr.phys_addr);
		if (unlikely(ret)) {
			netdev_err(ena_dev->net_device,
				   "Memory address set failed\n");
			return ret;
		}
	}

	ret = ena_com_execute_admin_command(admin_queue,
					    (struct ena_admin_aq_entry *)&create_cmd,
					    sizeof(create_cmd),
					    (struct ena_admin_acq_entry *)&cmd_completion,
					    sizeof(cmd_completion));
	if (unlikely(ret)) {
		netdev_err(ena_dev->net_device,
			   "Failed to create IO SQ. error: %d\n", ret);
		return ret;
	}

	io_sq->idx = cmd_completion.sq_idx;

	io_sq->db_addr = (u32 __iomem *)((uintptr_t)ena_dev->reg_bar +
		(uintptr_t)cmd_completion.sq_doorbell_offset);

	if (io_sq->mem_queue_type == ENA_ADMIN_PLACEMENT_POLICY_DEV) {
		io_sq->header_addr = (u8 __iomem *)((uintptr_t)ena_dev->mem_bar
				+ cmd_completion.llq_headers_offset);

		io_sq->desc_addr.pbuf_dev_addr =
			(u8 __iomem *)((uintptr_t)ena_dev->mem_bar +
			cmd_completion.llq_descriptors_offset);
	}

	netdev_dbg(ena_dev->net_device, "Created sq[%u], depth[%u]\n",
		   io_sq->idx, io_sq->q_depth);

	return ret;
}

static int ena_com_ind_tbl_convert_to_device(struct ena_com_dev *ena_dev)
{
	struct ena_rss *rss = &ena_dev->rss;
	struct ena_com_io_sq *io_sq;
	u16 qid;
	int i;

	for (i = 0; i < 1 << rss->tbl_log_size; i++) {
		qid = rss->host_rss_ind_tbl[i];
		if (qid >= ENA_TOTAL_NUM_QUEUES)
			return -EINVAL;

		io_sq = &ena_dev->io_sq_queues[qid];

		if (io_sq->direction != ENA_COM_IO_QUEUE_DIRECTION_RX)
			return -EINVAL;

		rss->rss_ind_tbl[i].cq_idx = io_sq->idx;
	}

	return 0;
}

static void ena_com_update_intr_delay_resolution(struct ena_com_dev *ena_dev,
						 u16 intr_delay_resolution)
{
	u16 prev_intr_delay_resolution = ena_dev->intr_delay_resolution;

	if (unlikely(!intr_delay_resolution)) {
		netdev_err(ena_dev->net_device,
			   "Illegal intr_delay_resolution provided. Going to use default 1 usec resolution\n");
		intr_delay_resolution = ENA_DEFAULT_INTR_DELAY_RESOLUTION;
	}

	/* update Rx */
	ena_dev->intr_moder_rx_interval =
		ena_dev->intr_moder_rx_interval *
		prev_intr_delay_resolution /
		intr_delay_resolution;

	/* update Tx */
	ena_dev->intr_moder_tx_interval =
		ena_dev->intr_moder_tx_interval *
		prev_intr_delay_resolution /
		intr_delay_resolution;

	ena_dev->intr_delay_resolution = intr_delay_resolution;
}

/*****************************************************************************/
/*******************************      API       ******************************/
/*****************************************************************************/

int ena_com_execute_admin_command(struct ena_com_admin_queue *admin_queue,
				  struct ena_admin_aq_entry *cmd,
				  size_t cmd_size,
				  struct ena_admin_acq_entry *comp,
				  size_t comp_size)
{
	struct ena_comp_ctx *comp_ctx;
	int ret;

	comp_ctx = ena_com_submit_admin_cmd(admin_queue, cmd, cmd_size,
					    comp, comp_size);
	if (IS_ERR(comp_ctx)) {
		ret = PTR_ERR(comp_ctx);
		if (ret == -ENODEV)
			netdev_dbg(admin_queue->ena_dev->net_device,
				   "Failed to submit command [%d]\n", ret);
		else
			netdev_err(admin_queue->ena_dev->net_device,
				   "Failed to submit command [%d]\n", ret);

		return ret;
	}

	ret = ena_com_wait_and_process_admin_cq(comp_ctx, admin_queue);
	if (unlikely(ret)) {
		if (admin_queue->running_state)
			netdev_err(admin_queue->ena_dev->net_device,
				   "Failed to process command. ret = %d\n", ret);
		else
			netdev_dbg(admin_queue->ena_dev->net_device,
				   "Failed to process command. ret = %d\n", ret);
	}
	return ret;
}

int ena_com_create_io_cq(struct ena_com_dev *ena_dev,
			 struct ena_com_io_cq *io_cq)
{
	struct ena_com_admin_queue *admin_queue = &ena_dev->admin_queue;
	struct ena_admin_aq_create_cq_cmd create_cmd;
	struct ena_admin_acq_create_cq_resp_desc cmd_completion;
	int ret;

	memset(&create_cmd, 0x0, sizeof(create_cmd));

	create_cmd.aq_common_descriptor.opcode = ENA_ADMIN_CREATE_CQ;

	create_cmd.cq_caps_2 |= (io_cq->cdesc_entry_size_in_bytes / 4) &
		ENA_ADMIN_AQ_CREATE_CQ_CMD_CQ_ENTRY_SIZE_WORDS_MASK;
	create_cmd.cq_caps_1 |=
		ENA_ADMIN_AQ_CREATE_CQ_CMD_INTERRUPT_MODE_ENABLED_MASK;

	create_cmd.msix_vector = io_cq->msix_vector;
	create_cmd.cq_depth = io_cq->q_depth;

	ret = ena_com_mem_addr_set(ena_dev,
				   &create_cmd.cq_ba,
				   io_cq->cdesc_addr.phys_addr);
	if (unlikely(ret)) {
		netdev_err(ena_dev->net_device, "Memory address set failed\n");
		return ret;
	}

	ret = ena_com_execute_admin_command(admin_queue,
					    (struct ena_admin_aq_entry *)&create_cmd,
					    sizeof(create_cmd),
					    (struct ena_admin_acq_entry *)&cmd_completion,
					    sizeof(cmd_completion));
	if (unlikely(ret)) {
		netdev_err(ena_dev->net_device,
			   "Failed to create IO CQ. error: %d\n", ret);
		return ret;
	}

	io_cq->idx = cmd_completion.cq_idx;

	io_cq->unmask_reg = (u32 __iomem *)((uintptr_t)ena_dev->reg_bar +
		cmd_completion.cq_interrupt_unmask_register_offset);

	if (cmd_completion.cq_head_db_register_offset)
		io_cq->cq_head_db_reg =
			(u32 __iomem *)((uintptr_t)ena_dev->reg_bar +
			cmd_completion.cq_head_db_register_offset);

	if (cmd_completion.numa_node_register_offset)
		io_cq->numa_node_cfg_reg =
			(u32 __iomem *)((uintptr_t)ena_dev->reg_bar +
			cmd_completion.numa_node_register_offset);

	netdev_dbg(ena_dev->net_device, "Created cq[%u], depth[%u]\n",
		   io_cq->idx, io_cq->q_depth);

	return ret;
}

int ena_com_get_io_handlers(struct ena_com_dev *ena_dev, u16 qid,
			    struct ena_com_io_sq **io_sq,
			    struct ena_com_io_cq **io_cq)
{
	if (qid >= ENA_TOTAL_NUM_QUEUES) {
		netdev_err(ena_dev->net_device,
			   "Invalid queue number %d but the max is %d\n", qid,
			   ENA_TOTAL_NUM_QUEUES);
		return -EINVAL;
	}

	*io_sq = &ena_dev->io_sq_queues[qid];
	*io_cq = &ena_dev->io_cq_queues[qid];

	return 0;
}

void ena_com_abort_admin_commands(struct ena_com_dev *ena_dev)
{
	struct ena_com_admin_queue *admin_queue = &ena_dev->admin_queue;
	struct ena_comp_ctx *comp_ctx;
	u16 i;

	if (!admin_queue->comp_ctx)
		return;

	for (i = 0; i < admin_queue->q_depth; i++) {
		comp_ctx = get_comp_ctxt(admin_queue, i, false);
		if (unlikely(!comp_ctx))
			break;

		comp_ctx->status = ENA_CMD_ABORTED;

		complete(&comp_ctx->wait_event);
	}
}

void ena_com_wait_for_abort_completion(struct ena_com_dev *ena_dev)
{
	struct ena_com_admin_queue *admin_queue = &ena_dev->admin_queue;
	unsigned long flags = 0;
	u32 exp = 0;

	spin_lock_irqsave(&admin_queue->q_lock, flags);
	while (atomic_read(&admin_queue->outstanding_cmds) != 0) {
		spin_unlock_irqrestore(&admin_queue->q_lock, flags);
		ena_delay_exponential_backoff_us(exp++,
						 ena_dev->ena_min_poll_delay_us);
		spin_lock_irqsave(&admin_queue->q_lock, flags);
	}
	spin_unlock_irqrestore(&admin_queue->q_lock, flags);
}

int ena_com_destroy_io_cq(struct ena_com_dev *ena_dev,
			  struct ena_com_io_cq *io_cq)
{
	struct ena_com_admin_queue *admin_queue = &ena_dev->admin_queue;
	struct ena_admin_aq_destroy_cq_cmd destroy_cmd;
	struct ena_admin_acq_destroy_cq_resp_desc destroy_resp;
	int ret;

	memset(&destroy_cmd, 0x0, sizeof(destroy_cmd));

	destroy_cmd.cq_idx = io_cq->idx;
	destroy_cmd.aq_common_descriptor.opcode = ENA_ADMIN_DESTROY_CQ;

	ret = ena_com_execute_admin_command(admin_queue,
					    (struct ena_admin_aq_entry *)&destroy_cmd,
					    sizeof(destroy_cmd),
					    (struct ena_admin_acq_entry *)&destroy_resp,
					    sizeof(destroy_resp));

	if (unlikely(ret && (ret != -ENODEV)))
		netdev_err(ena_dev->net_device,
			   "Failed to destroy IO CQ. error: %d\n", ret);

	return ret;
}

bool ena_com_get_admin_running_state(struct ena_com_dev *ena_dev)
{
	return ena_dev->admin_queue.running_state;
}

void ena_com_set_admin_running_state(struct ena_com_dev *ena_dev, bool state)
{
	struct ena_com_admin_queue *admin_queue = &ena_dev->admin_queue;
	unsigned long flags = 0;

	spin_lock_irqsave(&admin_queue->q_lock, flags);
	ena_dev->admin_queue.running_state = state;
	spin_unlock_irqrestore(&admin_queue->q_lock, flags);
}

void ena_com_admin_aenq_enable(struct ena_com_dev *ena_dev)
{
	u16 depth = ena_dev->aenq.q_depth;

	WARN(ena_dev->aenq.head != depth, "Invalid AENQ state\n");

	/* Init head_db to mark that all entries in the queue
	 * are initially available
	 */
	writel(depth, ena_dev->reg_bar + ENA_REGS_AENQ_HEAD_DB_OFF);
}

int ena_com_set_aenq_config(struct ena_com_dev *ena_dev, u32 groups_flag)
{
	struct ena_com_admin_queue *admin_queue;
	struct ena_admin_set_feat_cmd cmd;
	struct ena_admin_set_feat_resp resp;
	struct ena_admin_get_feat_resp get_resp;
	int ret;

	ret = ena_com_get_feature(ena_dev, &get_resp, ENA_ADMIN_AENQ_CONFIG, 0);
	if (ret) {
		dev_info(ena_dev->dmadev, "Can't get aenq configuration\n");
		return ret;
	}

	if ((get_resp.u.aenq.supported_groups & groups_flag) != groups_flag) {
		netdev_warn(ena_dev->net_device,
			    "Trying to set unsupported aenq events. supported flag: 0x%x asked flag: 0x%x\n",
			    get_resp.u.aenq.supported_groups, groups_flag);
		return -EOPNOTSUPP;
	}

	memset(&cmd, 0x0, sizeof(cmd));
	admin_queue = &ena_dev->admin_queue;

	cmd.aq_common_descriptor.opcode = ENA_ADMIN_SET_FEATURE;
	cmd.aq_common_descriptor.flags = 0;
	cmd.feat_common.feature_id = ENA_ADMIN_AENQ_CONFIG;
	cmd.u.aenq.enabled_groups = groups_flag;

	ret = ena_com_execute_admin_command(admin_queue,
					    (struct ena_admin_aq_entry *)&cmd,
					    sizeof(cmd),
					    (struct ena_admin_acq_entry *)&resp,
					    sizeof(resp));

	if (unlikely(ret))
		netdev_err(ena_dev->net_device,
			   "Failed to config AENQ ret: %d\n", ret);

	return ret;
}

int ena_com_get_dma_width(struct ena_com_dev *ena_dev)
{
	u32 caps = ena_com_reg_bar_read32(ena_dev, ENA_REGS_CAPS_OFF);
	u32 width;

	if (unlikely(caps == ENA_MMIO_READ_TIMEOUT)) {
		netdev_err(ena_dev->net_device, "Reg read timeout occurred\n");
		return -ETIME;
	}

	width = (caps & ENA_REGS_CAPS_DMA_ADDR_WIDTH_MASK) >>
		ENA_REGS_CAPS_DMA_ADDR_WIDTH_SHIFT;

	netdev_dbg(ena_dev->net_device, "ENA dma width: %d\n", width);

	if ((width < 32) || width > ENA_MAX_PHYS_ADDR_SIZE_BITS) {
		netdev_err(ena_dev->net_device, "DMA width illegal value: %d\n",
			   width);
		return -EINVAL;
	}

	ena_dev->dma_addr_bits = width;

	return width;
}

int ena_com_validate_version(struct ena_com_dev *ena_dev)
{
	u32 ver;
	u32 ctrl_ver;
	u32 ctrl_ver_masked;

	/* Make sure the ENA version and the controller version are at least
	 * as the driver expects
	 */
	ver = ena_com_reg_bar_read32(ena_dev, ENA_REGS_VERSION_OFF);
	ctrl_ver = ena_com_reg_bar_read32(ena_dev,
					  ENA_REGS_CONTROLLER_VERSION_OFF);

	if (unlikely((ver == ENA_MMIO_READ_TIMEOUT) ||
		     (ctrl_ver == ENA_MMIO_READ_TIMEOUT))) {
		netdev_err(ena_dev->net_device, "Reg read timeout occurred\n");
		return -ETIME;
	}

	dev_info(ena_dev->dmadev, "ENA device version: %d.%d\n",
		 (ver & ENA_REGS_VERSION_MAJOR_VERSION_MASK) >>
			 ENA_REGS_VERSION_MAJOR_VERSION_SHIFT,
		 ver & ENA_REGS_VERSION_MINOR_VERSION_MASK);

	dev_info(ena_dev->dmadev,
		 "ENA controller version: %d.%d.%d implementation version %d\n",
		 (ctrl_ver & ENA_REGS_CONTROLLER_VERSION_MAJOR_VERSION_MASK) >>
			 ENA_REGS_CONTROLLER_VERSION_MAJOR_VERSION_SHIFT,
		 (ctrl_ver & ENA_REGS_CONTROLLER_VERSION_MINOR_VERSION_MASK) >>
			 ENA_REGS_CONTROLLER_VERSION_MINOR_VERSION_SHIFT,
		 (ctrl_ver & ENA_REGS_CONTROLLER_VERSION_SUBMINOR_VERSION_MASK),
		 (ctrl_ver & ENA_REGS_CONTROLLER_VERSION_IMPL_ID_MASK) >>
			 ENA_REGS_CONTROLLER_VERSION_IMPL_ID_SHIFT);

	ctrl_ver_masked =
		(ctrl_ver & ENA_REGS_CONTROLLER_VERSION_MAJOR_VERSION_MASK) |
		(ctrl_ver & ENA_REGS_CONTROLLER_VERSION_MINOR_VERSION_MASK) |
		(ctrl_ver & ENA_REGS_CONTROLLER_VERSION_SUBMINOR_VERSION_MASK);

	/* Validate the ctrl version without the implementation ID */
	if (ctrl_ver_masked < MIN_ENA_CTRL_VER) {
		netdev_err(ena_dev->net_device,
			   "ENA ctrl version is lower than the minimal ctrl version the driver supports\n");
		return -1;
	}

	return 0;
}

static void
ena_com_free_ena_admin_queue_comp_ctx(struct ena_com_dev *ena_dev,
				      struct ena_com_admin_queue *admin_queue)

{
	if (!admin_queue->comp_ctx)
		return;

	devm_kfree(ena_dev->dmadev, admin_queue->comp_ctx);

	admin_queue->comp_ctx = NULL;
}

void ena_com_admin_destroy(struct ena_com_dev *ena_dev)
{
	struct ena_com_admin_queue *admin_queue = &ena_dev->admin_queue;
	struct ena_com_admin_cq *cq = &admin_queue->cq;
	struct ena_com_admin_sq *sq = &admin_queue->sq;
	struct ena_com_aenq *aenq = &ena_dev->aenq;
	u16 size;

	ena_com_free_ena_admin_queue_comp_ctx(ena_dev, admin_queue);

	size = ADMIN_SQ_SIZE(admin_queue->q_depth);
	if (sq->entries)
		dma_free_coherent(ena_dev->dmadev, size, sq->entries,
				  sq->dma_addr);
	sq->entries = NULL;

	size = ADMIN_CQ_SIZE(admin_queue->q_depth);
	if (cq->entries)
		dma_free_coherent(ena_dev->dmadev, size, cq->entries,
				  cq->dma_addr);
	cq->entries = NULL;

	size = ADMIN_AENQ_SIZE(aenq->q_depth);
	if (ena_dev->aenq.entries)
		dma_free_coherent(ena_dev->dmadev, size, aenq->entries,
				  aenq->dma_addr);
	aenq->entries = NULL;
}

void ena_com_set_admin_polling_mode(struct ena_com_dev *ena_dev, bool polling)
{
	u32 mask_value = 0;

	if (polling)
		mask_value = ENA_REGS_ADMIN_INTR_MASK;

	writel(mask_value, ena_dev->reg_bar + ENA_REGS_INTR_MASK_OFF);
	ena_dev->admin_queue.polling = polling;
}

void ena_com_set_admin_auto_polling_mode(struct ena_com_dev *ena_dev,
					 bool polling)
{
	ena_dev->admin_queue.auto_polling = polling;
}

int ena_com_mmio_reg_read_request_init(struct ena_com_dev *ena_dev)
{
	struct ena_com_mmio_read *mmio_read = &ena_dev->mmio_read;

	spin_lock_init(&mmio_read->lock);
	mmio_read->read_resp =
		dma_alloc_coherent(ena_dev->dmadev,
				   sizeof(*mmio_read->read_resp),
				   &mmio_read->read_resp_dma_addr, GFP_KERNEL);
	if (unlikely(!mmio_read->read_resp))
		goto err;

	ena_com_mmio_reg_read_request_write_dev_addr(ena_dev);

	mmio_read->read_resp->req_id = 0x0;
	mmio_read->seq_num = 0x0;
	mmio_read->readless_supported = true;

	return 0;

err:

	return -ENOMEM;
}

void ena_com_set_mmio_read_mode(struct ena_com_dev *ena_dev, bool readless_supported)
{
	struct ena_com_mmio_read *mmio_read = &ena_dev->mmio_read;

	mmio_read->readless_supported = readless_supported;
}

void ena_com_mmio_reg_read_request_destroy(struct ena_com_dev *ena_dev)
{
	struct ena_com_mmio_read *mmio_read = &ena_dev->mmio_read;

	writel(0x0, ena_dev->reg_bar + ENA_REGS_MMIO_RESP_LO_OFF);
	writel(0x0, ena_dev->reg_bar + ENA_REGS_MMIO_RESP_HI_OFF);

	dma_free_coherent(ena_dev->dmadev, sizeof(*mmio_read->read_resp),
			  mmio_read->read_resp, mmio_read->read_resp_dma_addr);

	mmio_read->read_resp = NULL;
}

void ena_com_mmio_reg_read_request_write_dev_addr(struct ena_com_dev *ena_dev)
{
	struct ena_com_mmio_read *mmio_read = &ena_dev->mmio_read;
	u32 addr_low, addr_high;

	addr_low = ENA_DMA_ADDR_TO_UINT32_LOW(mmio_read->read_resp_dma_addr);
	addr_high = ENA_DMA_ADDR_TO_UINT32_HIGH(mmio_read->read_resp_dma_addr);

	writel(addr_low, ena_dev->reg_bar + ENA_REGS_MMIO_RESP_LO_OFF);
	writel(addr_high, ena_dev->reg_bar + ENA_REGS_MMIO_RESP_HI_OFF);
}

int ena_com_admin_init(struct ena_com_dev *ena_dev,
		       struct ena_aenq_handlers *aenq_handlers)
{
	struct ena_com_admin_queue *admin_queue = &ena_dev->admin_queue;
	u32 aq_caps, acq_caps, dev_sts, addr_low, addr_high;
	int ret;

	dev_sts = ena_com_reg_bar_read32(ena_dev, ENA_REGS_DEV_STS_OFF);

	if (unlikely(dev_sts == ENA_MMIO_READ_TIMEOUT)) {
		netdev_err(ena_dev->net_device, "Reg read timeout occurred\n");
		return -ETIME;
	}

	if (!(dev_sts & ENA_REGS_DEV_STS_READY_MASK)) {
		netdev_err(ena_dev->net_device,
			   "Device isn't ready, abort com init\n");
		return -ENODEV;
	}

	admin_queue->q_depth = ENA_ADMIN_QUEUE_DEPTH;

	admin_queue->q_dmadev = ena_dev->dmadev;
	admin_queue->polling = false;
	admin_queue->curr_cmd_id = 0;

	atomic_set(&admin_queue->outstanding_cmds, 0);

	spin_lock_init(&admin_queue->q_lock);

	ret = ena_com_init_comp_ctxt(admin_queue);
	if (ret)
		goto error;

	ret = ena_com_admin_init_sq(admin_queue);
	if (ret)
		goto error;

	ret = ena_com_admin_init_cq(admin_queue);
	if (ret)
		goto error;

	admin_queue->sq.db_addr = (u32 __iomem *)((uintptr_t)ena_dev->reg_bar +
		ENA_REGS_AQ_DB_OFF);

	addr_low = ENA_DMA_ADDR_TO_UINT32_LOW(admin_queue->sq.dma_addr);
	addr_high = ENA_DMA_ADDR_TO_UINT32_HIGH(admin_queue->sq.dma_addr);

	writel(addr_low, ena_dev->reg_bar + ENA_REGS_AQ_BASE_LO_OFF);
	writel(addr_high, ena_dev->reg_bar + ENA_REGS_AQ_BASE_HI_OFF);

	addr_low = ENA_DMA_ADDR_TO_UINT32_LOW(admin_queue->cq.dma_addr);
	addr_high = ENA_DMA_ADDR_TO_UINT32_HIGH(admin_queue->cq.dma_addr);

	writel(addr_low, ena_dev->reg_bar + ENA_REGS_ACQ_BASE_LO_OFF);
	writel(addr_high, ena_dev->reg_bar + ENA_REGS_ACQ_BASE_HI_OFF);

	aq_caps = 0;
	aq_caps |= admin_queue->q_depth & ENA_REGS_AQ_CAPS_AQ_DEPTH_MASK;
	aq_caps |= (sizeof(struct ena_admin_aq_entry) <<
			ENA_REGS_AQ_CAPS_AQ_ENTRY_SIZE_SHIFT) &
			ENA_REGS_AQ_CAPS_AQ_ENTRY_SIZE_MASK;

	acq_caps = 0;
	acq_caps |= admin_queue->q_depth & ENA_REGS_ACQ_CAPS_ACQ_DEPTH_MASK;
	acq_caps |= (sizeof(struct ena_admin_acq_entry) <<
		ENA_REGS_ACQ_CAPS_ACQ_ENTRY_SIZE_SHIFT) &
		ENA_REGS_ACQ_CAPS_ACQ_ENTRY_SIZE_MASK;

	writel(aq_caps, ena_dev->reg_bar + ENA_REGS_AQ_CAPS_OFF);
	writel(acq_caps, ena_dev->reg_bar + ENA_REGS_ACQ_CAPS_OFF);
	ret = ena_com_admin_init_aenq(ena_dev, aenq_handlers);
	if (ret)
		goto error;

	admin_queue->ena_dev = ena_dev;
	admin_queue->running_state = true;

	return 0;
error:
	ena_com_admin_destroy(ena_dev);

	return ret;
}

int ena_com_create_io_queue(struct ena_com_dev *ena_dev,
			    struct ena_com_create_io_ctx *ctx)
{
	struct ena_com_io_sq *io_sq;
	struct ena_com_io_cq *io_cq;
	int ret;

	if (ctx->qid >= ENA_TOTAL_NUM_QUEUES) {
		netdev_err(ena_dev->net_device,
			   "Qid (%d) is bigger than max num of queues (%d)\n",
			   ctx->qid, ENA_TOTAL_NUM_QUEUES);
		return -EINVAL;
	}

	io_sq = &ena_dev->io_sq_queues[ctx->qid];
	io_cq = &ena_dev->io_cq_queues[ctx->qid];

	memset(io_sq, 0x0, sizeof(*io_sq));
	memset(io_cq, 0x0, sizeof(*io_cq));

	/* Init CQ */
	io_cq->q_depth = ctx->queue_size;
	io_cq->direction = ctx->direction;
	io_cq->qid = ctx->qid;

	io_cq->msix_vector = ctx->msix_vector;

	io_sq->q_depth = ctx->queue_size;
	io_sq->direction = ctx->direction;
	io_sq->qid = ctx->qid;

	io_sq->mem_queue_type = ctx->mem_queue_type;

	if (ctx->direction == ENA_COM_IO_QUEUE_DIRECTION_TX)
		/* header length is limited to 8 bits */
		io_sq->tx_max_header_size =
			min_t(u32, ena_dev->tx_max_header_size, SZ_256);

	ret = ena_com_init_io_sq(ena_dev, ctx, io_sq);
	if (ret)
		goto error;
	ret = ena_com_init_io_cq(ena_dev, ctx, io_cq);
	if (ret)
		goto error;

	ret = ena_com_create_io_cq(ena_dev, io_cq);
	if (ret)
		goto error;

	ret = ena_com_create_io_sq(ena_dev, io_sq, io_cq->idx);
	if (ret)
		goto destroy_io_cq;

	return 0;

destroy_io_cq:
	ena_com_destroy_io_cq(ena_dev, io_cq);
error:
	ena_com_io_queue_free(ena_dev, io_sq, io_cq);
	return ret;
}

void ena_com_destroy_io_queue(struct ena_com_dev *ena_dev, u16 qid)
{
	struct ena_com_io_sq *io_sq;
	struct ena_com_io_cq *io_cq;

	if (qid >= ENA_TOTAL_NUM_QUEUES) {
		netdev_err(ena_dev->net_device,
			   "Qid (%d) is bigger than max num of queues (%d)\n",
			   qid, ENA_TOTAL_NUM_QUEUES);
		return;
	}

	io_sq = &ena_dev->io_sq_queues[qid];
	io_cq = &ena_dev->io_cq_queues[qid];

	ena_com_destroy_io_sq(ena_dev, io_sq);
	ena_com_destroy_io_cq(ena_dev, io_cq);

	ena_com_io_queue_free(ena_dev, io_sq, io_cq);
}

int ena_com_get_link_params(struct ena_com_dev *ena_dev,
			    struct ena_admin_get_feat_resp *resp)
{
	return ena_com_get_feature(ena_dev, resp, ENA_ADMIN_LINK_CONFIG, 0);
}

int ena_com_get_dev_attr_feat(struct ena_com_dev *ena_dev,
			      struct ena_com_dev_get_features_ctx *get_feat_ctx)
{
	struct ena_admin_get_feat_resp get_resp;
	int rc;

	rc = ena_com_get_feature(ena_dev, &get_resp,
				 ENA_ADMIN_DEVICE_ATTRIBUTES, 0);
	if (rc)
		return rc;

	memcpy(&get_feat_ctx->dev_attr, &get_resp.u.dev_attr,
	       sizeof(get_resp.u.dev_attr));

	ena_dev->supported_features = get_resp.u.dev_attr.supported_features;
	ena_dev->capabilities = get_resp.u.dev_attr.capabilities;

	if (ena_dev->supported_features & BIT(ENA_ADMIN_MAX_QUEUES_EXT)) {
		rc = ena_com_get_feature(ena_dev, &get_resp,
					 ENA_ADMIN_MAX_QUEUES_EXT,
					 ENA_FEATURE_MAX_QUEUE_EXT_VER);
		if (rc)
			return rc;

		if (get_resp.u.max_queue_ext.version !=
		    ENA_FEATURE_MAX_QUEUE_EXT_VER)
			return -EINVAL;

		memcpy(&get_feat_ctx->max_queue_ext, &get_resp.u.max_queue_ext,
		       sizeof(get_resp.u.max_queue_ext));
		ena_dev->tx_max_header_size =
			get_resp.u.max_queue_ext.max_queue_ext.max_tx_header_size;
	} else {
		rc = ena_com_get_feature(ena_dev, &get_resp,
					 ENA_ADMIN_MAX_QUEUES_NUM, 0);
		memcpy(&get_feat_ctx->max_queues, &get_resp.u.max_queue,
		       sizeof(get_resp.u.max_queue));
		ena_dev->tx_max_header_size =
			get_resp.u.max_queue.max_header_size;

		if (rc)
			return rc;
	}

	rc = ena_com_get_feature(ena_dev, &get_resp,
				 ENA_ADMIN_AENQ_CONFIG, 0);
	if (rc)
		return rc;

	memcpy(&get_feat_ctx->aenq, &get_resp.u.aenq,
	       sizeof(get_resp.u.aenq));

	rc = ena_com_get_feature(ena_dev, &get_resp,
				 ENA_ADMIN_STATELESS_OFFLOAD_CONFIG, 0);
	if (rc)
		return rc;

	memcpy(&get_feat_ctx->offload, &get_resp.u.offload,
	       sizeof(get_resp.u.offload));

	/* Driver hints isn't mandatory admin command. So in case the
	 * command isn't supported set driver hints to 0
	 */
	rc = ena_com_get_feature(ena_dev, &get_resp, ENA_ADMIN_HW_HINTS, 0);

	if (!rc)
		memcpy(&get_feat_ctx->hw_hints, &get_resp.u.hw_hints,
		       sizeof(get_resp.u.hw_hints));
	else if (rc == -EOPNOTSUPP)
		memset(&get_feat_ctx->hw_hints, 0x0,
		       sizeof(get_feat_ctx->hw_hints));
	else
		return rc;

	rc = ena_com_get_feature(ena_dev, &get_resp, ENA_ADMIN_LLQ, 0);
	if (!rc)
		memcpy(&get_feat_ctx->llq, &get_resp.u.llq,
		       sizeof(get_resp.u.llq));
	else if (rc == -EOPNOTSUPP)
		memset(&get_feat_ctx->llq, 0x0, sizeof(get_feat_ctx->llq));
	else
		return rc;

	return 0;
}

void ena_com_admin_q_comp_intr_handler(struct ena_com_dev *ena_dev)
{
	ena_com_handle_admin_completion(&ena_dev->admin_queue);
}

/* ena_handle_specific_aenq_event:
 * return the handler that is relevant to the specific event group
 */
static ena_aenq_handler ena_com_get_specific_aenq_cb(struct ena_com_dev *ena_dev,
						     u16 group)
{
	struct ena_aenq_handlers *aenq_handlers = ena_dev->aenq.aenq_handlers;

	if ((group < ENA_MAX_HANDLERS) && aenq_handlers->handlers[group])
		return aenq_handlers->handlers[group];

	return aenq_handlers->unimplemented_handler;
}

/* ena_aenq_intr_handler:
 * handles the aenq incoming events.
 * pop events from the queue and apply the specific handler
 */
void ena_com_aenq_intr_handler(struct ena_com_dev *ena_dev, void *data)
{
	struct ena_admin_aenq_entry *aenq_e;
	struct ena_admin_aenq_common_desc *aenq_common;
	struct ena_com_aenq *aenq  = &ena_dev->aenq;
	u64 timestamp;
	ena_aenq_handler handler_cb;
	u16 masked_head, processed = 0;
	u8 phase;

	masked_head = aenq->head & (aenq->q_depth - 1);
	phase = aenq->phase;
	aenq_e = &aenq->entries[masked_head]; /* Get first entry */
	aenq_common = &aenq_e->aenq_common_desc;

	/* Go over all the events */
	while ((READ_ONCE(aenq_common->flags) &
		ENA_ADMIN_AENQ_COMMON_DESC_PHASE_MASK) == phase) {
		/* Make sure the phase bit (ownership) is as expected before
		 * reading the rest of the descriptor.
		 */
		dma_rmb();

		timestamp = (u64)aenq_common->timestamp_low |
			((u64)aenq_common->timestamp_high << 32);

		netdev_dbg(ena_dev->net_device,
			   "AENQ! Group[%x] Syndrome[%x] timestamp: [%llus]\n",
			   aenq_common->group, aenq_common->syndrome, timestamp);

		/* Handle specific event*/
		handler_cb = ena_com_get_specific_aenq_cb(ena_dev,
							  aenq_common->group);
		handler_cb(data, aenq_e); /* call the actual event handler*/

		/* Get next event entry */
		masked_head++;
		processed++;

		if (unlikely(masked_head == aenq->q_depth)) {
			masked_head = 0;
			phase = !phase;
		}
		aenq_e = &aenq->entries[masked_head];
		aenq_common = &aenq_e->aenq_common_desc;
	}

	aenq->head += processed;
	aenq->phase = phase;

	/* Don't update aenq doorbell if there weren't any processed events */
	if (!processed)
		return;

	/* write the aenq doorbell after all AENQ descriptors were read */
	mb();
	writel_relaxed((u32)aenq->head,
		       ena_dev->reg_bar + ENA_REGS_AENQ_HEAD_DB_OFF);
}

int ena_com_dev_reset(struct ena_com_dev *ena_dev,
		      enum ena_regs_reset_reason_types reset_reason)
{
	u32 stat, timeout, cap, reset_val;
	int rc;

	stat = ena_com_reg_bar_read32(ena_dev, ENA_REGS_DEV_STS_OFF);
	cap = ena_com_reg_bar_read32(ena_dev, ENA_REGS_CAPS_OFF);

	if (unlikely((stat == ENA_MMIO_READ_TIMEOUT) ||
		     (cap == ENA_MMIO_READ_TIMEOUT))) {
		netdev_err(ena_dev->net_device, "Reg read32 timeout occurred\n");
		return -ETIME;
	}

	if ((stat & ENA_REGS_DEV_STS_READY_MASK) == 0) {
		netdev_err(ena_dev->net_device,
			   "Device isn't ready, can't reset device\n");
		return -EINVAL;
	}

	timeout = (cap & ENA_REGS_CAPS_RESET_TIMEOUT_MASK) >>
			ENA_REGS_CAPS_RESET_TIMEOUT_SHIFT;
	if (timeout == 0) {
		netdev_err(ena_dev->net_device, "Invalid timeout value\n");
		return -EINVAL;
	}

	/* start reset */
	reset_val = ENA_REGS_DEV_CTL_DEV_RESET_MASK;
	reset_val |= (reset_reason << ENA_REGS_DEV_CTL_RESET_REASON_SHIFT) &
		     ENA_REGS_DEV_CTL_RESET_REASON_MASK;
	writel(reset_val, ena_dev->reg_bar + ENA_REGS_DEV_CTL_OFF);

	/* Write again the MMIO read request address */
	ena_com_mmio_reg_read_request_write_dev_addr(ena_dev);

	rc = wait_for_reset_state(ena_dev, timeout,
				  ENA_REGS_DEV_STS_RESET_IN_PROGRESS_MASK);
	if (rc != 0) {
		netdev_err(ena_dev->net_device,
			   "Reset indication didn't turn on\n");
		return rc;
	}

	/* reset done */
	writel(0, ena_dev->reg_bar + ENA_REGS_DEV_CTL_OFF);
	rc = wait_for_reset_state(ena_dev, timeout, 0);
	if (rc != 0) {
		netdev_err(ena_dev->net_device,
			   "Reset indication didn't turn off\n");
		return rc;
	}

	timeout = (cap & ENA_REGS_CAPS_ADMIN_CMD_TO_MASK) >>
		ENA_REGS_CAPS_ADMIN_CMD_TO_SHIFT;
	if (timeout)
		/* the resolution of timeout reg is 100ms */
		ena_dev->admin_queue.completion_timeout = timeout * 100000;
	else
		ena_dev->admin_queue.completion_timeout = ADMIN_CMD_TIMEOUT_US;

	return 0;
}

static int ena_get_dev_stats(struct ena_com_dev *ena_dev,
			     struct ena_com_stats_ctx *ctx,
			     enum ena_admin_get_stats_type type)
{
	struct ena_admin_aq_get_stats_cmd *get_cmd = &ctx->get_cmd;
	struct ena_admin_acq_get_stats_resp *get_resp = &ctx->get_resp;
	struct ena_com_admin_queue *admin_queue;
	int ret;

	admin_queue = &ena_dev->admin_queue;

	get_cmd->aq_common_descriptor.opcode = ENA_ADMIN_GET_STATS;
	get_cmd->aq_common_descriptor.flags = 0;
	get_cmd->type = type;

	ret =  ena_com_execute_admin_command(admin_queue,
					     (struct ena_admin_aq_entry *)get_cmd,
					     sizeof(*get_cmd),
					     (struct ena_admin_acq_entry *)get_resp,
					     sizeof(*get_resp));

	if (unlikely(ret))
		netdev_err(ena_dev->net_device,
			   "Failed to get stats. error: %d\n", ret);

	return ret;
}

int ena_com_get_eni_stats(struct ena_com_dev *ena_dev,
			  struct ena_admin_eni_stats *stats)
{
	struct ena_com_stats_ctx ctx;
	int ret;

	if (!ena_com_get_cap(ena_dev, ENA_ADMIN_ENI_STATS)) {
		netdev_err(ena_dev->net_device,
			   "Capability %d isn't supported\n",
			   ENA_ADMIN_ENI_STATS);
		return -EOPNOTSUPP;
	}

	memset(&ctx, 0x0, sizeof(ctx));
	ret = ena_get_dev_stats(ena_dev, &ctx, ENA_ADMIN_GET_STATS_TYPE_ENI);
	if (likely(ret == 0))
		memcpy(stats, &ctx.get_resp.u.eni_stats,
		       sizeof(ctx.get_resp.u.eni_stats));

	return ret;
}

int ena_com_get_dev_basic_stats(struct ena_com_dev *ena_dev,
				struct ena_admin_basic_stats *stats)
{
	struct ena_com_stats_ctx ctx;
	int ret;

	memset(&ctx, 0x0, sizeof(ctx));
	ret = ena_get_dev_stats(ena_dev, &ctx, ENA_ADMIN_GET_STATS_TYPE_BASIC);
	if (likely(ret == 0))
		memcpy(stats, &ctx.get_resp.u.basic_stats,
		       sizeof(ctx.get_resp.u.basic_stats));

	return ret;
}

int ena_com_set_dev_mtu(struct ena_com_dev *ena_dev, u32 mtu)
{
	struct ena_com_admin_queue *admin_queue;
	struct ena_admin_set_feat_cmd cmd;
	struct ena_admin_set_feat_resp resp;
	int ret;

	if (!ena_com_check_supported_feature_id(ena_dev, ENA_ADMIN_MTU)) {
		netdev_dbg(ena_dev->net_device, "Feature %d isn't supported\n",
			   ENA_ADMIN_MTU);
		return -EOPNOTSUPP;
	}

	memset(&cmd, 0x0, sizeof(cmd));
	admin_queue = &ena_dev->admin_queue;

	cmd.aq_common_descriptor.opcode = ENA_ADMIN_SET_FEATURE;
	cmd.aq_common_descriptor.flags = 0;
	cmd.feat_common.feature_id = ENA_ADMIN_MTU;
	cmd.u.mtu.mtu = mtu;

	ret = ena_com_execute_admin_command(admin_queue,
					    (struct ena_admin_aq_entry *)&cmd,
					    sizeof(cmd),
					    (struct ena_admin_acq_entry *)&resp,
					    sizeof(resp));

	if (unlikely(ret))
		netdev_err(ena_dev->net_device,
			   "Failed to set mtu %d. error: %d\n", mtu, ret);

	return ret;
}

int ena_com_get_offload_settings(struct ena_com_dev *ena_dev,
				 struct ena_admin_feature_offload_desc *offload)
{
	int ret;
	struct ena_admin_get_feat_resp resp;

	ret = ena_com_get_feature(ena_dev, &resp,
				  ENA_ADMIN_STATELESS_OFFLOAD_CONFIG, 0);
	if (unlikely(ret)) {
		netdev_err(ena_dev->net_device,
			   "Failed to get offload capabilities %d\n", ret);
		return ret;
	}

	memcpy(offload, &resp.u.offload, sizeof(resp.u.offload));

	return 0;
}

int ena_com_set_hash_function(struct ena_com_dev *ena_dev)
{
	struct ena_com_admin_queue *admin_queue = &ena_dev->admin_queue;
	struct ena_rss *rss = &ena_dev->rss;
	struct ena_admin_set_feat_cmd cmd;
	struct ena_admin_set_feat_resp resp;
	struct ena_admin_get_feat_resp get_resp;
	int ret;

	if (!ena_com_check_supported_feature_id(ena_dev,
						ENA_ADMIN_RSS_HASH_FUNCTION)) {
		netdev_dbg(ena_dev->net_device, "Feature %d isn't supported\n",
			   ENA_ADMIN_RSS_HASH_FUNCTION);
		return -EOPNOTSUPP;
	}

	/* Validate hash function is supported */
	ret = ena_com_get_feature(ena_dev, &get_resp,
				  ENA_ADMIN_RSS_HASH_FUNCTION, 0);
	if (unlikely(ret))
		return ret;

	if (!(get_resp.u.flow_hash_func.supported_func & BIT(rss->hash_func))) {
		netdev_err(ena_dev->net_device,
			   "Func hash %d isn't supported by device, abort\n",
			   rss->hash_func);
		return -EOPNOTSUPP;
	}

	memset(&cmd, 0x0, sizeof(cmd));

	cmd.aq_common_descriptor.opcode = ENA_ADMIN_SET_FEATURE;
	cmd.aq_common_descriptor.flags =
		ENA_ADMIN_AQ_COMMON_DESC_CTRL_DATA_INDIRECT_MASK;
	cmd.feat_common.feature_id = ENA_ADMIN_RSS_HASH_FUNCTION;
	cmd.u.flow_hash_func.init_val = rss->hash_init_val;
	cmd.u.flow_hash_func.selected_func = 1 << rss->hash_func;

	ret = ena_com_mem_addr_set(ena_dev,
				   &cmd.control_buffer.address,
				   rss->hash_key_dma_addr);
	if (unlikely(ret)) {
		netdev_err(ena_dev->net_device, "Memory address set failed\n");
		return ret;
	}

	cmd.control_buffer.length = sizeof(*rss->hash_key);

	ret = ena_com_execute_admin_command(admin_queue,
					    (struct ena_admin_aq_entry *)&cmd,
					    sizeof(cmd),
					    (struct ena_admin_acq_entry *)&resp,
					    sizeof(resp));
	if (unlikely(ret)) {
		netdev_err(ena_dev->net_device,
			   "Failed to set hash function %d. error: %d\n",
			   rss->hash_func, ret);
		return -EINVAL;
	}

	return 0;
}

int ena_com_fill_hash_function(struct ena_com_dev *ena_dev,
			       enum ena_admin_hash_functions func,
			       const u8 *key, u16 key_len, u32 init_val)
{
	struct ena_admin_feature_rss_flow_hash_control *hash_key;
	struct ena_admin_get_feat_resp get_resp;
	enum ena_admin_hash_functions old_func;
	struct ena_rss *rss = &ena_dev->rss;
	int rc;

	hash_key = rss->hash_key;

	/* Make sure size is a mult of DWs */
	if (unlikely(key_len & 0x3))
		return -EINVAL;

	rc = ena_com_get_feature_ex(ena_dev, &get_resp,
				    ENA_ADMIN_RSS_HASH_FUNCTION,
				    rss->hash_key_dma_addr,
				    sizeof(*rss->hash_key), 0);
	if (unlikely(rc))
		return rc;

	if (!(BIT(func) & get_resp.u.flow_hash_func.supported_func)) {
		netdev_err(ena_dev->net_device,
			   "Flow hash function %d isn't supported\n", func);
		return -EOPNOTSUPP;
	}

	if ((func == ENA_ADMIN_TOEPLITZ) && key) {
		if (key_len != sizeof(hash_key->key)) {
			netdev_err(ena_dev->net_device,
				   "key len (%u) doesn't equal the supported size (%zu)\n",
				   key_len, sizeof(hash_key->key));
			return -EINVAL;
		}
		memcpy(hash_key->key, key, key_len);
		hash_key->key_parts = key_len / sizeof(hash_key->key[0]);
	}

	rss->hash_init_val = init_val;
	old_func = rss->hash_func;
	rss->hash_func = func;
	rc = ena_com_set_hash_function(ena_dev);

	/* Restore the old function */
	if (unlikely(rc))
		rss->hash_func = old_func;

	return rc;
}

int ena_com_get_hash_function(struct ena_com_dev *ena_dev,
			      enum ena_admin_hash_functions *func)
{
	struct ena_rss *rss = &ena_dev->rss;
	struct ena_admin_get_feat_resp get_resp;
	int rc;

	if (unlikely(!func))
		return -EINVAL;

	rc = ena_com_get_feature_ex(ena_dev, &get_resp,
				    ENA_ADMIN_RSS_HASH_FUNCTION,
				    rss->hash_key_dma_addr,
				    sizeof(*rss->hash_key), 0);
	if (unlikely(rc))
		return rc;

	/* ffs() returns 1 in case the lsb is set */
	rss->hash_func = ffs(get_resp.u.flow_hash_func.selected_func);
	if (rss->hash_func)
		rss->hash_func--;

	*func = rss->hash_func;

	return 0;
}

int ena_com_get_hash_key(struct ena_com_dev *ena_dev, u8 *key)
{
	struct ena_admin_feature_rss_flow_hash_control *hash_key =
		ena_dev->rss.hash_key;

	if (key)
		memcpy(key, hash_key->key,
		       (size_t)(hash_key->key_parts) * sizeof(hash_key->key[0]));

	return 0;
}

int ena_com_get_hash_ctrl(struct ena_com_dev *ena_dev,
			  enum ena_admin_flow_hash_proto proto,
			  u16 *fields)
{
	struct ena_rss *rss = &ena_dev->rss;
	struct ena_admin_get_feat_resp get_resp;
	int rc;

	rc = ena_com_get_feature_ex(ena_dev, &get_resp,
				    ENA_ADMIN_RSS_HASH_INPUT,
				    rss->hash_ctrl_dma_addr,
				    sizeof(*rss->hash_ctrl), 0);
	if (unlikely(rc))
		return rc;

	if (fields)
		*fields = rss->hash_ctrl->selected_fields[proto].fields;

	return 0;
}

int ena_com_set_hash_ctrl(struct ena_com_dev *ena_dev)
{
	struct ena_com_admin_queue *admin_queue = &ena_dev->admin_queue;
	struct ena_rss *rss = &ena_dev->rss;
	struct ena_admin_feature_rss_hash_control *hash_ctrl = rss->hash_ctrl;
	struct ena_admin_set_feat_cmd cmd;
	struct ena_admin_set_feat_resp resp;
	int ret;

	if (!ena_com_check_supported_feature_id(ena_dev,
						ENA_ADMIN_RSS_HASH_INPUT)) {
		netdev_dbg(ena_dev->net_device, "Feature %d isn't supported\n",
			   ENA_ADMIN_RSS_HASH_INPUT);
		return -EOPNOTSUPP;
	}

	memset(&cmd, 0x0, sizeof(cmd));

	cmd.aq_common_descriptor.opcode = ENA_ADMIN_SET_FEATURE;
	cmd.aq_common_descriptor.flags =
		ENA_ADMIN_AQ_COMMON_DESC_CTRL_DATA_INDIRECT_MASK;
	cmd.feat_common.feature_id = ENA_ADMIN_RSS_HASH_INPUT;
	cmd.u.flow_hash_input.enabled_input_sort =
		ENA_ADMIN_FEATURE_RSS_FLOW_HASH_INPUT_L3_SORT_MASK |
		ENA_ADMIN_FEATURE_RSS_FLOW_HASH_INPUT_L4_SORT_MASK;

	ret = ena_com_mem_addr_set(ena_dev,
				   &cmd.control_buffer.address,
				   rss->hash_ctrl_dma_addr);
	if (unlikely(ret)) {
		netdev_err(ena_dev->net_device, "Memory address set failed\n");
		return ret;
	}
	cmd.control_buffer.length = sizeof(*hash_ctrl);

	ret = ena_com_execute_admin_command(admin_queue,
					    (struct ena_admin_aq_entry *)&cmd,
					    sizeof(cmd),
					    (struct ena_admin_acq_entry *)&resp,
					    sizeof(resp));
	if (unlikely(ret))
		netdev_err(ena_dev->net_device,
			   "Failed to set hash input. error: %d\n", ret);

	return ret;
}

int ena_com_set_default_hash_ctrl(struct ena_com_dev *ena_dev)
{
	struct ena_rss *rss = &ena_dev->rss;
	struct ena_admin_feature_rss_hash_control *hash_ctrl =
		rss->hash_ctrl;
	u16 available_fields = 0;
	int rc, i;

	/* Get the supported hash input */
	rc = ena_com_get_hash_ctrl(ena_dev, 0, NULL);
	if (unlikely(rc))
		return rc;

	hash_ctrl->selected_fields[ENA_ADMIN_RSS_TCP4].fields =
		ENA_ADMIN_RSS_L3_SA | ENA_ADMIN_RSS_L3_DA |
		ENA_ADMIN_RSS_L4_DP | ENA_ADMIN_RSS_L4_SP;

	hash_ctrl->selected_fields[ENA_ADMIN_RSS_UDP4].fields =
		ENA_ADMIN_RSS_L3_SA | ENA_ADMIN_RSS_L3_DA |
		ENA_ADMIN_RSS_L4_DP | ENA_ADMIN_RSS_L4_SP;

	hash_ctrl->selected_fields[ENA_ADMIN_RSS_TCP6].fields =
		ENA_ADMIN_RSS_L3_SA | ENA_ADMIN_RSS_L3_DA |
		ENA_ADMIN_RSS_L4_DP | ENA_ADMIN_RSS_L4_SP;

	hash_ctrl->selected_fields[ENA_ADMIN_RSS_UDP6].fields =
		ENA_ADMIN_RSS_L3_SA | ENA_ADMIN_RSS_L3_DA |
		ENA_ADMIN_RSS_L4_DP | ENA_ADMIN_RSS_L4_SP;

	hash_ctrl->selected_fields[ENA_ADMIN_RSS_IP4].fields =
		ENA_ADMIN_RSS_L3_SA | ENA_ADMIN_RSS_L3_DA;

	hash_ctrl->selected_fields[ENA_ADMIN_RSS_IP6].fields =
		ENA_ADMIN_RSS_L3_SA | ENA_ADMIN_RSS_L3_DA;

	hash_ctrl->selected_fields[ENA_ADMIN_RSS_IP4_FRAG].fields =
		ENA_ADMIN_RSS_L3_SA | ENA_ADMIN_RSS_L3_DA;

	hash_ctrl->selected_fields[ENA_ADMIN_RSS_NOT_IP].fields =
		ENA_ADMIN_RSS_L2_DA | ENA_ADMIN_RSS_L2_SA;

	for (i = 0; i < ENA_ADMIN_RSS_PROTO_NUM; i++) {
		available_fields = hash_ctrl->selected_fields[i].fields &
				hash_ctrl->supported_fields[i].fields;
		if (available_fields != hash_ctrl->selected_fields[i].fields) {
			netdev_err(ena_dev->net_device,
				   "Hash control doesn't support all the desire configuration. proto %x supported %x selected %x\n",
				   i, hash_ctrl->supported_fields[i].fields,
				   hash_ctrl->selected_fields[i].fields);
			return -EOPNOTSUPP;
		}
	}

	rc = ena_com_set_hash_ctrl(ena_dev);

	/* In case of failure, restore the old hash ctrl */
	if (unlikely(rc))
		ena_com_get_hash_ctrl(ena_dev, 0, NULL);

	return rc;
}

int ena_com_fill_hash_ctrl(struct ena_com_dev *ena_dev,
			   enum ena_admin_flow_hash_proto proto,
			   u16 hash_fields)
{
	struct ena_rss *rss = &ena_dev->rss;
	struct ena_admin_feature_rss_hash_control *hash_ctrl = rss->hash_ctrl;
	u16 supported_fields;
	int rc;

	if (proto >= ENA_ADMIN_RSS_PROTO_NUM) {
		netdev_err(ena_dev->net_device, "Invalid proto num (%u)\n",
			   proto);
		return -EINVAL;
	}

	/* Get the ctrl table */
	rc = ena_com_get_hash_ctrl(ena_dev, proto, NULL);
	if (unlikely(rc))
		return rc;

	/* Make sure all the fields are supported */
	supported_fields = hash_ctrl->supported_fields[proto].fields;
	if ((hash_fields & supported_fields) != hash_fields) {
		netdev_err(ena_dev->net_device,
			   "Proto %d doesn't support the required fields %x. supports only: %x\n",
			   proto, hash_fields, supported_fields);
	}

	hash_ctrl->selected_fields[proto].fields = hash_fields;

	rc = ena_com_set_hash_ctrl(ena_dev);

	/* In case of failure, restore the old hash ctrl */
	if (unlikely(rc))
		ena_com_get_hash_ctrl(ena_dev, 0, NULL);

	return 0;
}

int ena_com_indirect_table_fill_entry(struct ena_com_dev *ena_dev,
				      u16 entry_idx, u16 entry_value)
{
	struct ena_rss *rss = &ena_dev->rss;

	if (unlikely(entry_idx >= (1 << rss->tbl_log_size)))
		return -EINVAL;

	if (unlikely((entry_value > ENA_TOTAL_NUM_QUEUES)))
		return -EINVAL;

	rss->host_rss_ind_tbl[entry_idx] = entry_value;

	return 0;
}

int ena_com_indirect_table_set(struct ena_com_dev *ena_dev)
{
	struct ena_com_admin_queue *admin_queue = &ena_dev->admin_queue;
	struct ena_rss *rss = &ena_dev->rss;
	struct ena_admin_set_feat_cmd cmd;
	struct ena_admin_set_feat_resp resp;
	int ret;

	if (!ena_com_check_supported_feature_id(
		    ena_dev, ENA_ADMIN_RSS_INDIRECTION_TABLE_CONFIG)) {
		netdev_dbg(ena_dev->net_device, "Feature %d isn't supported\n",
			   ENA_ADMIN_RSS_INDIRECTION_TABLE_CONFIG);
		return -EOPNOTSUPP;
	}

	ret = ena_com_ind_tbl_convert_to_device(ena_dev);
	if (ret) {
		netdev_err(ena_dev->net_device,
			   "Failed to convert host indirection table to device table\n");
		return ret;
	}

	memset(&cmd, 0x0, sizeof(cmd));

	cmd.aq_common_descriptor.opcode = ENA_ADMIN_SET_FEATURE;
	cmd.aq_common_descriptor.flags =
		ENA_ADMIN_AQ_COMMON_DESC_CTRL_DATA_INDIRECT_MASK;
	cmd.feat_common.feature_id = ENA_ADMIN_RSS_INDIRECTION_TABLE_CONFIG;
	cmd.u.ind_table.size = rss->tbl_log_size;
	cmd.u.ind_table.inline_index = 0xFFFFFFFF;

	ret = ena_com_mem_addr_set(ena_dev,
				   &cmd.control_buffer.address,
				   rss->rss_ind_tbl_dma_addr);
	if (unlikely(ret)) {
		netdev_err(ena_dev->net_device, "Memory address set failed\n");
		return ret;
	}

	cmd.control_buffer.length = (1ULL << rss->tbl_log_size) *
		sizeof(struct ena_admin_rss_ind_table_entry);

	ret = ena_com_execute_admin_command(admin_queue,
					    (struct ena_admin_aq_entry *)&cmd,
					    sizeof(cmd),
					    (struct ena_admin_acq_entry *)&resp,
					    sizeof(resp));

	if (unlikely(ret))
		netdev_err(ena_dev->net_device,
			   "Failed to set indirect table. error: %d\n", ret);

	return ret;
}

int ena_com_indirect_table_get(struct ena_com_dev *ena_dev, u32 *ind_tbl)
{
	struct ena_rss *rss = &ena_dev->rss;
	struct ena_admin_get_feat_resp get_resp;
	u32 tbl_size;
	int i, rc;

	tbl_size = (1ULL << rss->tbl_log_size) *
		sizeof(struct ena_admin_rss_ind_table_entry);

	rc = ena_com_get_feature_ex(ena_dev, &get_resp,
				    ENA_ADMIN_RSS_INDIRECTION_TABLE_CONFIG,
				    rss->rss_ind_tbl_dma_addr,
				    tbl_size, 0);
	if (unlikely(rc))
		return rc;

	if (!ind_tbl)
		return 0;

	for (i = 0; i < (1 << rss->tbl_log_size); i++)
		ind_tbl[i] = rss->host_rss_ind_tbl[i];

	return 0;
}

int ena_com_rss_init(struct ena_com_dev *ena_dev, u16 indr_tbl_log_size)
{
	int rc;

	memset(&ena_dev->rss, 0x0, sizeof(ena_dev->rss));

	rc = ena_com_indirect_table_allocate(ena_dev, indr_tbl_log_size);
	if (unlikely(rc))
		goto err_indr_tbl;

	/* The following function might return unsupported in case the
	 * device doesn't support setting the key / hash function. We can safely
	 * ignore this error and have indirection table support only.
	 */
	rc = ena_com_hash_key_allocate(ena_dev);
	if (likely(!rc))
		ena_com_hash_key_fill_default_key(ena_dev);
	else if (rc != -EOPNOTSUPP)
		goto err_hash_key;

	rc = ena_com_hash_ctrl_init(ena_dev);
	if (unlikely(rc))
		goto err_hash_ctrl;

	return 0;

err_hash_ctrl:
	ena_com_hash_key_destroy(ena_dev);
err_hash_key:
	ena_com_indirect_table_destroy(ena_dev);
err_indr_tbl:

	return rc;
}

void ena_com_rss_destroy(struct ena_com_dev *ena_dev)
{
	ena_com_indirect_table_destroy(ena_dev);
	ena_com_hash_key_destroy(ena_dev);
	ena_com_hash_ctrl_destroy(ena_dev);

	memset(&ena_dev->rss, 0x0, sizeof(ena_dev->rss));
}

int ena_com_allocate_host_info(struct ena_com_dev *ena_dev)
{
	struct ena_host_attribute *host_attr = &ena_dev->host_attr;

	host_attr->host_info =
		dma_alloc_coherent(ena_dev->dmadev, SZ_4K,
				   &host_attr->host_info_dma_addr, GFP_KERNEL);
	if (unlikely(!host_attr->host_info))
		return -ENOMEM;

	host_attr->host_info->ena_spec_version = ((ENA_COMMON_SPEC_VERSION_MAJOR <<
		ENA_REGS_VERSION_MAJOR_VERSION_SHIFT) |
		(ENA_COMMON_SPEC_VERSION_MINOR));

	return 0;
}

int ena_com_allocate_debug_area(struct ena_com_dev *ena_dev,
				u32 debug_area_size)
{
	struct ena_host_attribute *host_attr = &ena_dev->host_attr;

	host_attr->debug_area_virt_addr =
		dma_alloc_coherent(ena_dev->dmadev, debug_area_size,
				   &host_attr->debug_area_dma_addr, GFP_KERNEL);
	if (unlikely(!host_attr->debug_area_virt_addr)) {
		host_attr->debug_area_size = 0;
		return -ENOMEM;
	}

	host_attr->debug_area_size = debug_area_size;

	return 0;
}

void ena_com_delete_host_info(struct ena_com_dev *ena_dev)
{
	struct ena_host_attribute *host_attr = &ena_dev->host_attr;

	if (host_attr->host_info) {
		dma_free_coherent(ena_dev->dmadev, SZ_4K, host_attr->host_info,
				  host_attr->host_info_dma_addr);
		host_attr->host_info = NULL;
	}
}

void ena_com_delete_debug_area(struct ena_com_dev *ena_dev)
{
	struct ena_host_attribute *host_attr = &ena_dev->host_attr;

	if (host_attr->debug_area_virt_addr) {
		dma_free_coherent(ena_dev->dmadev, host_attr->debug_area_size,
				  host_attr->debug_area_virt_addr,
				  host_attr->debug_area_dma_addr);
		host_attr->debug_area_virt_addr = NULL;
	}
}

int ena_com_set_host_attributes(struct ena_com_dev *ena_dev)
{
	struct ena_host_attribute *host_attr = &ena_dev->host_attr;
	struct ena_com_admin_queue *admin_queue;
	struct ena_admin_set_feat_cmd cmd;
	struct ena_admin_set_feat_resp resp;

	int ret;

	/* Host attribute config is called before ena_com_get_dev_attr_feat
	 * so ena_com can't check if the feature is supported.
	 */

	memset(&cmd, 0x0, sizeof(cmd));
	admin_queue = &ena_dev->admin_queue;

	cmd.aq_common_descriptor.opcode = ENA_ADMIN_SET_FEATURE;
	cmd.feat_common.feature_id = ENA_ADMIN_HOST_ATTR_CONFIG;

	ret = ena_com_mem_addr_set(ena_dev,
				   &cmd.u.host_attr.debug_ba,
				   host_attr->debug_area_dma_addr);
	if (unlikely(ret)) {
		netdev_err(ena_dev->net_device, "Memory address set failed\n");
		return ret;
	}

	ret = ena_com_mem_addr_set(ena_dev,
				   &cmd.u.host_attr.os_info_ba,
				   host_attr->host_info_dma_addr);
	if (unlikely(ret)) {
		netdev_err(ena_dev->net_device, "Memory address set failed\n");
		return ret;
	}

	cmd.u.host_attr.debug_area_size = host_attr->debug_area_size;

	ret = ena_com_execute_admin_command(admin_queue,
					    (struct ena_admin_aq_entry *)&cmd,
					    sizeof(cmd),
					    (struct ena_admin_acq_entry *)&resp,
					    sizeof(resp));

	if (unlikely(ret))
		netdev_err(ena_dev->net_device,
			   "Failed to set host attributes: %d\n", ret);

	return ret;
}

/* Interrupt moderation */
bool ena_com_interrupt_moderation_supported(struct ena_com_dev *ena_dev)
{
	return ena_com_check_supported_feature_id(ena_dev,
						  ENA_ADMIN_INTERRUPT_MODERATION);
}

static int ena_com_update_nonadaptive_moderation_interval(struct ena_com_dev *ena_dev,
							  u32 coalesce_usecs,
							  u32 intr_delay_resolution,
							  u32 *intr_moder_interval)
{
	if (!intr_delay_resolution) {
		netdev_err(ena_dev->net_device,
			   "Illegal interrupt delay granularity value\n");
		return -EFAULT;
	}

	*intr_moder_interval = coalesce_usecs / intr_delay_resolution;

	return 0;
}

int ena_com_update_nonadaptive_moderation_interval_tx(struct ena_com_dev *ena_dev,
						      u32 tx_coalesce_usecs)
{
	return ena_com_update_nonadaptive_moderation_interval(ena_dev,
							      tx_coalesce_usecs,
							      ena_dev->intr_delay_resolution,
							      &ena_dev->intr_moder_tx_interval);
}

int ena_com_update_nonadaptive_moderation_interval_rx(struct ena_com_dev *ena_dev,
						      u32 rx_coalesce_usecs)
{
	return ena_com_update_nonadaptive_moderation_interval(ena_dev,
							      rx_coalesce_usecs,
							      ena_dev->intr_delay_resolution,
							      &ena_dev->intr_moder_rx_interval);
}

int ena_com_init_interrupt_moderation(struct ena_com_dev *ena_dev)
{
	struct ena_admin_get_feat_resp get_resp;
	u16 delay_resolution;
	int rc;

	rc = ena_com_get_feature(ena_dev, &get_resp,
				 ENA_ADMIN_INTERRUPT_MODERATION, 0);

	if (rc) {
		if (rc == -EOPNOTSUPP) {
			netdev_dbg(ena_dev->net_device,
				   "Feature %d isn't supported\n",
				   ENA_ADMIN_INTERRUPT_MODERATION);
			rc = 0;
		} else {
			netdev_err(ena_dev->net_device,
				   "Failed to get interrupt moderation admin cmd. rc: %d\n",
				   rc);
		}

		/* no moderation supported, disable adaptive support */
		ena_com_disable_adaptive_moderation(ena_dev);
		return rc;
	}

	/* if moderation is supported by device we set adaptive moderation */
	delay_resolution = get_resp.u.intr_moderation.intr_delay_resolution;
	ena_com_update_intr_delay_resolution(ena_dev, delay_resolution);

	/* Disable adaptive moderation by default - can be enabled later */
	ena_com_disable_adaptive_moderation(ena_dev);

	return 0;
}

unsigned int ena_com_get_nonadaptive_moderation_interval_tx(struct ena_com_dev *ena_dev)
{
	return ena_dev->intr_moder_tx_interval;
}

unsigned int ena_com_get_nonadaptive_moderation_interval_rx(struct ena_com_dev *ena_dev)
{
	return ena_dev->intr_moder_rx_interval;
}

int ena_com_config_dev_mode(struct ena_com_dev *ena_dev,
			    struct ena_admin_feature_llq_desc *llq_features,
			    struct ena_llq_configurations *llq_default_cfg)
{
	struct ena_com_llq_info *llq_info = &ena_dev->llq_info;
	int rc;

	if (!llq_features->max_llq_num) {
		ena_dev->tx_mem_queue_type = ENA_ADMIN_PLACEMENT_POLICY_HOST;
		return 0;
	}

	rc = ena_com_config_llq_info(ena_dev, llq_features, llq_default_cfg);
	if (rc)
		return rc;

	ena_dev->tx_max_header_size = llq_info->desc_list_entry_size -
		(llq_info->descs_num_before_header * sizeof(struct ena_eth_io_tx_desc));

	if (unlikely(ena_dev->tx_max_header_size == 0)) {
		netdev_err(ena_dev->net_device,
			   "The size of the LLQ entry is smaller than needed\n");
		return -EINVAL;
	}

	ena_dev->tx_mem_queue_type = ENA_ADMIN_PLACEMENT_POLICY_DEV;

	return 0;
}
