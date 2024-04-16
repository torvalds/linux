// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2020-21 Intel Corporation.
 */

#include "iosm_ipc_protocol.h"

/* Timeout value in MS for the PM to wait for device to reach active state */
#define IPC_PM_ACTIVE_TIMEOUT_MS (500)

/* Note that here "active" has the value 1, as compared to the enums
 * ipc_mem_host_pm_state or ipc_mem_dev_pm_state, where "active" is 0
 */
#define IPC_PM_SLEEP (0)
#define CONSUME_STATE (0)
#define IPC_PM_ACTIVE (1)

void ipc_pm_signal_hpda_doorbell(struct iosm_pm *ipc_pm, u32 identifier,
				 bool host_slp_check)
{
	if (host_slp_check && ipc_pm->host_pm_state != IPC_MEM_HOST_PM_ACTIVE &&
	    ipc_pm->host_pm_state != IPC_MEM_HOST_PM_ACTIVE_WAIT) {
		ipc_pm->pending_hpda_update = true;
		dev_dbg(ipc_pm->dev,
			"Pend HPDA update set. Host PM_State: %d identifier:%d",
			ipc_pm->host_pm_state, identifier);
		return;
	}

	if (!ipc_pm_trigger(ipc_pm, IPC_PM_UNIT_IRQ, true)) {
		ipc_pm->pending_hpda_update = true;
		dev_dbg(ipc_pm->dev, "Pending HPDA update set. identifier:%d",
			identifier);
		return;
	}
	ipc_pm->pending_hpda_update = false;

	/* Trigger the irq towards CP */
	ipc_cp_irq_hpda_update(ipc_pm->pcie, identifier);

	ipc_pm_trigger(ipc_pm, IPC_PM_UNIT_IRQ, false);
}

/* Wake up the device if it is in low power mode. */
static bool ipc_pm_link_activate(struct iosm_pm *ipc_pm)
{
	if (ipc_pm->cp_state == IPC_MEM_DEV_PM_ACTIVE)
		return true;

	if (ipc_pm->cp_state == IPC_MEM_DEV_PM_SLEEP) {
		if (ipc_pm->ap_state == IPC_MEM_DEV_PM_SLEEP) {
			/* Wake up the device. */
			ipc_cp_irq_sleep_control(ipc_pm->pcie,
						 IPC_MEM_DEV_PM_WAKEUP);
			ipc_pm->ap_state = IPC_MEM_DEV_PM_ACTIVE_WAIT;

			goto not_active;
		}

		if (ipc_pm->ap_state == IPC_MEM_DEV_PM_ACTIVE_WAIT)
			goto not_active;

		return true;
	}

not_active:
	/* link is not ready */
	return false;
}

bool ipc_pm_wait_for_device_active(struct iosm_pm *ipc_pm)
{
	bool ret_val = false;

	if (ipc_pm->ap_state != IPC_MEM_DEV_PM_ACTIVE) {
		/* Complete all memory stores before setting bit */
		smp_mb__before_atomic();

		/* Wait for IPC_PM_ACTIVE_TIMEOUT_MS for Device sleep state
		 * machine to enter ACTIVE state.
		 */
		set_bit(0, &ipc_pm->host_sleep_pend);

		/* Complete all memory stores after setting bit */
		smp_mb__after_atomic();

		if (!wait_for_completion_interruptible_timeout
		   (&ipc_pm->host_sleep_complete,
		    msecs_to_jiffies(IPC_PM_ACTIVE_TIMEOUT_MS))) {
			dev_err(ipc_pm->dev,
				"PM timeout. Expected State:%d. Actual: %d",
				IPC_MEM_DEV_PM_ACTIVE, ipc_pm->ap_state);
			goto  active_timeout;
		}
	}

	ret_val = true;
active_timeout:
	/* Complete all memory stores before clearing bit */
	smp_mb__before_atomic();

	/* Reset the atomic variable in any case as device sleep
	 * state machine change is no longer of interest.
	 */
	clear_bit(0, &ipc_pm->host_sleep_pend);

	/* Complete all memory stores after clearing bit */
	smp_mb__after_atomic();

	return ret_val;
}

