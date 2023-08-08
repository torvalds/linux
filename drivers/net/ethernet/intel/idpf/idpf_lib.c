// SPDX-License-Identifier: GPL-2.0-only
/* Copyright (C) 2023 Intel Corporation */

#include "idpf.h"

const char * const idpf_vport_vc_state_str[] = {
	IDPF_FOREACH_VPORT_VC_STATE(IDPF_GEN_STRING)
};

/**
 * idpf_init_vector_stack - Fill the MSIX vector stack with vector index
 * @adapter: private data struct
 *
 * Return 0 on success, error on failure
 */
static int idpf_init_vector_stack(struct idpf_adapter *adapter)
{
	struct idpf_vector_lifo *stack;
	u16 min_vec;
	u32 i;

	mutex_lock(&adapter->vector_lock);
	min_vec = adapter->num_msix_entries - adapter->num_avail_msix;
	stack = &adapter->vector_stack;
	stack->size = adapter->num_msix_entries;
	/* set the base and top to point at start of the 'free pool' to
	 * distribute the unused vectors on-demand basis
	 */
	stack->base = min_vec;
	stack->top = min_vec;

	stack->vec_idx = kcalloc(stack->size, sizeof(u16), GFP_KERNEL);
	if (!stack->vec_idx) {
		mutex_unlock(&adapter->vector_lock);

		return -ENOMEM;
	}

	for (i = 0; i < stack->size; i++)
		stack->vec_idx[i] = i;

	mutex_unlock(&adapter->vector_lock);

	return 0;
}

/**
 * idpf_deinit_vector_stack - zero out the MSIX vector stack
 * @adapter: private data struct
 */
static void idpf_deinit_vector_stack(struct idpf_adapter *adapter)
{
	struct idpf_vector_lifo *stack;

	mutex_lock(&adapter->vector_lock);
	stack = &adapter->vector_stack;
	kfree(stack->vec_idx);
	stack->vec_idx = NULL;
	mutex_unlock(&adapter->vector_lock);
}

/**
 * idpf_mb_intr_rel_irq - Free the IRQ association with the OS
 * @adapter: adapter structure
 *
 * This will also disable interrupt mode and queue up mailbox task. Mailbox
 * task will reschedule itself if not in interrupt mode.
 */
static void idpf_mb_intr_rel_irq(struct idpf_adapter *adapter)
{
	clear_bit(IDPF_MB_INTR_MODE, adapter->flags);
	free_irq(adapter->msix_entries[0].vector, adapter);
	queue_delayed_work(adapter->mbx_wq, &adapter->mbx_task, 0);
}

/**
 * idpf_intr_rel - Release interrupt capabilities and free memory
 * @adapter: adapter to disable interrupts on
 */
void idpf_intr_rel(struct idpf_adapter *adapter)
{
	int err;

	if (!adapter->msix_entries)
		return;

	idpf_mb_intr_rel_irq(adapter);
	pci_free_irq_vectors(adapter->pdev);

	err = idpf_send_dealloc_vectors_msg(adapter);
	if (err)
		dev_err(&adapter->pdev->dev,
			"Failed to deallocate vectors: %d\n", err);

	idpf_deinit_vector_stack(adapter);
	kfree(adapter->msix_entries);
	adapter->msix_entries = NULL;
}

/**
 * idpf_mb_intr_clean - Interrupt handler for the mailbox
 * @irq: interrupt number
 * @data: pointer to the adapter structure
 */
static irqreturn_t idpf_mb_intr_clean(int __always_unused irq, void *data)
{
	struct idpf_adapter *adapter = (struct idpf_adapter *)data;

	queue_delayed_work(adapter->mbx_wq, &adapter->mbx_task, 0);

	return IRQ_HANDLED;
}

/**
 * idpf_mb_irq_enable - Enable MSIX interrupt for the mailbox
 * @adapter: adapter to get the hardware address for register write
 */
static void idpf_mb_irq_enable(struct idpf_adapter *adapter)
{
	struct idpf_intr_reg *intr = &adapter->mb_vector.intr_reg;
	u32 val;

	val = intr->dyn_ctl_intena_m | intr->dyn_ctl_itridx_m;
	writel(val, intr->dyn_ctl);
	writel(intr->icr_ena_ctlq_m, intr->icr_ena);
}

