// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2012-2021, The Linux Foundation. All rights reserved.
 * Copyright (c) 2022-2024 Qualcomm Innovation Center, Inc. All rights reserved.
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/kprobes.h>
#include <linux/irq.h>
#include <linux/irqdesc.h>
#include <linux/sched.h>
#include <linux/usb/dwc3-msm.h>
#include <linux/usb/composite.h>
#include <linux/device.h>
#include "core.h"
#include "debug-ipc.h"
#include "gadget.h"

struct kprobe_data {
	struct dwc3 *dwc;
	int xi0;
};

static int entry_usb_ep_set_maxpacket_limit(struct kretprobe_instance *ri,
				struct pt_regs *regs)
{
	struct usb_ep *ep = (struct usb_ep *)regs->regs[0];
	struct dwc3_ep *dep;
	struct dwc3 *dwc;
	struct kprobe_data *data = (struct kprobe_data *)ri->data;

	dep =  to_dwc3_ep(ep);
	dwc = dep->dwc;

	if (dwc && (dwc->dev) &&
	   (strcmp(dev_driver_string(dwc->dev), "dwc3") == 0)) {
		data->dwc = dwc;
		data->xi0 = dep->number;
	} else {
		data->dwc = NULL;
	}

	return 0;
}

static int exit_usb_ep_set_maxpacket_limit(struct kretprobe_instance *ri,
				struct pt_regs *regs)
{
	struct kprobe_data *data = (struct kprobe_data *)ri->data;
	struct dwc3 *dwc = data->dwc;
	u8 epnum = data->xi0;
	struct dwc3_ep *dep;
	struct usb_ep *ep;

	if (!dwc)
		return 0;

	dep = dwc->eps[epnum];
	ep = &dep->endpoint;

	if (epnum >= 2) {
		ep->maxpacket_limit = 1024;
		ep->maxpacket = 1024;
	}

	return 0;
}

static int entry_dwc3_gadget_run_stop(struct kretprobe_instance *ri,
				   struct pt_regs *regs)
{
	struct dwc3 *dwc = (struct dwc3 *)regs->regs[0];
	int is_on = (int)regs->regs[1];

	if (is_on) {
		/*
		 * DWC3 gadget IRQ uses a threaded handler which normally runs
		 * at SCHED_FIFO priority.  If it gets busy processing a high
		 * volume of events (usually EP events due to heavy traffic) it
		 * can potentially starve non-RT taks from running and trigger
		 * RT throttling in the scheduler; on some build configs this
		 * will panic.  So lower the thread's priority to run as non-RT
		 * (with a nice value equivalent to a high-priority workqueue).
		 * It has been found to not have noticeable performance impact.
		 */
		struct irq_desc *irq_desc = irq_to_desc(dwc->irq_gadget);
		struct irqaction *action = irq_desc ? irq_desc->action : NULL;

		for ( ; action != NULL; action = action->next) {
			if (action->thread) {
				dev_info(dwc->dev, "Set IRQ thread:%s pid:%d to SCHED_NORMAL prio\n",
					action->thread->comm, action->thread->pid);
				sched_set_normal(action->thread, MIN_NICE);
				break;
			}
		}
	} else {
		dwc3_core_stop_hw_active_transfers(dwc);
		dwc3_msm_notify_event(dwc, DWC3_GSI_EVT_BUF_CLEAR, 0);
		dwc3_msm_notify_event(dwc, DWC3_CONTROLLER_NOTIFY_CLEAR_DB, 0);
	}

	return 0;
}

static int entry_dwc3_send_gadget_ep_cmd(struct kretprobe_instance *ri,
				   struct pt_regs *regs)
{
	struct dwc3_ep *dep = (struct dwc3_ep *)regs->regs[0];
	unsigned int cmd = (unsigned int)regs->regs[1];
	struct dwc3 *dwc = dep->dwc;

	if (cmd == DWC3_DEPCMD_ENDTRANSFER)
		dwc3_msm_notify_event(dwc,
				DWC3_CONTROLLER_NOTIFY_DISABLE_UPDXFER,
				dep->number);

	return 0;
}

static int entry___dwc3_gadget_ep_enable(struct kretprobe_instance *ri,
				   struct pt_regs *regs)
{
	struct dwc3_ep *dep = (struct dwc3_ep *)regs->regs[0];
	unsigned int action = (unsigned int)regs->regs[1];

	/* DWC3_DEPCFG_ACTION_MODIFY is only done during CONNDONE */
	if (action == DWC3_DEPCFG_ACTION_MODIFY && dep->number == 1)
		dwc3_msm_notify_event(dep->dwc, DWC3_CONTROLLER_CONNDONE_EVENT, 0);

	return 0;
}

static int entry_dwc3_gadget_reset_interrupt(struct kretprobe_instance *ri,
				   struct pt_regs *regs)
{
	struct dwc3 *dwc = (struct dwc3 *)regs->regs[0];

	dwc3_msm_notify_event(dwc, DWC3_CONTROLLER_NOTIFY_CLEAR_DB, 0);
	return 0;
}

