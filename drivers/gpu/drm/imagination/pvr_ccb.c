// SPDX-License-Identifier: GPL-2.0-only OR MIT
/* Copyright (c) 2023 Imagination Technologies Ltd. */

#include "pvr_ccb.h"
#include "pvr_device.h"
#include "pvr_drv.h"
#include "pvr_free_list.h"
#include "pvr_fw.h"
#include "pvr_gem.h"
#include "pvr_power.h"

#include <drm/drm_managed.h>
#include <linux/compiler.h>
#include <linux/delay.h>
#include <linux/jiffies.h>
#include <linux/kernel.h>
#include <linux/mutex.h>
#include <linux/types.h>
#include <linux/workqueue.h>

#define RESERVE_SLOT_TIMEOUT (1 * HZ) /* 1s */
#define RESERVE_SLOT_MIN_RETRIES 10

static void
ccb_ctrl_init(void *cpu_ptr, void *priv)
{
	struct rogue_fwif_ccb_ctl *ctrl = cpu_ptr;
	struct pvr_ccb *pvr_ccb = priv;

	ctrl->write_offset = 0;
	ctrl->read_offset = 0;
	ctrl->wrap_mask = pvr_ccb->num_cmds - 1;
	ctrl->cmd_size = pvr_ccb->cmd_size;
}

/**
 * pvr_ccb_init() - Initialise a CCB
 * @pvr_dev: Device pointer.
 * @pvr_ccb: Pointer to CCB structure to initialise.
 * @num_cmds_log2: Log2 of number of commands in this CCB.
 * @cmd_size: Command size for this CCB.
 *
 * Return:
 *  * Zero on success, or
 *  * Any error code returned by pvr_fw_object_create_and_map().
 */
static int
pvr_ccb_init(struct pvr_device *pvr_dev, struct pvr_ccb *pvr_ccb,
	     u32 num_cmds_log2, size_t cmd_size)
{
	u32 num_cmds = 1 << num_cmds_log2;
	u32 ccb_size = num_cmds * cmd_size;
	int err;

	pvr_ccb->num_cmds = num_cmds;
	pvr_ccb->cmd_size = cmd_size;

	err = drmm_mutex_init(from_pvr_device(pvr_dev), &pvr_ccb->lock);
	if (err)
		return err;

	/*
	 * Map CCB and control structure as uncached, so we don't have to flush
	 * CPU cache repeatedly when polling for space.
	 */
	pvr_ccb->ctrl = pvr_fw_object_create_and_map(pvr_dev, sizeof(*pvr_ccb->ctrl),
						     PVR_BO_FW_FLAGS_DEVICE_UNCACHED,
						     ccb_ctrl_init, pvr_ccb, &pvr_ccb->ctrl_obj);
	if (IS_ERR(pvr_ccb->ctrl))
		return PTR_ERR(pvr_ccb->ctrl);

	pvr_ccb->ccb = pvr_fw_object_create_and_map(pvr_dev, ccb_size,
						    PVR_BO_FW_FLAGS_DEVICE_UNCACHED,
						    NULL, NULL, &pvr_ccb->ccb_obj);
	if (IS_ERR(pvr_ccb->ccb)) {
		err = PTR_ERR(pvr_ccb->ccb);
		goto err_free_ctrl;
	}

	pvr_fw_object_get_fw_addr(pvr_ccb->ctrl_obj, &pvr_ccb->ctrl_fw_addr);
	pvr_fw_object_get_fw_addr(pvr_ccb->ccb_obj, &pvr_ccb->ccb_fw_addr);

	WRITE_ONCE(pvr_ccb->ctrl->write_offset, 0);
	WRITE_ONCE(pvr_ccb->ctrl->read_offset, 0);
	WRITE_ONCE(pvr_ccb->ctrl->wrap_mask, num_cmds - 1);
	WRITE_ONCE(pvr_ccb->ctrl->cmd_size, cmd_size);

	return 0;

err_free_ctrl:
	pvr_fw_object_unmap_and_destroy(pvr_ccb->ctrl_obj);

	return err;
}

