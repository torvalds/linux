// SPDX-License-Identifier: GPL-2.0
/*
 * Kernel probes (kprobes) for SuperH
 *
 * Copyright (C) 2007 Chris Smith <chris.smith@st.com>
 * Copyright (C) 2006 Lineo Solutions, Inc.
 */
#include <linux/kprobes.h>
#include <linux/extable.h>
#include <linux/ptrace.h>
#include <linux/preempt.h>
#include <linux/kdebug.h>
#include <linux/slab.h>
#include <asm/cacheflush.h>
#include <linux/uaccess.h>

DEFINE_PER_CPU(struct kprobe *, current_kprobe) = NULL;
DEFINE_PER_CPU(struct kprobe_ctlblk, kprobe_ctlblk);

static DEFINE_PER_CPU(struct kprobe, saved_current_opcode);
static DEFINE_PER_CPU(struct kprobe, saved_next_opcode);
static DEFINE_PER_CPU(struct kprobe, saved_next_opcode2);

#define OPCODE_JMP(x)	(((x) & 0xF0FF) == 0x402b)
#define OPCODE_JSR(x)	(((x) & 0xF0FF) == 0x400b)
#define OPCODE_BRA(x)	(((x) & 0xF000) == 0xa000)
#define OPCODE_BRAF(x)	(((x) & 0xF0FF) == 0x0023)
#define OPCODE_BSR(x)	(((x) & 0xF000) == 0xb000)
#define OPCODE_BSRF(x)	(((x) & 0xF0FF) == 0x0003)

#define OPCODE_BF_S(x)	(((x) & 0xFF00) == 0x8f00)
#define OPCODE_BT_S(x)	(((x) & 0xFF00) == 0x8d00)

#define OPCODE_BF(x)	(((x) & 0xFF00) == 0x8b00)
#define OPCODE_BT(x)	(((x) & 0xFF00) == 0x8900)

#define OPCODE_RTS(x)	(((x) & 0x000F) == 0x000b)
#define OPCODE_RTE(x)	(((x) & 0xFFFF) == 0x002b)

int __kprobes arch_prepare_kprobe(struct kprobe *p)
{
	kprobe_opcode_t opcode = *(kprobe_opcode_t *) (p->addr);

	if (OPCODE_RTE(opcode))
		return -EFAULT;	/* Bad breakpoint */

	p->opcode = opcode;

	return 0;
}

void __kprobes arch_copy_kprobe(struct kprobe *p)
{
	memcpy(p->ainsn.insn, p->addr, MAX_INSN_SIZE * sizeof(kprobe_opcode_t));
	p->opcode = *p->addr;
}

void __kprobes arch_arm_kprobe(struct kprobe *p)
{
	*p->addr = BREAKPOINT_INSTRUCTION;
	flush_icache_range((unsigned long)p->addr,
			   (unsigned long)p->addr + sizeof(kprobe_opcode_t));
}

void __kprobes arch_disarm_kprobe(struct kprobe *p)
{
	*p->addr = p->opcode;
	flush_icache_range((unsigned long)p->addr,
			   (unsigned long)p->addr + sizeof(kprobe_opcode_t));
}

int __kprobes arch_trampoline_kprobe(struct kprobe *p)
{
	if (*p->addr == BREAKPOINT_INSTRUCTION)
		return 1;

	return 0;
}

/**
 * If an illegal slot instruction exception occurs for an address
 * containing a kprobe, remove the probe.
 *
 * Returns 0 if the exception was handled successfully, 1 otherwise.
 */
int __kprobes kprobe_handle_illslot(unsigned long pc)
{
	struct kprobe *p = get_kprobe((kprobe_opcode_t *) pc + 1);

	if (p != NULL) {
		printk("Warning: removing kprobe from delay slot: 0x%.8x\n",
		       (unsigned int)pc + 2);
		unregister_kprobe(p);
		return 0;
	}

	return 1;
}

void __kprobes arch_remove_kprobe(struct kprobe *p)
{
	struct kprobe *saved = this_cpu_ptr(&saved_next_opcode);

	if (saved->addr) {
		arch_disarm_kprobe(p);
		arch_disarm_kprobe(saved);

		saved->addr = NULL;
		saved->opcode = 0;

		saved = this_cpu_ptr(&saved_next_opcode2);
		if (saved->addr) {
			arch_disarm_kprobe(saved);

			saved->addr = NULL;
			saved->opcode = 0;
		}
	}
}

static void __kprobes save_previous_kprobe(struct kprobe_ctlblk *kcb)
{
	kcb->prev_kprobe.kp = kprobe_running();
	kcb->prev_kprobe.status = kcb->kprobe_status;
}

static void __kprobes restore_previous_kprobe(struct kprobe_ctlblk *kcb)
{
	__this_cpu_write(current_kprobe, kcb->prev_kprobe.kp);
	kcb->kprobe_status = kcb->prev_kprobe.status;
}

static void __kprobes set_current_kprobe(struct kprobe *p, struct pt_regs *regs,
					 struct kprobe_ctlblk *kcb)
{
	__this_cpu_write(current_kprobe, p);
}

