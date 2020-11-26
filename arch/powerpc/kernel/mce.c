// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Machine check exception handling.
 *
 * Copyright 2013 IBM Corporation
 * Author: Mahesh Salgaonkar <mahesh@linux.vnet.ibm.com>
 */

#undef DEBUG
#define pr_fmt(fmt) "mce: " fmt

#include <linux/hardirq.h>
#include <linux/types.h>
#include <linux/ptrace.h>
#include <linux/percpu.h>
#include <linux/export.h>
#include <linux/irq_work.h>
#include <linux/extable.h>
#include <linux/ftrace.h>

#include <asm/machdep.h>
#include <asm/mce.h>
#include <asm/nmi.h>

static DEFINE_PER_CPU(int, mce_nest_count);
static DEFINE_PER_CPU(struct machine_check_event[MAX_MC_EVT], mce_event);

/* Queue for delayed MCE events. */
static DEFINE_PER_CPU(int, mce_queue_count);
static DEFINE_PER_CPU(struct machine_check_event[MAX_MC_EVT], mce_event_queue);

/* Queue for delayed MCE UE events. */
static DEFINE_PER_CPU(int, mce_ue_count);
static DEFINE_PER_CPU(struct machine_check_event[MAX_MC_EVT],
					mce_ue_event_queue);

static void machine_check_process_queued_event(struct irq_work *work);
static void machine_check_ue_irq_work(struct irq_work *work);
static void machine_check_ue_event(struct machine_check_event *evt);
static void machine_process_ue_event(struct work_struct *work);

static struct irq_work mce_event_process_work = {
        .func = machine_check_process_queued_event,
};

static struct irq_work mce_ue_event_irq_work = {
	.func = machine_check_ue_irq_work,
};

DECLARE_WORK(mce_ue_event_work, machine_process_ue_event);

static BLOCKING_NOTIFIER_HEAD(mce_notifier_list);

int mce_register_notifier(struct notifier_block *nb)
{
	return blocking_notifier_chain_register(&mce_notifier_list, nb);
}
EXPORT_SYMBOL_GPL(mce_register_notifier);

int mce_unregister_notifier(struct notifier_block *nb)
{
	return blocking_notifier_chain_unregister(&mce_notifier_list, nb);
}
EXPORT_SYMBOL_GPL(mce_unregister_notifier);

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
	case MCE_ERROR_TYPE_USER:
		mce->u.user_error.user_error_type = mce_err->u.user_error_type;
		break;
	case MCE_ERROR_TYPE_RA:
		mce->u.ra_error.ra_error_type = mce_err->u.ra_error_type;
		break;
	case MCE_ERROR_TYPE_LINK:
		mce->u.link_error.link_error_type = mce_err->u.link_error_type;
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
		    uint64_t nip, uint64_t addr, uint64_t phys_addr)
{
	int index = __this_cpu_inc_return(mce_nest_count) - 1;
	struct machine_check_event *mce = this_cpu_ptr(&mce_event[index]);

	/*
	 * Return if we don't have enough space to log mce event.
	 * mce_nest_count may go beyond MAX_MC_EVT but that's ok,
	 * the check below will stop buffer overrun.
	 */
	if (index >= MAX_MC_EVT)
		return;

	/* Populate generic machine check info */
	mce->version = MCE_V1;
	mce->srr0 = nip;
	mce->srr1 = regs->msr;
	mce->gpr3 = regs->gpr[3];
	mce->in_use = 1;
	mce->cpu = get_paca()->paca_index;

	/* Mark it recovered if we have handled it and MSR(RI=1). */
	if (handled && (regs->msr & MSR_RI))
		mce->disposition = MCE_DISPOSITION_RECOVERED;
	else
		mce->disposition = MCE_DISPOSITION_NOT_RECOVERED;

	mce->initiator = mce_err->initiator;
	mce->severity = mce_err->severity;
	mce->sync_error = mce_err->sync_error;
	mce->error_class = mce_err->error_class;

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
	} else if (mce->error_type == MCE_ERROR_TYPE_USER) {
		mce->u.user_error.effective_address_provided = true;
		mce->u.user_error.effective_address = addr;
	} else if (mce->error_type == MCE_ERROR_TYPE_RA) {
		mce->u.ra_error.effective_address_provided = true;
		mce->u.ra_error.effective_address = addr;
	} else if (mce->error_type == MCE_ERROR_TYPE_LINK) {
		mce->u.link_error.effective_address_provided = true;
		mce->u.link_error.effective_address = addr;
	} else if (mce->error_type == MCE_ERROR_TYPE_UE) {
		mce->u.ue_error.effective_address_provided = true;
		mce->u.ue_error.effective_address = addr;
		if (phys_addr != ULONG_MAX) {
			mce->u.ue_error.physical_address_provided = true;
			mce->u.ue_error.physical_address = phys_addr;
			mce->u.ue_error.ignore_event = mce_err->ignore_event;
			machine_check_ue_event(mce);
		}
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
	int index = __this_cpu_read(mce_nest_count) - 1;
	struct machine_check_event *mc_evt;
	int ret = 0;

	/* Sanity check */
	if (index < 0)
		return ret;

	/* Check if we have MCE info to process. */
	if (index < MAX_MC_EVT) {
		mc_evt = this_cpu_ptr(&mce_event[index]);
		/* Copy the event structure and release the original */
		if (mce)
			*mce = *mc_evt;
		if (release)
			mc_evt->in_use = 0;
		ret = 1;
	}
	/* Decrement the count to free the slot. */
	if (release)
		__this_cpu_dec(mce_nest_count);

	return ret;
}