/**
 * pvr_ccb_fini() - Release CCB structure
 * @pvr_ccb: CCB to release.
 */
void
pvr_ccb_fini(struct pvr_ccb *pvr_ccb)
{
	pvr_fw_object_unmap_and_destroy(pvr_ccb->ccb_obj);
	pvr_fw_object_unmap_and_destroy(pvr_ccb->ctrl_obj);
}

/**
 * pvr_ccb_slot_available_locked() - Test whether any slots are available in CCB
 * @pvr_ccb: CCB to test.
 * @write_offset: Address to store number of next available slot. May be %NULL.
 *
 * Caller must hold @pvr_ccb->lock.
 *
 * Return:
 *  * %true if a slot is available, or
 *  * %false if no slot is available.
 */
static __always_inline bool
pvr_ccb_slot_available_locked(struct pvr_ccb *pvr_ccb, u32 *write_offset)
{
	struct rogue_fwif_ccb_ctl *ctrl = pvr_ccb->ctrl;
	u32 next_write_offset = (READ_ONCE(ctrl->write_offset) + 1) & READ_ONCE(ctrl->wrap_mask);

	lockdep_assert_held(&pvr_ccb->lock);

	if (READ_ONCE(ctrl->read_offset) != next_write_offset) {
		if (write_offset)
			*write_offset = next_write_offset;
		return true;
	}

	return false;
}

static void
process_fwccb_command(struct pvr_device *pvr_dev, struct rogue_fwif_fwccb_cmd *cmd)
{
	switch (cmd->cmd_type) {
	case ROGUE_FWIF_FWCCB_CMD_REQUEST_GPU_RESTART:
		pvr_power_reset(pvr_dev, false);
		break;

	case ROGUE_FWIF_FWCCB_CMD_FREELISTS_RECONSTRUCTION:
		pvr_free_list_process_reconstruct_req(pvr_dev,
						      &cmd->cmd_data.cmd_freelists_reconstruction);
		break;

	case ROGUE_FWIF_FWCCB_CMD_FREELIST_GROW:
		pvr_free_list_process_grow_req(pvr_dev, &cmd->cmd_data.cmd_free_list_gs);
		break;

	default:
		drm_info(from_pvr_device(pvr_dev), "Received unknown FWCCB command %x\n",
			 cmd->cmd_type);
		break;
	}
}

/**
 * pvr_fwccb_process() - Process any pending FWCCB commands
 * @pvr_dev: Target PowerVR device
 */
void pvr_fwccb_process(struct pvr_device *pvr_dev)
{
	struct rogue_fwif_fwccb_cmd *fwccb = pvr_dev->fwccb.ccb;
	struct rogue_fwif_ccb_ctl *ctrl = pvr_dev->fwccb.ctrl;
	u32 read_offset;

	mutex_lock(&pvr_dev->fwccb.lock);

	while ((read_offset = READ_ONCE(ctrl->read_offset)) != READ_ONCE(ctrl->write_offset)) {
		struct rogue_fwif_fwccb_cmd cmd = fwccb[read_offset];

		WRITE_ONCE(ctrl->read_offset, (read_offset + 1) & READ_ONCE(ctrl->wrap_mask));

		/* Drop FWCCB lock while we process command. */
		mutex_unlock(&pvr_dev->fwccb.lock);

		process_fwccb_command(pvr_dev, &cmd);

		mutex_lock(&pvr_dev->fwccb.lock);
	}

	mutex_unlock(&pvr_dev->fwccb.lock);
}

/**
 * pvr_kccb_capacity() - Returns the maximum number of usable KCCB slots.
 * @pvr_dev: Target PowerVR device
 *
 * Return:
 *  * The maximum number of active slots.
 */
static u32 pvr_kccb_capacity(struct pvr_device *pvr_dev)
{
	/* Capacity is the number of slot minus one to cope with the wrapping
	 * mechanisms. If we were to use all slots, we might end up with
	 * read_offset == write_offset, which the FW considers as a KCCB-is-empty
	 * condition.
	 */
	return pvr_dev->kccb.slot_count - 1;
}

