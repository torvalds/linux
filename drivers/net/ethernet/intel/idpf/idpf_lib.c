// SPDX-License-Identifier: GPL-2.0-only
/* Copyright (C) 2023 Intel Corporation */

#include "idpf.h"

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
	if (err)
		dev_err(dev, "Failed to initialize default mailbox: %d\n", err);

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