void release_mce_event(void)
{
	get_mce_event(NULL, true);
}

static void machine_check_ue_irq_work(struct irq_work *work)
{
	schedule_work(&mce_ue_event_work);
}

/*
 * Queue up the MCE event which then can be handled later.
 */
static void machine_check_ue_event(struct machine_check_event *evt)
{
	int index;

	index = __this_cpu_inc_return(mce_ue_count) - 1;
	/* If queue is full, just return for now. */
	if (index >= MAX_MC_EVT) {
		__this_cpu_dec(mce_ue_count);
		return;
	}
	memcpy(this_cpu_ptr(&mce_ue_event_queue[index]), evt, sizeof(*evt));

	/* Queue work to process this event later. */
	irq_work_queue(&mce_ue_event_irq_work);
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

	index = __this_cpu_inc_return(mce_queue_count) - 1;
	/* If queue is full, just return for now. */
	if (index >= MAX_MC_EVT) {
		__this_cpu_dec(mce_queue_count);
		return;
	}
	memcpy(this_cpu_ptr(&mce_event_queue[index]), &evt, sizeof(evt));

	/* Queue irq work to process this event later. */
	irq_work_queue(&mce_event_process_work);
}

void mce_common_process_ue(struct pt_regs *regs,
			   struct mce_error_info *mce_err)
{
	const struct exception_table_entry *entry;

	entry = search_kernel_exception_table(regs->nip);
	if (entry) {
		mce_err->ignore_event = true;
		regs->nip = extable_fixup(entry);
	}
}

/*
 * process pending MCE event from the mce event queue. This function will be
 * called during syscall exit.
 */
static void machine_process_ue_event(struct work_struct *work)
{
	int index;
	struct machine_check_event *evt;

	while (__this_cpu_read(mce_ue_count) > 0) {
		index = __this_cpu_read(mce_ue_count) - 1;
		evt = this_cpu_ptr(&mce_ue_event_queue[index]);
		blocking_notifier_call_chain(&mce_notifier_list, 0, evt);
#ifdef CONFIG_MEMORY_FAILURE
		/*
		 * This should probably queued elsewhere, but
		 * oh! well
		 *
		 * Don't report this machine check because the caller has a
		 * asked us to ignore the event, it has a fixup handler which
		 * will do the appropriate error handling and reporting.
		 */
		if (evt->error_type == MCE_ERROR_TYPE_UE) {
			if (evt->u.ue_error.ignore_event) {
				__this_cpu_dec(mce_ue_count);
				continue;
			}

			if (evt->u.ue_error.physical_address_provided) {
				unsigned long pfn;

				pfn = evt->u.ue_error.physical_address >>
					PAGE_SHIFT;
				memory_failure(pfn, 0);
			} else
				pr_warn("Failed to identify bad address from "
					"where the uncorrectable error (UE) "
					"was generated\n");
		}
#endif
		__this_cpu_dec(mce_ue_count);
	}
}
/*
 * process pending MCE event from the mce event queue. This function will be
 * called during syscall exit.
 */