/**
 * pvr_kccb_used_slot_count_locked() - Get the number of used slots
 * @pvr_dev: Device pointer.
 *
 * KCCB lock must be held.
 *
 * Return:
 *  * The number of slots currently used.
 */
static u32
pvr_kccb_used_slot_count_locked(struct pvr_device *pvr_dev)
{
	struct pvr_ccb *pvr_ccb = &pvr_dev->kccb.ccb;
	struct rogue_fwif_ccb_ctl *ctrl = pvr_ccb->ctrl;
	u32 wr_offset = READ_ONCE(ctrl->write_offset);
	u32 rd_offset = READ_ONCE(ctrl->read_offset);
	u32 used_count;

	lockdep_assert_held(&pvr_ccb->lock);

	if (wr_offset >= rd_offset)
		used_count = wr_offset - rd_offset;
	else
		used_count = wr_offset + pvr_dev->kccb.slot_count - rd_offset;

	return used_count;
}

/**
 * pvr_kccb_send_cmd_reserved_powered() - Send command to the KCCB, with the PM ref
 * held and a slot pre-reserved
 * @pvr_dev: Device pointer.
 * @cmd: Command to sent.
 * @kccb_slot: Address to store the KCCB slot for this command. May be %NULL.
 */
void
pvr_kccb_send_cmd_reserved_powered(struct pvr_device *pvr_dev,
				   struct rogue_fwif_kccb_cmd *cmd,
				   u32 *kccb_slot)
{
	struct pvr_ccb *pvr_ccb = &pvr_dev->kccb.ccb;
	struct rogue_fwif_kccb_cmd *kccb = pvr_ccb->ccb;
	struct rogue_fwif_ccb_ctl *ctrl = pvr_ccb->ctrl;
	u32 old_write_offset;
	u32 new_write_offset;

	WARN_ON(pvr_dev->lost);

	mutex_lock(&pvr_ccb->lock);

	if (WARN_ON(!pvr_dev->kccb.reserved_count))
		goto out_unlock;

	old_write_offset = READ_ONCE(ctrl->write_offset);

	/* We reserved the slot, we should have one available. */
	if (WARN_ON(!pvr_ccb_slot_available_locked(pvr_ccb, &new_write_offset)))
		goto out_unlock;

	memcpy(&kccb[old_write_offset], cmd,
	       sizeof(struct rogue_fwif_kccb_cmd));
	if (kccb_slot) {
		*kccb_slot = old_write_offset;
		/* Clear return status for this slot. */
		WRITE_ONCE(pvr_dev->kccb.rtn[old_write_offset],
			   ROGUE_FWIF_KCCB_RTN_SLOT_NO_RESPONSE);
	}
	mb(); /* memory barrier */
	WRITE_ONCE(ctrl->write_offset, new_write_offset);
	pvr_dev->kccb.reserved_count--;

	/* Kick MTS */
	pvr_fw_mts_schedule(pvr_dev,
			    PVR_FWIF_DM_GP & ~ROGUE_CR_MTS_SCHEDULE_DM_CLRMSK);

out_unlock:
	mutex_unlock(&pvr_ccb->lock);
}

/**
 * pvr_kccb_try_reserve_slot() - Try to reserve a KCCB slot
 * @pvr_dev: Device pointer.
 *
 * Return:
 *  * true if a KCCB slot was reserved, or
 *  * false otherwise.
 */
static bool pvr_kccb_try_reserve_slot(struct pvr_device *pvr_dev)
{
	bool reserved = false;
	u32 used_count;

	mutex_lock(&pvr_dev->kccb.ccb.lock);

	used_count = pvr_kccb_used_slot_count_locked(pvr_dev);
	if (pvr_dev->kccb.reserved_count < pvr_kccb_capacity(pvr_dev) - used_count) {
		pvr_dev->kccb.reserved_count++;
		reserved = true;
	}

	mutex_unlock(&pvr_dev->kccb.ccb.lock);

	return reserved;
}

