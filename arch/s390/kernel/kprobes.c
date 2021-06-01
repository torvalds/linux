// SPDX-License-Identifier: GPL-2.0+
/*
 *  Kernel Probes (KProbes)
 *
 * Copyright IBM Corp. 2002, 2006
 *
 * s390 port, used ppc64 as template. Mike Grundy <grundym@us.ibm.com>
 */

#include <linux/moduleloader.h>
#include <linux/kprobes.h>
#include <linux/ptrace.h>
#include <linux/preempt.h>
#include <linux/stop_machine.h>
#include <linux/kdebug.h>
#include <linux/uaccess.h>
#include <linux/extable.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/hardirq.h>
#include <linux/ftrace.h>
#include <asm/set_memory.h>
#include <asm/sections.h>
#include <asm/dis.h>
#include "entry.h"

DEFINE_PER_CPU(struct kprobe *, current_kprobe);
DEFINE_PER_CPU(struct kprobe_ctlblk, kprobe_ctlblk);

struct kretprobe_blackpoint kretprobe_blacklist[] = { };

DEFINE_INSN_CACHE_OPS(s390_insn);

static int insn_page_in_use;

void *alloc_insn_page(void)
{
	void *page;

	page = module_alloc(PAGE_SIZE);
	if (!page)
		return NULL;
	__set_memory((unsigned long) page, 1, SET_MEMORY_RO | SET_MEMORY_X);
	return page;
}

void free_insn_page(void *page)
{
	module_memfree(page);
}

static void *alloc_s390_insn_page(void)
{
	if (xchg(&insn_page_in_use, 1) == 1)
		return NULL;
	return &kprobes_insn_page;
}

static void free_s390_insn_page(void *page)
{
	xchg(&insn_page_in_use, 0);
}

struct kprobe_insn_cache kprobe_s390_insn_slots = {
	.mutex = __MUTEX_INITIALIZER(kprobe_s390_insn_slots.mutex),
	.alloc = alloc_s390_insn_page,
	.free = free_s390_insn_page,
	.pages = LIST_HEAD_INIT(kprobe_s390_insn_slots.pages),
	.insn_size = MAX_INSN_SIZE,
};

static void copy_instruction(struct kprobe *p)
{
	kprobe_opcode_t insn[MAX_INSN_SIZE];
	s64 disp, new_disp;
	u64 addr, new_addr;
	unsigned int len;

	len = insn_length(*p->addr >> 8);
	memcpy(&insn, p->addr, len);
	p->opcode = insn[0];
	if (probe_is_insn_relative_long(&insn[0])) {
		/*
		 * For pc-relative instructions in RIL-b or RIL-c format patch
		 * the RI2 displacement field. We have already made sure that
		 * the insn slot for the patched instruction is within the same
		 * 2GB area as the original instruction (either kernel image or
		 * module area). Therefore the new displacement will always fit.
		 */
		disp = *(s32 *)&insn[1];
		addr = (u64)(unsigned long)p->addr;
		new_addr = (u64)(unsigned long)p->ainsn.insn;
		new_disp = ((addr + (disp * 2)) - new_addr) / 2;
		*(s32 *)&insn[1] = new_disp;
	}
	s390_kernel_write(p->ainsn.insn, &insn, len);
}
NOKPROBE_SYMBOL(copy_instruction);

static inline int is_kernel_addr(void *addr)
{
	return addr < (void *)_end;
}

static int s390_get_insn_slot(struct kprobe *p)
{
	/*
	 * Get an insn slot that is within the same 2GB area like the original
	 * instruction. That way instructions with a 32bit signed displacement
	 * field can be patched and executed within the insn slot.
	 */
	p->ainsn.insn = NULL;
	if (is_kernel_addr(p->addr))
		p->ainsn.insn = get_s390_insn_slot();
	else if (is_module_addr(p->addr))
		p->ainsn.insn = get_insn_slot();
	return p->ainsn.insn ? 0 : -ENOMEM;
}
NOKPROBE_SYMBOL(s390_get_insn_slot);

static void s390_free_insn_slot(struct kprobe *p)
{
	if (!p->ainsn.insn)
		return;
	if (is_kernel_addr(p->addr))
		free_s390_insn_slot(p->ainsn.insn, 0);
	else
		free_insn_slot(p->ainsn.insn, 0);
	p->ainsn.insn = NULL;
}
NOKPROBE_SYMBOL(s390_free_insn_slot);

