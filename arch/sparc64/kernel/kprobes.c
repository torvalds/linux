/* arch/sparc64/kernel/kprobes.c
 *
 * Copyright (C) 2004 David S. Miller <davem@davemloft.net>
 */

#include <linux/config.h>
#include <linux/kernel.h>
#include <linux/kprobes.h>
#include <asm/kdebug.h>
#include <asm/signal.h>
#include <asm/cacheflush.h>

/* We do not have hardware single-stepping on sparc64.
 * So we implement software single-stepping with breakpoint
 * traps.  The top-level scheme is similar to that used
 * in the x86 kprobes implementation.
 *
 * In the kprobe->ainsn.insn[] array we store the original
 * instruction at index zero and a break instruction at
 * index one.
 *
 * When we hit a kprobe we:
 * - Run the pre-handler
 * - Remember "regs->tnpc" and interrupt level stored in
 *   "regs->tstate" so we can restore them later
 * - Disable PIL interrupts
 * - Set regs->tpc to point to kprobe->ainsn.insn[0]
 * - Set regs->tnpc to point to kprobe->ainsn.insn[1]
 * - Mark that we are actively in a kprobe
 *
 * At this point we wait for the second breakpoint at
 * kprobe->ainsn.insn[1] to hit.  When it does we:
 * - Run the post-handler
 * - Set regs->tpc to "remembered" regs->tnpc stored above,
 *   restore the PIL interrupt level in "regs->tstate" as well
 * - Make any adjustments necessary to regs->tnpc in order
 *   to handle relative branches correctly.  See below.
 * - Mark that we are no longer actively in a kprobe.
 */

int __kprobes arch_prepare_kprobe(struct kprobe *p)
{
	return 0;
}

void __kprobes arch_copy_kprobe(struct kprobe *p)
{
	p->ainsn.insn[0] = *p->addr;
	p->ainsn.insn[1] = BREAKPOINT_INSTRUCTION_2;
	p->opcode = *p->addr;
}

void __kprobes arch_arm_kprobe(struct kprobe *p)
{
	*p->addr = BREAKPOINT_INSTRUCTION;
	flushi(p->addr);
}

void __kprobes arch_disarm_kprobe(struct kprobe *p)
{
	*p->addr = p->opcode;
	flushi(p->addr);
}

void __kprobes arch_remove_kprobe(struct kprobe *p)
{
}

static struct kprobe *current_kprobe;
static unsigned long current_kprobe_orig_tnpc;
static unsigned long current_kprobe_orig_tstate_pil;
static unsigned int kprobe_status;
static struct kprobe *kprobe_prev;
static unsigned long kprobe_orig_tnpc_prev;
static unsigned long kprobe_orig_tstate_pil_prev;
static unsigned int kprobe_status_prev;

static inline void save_previous_kprobe(void)
{
	kprobe_status_prev = kprobe_status;
	kprobe_orig_tnpc_prev = current_kprobe_orig_tnpc;
	kprobe_orig_tstate_pil_prev = current_kprobe_orig_tstate_pil;
	kprobe_prev = current_kprobe;
}

static inline void restore_previous_kprobe(void)
{
	kprobe_status = kprobe_status_prev;
	current_kprobe_orig_tnpc = kprobe_orig_tnpc_prev;
	current_kprobe_orig_tstate_pil = kprobe_orig_tstate_pil_prev;
	current_kprobe = kprobe_prev;
}

static inline void set_current_kprobe(struct kprobe *p, struct pt_regs *regs)
{
	current_kprobe_orig_tnpc = regs->tnpc;
	current_kprobe_orig_tstate_pil = (regs->tstate & TSTATE_PIL);
	current_kprobe = p;
}

static inline void prepare_singlestep(struct kprobe *p, struct pt_regs *regs)
{
	regs->tstate |= TSTATE_PIL;

	/*single step inline, if it a breakpoint instruction*/
	if (p->opcode == BREAKPOINT_INSTRUCTION) {
		regs->tpc = (unsigned long) p->addr;
		regs->tnpc = current_kprobe_orig_tnpc;
	} else {
		regs->tpc = (unsigned long) &p->ainsn.insn[0];
		regs->tnpc = (unsigned long) &p->ainsn.insn[1];
	}
}