/**
 * pvr_kccb_reserve_slot_sync() - Try to reserve a slot synchronously
 * @pvr_dev: Device pointer.
 *
 * Return:
 *  * 0 on success, or
 *  * -EBUSY if no slots were reserved after %RESERVE_SLOT_TIMEOUT, with a minimum of
 *    %RESERVE_SLOT_MIN_RETRIES retries.
 */
static int pvr_kccb_reserve_slot_sync(struct pvr_device *pvr_dev)
{
	unsigned long start_timestamp = jiffies;
	bool reserved = false;
	u32 retries = 0;

	while ((jiffies - start_timestamp) < (u32)RESERVE_SLOT_TIMEOUT ||
	       retries < RESERVE_SLOT_MIN_RETRIES) {
		reserved = pvr_kccb_try_reserve_slot(pvr_dev);
		if (reserved)
			break;

		usleep_range(1, 50);

		if (retries < U32_MAX)
			retries++;
	}

	return reserved ? 0 : -EBUSY;
}

/**
 * pvr_kccb_send_cmd_powered() - Send command to the KCCB, with a PM ref held
 * @pvr_dev: Device pointer.
 * @cmd: Command to sent.
 * @kccb_slot: Address to store the KCCB slot for this command. May be %NULL.
 *
 * Returns:
 *  * Zero on success, or
 *  * -EBUSY if timeout while waiting for a free KCCB slot.
 */
int
pvr_kccb_send_cmd_powered(struct pvr_device *pvr_dev, struct rogue_fwif_kccb_cmd *cmd,
			  u32 *kccb_slot)
{
	int err;

	err = pvr_kccb_reserve_slot_sync(pvr_dev);
	if (err)
		return err;

	pvr_kccb_send_cmd_reserved_powered(pvr_dev, cmd, kccb_slot);
	return 0;
}

/**
 * pvr_kccb_send_cmd() - Send command to the KCCB
 * @pvr_dev: Device pointer.
 * @cmd: Command to sent.
 * @kccb_slot: Address to store the KCCB slot for this command. May be %NULL.
 *
 * Returns:
 *  * Zero on success, or
 *  * -EBUSY if timeout while waiting for a free KCCB slot.
 */
int
pvr_kccb_send_cmd(struct pvr_device *pvr_dev, struct rogue_fwif_kccb_cmd *cmd,
		  u32 *kccb_slot)
{
	int err;

	err = pvr_power_get(pvr_dev);
	if (err)
		return err;

	err = pvr_kccb_send_cmd_powered(pvr_dev, cmd, kccb_slot);

	pvr_power_put(pvr_dev);

	return err;
}

/**
 * pvr_kccb_wait_for_completion() - Wait for a KCCB command to complete
 * @pvr_dev: Device pointer.
 * @slot_nr: KCCB slot to wait on.
 * @timeout: Timeout length (in jiffies).
 * @rtn_out: Location to store KCCB command result. May be %NULL.
 *
 * Returns:
 *  * Zero on success, or
 *  * -ETIMEDOUT on timeout.
 */
int
pvr_kccb_wait_for_completion(struct pvr_device *pvr_dev, u32 slot_nr,
			     u32 timeout, u32 *rtn_out)
{
	int ret = wait_event_timeout(pvr_dev->kccb.rtn_q, READ_ONCE(pvr_dev->kccb.rtn[slot_nr]) &
				     ROGUE_FWIF_KCCB_RTN_SLOT_CMD_EXECUTED, timeout);

	if (ret && rtn_out)
		*rtn_out = READ_ONCE(pvr_dev->kccb.rtn[slot_nr]);

	return ret ? 0 : -ETIMEDOUT;
}

/**
 * pvr_kccb_is_idle() - Returns whether the device's KCCB is idle
 * @pvr_dev: Device pointer
 *
 * Returns:
 *  * %true if the KCCB is idle (contains no commands), or
 *  * %false if the KCCB contains pending commands.
 */