static int entry_dwc3_gadget_pullup(struct kretprobe_instance *ri,
				   struct pt_regs *regs)
{
	struct kprobe_data *data = (struct kprobe_data *)ri->data;
	struct usb_gadget *g = (struct usb_gadget *)regs->regs[0];

	data->dwc = gadget_to_dwc(g);
	data->xi0 = (int)regs->regs[1];
	dwc3_msm_notify_event(data->dwc, DWC3_CONTROLLER_PULLUP_ENTER,
				data->xi0);

	/* Only write PID to IMEM if pullup is being enabled */
	if (data->xi0)
		dwc3_msm_notify_event(data->dwc, DWC3_IMEM_UPDATE_PID, 0);

	return 0;
}

static int exit_dwc3_gadget_pullup(struct kretprobe_instance *ri,
				   struct pt_regs *regs)
{
	struct kprobe_data *data = (struct kprobe_data *)ri->data;

	dwc3_msm_notify_event(data->dwc, DWC3_CONTROLLER_PULLUP_EXIT,
				data->xi0);

	return 0;
}

static int entry___dwc3_gadget_start(struct kretprobe_instance *ri,
				   struct pt_regs *regs)
{
	struct dwc3 *dwc = (struct dwc3 *)regs->regs[0];

	/*
	 * Setup USB GSI event buffer as controller soft reset has cleared
	 * configured event buffer.
	 */
	dwc3_msm_notify_event(dwc, DWC3_GSI_EVT_BUF_SETUP, 0);

	return 0;
}

static int entry_trace_event_raw_event_dwc3_log_request(struct kretprobe_instance *ri,
				   struct pt_regs *regs)
{
	struct dwc3_request *req = (struct dwc3_request *)regs->regs[1];

	dbg_trace_ep_queue(req);

	return 0;
}

static int entry_trace_event_raw_event_dwc3_log_gadget_ep_cmd(struct kretprobe_instance *ri,
				   struct pt_regs *regs)
{
	struct dwc3_ep *dep = (struct dwc3_ep *)regs->regs[1];
	unsigned int cmd = regs->regs[2];
	struct dwc3_gadget_ep_cmd_params *param = (struct dwc3_gadget_ep_cmd_params *)regs->regs[3];
	int cmd_status = regs->regs[4];

	dbg_trace_gadget_ep_cmd(dep, cmd, param, cmd_status);

	return 0;
}

static int entry_trace_event_raw_event_dwc3_log_trb(struct kretprobe_instance *ri,
				   struct pt_regs *regs)
{
	struct dwc3_ep *dep = (struct dwc3_ep *)regs->regs[1];
	struct dwc3_trb *trb = (struct dwc3_trb *)regs->regs[2];

	dbg_trace_trb_prepare(dep, trb);

	return 0;
}

static int entry_trace_event_raw_event_dwc3_log_event(struct kretprobe_instance *ri,
				   struct pt_regs *regs)
{
	u32 event = regs->regs[1];
	struct dwc3 *dwc = (struct dwc3 *)regs->regs[2];

	dbg_trace_event(event, dwc);

	return 0;
}

static int entry_trace_event_raw_event_dwc3_log_ep(struct kretprobe_instance *ri,
				   struct pt_regs *regs)
{
	struct dwc3_ep *dep = (struct dwc3_ep *)regs->regs[1];

	dbg_trace_ep(dep);

	return 0;
}

#define ENTRY_EXIT(name) {\
	.handler = exit_##name,\
	.entry_handler = entry_##name,\
	.data_size = sizeof(struct kprobe_data),\
	.maxactive = 8,\
	.kp.symbol_name = #name,\
}

#define ENTRY(name) {\
	.entry_handler = entry_##name,\
	.data_size = sizeof(struct kprobe_data),\
	.maxactive = 8,\
	.kp.symbol_name = #name,\
}

static struct kretprobe dwc3_msm_probes[] = {
	ENTRY(dwc3_gadget_run_stop),
	ENTRY(dwc3_send_gadget_ep_cmd),
	ENTRY(dwc3_gadget_reset_interrupt),
	ENTRY(__dwc3_gadget_ep_enable),
	ENTRY_EXIT(dwc3_gadget_pullup),
	ENTRY(__dwc3_gadget_start),
	ENTRY_EXIT(usb_ep_set_maxpacket_limit),
	ENTRY(trace_event_raw_event_dwc3_log_request),
	ENTRY(trace_event_raw_event_dwc3_log_gadget_ep_cmd),
	ENTRY(trace_event_raw_event_dwc3_log_trb),
	ENTRY(trace_event_raw_event_dwc3_log_event),
	ENTRY(trace_event_raw_event_dwc3_log_ep),
};


int dwc3_msm_kretprobe_init(void)
{
	int ret;
	int i;

	for (i = 0; i < ARRAY_SIZE(dwc3_msm_probes) ; i++) {
		ret = register_kretprobe(&dwc3_msm_probes[i]);
		if (ret < 0)
			pr_err("register_kretprobe failed for %s, returned %d\n",
					dwc3_msm_probes[i].kp.symbol_name, ret);
	}

	return 0;
}

void dwc3_msm_kretprobe_exit(void)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(dwc3_msm_probes); i++)
		unregister_kretprobe(&dwc3_msm_probes[i]);
}