static int __kprobes kprobe_handler(struct pt_regs *regs)
{
	struct kprobe *p;
	void *addr = (void *) regs->tpc;
	int ret = 0;

	preempt_disable();

	if (kprobe_running()) {
		/* We *are* holding lock here, so this is safe.
		 * Disarm the probe we just hit, and ignore it.
		 */
		p = get_kprobe(addr);
		if (p) {
			if (kprobe_status == KPROBE_HIT_SS) {
				regs->tstate = ((regs->tstate & ~TSTATE_PIL) |
					current_kprobe_orig_tstate_pil);
				unlock_kprobes();
				goto no_kprobe;
			}
			/* We have reentered the kprobe_handler(), since
			 * another probe was hit while within the handler.
			 * We here save the original kprobes variables and
			 * just single step on the instruction of the new probe
			 * without calling any user handlers.
			 */
			save_previous_kprobe();
			set_current_kprobe(p, regs);
			p->nmissed++;
			kprobe_status = KPROBE_REENTER;
			prepare_singlestep(p, regs);
			return 1;
		} else {
			p = current_kprobe;
			if (p->break_handler && p->break_handler(p, regs))
				goto ss_probe;
		}
		/* If it's not ours, can't be delete race, (we hold lock). */
		goto no_kprobe;
	}

	lock_kprobes();
	p = get_kprobe(addr);
	if (!p) {
		unlock_kprobes();
		if (*(u32 *)addr != BREAKPOINT_INSTRUCTION) {
			/*
			 * The breakpoint instruction was removed right
			 * after we hit it.  Another cpu has removed
			 * either a probepoint or a debugger breakpoint
			 * at this address.  In either case, no further
			 * handling of this interrupt is appropriate.
			 */
			ret = 1;
		}
		/* Not one of ours: let kernel handle it */
		goto no_kprobe;
	}

	set_current_kprobe(p, regs);
	kprobe_status = KPROBE_HIT_ACTIVE;
	if (p->pre_handler && p->pre_handler(p, regs))
		return 1;

ss_probe:
	prepare_singlestep(p, regs);
	kprobe_status = KPROBE_HIT_SS;
	return 1;

no_kprobe:
	preempt_enable_no_resched();
	return ret;
}

/* If INSN is a relative control transfer instruction,
 * return the corrected branch destination value.
 *
 * The original INSN location was REAL_PC, it actually
 * executed at PC and produced destination address NPC.
 */
static unsigned long __kprobes relbranch_fixup(u32 insn, unsigned long real_pc,
					       unsigned long pc,
					       unsigned long npc)
{
	/* Branch not taken, no mods necessary.  */
	if (npc == pc + 0x4UL)
		return real_pc + 0x4UL;

	/* The three cases are call, branch w/prediction,
	 * and traditional branch.
	 */
	if ((insn & 0xc0000000) == 0x40000000 ||
	    (insn & 0xc1c00000) == 0x00400000 ||
	    (insn & 0xc1c00000) == 0x00800000) {
		/* The instruction did all the work for us
		 * already, just apply the offset to the correct
		 * instruction location.
		 */
		return (real_pc + (npc - pc));
	}

	return real_pc + 0x4UL;
}

/* If INSN is an instruction which writes it's PC location
 * into a destination register, fix that up.
 */
static void __kprobes retpc_fixup(struct pt_regs *regs, u32 insn,
				  unsigned long real_pc)
{
	unsigned long *slot = NULL;

	/* Simplest cast is call, which always uses %o7 */
	if ((insn & 0xc0000000) == 0x40000000) {
		slot = &regs->u_regs[UREG_I7];
	}

	/* Jmpl encodes the register inside of the opcode */
	if ((insn & 0xc1f80000) == 0x81c00000) {
		unsigned long rd = ((insn >> 25) & 0x1f);

		if (rd <= 15) {
			slot = &regs->u_regs[rd];
		} else {
			/* Hard case, it goes onto the stack. */
			flushw_all();

			rd -= 16;
			slot = (unsigned long *)
				(regs->u_regs[UREG_FP] + STACK_BIAS);
			slot += rd;
		}
	}
	if (slot != NULL)
		*slot = real_pc;
}

/*
 * Called after single-stepping.  p->addr is the address of the
 * instruction whose first byte has been replaced by the breakpoint
 * instruction.  To avoid the SMP problems that can occur when we
 * temporarily put back the original opcode to single-step, we
 * single-stepped a copy of the instruction.  The address of this
 * copy is p->ainsn.insn.
 *
 * This function prepares to return from the post-single-step
 * breakpoint trap.
 */
static void __kprobes resume_execution(struct kprobe *p, struct pt_regs *regs)
{
	u32 insn = p->ainsn.insn[0];

	regs->tpc = current_kprobe_orig_tnpc;
	regs->tnpc = relbranch_fixup(insn,
				     (unsigned long) p->addr,
				     (unsigned long) &p->ainsn.insn[0],
				     regs->tnpc);
	retpc_fixup(regs, insn, (unsigned long) p->addr);

	regs->tstate = ((regs->tstate & ~TSTATE_PIL) |
			current_kprobe_orig_tstate_pil);
}

static inline int post_kprobe_handler(struct pt_regs *regs)
{
	if (!kprobe_running())
		return 0;

	if ((kprobe_status != KPROBE_REENTER) && current_kprobe->post_handler) {
		kprobe_status = KPROBE_HIT_SSDONE;
		current_kprobe->post_handler(current_kprobe, regs, 0);
	}

	resume_execution(current_kprobe, regs);

	/*Restore back the original saved kprobes variables and continue. */
	if (kprobe_status == KPROBE_REENTER) {
		restore_previous_kprobe();
		goto out;
	}
	unlock_kprobes();
out:
	preempt_enable_no_resched();

	return 1;
}