/*
 * Singlestep is implemented by disabling the current kprobe and setting one
 * on the next instruction, following branches. Two probes are set if the
 * branch is conditional.
 */
static void __kprobes prepare_singlestep(struct kprobe *p, struct pt_regs *regs)
{
	__this_cpu_write(saved_current_opcode.addr, (kprobe_opcode_t *)regs->pc);

	if (p != NULL) {
		struct kprobe *op1, *op2;

		arch_disarm_kprobe(p);

		op1 = this_cpu_ptr(&saved_next_opcode);
		op2 = this_cpu_ptr(&saved_next_opcode2);

		if (OPCODE_JSR(p->opcode) || OPCODE_JMP(p->opcode)) {
			unsigned int reg_nr = ((p->opcode >> 8) & 0x000F);
			op1->addr = (kprobe_opcode_t *) regs->regs[reg_nr];
		} else if (OPCODE_BRA(p->opcode) || OPCODE_BSR(p->opcode)) {
			unsigned long disp = (p->opcode & 0x0FFF);
			op1->addr =
			    (kprobe_opcode_t *) (regs->pc + 4 + disp * 2);

		} else if (OPCODE_BRAF(p->opcode) || OPCODE_BSRF(p->opcode)) {
			unsigned int reg_nr = ((p->opcode >> 8) & 0x000F);
			op1->addr =
			    (kprobe_opcode_t *) (regs->pc + 4 +
						 regs->regs[reg_nr]);

		} else if (OPCODE_RTS(p->opcode)) {
			op1->addr = (kprobe_opcode_t *) regs->pr;

		} else if (OPCODE_BF(p->opcode) || OPCODE_BT(p->opcode)) {
			unsigned long disp = (p->opcode & 0x00FF);
			/* case 1 */
			op1->addr = p->addr + 1;
			/* case 2 */
			op2->addr =
			    (kprobe_opcode_t *) (regs->pc + 4 + disp * 2);
			op2->opcode = *(op2->addr);
			arch_arm_kprobe(op2);

		} else if (OPCODE_BF_S(p->opcode) || OPCODE_BT_S(p->opcode)) {
			unsigned long disp = (p->opcode & 0x00FF);
			/* case 1 */
			op1->addr = p->addr + 2;
			/* case 2 */
			op2->addr =
			    (kprobe_opcode_t *) (regs->pc + 4 + disp * 2);
			op2->opcode = *(op2->addr);
			arch_arm_kprobe(op2);

		} else {
			op1->addr = p->addr + 1;
		}

		op1->opcode = *(op1->addr);
		arch_arm_kprobe(op1);
	}
}

/* Called with kretprobe_lock held */
void __kprobes arch_prepare_kretprobe(struct kretprobe_instance *ri,
				      struct pt_regs *regs)
{
	ri->ret_addr = (kprobe_opcode_t *) regs->pr;
	ri->fp = NULL;

	/* Replace the return addr with trampoline addr */
	regs->pr = (unsigned long)kretprobe_trampoline;
}

static int __kprobes kprobe_handler(struct pt_regs *regs)
{
	struct kprobe *p;
	int ret = 0;
	kprobe_opcode_t *addr = NULL;
	struct kprobe_ctlblk *kcb;

	/*
	 * We don't want to be preempted for the entire
	 * duration of kprobe processing
	 */
	preempt_disable();
	kcb = get_kprobe_ctlblk();

	addr = (kprobe_opcode_t *) (regs->pc);

	/* Check we're not actually recursing */
	if (kprobe_running()) {
		p = get_kprobe(addr);
		if (p) {
			if (kcb->kprobe_status == KPROBE_HIT_SS &&
			    *p->ainsn.insn == BREAKPOINT_INSTRUCTION) {
				goto no_kprobe;
			}
			/* We have reentered the kprobe_handler(), since
			 * another probe was hit while within the handler.
			 * We here save the original kprobes variables and
			 * just single step on the instruction of the new probe
			 * without calling any user handlers.
			 */
			save_previous_kprobe(kcb);
			set_current_kprobe(p, regs, kcb);
			kprobes_inc_nmissed_count(p);
			prepare_singlestep(p, regs);
			kcb->kprobe_status = KPROBE_REENTER;
			return 1;
		}
		goto no_kprobe;
	}

	p = get_kprobe(addr);
	if (!p) {
		/* Not one of ours: let kernel handle it */
		if (*(kprobe_opcode_t *)addr != BREAKPOINT_INSTRUCTION) {
			/*
			 * The breakpoint instruction was removed right
			 * after we hit it. Another cpu has removed
			 * either a probepoint or a debugger breakpoint
			 * at this address. In either case, no further
			 * handling of this interrupt is appropriate.
			 */
			ret = 1;
		}

		goto no_kprobe;
	}

	set_current_kprobe(p, regs, kcb);
	kcb->kprobe_status = KPROBE_HIT_ACTIVE;

	if (p->pre_handler && p->pre_handler(p, regs)) {
		/* handler has already set things up, so skip ss setup */
		reset_current_kprobe();
		preempt_enable_no_resched();
		return 1;
	}

	prepare_singlestep(p, regs);
	kcb->kprobe_status = KPROBE_HIT_SS;
	return 1;

no_kprobe:
	preempt_enable_no_resched();
	return ret;
}