int arch_prepare_kprobe(struct kprobe *p)
{
	if ((unsigned long) p->addr & 0x01)
		return -EINVAL;
	/* Make sure the probe isn't going on a difficult instruction */
	if (probe_is_prohibited_opcode(p->addr))
		return -EINVAL;
	if (s390_get_insn_slot(p))
		return -ENOMEM;
	copy_instruction(p);
	return 0;
}
NOKPROBE_SYMBOL(arch_prepare_kprobe);

struct swap_insn_args {
	struct kprobe *p;
	unsigned int arm_kprobe : 1;
};

static int swap_instruction(void *data)
{
	struct swap_insn_args *args = data;
	struct kprobe *p = args->p;
	u16 opc;

	opc = args->arm_kprobe ? BREAKPOINT_INSTRUCTION : p->opcode;
	s390_kernel_write(p->addr, &opc, sizeof(opc));
	return 0;
}
NOKPROBE_SYMBOL(swap_instruction);

void arch_arm_kprobe(struct kprobe *p)
{
	struct swap_insn_args args = {.p = p, .arm_kprobe = 1};

	stop_machine_cpuslocked(swap_instruction, &args, NULL);
}
NOKPROBE_SYMBOL(arch_arm_kprobe);

void arch_disarm_kprobe(struct kprobe *p)
{
	struct swap_insn_args args = {.p = p, .arm_kprobe = 0};

	stop_machine_cpuslocked(swap_instruction, &args, NULL);
}
NOKPROBE_SYMBOL(arch_disarm_kprobe);

void arch_remove_kprobe(struct kprobe *p)
{
	s390_free_insn_slot(p);
}
NOKPROBE_SYMBOL(arch_remove_kprobe);

static void enable_singlestep(struct kprobe_ctlblk *kcb,
			      struct pt_regs *regs,
			      unsigned long ip)
{
	struct per_regs per_kprobe;

	/* Set up the PER control registers %cr9-%cr11 */
	per_kprobe.control = PER_EVENT_IFETCH;
	per_kprobe.start = ip;
	per_kprobe.end = ip;

	/* Save control regs and psw mask */
	__ctl_store(kcb->kprobe_saved_ctl, 9, 11);
	kcb->kprobe_saved_imask = regs->psw.mask &
		(PSW_MASK_PER | PSW_MASK_IO | PSW_MASK_EXT);

	/* Set PER control regs, turns on single step for the given address */
	__ctl_load(per_kprobe, 9, 11);
	regs->psw.mask |= PSW_MASK_PER;
	regs->psw.mask &= ~(PSW_MASK_IO | PSW_MASK_EXT);
	regs->psw.addr = ip;
}
NOKPROBE_SYMBOL(enable_singlestep);

static void disable_singlestep(struct kprobe_ctlblk *kcb,
			       struct pt_regs *regs,
			       unsigned long ip)
{
	/* Restore control regs and psw mask, set new psw address */
	__ctl_load(kcb->kprobe_saved_ctl, 9, 11);
	regs->psw.mask &= ~PSW_MASK_PER;
	regs->psw.mask |= kcb->kprobe_saved_imask;
	regs->psw.addr = ip;
}
NOKPROBE_SYMBOL(disable_singlestep);

/*
 * Activate a kprobe by storing its pointer to current_kprobe. The
 * previous kprobe is stored in kcb->prev_kprobe. A stack of up to
 * two kprobes can be active, see KPROBE_REENTER.
 */
static void push_kprobe(struct kprobe_ctlblk *kcb, struct kprobe *p)
{
	kcb->prev_kprobe.kp = __this_cpu_read(current_kprobe);
	kcb->prev_kprobe.status = kcb->kprobe_status;
	__this_cpu_write(current_kprobe, p);
}
NOKPROBE_SYMBOL(push_kprobe);

/*
 * Deactivate a kprobe by backing up to the previous state. If the
 * current state is KPROBE_REENTER prev_kprobe.kp will be non-NULL,
 * for any other state prev_kprobe.kp will be NULL.
 */
static void pop_kprobe(struct kprobe_ctlblk *kcb)
{
	__this_cpu_write(current_kprobe, kcb->prev_kprobe.kp);
	kcb->kprobe_status = kcb->prev_kprobe.status;
}
NOKPROBE_SYMBOL(pop_kprobe);

void arch_prepare_kretprobe(struct kretprobe_instance *ri, struct pt_regs *regs)
{
	ri->ret_addr = (kprobe_opcode_t *) regs->gprs[14];
	ri->fp = NULL;

	/* Replace the return addr with trampoline addr */
	regs->gprs[14] = (unsigned long) &kretprobe_trampoline;
}
NOKPROBE_SYMBOL(arch_prepare_kretprobe);