/**
 * idpf_mb_intr_req_irq - Request irq for the mailbox interrupt
 * @adapter: adapter structure to pass to the mailbox irq handler
 */
static int idpf_mb_intr_req_irq(struct idpf_adapter *adapter)
{
	struct idpf_q_vector *mb_vector = &adapter->mb_vector;
	int irq_num, mb_vidx = 0, err;

	irq_num = adapter->msix_entries[mb_vidx].vector;
	mb_vector->name = kasprintf(GFP_KERNEL, "%s-%s-%d",
				    dev_driver_string(&adapter->pdev->dev),
				    "Mailbox", mb_vidx);
	err = request_irq(irq_num, adapter->irq_mb_handler, 0,
			  mb_vector->name, adapter);
	if (err) {
		dev_err(&adapter->pdev->dev,
			"IRQ request for mailbox failed, error: %d\n", err);

		return err;
	}

	set_bit(IDPF_MB_INTR_MODE, adapter->flags);

	return 0;
}

/**
 * idpf_set_mb_vec_id - Set vector index for mailbox
 * @adapter: adapter structure to access the vector chunks
 *
 * The first vector id in the requested vector chunks from the CP is for
 * the mailbox
 */
static void idpf_set_mb_vec_id(struct idpf_adapter *adapter)
{
	if (adapter->req_vec_chunks)
		adapter->mb_vector.v_idx =
			le16_to_cpu(adapter->caps.mailbox_vector_id);
	else
		adapter->mb_vector.v_idx = 0;
}

/**
 * idpf_mb_intr_init - Initialize the mailbox interrupt
 * @adapter: adapter structure to store the mailbox vector
 */
static int idpf_mb_intr_init(struct idpf_adapter *adapter)
{
	adapter->dev_ops.reg_ops.mb_intr_reg_init(adapter);
	adapter->irq_mb_handler = idpf_mb_intr_clean;

	return idpf_mb_intr_req_irq(adapter);
}

/**
 * idpf_intr_req - Request interrupt capabilities
 * @adapter: adapter to enable interrupts on
 *
 * Returns 0 on success, negative on failure
 */
int idpf_intr_req(struct idpf_adapter *adapter)
{
	u16 default_vports = idpf_get_default_vports(adapter);
	int num_q_vecs, total_vecs, num_vec_ids;
	int min_vectors, v_actual, err;
	unsigned int vector;
	u16 *vecids;

	total_vecs = idpf_get_reserved_vecs(adapter);
	num_q_vecs = total_vecs - IDPF_MBX_Q_VEC;

	err = idpf_send_alloc_vectors_msg(adapter, num_q_vecs);
	if (err) {
		dev_err(&adapter->pdev->dev,
			"Failed to allocate %d vectors: %d\n", num_q_vecs, err);

		return -EAGAIN;
	}

	min_vectors = IDPF_MBX_Q_VEC + IDPF_MIN_Q_VEC * default_vports;
	v_actual = pci_alloc_irq_vectors(adapter->pdev, min_vectors,
					 total_vecs, PCI_IRQ_MSIX);
	if (v_actual < min_vectors) {
		dev_err(&adapter->pdev->dev, "Failed to allocate MSIX vectors: %d\n",
			v_actual);
		err = -EAGAIN;
		goto send_dealloc_vecs;
	}

	adapter->msix_entries = kcalloc(v_actual, sizeof(struct msix_entry),
					GFP_KERNEL);

	if (!adapter->msix_entries) {
		err = -ENOMEM;
		goto free_irq;
	}

	idpf_set_mb_vec_id(adapter);

	vecids = kcalloc(total_vecs, sizeof(u16), GFP_KERNEL);
	if (!vecids) {
		err = -ENOMEM;
		goto free_msix;
	}

	if (adapter->req_vec_chunks) {
		struct virtchnl2_vector_chunks *vchunks;
		struct virtchnl2_alloc_vectors *ac;

		ac = adapter->req_vec_chunks;
		vchunks = &ac->vchunks;

		num_vec_ids = idpf_get_vec_ids(adapter, vecids, total_vecs,
					       vchunks);
		if (num_vec_ids < v_actual) {
			err = -EINVAL;
			goto free_vecids;
		}
	} else {
		int i;

		for (i = 0; i < v_actual; i++)
			vecids[i] = i;
	}

	for (vector = 0; vector < v_actual; vector++) {
		adapter->msix_entries[vector].entry = vecids[vector];
		adapter->msix_entries[vector].vector =
			pci_irq_vector(adapter->pdev, vector);
	}

	adapter->num_req_msix = total_vecs;
	adapter->num_msix_entries = v_actual;
	/* 'num_avail_msix' is used to distribute excess vectors to the vports
	 * after considering the minimum vectors required per each default
	 * vport
	 */
	adapter->num_avail_msix = v_actual - min_vectors;

	/* Fill MSIX vector lifo stack with vector indexes */
	err = idpf_init_vector_stack(adapter);
	if (err)
		goto free_vecids;

	err = idpf_mb_intr_init(adapter);
	if (err)
		goto deinit_vec_stack;
	idpf_mb_irq_enable(adapter);
	kfree(vecids);

	return 0;

deinit_vec_stack:
	idpf_deinit_vector_stack(adapter);
free_vecids:
	kfree(vecids);
free_msix:
	kfree(adapter->msix_entries);
	adapter->msix_entries = NULL;
free_irq:
	pci_free_irq_vectors(adapter->pdev);
send_dealloc_vecs:
	idpf_send_dealloc_vectors_msg(adapter);

	return err;
}