static void machine_check_process_queued_event(struct irq_work *work)
{
	int index;
	struct machine_check_event *evt;

	add_taint(TAINT_MACHINE_CHECK, LOCKDEP_NOW_UNRELIABLE);

	/*
	 * For now just print it to console.
	 * TODO: log this error event to FSP or nvram.
	 */
	while (__this_cpu_read(mce_queue_count) > 0) {
		index = __this_cpu_read(mce_queue_count) - 1;
		evt = this_cpu_ptr(&mce_event_queue[index]);

		if (evt->error_type == MCE_ERROR_TYPE_UE &&
		    evt->u.ue_error.ignore_event) {
			__this_cpu_dec(mce_queue_count);
			continue;
		}
		machine_check_print_event_info(evt, false, false);
		__this_cpu_dec(mce_queue_count);
	}
}

void machine_check_print_event_info(struct machine_check_event *evt,
				    bool user_mode, bool in_guest)
{
	const char *level, *sevstr, *subtype, *err_type, *initiator;
	uint64_t ea = 0, pa = 0;
	int n = 0;
	char dar_str[50];
	char pa_str[50];
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
	static const char *mc_user_types[] = {
		"Indeterminate",
		"tlbie(l) invalid",
		"scv invalid",
	};
	static const char *mc_ra_types[] = {
		"Indeterminate",
		"Instruction fetch (bad)",
		"Instruction fetch (foreign)",
		"Page table walk ifetch (bad)",
		"Page table walk ifetch (foreign)",
		"Load (bad)",
		"Store (bad)",
		"Page table walk Load/Store (bad)",
		"Page table walk Load/Store (foreign)",
		"Load/Store (foreign)",
	};
	static const char *mc_link_types[] = {
		"Indeterminate",
		"Instruction fetch (timeout)",
		"Page table walk ifetch (timeout)",
		"Load (timeout)",
		"Store (timeout)",
		"Page table walk Load/Store (timeout)",
	};
	static const char *mc_error_class[] = {
		"Unknown",
		"Hardware error",
		"Probable Hardware error (some chance of software cause)",
		"Software error",
		"Probable Software error (some chance of hardware cause)",
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
		sevstr = "Warning";
		break;
	case MCE_SEV_SEVERE:
		level = KERN_ERR;
		sevstr = "Severe";
		break;
	case MCE_SEV_FATAL:
	default:
		level = KERN_ERR;
		sevstr = "Fatal";
		break;
	}

	switch(evt->initiator) {
	case MCE_INITIATOR_CPU:
		initiator = "CPU";
		break;
	case MCE_INITIATOR_PCI:
		initiator = "PCI";
		break;
	case MCE_INITIATOR_ISA:
		initiator = "ISA";
		break;
	case MCE_INITIATOR_MEMORY:
		initiator = "Memory";
		break;
	case MCE_INITIATOR_POWERMGM:
		initiator = "Power Management";
		break;
	case MCE_INITIATOR_UNKNOWN:
	default:
		initiator = "Unknown";
		break;
	}

	switch (evt->error_type) {
	case MCE_ERROR_TYPE_UE:
		err_type = "UE";
		subtype = evt->u.ue_error.ue_error_type <
			ARRAY_SIZE(mc_ue_types) ?
			mc_ue_types[evt->u.ue_error.ue_error_type]
			: "Unknown";
		if (evt->u.ue_error.effective_address_provided)
			ea = evt->u.ue_error.effective_address;
		if (evt->u.ue_error.physical_address_provided)
			pa = evt->u.ue_error.physical_address;
		break;
	case MCE_ERROR_TYPE_SLB:
		err_type = "SLB";
		subtype = evt->u.slb_error.slb_error_type <
			ARRAY_SIZE(mc_slb_types) ?
			mc_slb_types[evt->u.slb_error.slb_error_type]
			: "Unknown";
		if (evt->u.slb_error.effective_address_provided)
			ea = evt->u.slb_error.effective_address;
		break;
	case MCE_ERROR_TYPE_ERAT:
		err_type = "ERAT";
		subtype = evt->u.erat_error.erat_error_type <
			ARRAY_SIZE(mc_erat_types) ?
			mc_erat_types[evt->u.erat_error.erat_error_type]
			: "Unknown";
		if (evt->u.erat_error.effective_address_provided)
			ea = evt->u.erat_error.effective_address;
		break;
	case MCE_ERROR_TYPE_TLB:
		err_type = "TLB";
		subtype = evt->u.tlb_error.tlb_error_type <
			ARRAY_SIZE(mc_tlb_types) ?
			mc_tlb_types[evt->u.tlb_error.tlb_error_type]
			: "Unknown";
		if (evt->u.tlb_error.effective_address_provided)
			ea = evt->u.tlb_error.effective_address;
		break;
	case MCE_ERROR_TYPE_USER:
		err_type = "User";
		subtype = evt->u.user_error.user_error_type <
			ARRAY_SIZE(mc_user_types) ?
			mc_user_types[evt->u.user_error.user_error_type]
			: "Unknown";
		if (evt->u.user_error.effective_address_provided)
			ea = evt->u.user_error.effective_address;
		break;
	case MCE_ERROR_TYPE_RA:
		err_type = "Real address";
		subtype = evt->u.ra_error.ra_error_type <
			ARRAY_SIZE(mc_ra_types) ?
			mc_ra_types[evt->u.ra_error.ra_error_type]
			: "Unknown";
		if (evt->u.ra_error.effective_address_provided)
			ea = evt->u.ra_error.effective_address;
		break;
	case MCE_ERROR_TYPE_LINK:
		err_type = "Link";
		subtype = evt->u.link_error.link_error_type <
			ARRAY_SIZE(mc_link_types) ?
			mc_link_types[evt->u.link_error.link_error_type]
			: "Unknown";
		if (evt->u.link_error.effective_address_provided)
			ea = evt->u.link_error.effective_address;
		break;
	case MCE_ERROR_TYPE_DCACHE:
		err_type = "D-Cache";
		subtype = "Unknown";
		break;
	case MCE_ERROR_TYPE_ICACHE:
		err_type = "I-Cache";
		subtype = "Unknown";
		break;
	default:
	case MCE_ERROR_TYPE_UNKNOWN:
		err_type = "Unknown";
		subtype = "";
		break;
	}

	dar_str[0] = pa_str[0] = '\0';
	if (ea && evt->srr0 != ea) {
		/* Load/Store address */
		n = sprintf(dar_str, "DAR: %016llx ", ea);
		if (pa)
			sprintf(dar_str + n, "paddr: %016llx ", pa);
	} else if (pa) {
		sprintf(pa_str, " paddr: %016llx", pa);
	}

	printk("%sMCE: CPU%d: machine check (%s) %s %s %s %s[%s]\n",
		level, evt->cpu, sevstr, in_guest ? "Guest" : "Host",
		err_type, subtype, dar_str,
		evt->disposition == MCE_DISPOSITION_RECOVERED ?
		"Recovered" : "Not recovered");

	if (in_guest || user_mode) {
		printk("%sMCE: CPU%d: PID: %d Comm: %s %sNIP: [%016llx]%s\n",
			level, evt->cpu, current->pid, current->comm,
			in_guest ? "Guest " : "", evt->srr0, pa_str);
	} else {
		printk("%sMCE: CPU%d: NIP: [%016llx] %pS%s\n",
			level, evt->cpu, evt->srr0, (void *)evt->srr0, pa_str);
	}

	printk("%sMCE: CPU%d: Initiator %s\n", level, evt->cpu, initiator);

	subtype = evt->error_class < ARRAY_SIZE(mc_error_class) ?
		mc_error_class[evt->error_class] : "Unknown";
	printk("%sMCE: CPU%d: %s\n", level, evt->cpu, subtype);

#ifdef CONFIG_PPC_BOOK3S_64
	/* Display faulty slb contents for SLB errors. */
	if (evt->error_type == MCE_ERROR_TYPE_SLB)
		slb_dump_contents(local_paca->mce_faulty_slbs);
#endif
}
EXPORT_SYMBOL_GPL(machine_check_print_event_info);