bool
pvr_kccb_is_idle(struct pvr_device *pvr_dev)
{
	struct rogue_fwif_ccb_ctl *ctrl = pvr_dev->kccb.ccb.ctrl;
	bool idle;

	mutex_lock(&pvr_dev->kccb.ccb.lock);

	idle = (READ_ONCE(ctrl->write_offset) == READ_ONCE(ctrl->read_offset));

	mutex_unlock(&pvr_dev->kccb.ccb.lock);

	return idle;
}

static const char *
pvr_kccb_fence_get_driver_name(struct dma_fence *f)
{
	return PVR_DRIVER_NAME;
}

static const char *
pvr_kccb_fence_get_timeline_name(struct dma_fence *f)
{
	return "kccb";
}

static const struct dma_fence_ops pvr_kccb_fence_ops = {
	.get_driver_name = pvr_kccb_fence_get_driver_name,
	.get_timeline_name = pvr_kccb_fence_get_timeline_name,
};

/**
 * struct pvr_kccb_fence - Fence object used to wait for a KCCB slot
 */
struct pvr_kccb_fence {
	/** @base: Base dma_fence object. */
	struct dma_fence base;

	/** @node: Node used to insert the fence in the pvr_device::kccb::waiters list. */
	struct list_head node;
};

/**
 * pvr_kccb_wake_up_waiters() - Check the KCCB waiters
 * @pvr_dev: Target PowerVR device
 *
 * Signal as many KCCB fences as we have slots available.
 */
void pvr_kccb_wake_up_waiters(struct pvr_device *pvr_dev)
{
	struct pvr_kccb_fence *fence, *tmp_fence;
	u32 used_count, available_count;

	/* Wake up those waiting for KCCB slot execution. */
	wake_up_all(&pvr_dev->kccb.rtn_q);

	/* Then iterate over all KCCB fences and signal as many as we can. */
	mutex_lock(&pvr_dev->kccb.ccb.lock);
	used_count = pvr_kccb_used_slot_count_locked(pvr_dev);

	if (WARN_ON(used_count + pvr_dev->kccb.reserved_count > pvr_kccb_capacity(pvr_dev)))
		goto out_unlock;

	available_count = pvr_kccb_capacity(pvr_dev) - used_count - pvr_dev->kccb.reserved_count;
	list_for_each_entry_safe(fence, tmp_fence, &pvr_dev->kccb.waiters, node) {
		if (!available_count)
			break;

		list_del(&fence->node);
		pvr_dev->kccb.reserved_count++;
		available_count--;
		dma_fence_signal(&fence->base);
		dma_fence_put(&fence->base);
	}

out_unlock:
	mutex_unlock(&pvr_dev->kccb.ccb.lock);
}

/**
 * pvr_kccb_fini() - Cleanup device KCCB
 * @pvr_dev: Target PowerVR device
 */
void pvr_kccb_fini(struct pvr_device *pvr_dev)
{
	pvr_ccb_fini(&pvr_dev->kccb.ccb);
	WARN_ON(!list_empty(&pvr_dev->kccb.waiters));
	WARN_ON(pvr_dev->kccb.reserved_count);
}

/**
 * pvr_kccb_init() - Initialise device KCCB
 * @pvr_dev: Target PowerVR device
 *
 * Returns:
 *  * 0 on success, or
 *  * Any error returned by pvr_ccb_init().
 */
int
pvr_kccb_init(struct pvr_device *pvr_dev)
{
	pvr_dev->kccb.slot_count = 1 << ROGUE_FWIF_KCCB_NUMCMDS_LOG2_DEFAULT;
	INIT_LIST_HEAD(&pvr_dev->kccb.waiters);
	pvr_dev->kccb.fence_ctx.id = dma_fence_context_alloc(1);
	spin_lock_init(&pvr_dev->kccb.fence_ctx.lock);

	return pvr_ccb_init(pvr_dev, &pvr_dev->kccb.ccb,
			    ROGUE_FWIF_KCCB_NUMCMDS_LOG2_DEFAULT,
			    sizeof(struct rogue_fwif_kccb_cmd));
}