static void ipc_pm_on_link_sleep(struct iosm_pm *ipc_pm)
{
	/* pending sleep ack and all conditions are cleared
	 * -> signal SLEEP__ACK to CP
	 */
	ipc_pm->cp_state = IPC_MEM_DEV_PM_SLEEP;
	ipc_pm->ap_state = IPC_MEM_DEV_PM_SLEEP;

	ipc_cp_irq_sleep_control(ipc_pm->pcie, IPC_MEM_DEV_PM_SLEEP);
}

static void ipc_pm_on_link_wake(struct iosm_pm *ipc_pm, bool ack)
{
	ipc_pm->ap_state = IPC_MEM_DEV_PM_ACTIVE;

	if (ack) {
		ipc_pm->cp_state = IPC_MEM_DEV_PM_ACTIVE;

		ipc_cp_irq_sleep_control(ipc_pm->pcie, IPC_MEM_DEV_PM_ACTIVE);

		/* check the consume state !!! */
		if (test_bit(CONSUME_STATE, &ipc_pm->host_sleep_pend))
			complete(&ipc_pm->host_sleep_complete);
	}

	/* Check for pending HPDA update.
	 * Pending HP update could be because of sending message was
	 * put on hold due to Device sleep state or due to TD update
	 * which could be because of Device Sleep and Host Sleep
	 * states.
	 */
	if (ipc_pm->pending_hpda_update &&
	    ipc_pm->host_pm_state == IPC_MEM_HOST_PM_ACTIVE)
		ipc_pm_signal_hpda_doorbell(ipc_pm, IPC_HP_PM_TRIGGER, true);
}

bool ipc_pm_trigger(struct iosm_pm *ipc_pm, enum ipc_pm_unit unit, bool active)
{
	union ipc_pm_cond old_cond;
	union ipc_pm_cond new_cond;
	bool link_active;

	/* Save the current D3 state. */
	new_cond = ipc_pm->pm_cond;
	old_cond = ipc_pm->pm_cond;

	/* Calculate the power state only in the runtime phase. */
	switch (unit) {
	case IPC_PM_UNIT_IRQ: /* CP irq */
		new_cond.irq = active;
		break;

	case IPC_PM_UNIT_LINK: /* Device link state. */
		new_cond.link = active;
		break;

	case IPC_PM_UNIT_HS: /* Host sleep trigger requires Link. */
		new_cond.hs = active;
		break;

	default:
		break;
	}

	/* Something changed ? */
	if (old_cond.raw == new_cond.raw) {
		/* Stay in the current PM state. */
		link_active = old_cond.link == IPC_PM_ACTIVE;
		goto ret;
	}

	ipc_pm->pm_cond = new_cond;

	if (new_cond.link)
		ipc_pm_on_link_wake(ipc_pm, unit == IPC_PM_UNIT_LINK);
	else if (unit == IPC_PM_UNIT_LINK)
		ipc_pm_on_link_sleep(ipc_pm);

	if (old_cond.link == IPC_PM_SLEEP && new_cond.raw) {
		link_active = ipc_pm_link_activate(ipc_pm);
		goto ret;
	}

	link_active = old_cond.link == IPC_PM_ACTIVE;

ret:
	return link_active;
}

bool ipc_pm_prepare_host_sleep(struct iosm_pm *ipc_pm)
{
	/* suspend not allowed if host_pm_state is not IPC_MEM_HOST_PM_ACTIVE */
	if (ipc_pm->host_pm_state != IPC_MEM_HOST_PM_ACTIVE) {
		dev_err(ipc_pm->dev, "host_pm_state=%d\tExpected to be: %d",
			ipc_pm->host_pm_state, IPC_MEM_HOST_PM_ACTIVE);
		return false;
	}

	ipc_pm->host_pm_state = IPC_MEM_HOST_PM_SLEEP_WAIT_D3;

	return true;
}

bool ipc_pm_prepare_host_active(struct iosm_pm *ipc_pm)
{
	if (ipc_pm->host_pm_state != IPC_MEM_HOST_PM_SLEEP) {
		dev_err(ipc_pm->dev, "host_pm_state=%d\tExpected to be: %d",
			ipc_pm->host_pm_state, IPC_MEM_HOST_PM_SLEEP);
		return false;
	}

	/* Sending Sleep Exit message to CP. Update the state */
	ipc_pm->host_pm_state = IPC_MEM_HOST_PM_ACTIVE_WAIT;

	return true;
}

