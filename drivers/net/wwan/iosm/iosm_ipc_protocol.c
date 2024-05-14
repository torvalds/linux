// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2020-21 Intel Corporation.
 */

#include "iosm_ipc_imem.h"
#include "iosm_ipc_protocol.h"
#include "iosm_ipc_protocol_ops.h"
#include "iosm_ipc_pm.h"
#include "iosm_ipc_task_queue.h"

int ipc_protocol_tq_msg_send(struct iosm_protocol *ipc_protocol,
			     enum ipc_msg_prep_type msg_type,
			     union ipc_msg_prep_args *prep_args,
			     struct ipc_rsp *response)
{
	int index = ipc_protocol_msg_prep(ipc_protocol->imem, msg_type,
					  prep_args);

	/* Store reference towards caller specified response in response ring
	 * and signal CP
	 */
	if (index >= 0 && index < IPC_MEM_MSG_ENTRIES) {
		ipc_protocol->rsp_ring[index] = response;
		ipc_protocol_msg_hp_update(ipc_protocol->imem);
	}

	return index;
}

/* Callback for message send */
static int ipc_protocol_tq_msg_send_cb(struct iosm_imem *ipc_imem, int arg,
				       void *msg, size_t size)
{
	struct ipc_call_msg_send_args *send_args = msg;
	struct iosm_protocol *ipc_protocol = ipc_imem->ipc_protocol;

	return ipc_protocol_tq_msg_send(ipc_protocol, send_args->msg_type,
					send_args->prep_args,
					send_args->response);
}

/* Remove reference to a response. This is typically used when a requestor timed
 * out and is no longer interested in the response.
 */
static int ipc_protocol_tq_msg_remove(struct iosm_imem *ipc_imem, int arg,
				      void *msg, size_t size)
{
	struct iosm_protocol *ipc_protocol = ipc_imem->ipc_protocol;

	ipc_protocol->rsp_ring[arg] = NULL;
	return 0;
}

int ipc_protocol_msg_send(struct iosm_protocol *ipc_protocol,
			  enum ipc_msg_prep_type prep,
			  union ipc_msg_prep_args *prep_args)
{
	struct ipc_call_msg_send_args send_args;
	unsigned int exec_timeout;
	struct ipc_rsp response;
	int index;

	exec_timeout = (ipc_protocol_get_ap_exec_stage(ipc_protocol) ==
					IPC_MEM_EXEC_STAGE_RUN ?
				IPC_MSG_COMPLETE_RUN_DEFAULT_TIMEOUT :
				IPC_MSG_COMPLETE_BOOT_DEFAULT_TIMEOUT);

	/* Trap if called from non-preemptible context */
	might_sleep();

	response.status = IPC_MEM_MSG_CS_INVALID;
	init_completion(&response.completion);

	send_args.msg_type = prep;
	send_args.prep_args = prep_args;
	send_args.response = &response;

	/* Allocate and prepare message to be sent in tasklet context.
	 * A positive index returned form tasklet_call references the message
	 * in case it needs to be cancelled when there is a timeout.
	 */
	index = ipc_task_queue_send_task(ipc_protocol->imem,
					 ipc_protocol_tq_msg_send_cb, 0,
					 &send_args, 0, true);

	if (index < 0) {
		dev_err(ipc_protocol->dev, "msg %d failed", prep);
		return index;
	}

	/* Wait for the device to respond to the message */
	switch (wait_for_completion_timeout(&response.completion,
					    msecs_to_jiffies(exec_timeout))) {
	case 0:
		/* Timeout, there was no response from the device.
		 * Remove the reference to the local response completion
		 * object as we are no longer interested in the response.
		 */
		ipc_task_queue_send_task(ipc_protocol->imem,
					 ipc_protocol_tq_msg_remove, index,
					 NULL, 0, true);
		dev_err(ipc_protocol->dev, "msg timeout");
		ipc_uevent_send(ipc_protocol->pcie->dev, UEVENT_MDM_TIMEOUT);
		break;
	default:
		/* We got a response in time; check completion status: */
		if (response.status != IPC_MEM_MSG_CS_SUCCESS) {
			dev_err(ipc_protocol->dev,
				"msg completion status error %d",
				response.status);
			return -EIO;
		}
	}

	return 0;
}

static int ipc_protocol_msg_send_host_sleep(struct iosm_protocol *ipc_protocol,
					    u32 state)
{
	union ipc_msg_prep_args prep_args = {
		.sleep.target = 0,
		.sleep.state = state,
	};

	return ipc_protocol_msg_send(ipc_protocol, IPC_MSG_PREP_SLEEP,
				     &prep_args);
}

void ipc_protocol_doorbell_trigger(struct iosm_protocol *ipc_protocol,
				   u32 identifier)
{
	ipc_pm_signal_hpda_doorbell(&ipc_protocol->pm, identifier, true);
}

bool ipc_protocol_pm_dev_sleep_handle(struct iosm_protocol *ipc_protocol)
{
	u32 ipc_status = ipc_protocol_get_ipc_status(ipc_protocol);
	u32 requested;

	if (ipc_status != IPC_MEM_DEVICE_IPC_RUNNING) {
		dev_err(ipc_protocol->dev,
			"irq ignored, CP IPC state is %d, should be RUNNING",
			ipc_status);

		/* Stop further processing. */
		return false;
	}

	/* Get a copy of the requested PM state by the device and the local
	 * device PM state.
	 */
	requested = ipc_protocol_pm_dev_get_sleep_notification(ipc_protocol);

	return ipc_pm_dev_slp_notification(&ipc_protocol->pm, requested);
}