/**
 * idpf_mbx_task - Delayed task to handle mailbox responses
 * @work: work_struct handle
 */
void idpf_mbx_task(struct work_struct *work)
{
	struct idpf_adapter *adapter;

	adapter = container_of(work, struct idpf_adapter, mbx_task.work);

	if (test_bit(IDPF_MB_INTR_MODE, adapter->flags))
		idpf_mb_irq_enable(adapter);
	else
		queue_delayed_work(adapter->mbx_wq, &adapter->mbx_task,
				   msecs_to_jiffies(300));

	idpf_recv_mb_msg(adapter, VIRTCHNL2_OP_UNKNOWN, NULL, 0);
}

/**
 * idpf_service_task - Delayed task for handling mailbox responses
 * @work: work_struct handle to our data
 *
 */
void idpf_service_task(struct work_struct *work)
{
	struct idpf_adapter *adapter;

	adapter = container_of(work, struct idpf_adapter, serv_task.work);

	if (idpf_is_reset_detected(adapter) &&
	    !idpf_is_reset_in_prog(adapter) &&
	    !test_bit(IDPF_REMOVE_IN_PROG, adapter->flags)) {
		dev_info(&adapter->pdev->dev, "HW reset detected\n");
		set_bit(IDPF_HR_FUNC_RESET, adapter->flags);
		queue_delayed_work(adapter->vc_event_wq,
				   &adapter->vc_event_task,
				   msecs_to_jiffies(10));
	}

	queue_delayed_work(adapter->serv_wq, &adapter->serv_task,
			   msecs_to_jiffies(300));
}

/**
 * idpf_check_reset_complete - check that reset is complete
 * @hw: pointer to hw struct
 * @reset_reg: struct with reset registers
 *
 * Returns 0 if device is ready to use, or -EBUSY if it's in reset.
 **/
static int idpf_check_reset_complete(struct idpf_hw *hw,
				     struct idpf_reset_reg *reset_reg)
{
	struct idpf_adapter *adapter = hw->back;
	int i;

	for (i = 0; i < 2000; i++) {
		u32 reg_val = readl(reset_reg->rstat);

		/* 0xFFFFFFFF might be read if other side hasn't cleared the
		 * register for us yet and 0xFFFFFFFF is not a valid value for
		 * the register, so treat that as invalid.
		 */
		if (reg_val != 0xFFFFFFFF && (reg_val & reset_reg->rstat_m))
			return 0;

		usleep_range(5000, 10000);
	}

	dev_warn(&adapter->pdev->dev, "Device reset timeout!\n");
	/* Clear the reset flag unconditionally here since the reset
	 * technically isn't in progress anymore from the driver's perspective
	 */
	clear_bit(IDPF_HR_RESET_IN_PROG, adapter->flags);