/*
 * This function is called in real mode. Strictly no printk's please.
 *
 * regs->nip and regs->msr contains srr0 and ssr1.
 */
long notrace machine_check_early(struct pt_regs *regs)
{
	long handled = 0;
	u8 ftrace_enabled = this_cpu_get_ftrace_enabled();

	this_cpu_set_ftrace_enabled(0);
	/* Do not use nmi_enter/exit for pseries hpte guest */
	if (radix_enabled() || !firmware_has_feature(FW_FEATURE_LPAR))
		nmi_enter();

	hv_nmi_check_nonrecoverable(regs);

	/*
	 * See if platform is capable of handling machine check.
	 */
	if (ppc_md.machine_check_early)
		handled = ppc_md.machine_check_early(regs);

	if (radix_enabled() || !firmware_has_feature(FW_FEATURE_LPAR))
		nmi_exit();

	this_cpu_set_ftrace_enabled(ftrace_enabled);

	return handled;
}

/* Possible meanings for HMER_DEBUG_TRIG bit being set on POWER9 */
static enum {
	DTRIG_UNKNOWN,
	DTRIG_VECTOR_CI,	/* need to emulate vector CI load instr */
	DTRIG_SUSPEND_ESCAPE,	/* need to escape from TM suspend mode */
} hmer_debug_trig_function;