void ipc_pm_set_s2idle_sleep(struct iosm_pm *ipc_pm, bool sleep)
{
	if (sleep) {
		ipc_pm->ap_state = IPC_MEM_DEV_PM_SLEEP;
		ipc_pm->cp_state = IPC_MEM_DEV_PM_SLEEP;
		ipc_pm->device_sleep_notification = IPC_MEM_DEV_PM_SLEEP;
	} else {
		ipc_pm->ap_state = IPC_MEM_DEV_PM_ACTIVE;
		ipc_pm->cp_state = IPC_MEM_DEV_PM_ACTIVE;
		ipc_pm->device_sleep_notification = IPC_MEM_DEV_PM_ACTIVE;
		ipc_pm->pm_cond.link = IPC_PM_ACTIVE;
	}
}

bool ipc_pm_dev_slp_notification(struct iosm_pm *ipc_pm, u32 cp_pm_req)
{
	if (cp_pm_req == ipc_pm->device_sleep_notification)
		return false;

	ipc_pm->device_sleep_notification = cp_pm_req;

	/* Evaluate the PM request. */
	switch (ipc_pm->cp_state) {
	case IPC_MEM_DEV_PM_ACTIVE:
		switch (cp_pm_req) {
		case IPC_MEM_DEV_PM_ACTIVE:
			break;

		case IPC_MEM_DEV_PM_SLEEP:
			/* Inform the PM that the device link can go down. */
			ipc_pm_trigger(ipc_pm, IPC_PM_UNIT_LINK, false);
			return true;

		default:
			dev_err(ipc_pm->dev,
				"loc-pm=%d active: confused req-pm=%d",
				ipc_pm->cp_state, cp_pm_req);
			break;
		}
		break;

	case IPC_MEM_DEV_PM_SLEEP:
		switch (cp_pm_req) {
		case IPC_MEM_DEV_PM_ACTIVE:
			/* Inform the PM that the device link is active. */
			ipc_pm_trigger(ipc_pm, IPC_PM_UNIT_LINK, true);
			break;

		case IPC_MEM_DEV_PM_SLEEP:
			break;

		default:
			dev_err(ipc_pm->dev,
				"loc-pm=%d sleep: confused req-pm=%d",
				ipc_pm->cp_state, cp_pm_req);
			break;
		}
		break;

	default:
		dev_err(ipc_pm->dev, "confused loc-pm=%d, req-pm=%d",
			ipc_pm->cp_state, cp_pm_req);
		break;
	}

	return false;
}

void ipc_pm_init(struct iosm_protocol *ipc_protocol)
{
	struct iosm_imem *ipc_imem = ipc_protocol->imem;
	struct iosm_pm *ipc_pm = &ipc_protocol->pm;

	ipc_pm->pcie = ipc_imem->pcie;
	ipc_pm->dev = ipc_imem->dev;

	ipc_pm->pm_cond.irq = IPC_PM_SLEEP;
	ipc_pm->pm_cond.hs = IPC_PM_SLEEP;
	ipc_pm->pm_cond.link = IPC_PM_ACTIVE;

	ipc_pm->cp_state = IPC_MEM_DEV_PM_ACTIVE;
	ipc_pm->ap_state = IPC_MEM_DEV_PM_ACTIVE;
	ipc_pm->host_pm_state = IPC_MEM_HOST_PM_ACTIVE;

	/* Create generic wait-for-completion handler for Host Sleep
	 * and device sleep coordination.
	 */
	init_completion(&ipc_pm->host_sleep_complete);

	/* Complete all memory stores before clearing bit */
	smp_mb__before_atomic();

	clear_bit(0, &ipc_pm->host_sleep_pend);

	/* Complete all memory stores after clearing bit */
	smp_mb__after_atomic();
}

void ipc_pm_deinit(struct iosm_protocol *proto)
{
	struct iosm_pm *ipc_pm = &proto->pm;

	complete(&ipc_pm->host_sleep_complete);
}