	return -EBUSY;
}

/**
 * idpf_init_hard_reset - Initiate a hardware reset
 * @adapter: Driver specific private structure
 *
 * Deallocate the vports and all the resources associated with them and
 * reallocate. Also reinitialize the mailbox. Return 0 on success,
 * negative on failure.
 */
static int idpf_init_hard_reset(struct idpf_adapter *adapter)
{
	struct idpf_reg_ops *reg_ops = &adapter->dev_ops.reg_ops;
	struct device *dev = &adapter->pdev->dev;
	int err;

	mutex_lock(&adapter->vport_ctrl_lock);

	dev_info(dev, "Device HW Reset initiated\n");
	/* Prepare for reset */
	if (test_and_clear_bit(IDPF_HR_DRV_LOAD, adapter->flags)) {
		reg_ops->trigger_reset(adapter, IDPF_HR_DRV_LOAD);
	} else if (test_and_clear_bit(IDPF_HR_FUNC_RESET, adapter->flags)) {
		bool is_reset = idpf_is_reset_detected(adapter);

		idpf_vc_core_deinit(adapter);
		if (!is_reset)
			reg_ops->trigger_reset(adapter, IDPF_HR_FUNC_RESET);
		idpf_deinit_dflt_mbx(adapter);
	} else {
		dev_err(dev, "Unhandled hard reset cause\n");
		err = -EBADRQC;
		goto unlock_mutex;
	}

	/* Wait for reset to complete */
	err = idpf_check_reset_complete(&adapter->hw, &adapter->reset_reg);
	if (err) {
		dev_err(dev, "The driver was unable to contact the device's firmware. Check that the FW is running. Driver state= 0x%x\n",
			adapter->state);
		goto unlock_mutex;
	}

	/* Reset is complete and so start building the driver resources again */
	err = idpf_init_dflt_mbx(adapter);
	if (err) {
		dev_err(dev, "Failed to initialize default mailbox: %d\n", err);
		goto unlock_mutex;
	}

	/* Initialize the state machine, also allocate memory and request
	 * resources
	 */
	err = idpf_vc_core_init(adapter);
	if (err) {
		idpf_deinit_dflt_mbx(adapter);
		goto unlock_mutex;
	}

unlock_mutex:
	mutex_unlock(&adapter->vport_ctrl_lock);

	return err;
}

/**
 * idpf_vc_event_task - Handle virtchannel event logic
 * @work: work queue struct
 */
void idpf_vc_event_task(struct work_struct *work)
{
	struct idpf_adapter *adapter;

	adapter = container_of(work, struct idpf_adapter, vc_event_task.work);

	if (test_bit(IDPF_REMOVE_IN_PROG, adapter->flags))
		return;

	if (test_bit(IDPF_HR_FUNC_RESET, adapter->flags) ||
	    test_bit(IDPF_HR_DRV_LOAD, adapter->flags)) {
		set_bit(IDPF_HR_RESET_IN_PROG, adapter->flags);
		idpf_init_hard_reset(adapter);
	}
}

/**
 * idpf_alloc_dma_mem - Allocate dma memory
 * @hw: pointer to hw struct
 * @mem: pointer to dma_mem struct
 * @size: size of the memory to allocate
 */
void *idpf_alloc_dma_mem(struct idpf_hw *hw, struct idpf_dma_mem *mem, u64 size)
{
	struct idpf_adapter *adapter = hw->back;
	size_t sz = ALIGN(size, 4096);

	mem->va = dma_alloc_coherent(&adapter->pdev->dev, sz,
				     &mem->pa, GFP_KERNEL);
	mem->size = sz;

	return mem->va;
}

/**
 * idpf_free_dma_mem - Free the allocated dma memory
 * @hw: pointer to hw struct
 * @mem: pointer to dma_mem struct
 */
void idpf_free_dma_mem(struct idpf_hw *hw, struct idpf_dma_mem *mem)
{
	struct idpf_adapter *adapter = hw->back;

	dma_free_coherent(&adapter->pdev->dev, mem->size,
			  mem->va, mem->pa);
	mem->size = 0;
	mem->va = NULL;
	mem->pa = 0;
}
