/*
 * Machine check exception handling.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 *
 * Copyright 2013 IBM Corporation
 * Author: Mahesh Salgaonkar <mahesh@linux.vnet.ibm.com>
 */

#undef DEBUG
#define pr_fmt(fmt) "mce: " fmt

#include <linux/types.h>
#include <linux/ptrace.h>
#include <linux/percpu.h>
#include <linux/export.h>
#include <linux/irq_work.h>
#include <asm/mce.h>

static DEFINE_PER_CPU(int, mce_nest_count);
static DEFINE_PER_CPU(struct machine_check_event[MAX_MC_EVT], mce_event);

/* Queue for delayed MCE events. */
static DEFINE_PER_CPU(int, mce_queue_count);
static DEFINE_PER_CPU(struct machine_check_event[MAX_MC_EVT], mce_event_queue);

static void machine_check_process_queued_event(struct irq_work *work);
struct irq_work mce_event_process_work = {
        .func = machine_check_process_queued_event,
};

static void mce_set_error_info(struct machine_check_event *mce,
			       struct mce_error_info *mce_err)
{
	mce->error_type = mce_err->error_type;
	switch (mce_err->error_type) {
	case MCE_ERROR_TYPE_UE:
		mce->u.ue_error.ue_error_type = mce_err->u.ue_error_type;
		break;
	case MCE_ERROR_TYPE_SLB:
		mce->u.slb_error.slb_error_type = mce_err->u.slb_error_type;
		break;
	case MCE_ERROR_TYPE_ERAT:
		mce->u.erat_error.erat_error_type = mce_err->u.erat_error_type;
		break;
	case MCE_ERROR_TYPE_TLB:
		mce->u.tlb_error.tlb_error_type = mce_err->u.tlb_error_type;
		break;
	case MCE_ERROR_TYPE_UNKNOWN:
	default:
		break;
	}
}

/*
 * Decode and save high level MCE information into per cpu buffer which
 * is an array of machine_check_event structure.
 */
void save_mce_event(struct pt_regs *regs, long handled,
		    struct mce_error_info *mce_err,
		    uint64_t addr)
{
	uint64_t srr1;
	int index = __get_cpu_var(mce_nest_count)++;
	struct machine_check_event *mce = &__get_cpu_var(mce_event[index]);

	/*
	 * Return if we don't have enough space to log mce event.
	 * mce_nest_count may go beyond MAX_MC_EVT but that's ok,
	 * the check below will stop buffer overrun.
	 */
	if (index >= MAX_MC_EVT)
		return;

	/* Populate generic machine check info */
	mce->version = MCE_V1;
	mce->srr0 = regs->nip;
	mce->srr1 = regs->msr;
	mce->gpr3 = regs->gpr[3];
	mce->in_use = 1;

	mce->initiator = MCE_INITIATOR_CPU;
	if (handled)
		mce->disposition = MCE_DISPOSITION_RECOVERED;
	else
		mce->disposition = MCE_DISPOSITION_NOT_RECOVERED;
	mce->severity = MCE_SEV_ERROR_SYNC;

	srr1 = regs->msr;

	/*
	 * Populate the mce error_type and type-specific error_type.
	 */
	mce_set_error_info(mce, mce_err);

	if (!addr)
		return;

	if (mce->error_type == MCE_ERROR_TYPE_TLB) {
		mce->u.tlb_error.effective_address_provided = true;
		mce->u.tlb_error.effective_address = addr;
	} else if (mce->error_type == MCE_ERROR_TYPE_SLB) {
		mce->u.slb_error.effective_address_provided = true;
		mce->u.slb_error.effective_address = addr;
	} else if (mce->error_type == MCE_ERROR_TYPE_ERAT) {
		mce->u.erat_error.effective_address_provided = true;
		mce->u.erat_error.effective_address = addr;
	} else if (mce->error_type == MCE_ERROR_TYPE_UE) {
		mce->u.ue_error.effective_address_provided = true;
		mce->u.ue_error.effective_address = addr;
	}
	return;
}

/*
 * get_mce_event:
 *	mce	Pointer to machine_check_event structure to be filled.
 *	release Flag to indicate whether to free the event slot or not.
 *		0 <= do not release the mce event. Caller will invoke
 *		     release_mce_event() once event has been consumed.
 *		1 <= release the slot.
 *
 *	return	1 = success
 *		0 = failure
 *
 * get_mce_event() will be called by platform specific machine check
 * handle routine and in KVM.
 * When we call get_mce_event(), we are still in interrupt context and
 * preemption will not be scheduled until ret_from_expect() routine
 * is called.
 */
int get_mce_event(struct machine_check_event *mce, bool release)
{
	int index = __get_cpu_var(mce_nest_count) - 1;
	struct machine_check_event *mc_evt;
	int ret = 0;

	/* Sanity check */
	if (index < 0)
		return ret;

	/* Check if we have MCE info to process. */
	if (index < MAX_MC_EVT) {
		mc_evt = &__get_cpu_var(mce_event[index]);
		/* Copy the event structure and release the original */
		if (mce)
			*mce = *mc_evt;
		if (release)
			mc_evt->in_use = 0;
		ret = 1;
	}
	/* Decrement the count to free the slot. */
	if (release)
		__get_cpu_var(mce_nest_count)--;

	return ret;
}

void release_mce_event(void)
{
	get_mce_event(NULL, true);
}

/*
 * Queue up the MCE event which then can be handled later.
 */
void machine_check_queue_event(void)
{
	int index;
	struct machine_check_event evt;

	if (!get_mce_event(&evt, MCE_EVENT_RELEASE))
		return;

	index = __get_cpu_var(mce_queue_count)++;
	/* If queue is full, just return for now. */
	if (index >= MAX_MC_EVT) {
		__get_cpu_var(mce_queue_count)--;
		return;
	}
	__get_cpu_var(mce_event_queue[index]) = evt;

	/* Queue irq work to process this event later. */
	irq_work_queue(&mce_event_process_work);
}