static void kprobe_reenter_check(struct kprobe_ctlblk *kcb, struct kprobe *p)
{
	switch (kcb->kprobe_status) {
	case KPROBE_HIT_SSDONE:
	case KPROBE_HIT_ACTIVE:
		kprobes_inc_nmissed_count(p);
		break;
	case KPROBE_HIT_SS:
	case KPROBE_REENTER:
	default:
		/*
		 * A kprobe on the code path to single step an instruction
		 * is a BUG. The code path resides in the .kprobes.text
		 * section and is executed with interrupts disabled.
		 */
		pr_err("Invalid kprobe detected.\n");
		dump_kprobe(p);
		BUG();
	}
}
NOKPROBE_SYMBOL(kprobe_reenter_check);

static int kprobe_handler(struct pt_regs *regs)
{
	struct kprobe_ctlblk *kcb;
	struct kprobe *p;

	/*
	 * We want to disable preemption for the entire duration of kprobe
	 * processing. That includes the calls to the pre/post handlers
	 * and single stepping the kprobe instruction.
	 */
	preempt_disable();
	kcb = get_kprobe_ctlblk();
	p = get_kprobe((void *)(regs->psw.addr - 2));

	if (p) {
		if (kprobe_running()) {
			/*
			 * We have hit a kprobe while another is still
			 * active. This can happen in the pre and post
			 * handler. Single step the instruction of the
			 * new probe but do not call any handler function
			 * of this secondary kprobe.
			 * push_kprobe and pop_kprobe saves and restores
			 * the currently active kprobe.
			 */
			kprobe_reenter_check(kcb, p);
			push_kprobe(kcb, p);
			kcb->kprobe_status = KPROBE_REENTER;
		} else {
			/*
			 * If we have no pre-handler or it returned 0, we
			 * continue with single stepping. If we have a
			 * pre-handler and it returned non-zero, it prepped
			 * for changing execution path, so get out doing
			 * nothing more here.
			 */
			push_kprobe(kcb, p);
			kcb->kprobe_status = KPROBE_HIT_ACTIVE;
			if (p->pre_handler && p->pre_handler(p, regs)) {
				pop_kprobe(kcb);
				preempt_enable_no_resched();
				return 1;
			}
			kcb->kprobe_status = KPROBE_HIT_SS;
		}
		enable_singlestep(kcb, regs, (unsigned long) p->ainsn.insn);
		return 1;
	} /* else:
	   * No kprobe at this address and no active kprobe. The trap has
	   * not been caused by a kprobe breakpoint. The race of breakpoint
	   * vs. kprobe remove does not exist because on s390 as we use
	   * stop_machine to arm/disarm the breakpoints.
	   */
	preempt_enable_no_resched();
	return 0;
}
NOKPROBE_SYMBOL(kprobe_handler);

/*
 * Function return probe trampoline:
 *	- init_kprobes() establishes a probepoint here
 *	- When the probed function returns, this probe
 *		causes the handlers to fire
 */
static void __used kretprobe_trampoline_holder(void)
{
	asm volatile(".global kretprobe_trampoline\n"
		     "kretprobe_trampoline: bcr 0,0\n");
}

/*
 * Called when the probe at kretprobe trampoline is hit
 */
static int trampoline_probe_handler(struct kprobe *p, struct pt_regs *regs)
{
	regs->psw.addr = __kretprobe_trampoline_handler(regs, &kretprobe_trampoline, NULL);
	/*
	 * By returning a non-zero value, we are telling
	 * kprobe_handler() that we don't want the post_handler
	 * to run (and have re-enabled preemption)
	 */
	return 1;
}
NOKPROBE_SYMBOL(trampoline_probe_handler);

/*
 * Called after single-stepping.  p->addr is the address of the
 * instruction whose first byte has been replaced by the "breakpoint"
 * instruction.  To avoid the SMP problems that can occur when we
 * temporarily put back the original opcode to single-step, we
 * single-stepped a copy of the instruction.  The address of this
 * copy is p->ainsn.insn.
 */
static void resume_execution(struct kprobe *p, struct pt_regs *regs)
{
	struct kprobe_ctlblk *kcb = get_kprobe_ctlblk();
	unsigned long ip = regs->psw.addr;
	int fixup = probe_get_fixup_type(p->ainsn.insn);

	if (fixup & FIXUP_PSW_NORMAL)
		ip += (unsigned long) p->addr - (unsigned long) p->ainsn.insn;

	if (fixup & FIXUP_BRANCH_NOT_TAKEN) {
		int ilen = insn_length(p->ainsn.insn[0] >> 8);
		if (ip - (unsigned long) p->ainsn.insn == ilen)
			ip = (unsigned long) p->addr + ilen;
	}

	if (fixup & FIXUP_RETURN_REGISTER) {
		int reg = (p->ainsn.insn[0] & 0xf0) >> 4;
		regs->gprs[reg] += (unsigned long) p->addr -
				   (unsigned long) p->ainsn.insn;
	}

	disable_singlestep(kcb, regs, ip);
}
NOKPROBE_SYMBOL(resume_execution);