/*
 * For function-return probes, init_kprobes() establishes a probepoint
 * here. When a retprobed function returns, this probe is hit and
 * trampoline_probe_handler() runs, calling the kretprobe's handler.
 */
static void __used kretprobe_trampoline_holder(void)
{
	asm volatile (".globl kretprobe_trampoline\n"
		      "kretprobe_trampoline:\n\t"
		      "nop\n");
}

/*
 * Called when we hit the probe point at kretprobe_trampoline
 */
int __kprobes trampoline_probe_handler(struct kprobe *p, struct pt_regs *regs)
{
	regs->pc = __kretprobe_trampoline_handler(regs, &kretprobe_trampoline, NULL);

	return 1;
}

static int __kprobes post_kprobe_handler(struct pt_regs *regs)
{
	struct kprobe *cur = kprobe_running();
	struct kprobe_ctlblk *kcb = get_kprobe_ctlblk();
	kprobe_opcode_t *addr = NULL;
	struct kprobe *p = NULL;

	if (!cur)
		return 0;

	if ((kcb->kprobe_status != KPROBE_REENTER) && cur->post_handler) {
		kcb->kprobe_status = KPROBE_HIT_SSDONE;
		cur->post_handler(cur, regs, 0);
	}

	p = this_cpu_ptr(&saved_next_opcode);
	if (p->addr) {
		arch_disarm_kprobe(p);
		p->addr = NULL;
		p->opcode = 0;

		addr = __this_cpu_read(saved_current_opcode.addr);
		__this_cpu_write(saved_current_opcode.addr, NULL);

		p = get_kprobe(addr);
		arch_arm_kprobe(p);

		p = this_cpu_ptr(&saved_next_opcode2);
		if (p->addr) {
			arch_disarm_kprobe(p);
			p->addr = NULL;
			p->opcode = 0;
		}
	}

	/* Restore back the original saved kprobes variables and continue. */
	if (kcb->kprobe_status == KPROBE_REENTER) {
		restore_previous_kprobe(kcb);
		goto out;
	}

	reset_current_kprobe();

out:
	preempt_enable_no_resched();

	return 1;
}

int __kprobes kprobe_fault_handler(struct pt_regs *regs, int trapnr)
{
	struct kprobe *cur = kprobe_running();
	struct kprobe_ctlblk *kcb = get_kprobe_ctlblk();
	const struct exception_table_entry *entry;

	switch (kcb->kprobe_status) {
	case KPROBE_HIT_SS:
	case KPROBE_REENTER:
		/*
		 * We are here because the instruction being single
		 * stepped caused a page fault. We reset the current
		 * kprobe, point the pc back to the probe address
		 * and allow the page fault handler to continue as a
		 * normal page fault.
		 */
		regs->pc = (unsigned long)cur->addr;
		if (kcb->kprobe_status == KPROBE_REENTER)
			restore_previous_kprobe(kcb);
		else
			reset_current_kprobe();
		preempt_enable_no_resched();
		break;
	case KPROBE_HIT_ACTIVE:
	case KPROBE_HIT_SSDONE:
		/*
		 * In case the user-specified fault handler returned
		 * zero, try to fix up.
		 */
		if ((entry = search_exception_tables(regs->pc)) != NULL) {
			regs->pc = entry->fixup;
			return 1;
		}

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

/*
 * Wrapper routine to for handling exceptions.
 */
int __kprobes kprobe_exceptions_notify(struct notifier_block *self,
				       unsigned long val, void *data)
{
	struct kprobe *p = NULL;
	struct die_args *args = (struct die_args *)data;
	int ret = NOTIFY_DONE;
	kprobe_opcode_t *addr = NULL;
	struct kprobe_ctlblk *kcb = get_kprobe_ctlblk();

	addr = (kprobe_opcode_t *) (args->regs->pc);
	if (val == DIE_TRAP &&
	    args->trapnr == (BREAKPOINT_INSTRUCTION & 0xff)) {
		if (!kprobe_running()) {
			if (kprobe_handler(args->regs)) {
				ret = NOTIFY_STOP;
			} else {
				/* Not a kprobe trap */
				ret = NOTIFY_DONE;
			}
		} else {
			p = get_kprobe(addr);
			if ((kcb->kprobe_status == KPROBE_HIT_SS) ||
			    (kcb->kprobe_status == KPROBE_REENTER)) {
				if (post_kprobe_handler(args->regs))
					ret = NOTIFY_STOP;
			} else {
				if (kprobe_handler(args->regs))
					ret = NOTIFY_STOP;
			}
		}
	}

	return ret;
}

static struct kprobe trampoline_p = {
	.addr = (kprobe_opcode_t *)&kretprobe_trampoline,
	.pre_handler = trampoline_probe_handler
};

int __init arch_init_kprobes(void)
{
	return register_kprobe(&trampoline_p);
}