/*
 * process pending MCE event from the mce event queue. This function will be
 * called during syscall exit.
 */
static void machine_check_process_queued_event(struct irq_work *work)
{
	int index;

	/*
	 * For now just print it to console.
	 * TODO: log this error event to FSP or nvram.
	 */
	while (__get_cpu_var(mce_queue_count) > 0) {
		index = __get_cpu_var(mce_queue_count) - 1;
		machine_check_print_event_info(
				&__get_cpu_var(mce_event_queue[index]));
		__get_cpu_var(mce_queue_count)--;
	}
}

void machine_check_print_event_info(struct machine_check_event *evt)
{
	const char *level, *sevstr, *subtype;
	static const char *mc_ue_types[] = {
		"Indeterminate",
		"Instruction fetch",
		"Page table walk ifetch",
		"Load/Store",
		"Page table walk Load/Store",
	};
	static const char *mc_slb_types[] = {
		"Indeterminate",
		"Parity",
		"Multihit",
	};
	static const char *mc_erat_types[] = {
		"Indeterminate",
		"Parity",
		"Multihit",
	};
	static const char *mc_tlb_types[] = {
		"Indeterminate",
		"Parity",
		"Multihit",
	};

	/* Print things out */
	if (evt->version != MCE_V1) {
		pr_err("Machine Check Exception, Unknown event version %d !\n",
		       evt->version);
		return;
	}
	switch (evt->severity) {
	case MCE_SEV_NO_ERROR:
		level = KERN_INFO;
		sevstr = "Harmless";
		break;
	case MCE_SEV_WARNING:
		level = KERN_WARNING;
		sevstr = "";
		break;
	case MCE_SEV_ERROR_SYNC:
		level = KERN_ERR;
		sevstr = "Severe";
		break;
	case MCE_SEV_FATAL:
	default:
		level = KERN_ERR;
		sevstr = "Fatal";
		break;
	}

	printk("%s%s Machine check interrupt [%s]\n", level, sevstr,
	       evt->disposition == MCE_DISPOSITION_RECOVERED ?
	       "Recovered" : "[Not recovered");
	printk("%s  Initiator: %s\n", level,
	       evt->initiator == MCE_INITIATOR_CPU ? "CPU" : "Unknown");
	switch (evt->error_type) {
	case MCE_ERROR_TYPE_UE:
		subtype = evt->u.ue_error.ue_error_type <
			ARRAY_SIZE(mc_ue_types) ?
			mc_ue_types[evt->u.ue_error.ue_error_type]
			: "Unknown";
		printk("%s  Error type: UE [%s]\n", level, subtype);
		if (evt->u.ue_error.effective_address_provided)
			printk("%s    Effective address: %016llx\n",
			       level, evt->u.ue_error.effective_address);
		if (evt->u.ue_error.physical_address_provided)
			printk("%s      Physial address: %016llx\n",
			       level, evt->u.ue_error.physical_address);
		break;
	case MCE_ERROR_TYPE_SLB:
		subtype = evt->u.slb_error.slb_error_type <
			ARRAY_SIZE(mc_slb_types) ?
			mc_slb_types[evt->u.slb_error.slb_error_type]
			: "Unknown";
		printk("%s  Error type: SLB [%s]\n", level, subtype);
		if (evt->u.slb_error.effective_address_provided)
			printk("%s    Effective address: %016llx\n",
			       level, evt->u.slb_error.effective_address);
		break;
	case MCE_ERROR_TYPE_ERAT:
		subtype = evt->u.erat_error.erat_error_type <
			ARRAY_SIZE(mc_erat_types) ?
			mc_erat_types[evt->u.erat_error.erat_error_type]
			: "Unknown";
		printk("%s  Error type: ERAT [%s]\n", level, subtype);
		if (evt->u.erat_error.effective_address_provided)
			printk("%s    Effective address: %016llx\n",
			       level, evt->u.erat_error.effective_address);
		break;
	case MCE_ERROR_TYPE_TLB:
		subtype = evt->u.tlb_error.tlb_error_type <
			ARRAY_SIZE(mc_tlb_types) ?
			mc_tlb_types[evt->u.tlb_error.tlb_error_type]
			: "Unknown";
		printk("%s  Error type: TLB [%s]\n", level, subtype);
		if (evt->u.tlb_error.effective_address_provided)
			printk("%s    Effective address: %016llx\n",
			       level, evt->u.tlb_error.effective_address);
		break;
	default:
	case MCE_ERROR_TYPE_UNKNOWN:
		printk("%s  Error type: Unknown\n", level);
		break;
	}
}

uint64_t get_mce_fault_addr(struct machine_check_event *evt)
{
	switch (evt->error_type) {
	case MCE_ERROR_TYPE_UE:
		if (evt->u.ue_error.effective_address_provided)
			return evt->u.ue_error.effective_address;
		break;
	case MCE_ERROR_TYPE_SLB:
		if (evt->u.slb_error.effective_address_provided)
			return evt->u.slb_error.effective_address;
		break;
	case MCE_ERROR_TYPE_ERAT:
		if (evt->u.erat_error.effective_address_provided)
			return evt->u.erat_error.effective_address;
		break;
	case MCE_ERROR_TYPE_TLB:
		if (evt->u.tlb_error.effective_address_provided)
			return evt->u.tlb_error.effective_address;
		break;
	default:
	case MCE_ERROR_TYPE_UNKNOWN:
		break;
	}
	return 0;
}
EXPORT_SYMBOL(get_mce_fault_addr);