static int post_kprobe_handler(struct pt_regs *regs)
{
	struct kprobe_ctlblk *kcb = get_kprobe_ctlblk();
	struct kprobe *p = kprobe_running();

	if (!p)
		return 0;

	if (kcb->kprobe_status != KPROBE_REENTER && p->post_handler) {
		kcb->kprobe_status = KPROBE_HIT_SSDONE;
		p->post_handler(p, regs, 0);
	}

	resume_execution(p, regs);
	pop_kprobe(kcb);
	preempt_enable_no_resched();

	/*
	 * if somebody else is singlestepping across a probe point, psw mask
	 * will have PER set, in which case, continue the remaining processing
	 * of do_single_step, as if this is not a probe hit.
	 */
	if (regs->psw.mask & PSW_MASK_PER)
		return 0;

	return 1;
}
NOKPROBE_SYMBOL(post_kprobe_handler);

static int kprobe_trap_handler(struct pt_regs *regs, int trapnr)
{
	struct kprobe_ctlblk *kcb = get_kprobe_ctlblk();
	struct kprobe *p = kprobe_running();
	const struct exception_table_entry *entry;

	switch(kcb->kprobe_status) {
	case KPROBE_HIT_SS:
	case KPROBE_REENTER:
		/*
		 * We are here because the instruction being single
		 * stepped caused a page fault. We reset the current
		 * kprobe and the nip points back to the probe address
		 * and allow the page fault handler to continue as a
		 * normal page fault.
		 */
		disable_singlestep(kcb, regs, (unsigned long) p->addr);
		pop_kprobe(kcb);
		preempt_enable_no_resched();
		break;
	case KPROBE_HIT_ACTIVE:
	case KPROBE_HIT_SSDONE:
		/*
		 * In case the user-specified fault handler returned
		 * zero, try to fix up.
		 */
		entry = s390_search_extables(regs->psw.addr);
		if (entry && ex_handle(entry, regs))
			return 1;

		/*
		 * fixup_exception() could not handle it,
		 * Let do_page_fault() fix it.
		 */
		break;
	default:
		break;
	}
	return 0;
}
NOKPROBE_SYMBOL(kprobe_trap_handler);

int kprobe_fault_handler(struct pt_regs *regs, int trapnr)
{
	int ret;

	if (regs->psw.mask & (PSW_MASK_IO | PSW_MASK_EXT))
		local_irq_disable();
	ret = kprobe_trap_handler(regs, trapnr);
	if (regs->psw.mask & (PSW_MASK_IO | PSW_MASK_EXT))
		local_irq_restore(regs->psw.mask & ~PSW_MASK_PER);
	return ret;
}
NOKPROBE_SYMBOL(kprobe_fault_handler);

/*
 * Wrapper routine to for handling exceptions.
 */
int kprobe_exceptions_notify(struct notifier_block *self,
			     unsigned long val, void *data)
{
	struct die_args *args = (struct die_args *) data;
	struct pt_regs *regs = args->regs;
	int ret = NOTIFY_DONE;

	if (regs->psw.mask & (PSW_MASK_IO | PSW_MASK_EXT))
		local_irq_disable();

	switch (val) {
	case DIE_BPT:
		if (kprobe_handler(regs))
			ret = NOTIFY_STOP;
		break;
	case DIE_SSTEP:
		if (post_kprobe_handler(regs))
			ret = NOTIFY_STOP;
		break;
	case DIE_TRAP:
		if (!preemptible() && kprobe_running() &&
		    kprobe_trap_handler(regs, args->trapnr))
			ret = NOTIFY_STOP;
		break;
	default:
		break;
	}

	if (regs->psw.mask & (PSW_MASK_IO | PSW_MASK_EXT))
		local_irq_restore(regs->psw.mask & ~PSW_MASK_PER);

	return ret;
}
NOKPROBE_SYMBOL(kprobe_exceptions_notify);

static struct kprobe trampoline = {
	.addr = (kprobe_opcode_t *) &kretprobe_trampoline,
	.pre_handler = trampoline_probe_handler
};

int __init arch_init_kprobes(void)
{
	return register_kprobe(&trampoline);
}

int arch_trampoline_kprobe(struct kprobe *p)
{
	return p->addr == (kprobe_opcode_t *) &kretprobe_trampoline;
}
NOKPROBE_SYMBOL(arch_trampoline_kprobe);