/**
 * pvr_kccb_fence_alloc() - Allocate a pvr_kccb_fence object
 *
 * Return:
 *  * NULL if the allocation fails, or
 *  * A valid dma_fence pointer otherwise.
 */
struct dma_fence *pvr_kccb_fence_alloc(void)
{
	struct pvr_kccb_fence *kccb_fence;

	kccb_fence = kzalloc(sizeof(*kccb_fence), GFP_KERNEL);
	if (!kccb_fence)
		return NULL;

	return &kccb_fence->base;
}

/**
 * pvr_kccb_fence_put() - Drop a KCCB fence reference
 * @fence: The fence to drop the reference on.
 *
 * If the fence hasn't been initialized yet, dma_fence_free() is called. This
 * way we have a single function taking care of both cases.
 */
void pvr_kccb_fence_put(struct dma_fence *fence)
{
	if (!fence)
		return;

	if (!fence->ops) {
		dma_fence_free(fence);
	} else {
		WARN_ON(fence->ops != &pvr_kccb_fence_ops);
		dma_fence_put(fence);
	}
}

/**
 * pvr_kccb_reserve_slot() - Reserve a KCCB slot for later use
 * @pvr_dev: Target PowerVR device
 * @f: KCCB fence object previously allocated with pvr_kccb_fence_alloc()
 *
 * Try to reserve a KCCB slot, and if there's no slot available,
 * initializes the fence object and queue it to the waiters list.
 *
 * If NULL is returned, that means the slot is reserved. In that case,
 * the @f is freed and shouldn't be accessed after that point.
 *
 * Return:
 *  * NULL if a slot was available directly, or
 *  * A valid dma_fence object to wait on if no slot was available.
 */
struct dma_fence *
pvr_kccb_reserve_slot(struct pvr_device *pvr_dev, struct dma_fence *f)
{
	struct pvr_kccb_fence *fence = container_of(f, struct pvr_kccb_fence, base);
	struct dma_fence *out_fence = NULL;
	u32 used_count;

	mutex_lock(&pvr_dev->kccb.ccb.lock);

	used_count = pvr_kccb_used_slot_count_locked(pvr_dev);
	if (pvr_dev->kccb.reserved_count >= pvr_kccb_capacity(pvr_dev) - used_count) {
		dma_fence_init(&fence->base, &pvr_kccb_fence_ops,
			       &pvr_dev->kccb.fence_ctx.lock,
			       pvr_dev->kccb.fence_ctx.id,
			       atomic_inc_return(&pvr_dev->kccb.fence_ctx.seqno));
		out_fence = dma_fence_get(&fence->base);
		list_add_tail(&fence->node, &pvr_dev->kccb.waiters);
	} else {
		pvr_kccb_fence_put(f);
		pvr_dev->kccb.reserved_count++;
	}

	mutex_unlock(&pvr_dev->kccb.ccb.lock);

	return out_fence;
}

/**
 * pvr_kccb_release_slot() - Release a KCCB slot reserved with
 * pvr_kccb_reserve_slot()
 * @pvr_dev: Target PowerVR device
 *
 * Should only be called if something failed after the
 * pvr_kccb_reserve_slot() call and you know you won't call
 * pvr_kccb_send_cmd_reserved().
 */
void pvr_kccb_release_slot(struct pvr_device *pvr_dev)
{
	mutex_lock(&pvr_dev->kccb.ccb.lock);
	if (!WARN_ON(!pvr_dev->kccb.reserved_count))
		pvr_dev->kccb.reserved_count--;
	mutex_unlock(&pvr_dev->kccb.ccb.lock);
}

/**
 * pvr_fwccb_init() - Initialise device FWCCB
 * @pvr_dev: Target PowerVR device
 *
 * Returns:
 *  * 0 on success, or
 *  * Any error returned by pvr_ccb_init().
 */
int
pvr_fwccb_init(struct pvr_device *pvr_dev)
{
	return pvr_ccb_init(pvr_dev, &pvr_dev->fwccb,
			    ROGUE_FWIF_FWCCB_NUMCMDS_LOG2,
			    sizeof(struct rogue_fwif_fwccb_cmd));
}