static int init_debug_trig_function(void)
{
	int pvr;
	struct device_node *cpun;
	struct property *prop = NULL;
	const char *str;

	/* First look in the device tree */
	preempt_disable();
	cpun = of_get_cpu_node(smp_processor_id(), NULL);
	if (cpun) {
		of_property_for_each_string(cpun, "ibm,hmi-special-triggers",
					    prop, str) {
			if (strcmp(str, "bit17-vector-ci-load") == 0)
				hmer_debug_trig_function = DTRIG_VECTOR_CI;
			else if (strcmp(str, "bit17-tm-suspend-escape") == 0)
				hmer_debug_trig_function = DTRIG_SUSPEND_ESCAPE;
		}
		of_node_put(cpun);
	}
	preempt_enable();

	/* If we found the property, don't look at PVR */
	if (prop)
		goto out;

	pvr = mfspr(SPRN_PVR);
	/* Check for POWER9 Nimbus (scale-out) */
	if ((PVR_VER(pvr) == PVR_POWER9) && (pvr & 0xe000) == 0) {
		/* DD2.2 and later */
		if ((pvr & 0xfff) >= 0x202)
			hmer_debug_trig_function = DTRIG_SUSPEND_ESCAPE;
		/* DD2.0 and DD2.1 - used for vector CI load emulation */
		else if ((pvr & 0xfff) >= 0x200)
			hmer_debug_trig_function = DTRIG_VECTOR_CI;
	}

 out:
	switch (hmer_debug_trig_function) {
	case DTRIG_VECTOR_CI:
		pr_debug("HMI debug trigger used for vector CI load\n");
		break;
	case DTRIG_SUSPEND_ESCAPE:
		pr_debug("HMI debug trigger used for TM suspend escape\n");
		break;
	default:
		break;
	}
	return 0;
}
__initcall(init_debug_trig_function);

/*
 * Handle HMIs that occur as a result of a debug trigger.
 * Return values:
 * -1 means this is not a HMI cause that we know about
 *  0 means no further handling is required
 *  1 means further handling is required
 */
long hmi_handle_debugtrig(struct pt_regs *regs)
{
	unsigned long hmer = mfspr(SPRN_HMER);
	long ret = 0;

	/* HMER_DEBUG_TRIG bit is used for various workarounds on P9 */
	if (!((hmer & HMER_DEBUG_TRIG)
	      && hmer_debug_trig_function != DTRIG_UNKNOWN))
		return -1;
		
	hmer &= ~HMER_DEBUG_TRIG;
	/* HMER is a write-AND register */
	mtspr(SPRN_HMER, ~HMER_DEBUG_TRIG);

	switch (hmer_debug_trig_function) {
	case DTRIG_VECTOR_CI:
		/*
		 * Now to avoid problems with soft-disable we
		 * only do the emulation if we are coming from
		 * host user space
		 */
		if (regs && user_mode(regs))
			ret = local_paca->hmi_p9_special_emu = 1;

		break;

	default:
		break;
	}

	/*
	 * See if any other HMI causes remain to be handled
	 */
	if (hmer & mfspr(SPRN_HMEER))
		return -1;

	return ret;
}

/*
 * Return values:
 */
long hmi_exception_realmode(struct pt_regs *regs)
{	
	int ret;

	local_paca->hmi_irqs++;

	ret = hmi_handle_debugtrig(regs);
	if (ret >= 0)
		return ret;

	wait_for_subcore_guest_exit();

	if (ppc_md.hmi_exception_early)
		ppc_md.hmi_exception_early(regs);

	wait_for_tb_resync();

	return 1;
}