/* Interrupts disabled, kprobe_lock held. */
static inline int kprobe_fault_handler(struct pt_regs *regs, int trapnr)
{
	if (current_kprobe->fault_handler
	    && current_kprobe->fault_handler(current_kprobe, regs, trapnr))
		return 1;

	if (kprobe_status & KPROBE_HIT_SS) {
		resume_execution(current_kprobe, regs);

		unlock_kprobes();
		preempt_enable_no_resched();
	}
	return 0;
}

/*
 * Wrapper routine to for handling exceptions.
 */
int __kprobes kprobe_exceptions_notify(struct notifier_block *self,
				       unsigned long val, void *data)
{
	struct die_args *args = (struct die_args *)data;
	switch (val) {
	case DIE_DEBUG:
		if (kprobe_handler(args->regs))
			return NOTIFY_STOP;
		break;
	case DIE_DEBUG_2:
		if (post_kprobe_handler(args->regs))
			return NOTIFY_STOP;
		break;
	case DIE_GPF:
		if (kprobe_running() &&
		    kprobe_fault_handler(args->regs, args->trapnr))
			return NOTIFY_STOP;
		break;
	case DIE_PAGE_FAULT:
		if (kprobe_running() &&
		    kprobe_fault_handler(args->regs, args->trapnr))
			return NOTIFY_STOP;
		break;
	default:
		break;
	}
	return NOTIFY_DONE;
}

asmlinkage void __kprobes kprobe_trap(unsigned long trap_level,
				      struct pt_regs *regs)
{
	BUG_ON(trap_level != 0x170 && trap_level != 0x171);

	if (user_mode(regs)) {
		local_irq_enable();
		bad_trap(regs, trap_level);
		return;
	}

	/* trap_level == 0x170 --> ta 0x70
	 * trap_level == 0x171 --> ta 0x71
	 */
	if (notify_die((trap_level == 0x170) ? DIE_DEBUG : DIE_DEBUG_2,
		       (trap_level == 0x170) ? "debug" : "debug_2",
		       regs, 0, trap_level, SIGTRAP) != NOTIFY_STOP)
		bad_trap(regs, trap_level);
}

/* Jprobes support.  */
static struct pt_regs jprobe_saved_regs;
static struct pt_regs *jprobe_saved_regs_location;
static struct sparc_stackf jprobe_saved_stack;

int __kprobes setjmp_pre_handler(struct kprobe *p, struct pt_regs *regs)
{
	struct jprobe *jp = container_of(p, struct jprobe, kp);

	jprobe_saved_regs_location = regs;
	memcpy(&jprobe_saved_regs, regs, sizeof(*regs));

	/* Save a whole stack frame, this gets arguments
	 * pushed onto the stack after using up all the
	 * arg registers.
	 */
	memcpy(&jprobe_saved_stack,
	       (char *) (regs->u_regs[UREG_FP] + STACK_BIAS),
	       sizeof(jprobe_saved_stack));

	regs->tpc  = (unsigned long) jp->entry;
	regs->tnpc = ((unsigned long) jp->entry) + 0x4UL;
	regs->tstate |= TSTATE_PIL;

	return 1;
}

void __kprobes jprobe_return(void)
{
	preempt_enable_no_resched();
	__asm__ __volatile__(
		".globl	jprobe_return_trap_instruction\n"
"jprobe_return_trap_instruction:\n\t"
		"ta 0x70");
}

extern void jprobe_return_trap_instruction(void);

extern void __show_regs(struct pt_regs * regs);

int __kprobes longjmp_break_handler(struct kprobe *p, struct pt_regs *regs)
{
	u32 *addr = (u32 *) regs->tpc;

	if (addr == (u32 *) jprobe_return_trap_instruction) {
		if (jprobe_saved_regs_location != regs) {
			printk("JPROBE: Current regs (%p) does not match "
			       "saved regs (%p).\n",
			       regs, jprobe_saved_regs_location);
			printk("JPROBE: Saved registers\n");
			__show_regs(jprobe_saved_regs_location);
			printk("JPROBE: Current registers\n");
			__show_regs(regs);
			BUG();
		}
		/* Restore old register state.  Do pt_regs
		 * first so that UREG_FP is the original one for
		 * the stack frame restore.
		 */
		memcpy(regs, &jprobe_saved_regs, sizeof(*regs));

		memcpy((char *) (regs->u_regs[UREG_FP] + STACK_BIAS),
		       &jprobe_saved_stack,
		       sizeof(jprobe_saved_stack));

		return 1;
	}
	return 0;
}

/* architecture specific initialization */
int arch_init_kprobes(void)
{
	return 0;
}