static int ipc_protocol_tq_wakeup_dev_slp(struct iosm_imem *ipc_imem, int arg,
					  void *msg, size_t size)
{
	struct iosm_pm *ipc_pm = &ipc_imem->ipc_protocol->pm;

	/* Wakeup from device sleep if it is not ACTIVE */
	ipc_pm_trigger(ipc_pm, IPC_PM_UNIT_HS, true);

	ipc_pm_trigger(ipc_pm, IPC_PM_UNIT_HS, false);

	return 0;
}

void ipc_protocol_s2idle_sleep(struct iosm_protocol *ipc_protocol, bool sleep)
{
	ipc_pm_set_s2idle_sleep(&ipc_protocol->pm, sleep);
}

bool ipc_protocol_suspend(struct iosm_protocol *ipc_protocol)
{
	if (!ipc_pm_prepare_host_sleep(&ipc_protocol->pm))
		goto err;

	ipc_task_queue_send_task(ipc_protocol->imem,
				 ipc_protocol_tq_wakeup_dev_slp, 0, NULL, 0,
				 true);

	if (!ipc_pm_wait_for_device_active(&ipc_protocol->pm)) {
		ipc_uevent_send(ipc_protocol->pcie->dev, UEVENT_MDM_TIMEOUT);
		goto err;
	}

	/* Send the sleep message for sync sys calls. */
	dev_dbg(ipc_protocol->dev, "send TARGET_HOST, ENTER_SLEEP");
	if (ipc_protocol_msg_send_host_sleep(ipc_protocol,
					     IPC_HOST_SLEEP_ENTER_SLEEP)) {
		/* Sending ENTER_SLEEP message failed, we are still active */
		ipc_protocol->pm.host_pm_state = IPC_MEM_HOST_PM_ACTIVE;
		goto err;
	}

	ipc_protocol->pm.host_pm_state = IPC_MEM_HOST_PM_SLEEP;
	return true;
err:
	return false;
}

bool ipc_protocol_resume(struct iosm_protocol *ipc_protocol)
{
	if (!ipc_pm_prepare_host_active(&ipc_protocol->pm))
		return false;

	dev_dbg(ipc_protocol->dev, "send TARGET_HOST, EXIT_SLEEP");
	if (ipc_protocol_msg_send_host_sleep(ipc_protocol,
					     IPC_HOST_SLEEP_EXIT_SLEEP)) {
		ipc_protocol->pm.host_pm_state = IPC_MEM_HOST_PM_SLEEP;
		return false;
	}

	ipc_protocol->pm.host_pm_state = IPC_MEM_HOST_PM_ACTIVE;

	return true;
}

struct iosm_protocol *ipc_protocol_init(struct iosm_imem *ipc_imem)
{
	struct iosm_protocol *ipc_protocol =
		kzalloc(sizeof(*ipc_protocol), GFP_KERNEL);
	struct ipc_protocol_context_info *p_ci;
	u64 addr;

	if (!ipc_protocol)
		return NULL;

	ipc_protocol->dev = ipc_imem->dev;
	ipc_protocol->pcie = ipc_imem->pcie;
	ipc_protocol->imem = ipc_imem;
	ipc_protocol->p_ap_shm = NULL;
	ipc_protocol->phy_ap_shm = 0;

	ipc_protocol->old_msg_tail = 0;

	ipc_protocol->p_ap_shm =
		dma_alloc_coherent(&ipc_protocol->pcie->pci->dev,
				   sizeof(*ipc_protocol->p_ap_shm),
				   &ipc_protocol->phy_ap_shm, GFP_KERNEL);

	if (!ipc_protocol->p_ap_shm) {
		dev_err(ipc_protocol->dev, "pci shm alloc error");
		kfree(ipc_protocol);
		return NULL;
	}

	/* Prepare the context info for CP. */
	addr = ipc_protocol->phy_ap_shm;
	p_ci = &ipc_protocol->p_ap_shm->ci;
	p_ci->device_info_addr =
		addr + offsetof(struct ipc_protocol_ap_shm, device_info);
	p_ci->head_array =
		addr + offsetof(struct ipc_protocol_ap_shm, head_array);
	p_ci->tail_array =
		addr + offsetof(struct ipc_protocol_ap_shm, tail_array);
	p_ci->msg_head = addr + offsetof(struct ipc_protocol_ap_shm, msg_head);
	p_ci->msg_tail = addr + offsetof(struct ipc_protocol_ap_shm, msg_tail);
	p_ci->msg_ring_addr =
		addr + offsetof(struct ipc_protocol_ap_shm, msg_ring);
	p_ci->msg_ring_entries = cpu_to_le16(IPC_MEM_MSG_ENTRIES);
	p_ci->msg_irq_vector = IPC_MSG_IRQ_VECTOR;
	p_ci->device_info_irq_vector = IPC_DEVICE_IRQ_VECTOR;

	ipc_mmio_set_contex_info_addr(ipc_imem->mmio, addr);

	ipc_pm_init(ipc_protocol);

	return ipc_protocol;
}

void ipc_protocol_deinit(struct iosm_protocol *proto)
{
	dma_free_coherent(&proto->pcie->pci->dev, sizeof(*proto->p_ap_shm),
			  proto->p_ap_shm, proto->phy_ap_shm);

	ipc_pm_deinit(proto);
	kfree(proto);
}
