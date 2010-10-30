/*
 *  linux/arch/arm/kernel/ptrace.c
 *
 *  By Ross Biro 1/23/92
 * edited by Linus Torvalds
 * ARM modifications Copyright (C) 2000 Russell King
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/mm.h>
#include <linux/smp.h>
#include <linux/ptrace.h>
#include <linux/user.h>
#include <linux/security.h>
#include <linux/init.h>
#include <linux/signal.h>
#include <linux/uaccess.h>
#include <linux/perf_event.h>
#include <linux/hw_breakpoint.h>

#include <asm/pgtable.h>
#include <asm/system.h>
#include <asm/traps.h>

#include "ptrace.h"

#define REG_PC	15
#define REG_PSR	16
/*
 * does not yet catch signals sent when the child dies.
 * in exit.c or in signal.c.
 */

#if 0
/*
 * Breakpoint SWI instruction: SWI &9F0001
 */
#define BREAKINST_ARM	0xef9f0001
#define BREAKINST_THUMB	0xdf00		/* fill this in later */
#else
/*
 * New breakpoints - use an undefined instruction.  The ARM architecture
 * reference manual guarantees that the following instruction space
 * will produce an undefined instruction exception on all CPUs:
 *
 *  ARM:   xxxx 0111 1111 xxxx xxxx xxxx 1111 xxxx
 *  Thumb: 1101 1110 xxxx xxxx
 */
#define BREAKINST_ARM	0xe7f001f0
#define BREAKINST_THUMB	0xde01
#endif

struct pt_regs_offset {
	const char *name;
	int offset;
};

#define REG_OFFSET_NAME(r) \
	{.name = #r, .offset = offsetof(struct pt_regs, ARM_##r)}
#define REG_OFFSET_END {.name = NULL, .offset = 0}

static const struct pt_regs_offset regoffset_table[] = {
	REG_OFFSET_NAME(r0),
	REG_OFFSET_NAME(r1),
	REG_OFFSET_NAME(r2),
	REG_OFFSET_NAME(r3),
	REG_OFFSET_NAME(r4),
	REG_OFFSET_NAME(r5),
	REG_OFFSET_NAME(r6),
	REG_OFFSET_NAME(r7),
	REG_OFFSET_NAME(r8),
	REG_OFFSET_NAME(r9),
	REG_OFFSET_NAME(r10),
	REG_OFFSET_NAME(fp),
	REG_OFFSET_NAME(ip),
	REG_OFFSET_NAME(sp),
	REG_OFFSET_NAME(lr),
	REG_OFFSET_NAME(pc),
	REG_OFFSET_NAME(cpsr),
	REG_OFFSET_NAME(ORIG_r0),
	REG_OFFSET_END,
};

/**
 * regs_query_register_offset() - query register offset from its name
 * @name:	the name of a register
 *
 * regs_query_register_offset() returns the offset of a register in struct
 * pt_regs from its name. If the name is invalid, this returns -EINVAL;
 */
int regs_query_register_offset(const char *name)
{
	const struct pt_regs_offset *roff;
	for (roff = regoffset_table; roff->name != NULL; roff++)
		if (!strcmp(roff->name, name))
			return roff->offset;
	return -EINVAL;
}

/**
 * regs_query_register_name() - query register name from its offset
 * @offset:	the offset of a register in struct pt_regs.
 *
 * regs_query_register_name() returns the name of a register from its
 * offset in struct pt_regs. If the @offset is invalid, this returns NULL;
 */
const char *regs_query_register_name(unsigned int offset)
{
	const struct pt_regs_offset *roff;
	for (roff = regoffset_table; roff->name != NULL; roff++)
		if (roff->offset == offset)
			return roff->name;
	return NULL;
}

/**
 * regs_within_kernel_stack() - check the address in the stack
 * @regs:      pt_regs which contains kernel stack pointer.
 * @addr:      address which is checked.
 *
 * regs_within_kernel_stack() checks @addr is within the kernel stack page(s).
 * If @addr is within the kernel stack, it returns true. If not, returns false.
 */
bool regs_within_kernel_stack(struct pt_regs *regs, unsigned long addr)
{
	return ((addr & ~(THREAD_SIZE - 1))  ==
		(kernel_stack_pointer(regs) & ~(THREAD_SIZE - 1)));
}

/**
 * regs_get_kernel_stack_nth() - get Nth entry of the stack
 * @regs:	pt_regs which contains kernel stack pointer.
 * @n:		stack entry number.
 *
 * regs_get_kernel_stack_nth() returns @n th entry of the kernel stack which
 * is specified by @regs. If the @n th entry is NOT in the kernel stack,
 * this returns 0.
 */
unsigned long regs_get_kernel_stack_nth(struct pt_regs *regs, unsigned int n)
{
	unsigned long *addr = (unsigned long *)kernel_stack_pointer(regs);
	addr += n;
	if (regs_within_kernel_stack(regs, (unsigned long)addr))
		return *addr;
	else
		return 0;
}

/*
 * this routine will get a word off of the processes privileged stack.
 * the offset is how far from the base addr as stored in the THREAD.
 * this routine assumes that all the privileged stacks are in our
 * data space.
 */
static inline long get_user_reg(struct task_struct *task, int offset)
{
	return task_pt_regs(task)->uregs[offset];
}

/*
 * this routine will put a word on the processes privileged stack.
 * the offset is how far from the base addr as stored in the THREAD.
 * this routine assumes that all the privileged stacks are in our
 * data space.
 */
static inline int
put_user_reg(struct task_struct *task, int offset, long data)
{
	struct pt_regs newregs, *regs = task_pt_regs(task);
	int ret = -EINVAL;

	newregs = *regs;
	newregs.uregs[offset] = data;

	if (valid_user_regs(&newregs)) {
		regs->uregs[offset] = data;
		ret = 0;
	}

	return ret;
}

static inline int
read_u32(struct task_struct *task, unsigned long addr, u32 *res)
{
	int ret;

	ret = access_process_vm(task, addr, res, sizeof(*res), 0);

	return ret == sizeof(*res) ? 0 : -EIO;
}

static inline int
read_instr(struct task_struct *task, unsigned long addr, u32 *res)
{
	int ret;

	if (addr & 1) {
		u16 val;
		ret = access_process_vm(task, addr & ~1, &val, sizeof(val), 0);
		ret = ret == sizeof(val) ? 0 : -EIO;
		*res = val;
	} else {
		u32 val;
		ret = access_process_vm(task, addr & ~3, &val, sizeof(val), 0);
		ret = ret == sizeof(val) ? 0 : -EIO;
		*res = val;
	}
	return ret;
}

/*
 * Get value of register `rn' (in the instruction)
 */
static unsigned long
ptrace_getrn(struct task_struct *child, unsigned long insn)
{
	unsigned int reg = (insn >> 16) & 15;
	unsigned long val;

	val = get_user_reg(child, reg);
	if (reg == 15)
		val += 8;

	return val;
}

/*
 * Get value of operand 2 (in an ALU instruction)
 */
static unsigned long
ptrace_getaluop2(struct task_struct *child, unsigned long insn)
{
	unsigned long val;
	int shift;
	int type;

	if (insn & 1 << 25) {
		val = insn & 255;
		shift = (insn >> 8) & 15;
		type = 3;
	} else {
		val = get_user_reg (child, insn & 15);

		if (insn & (1 << 4))
			shift = (int)get_user_reg (child, (insn >> 8) & 15);
		else
			shift = (insn >> 7) & 31;

		type = (insn >> 5) & 3;
	}

	switch (type) {
	case 0:	val <<= shift;	break;
	case 1:	val >>= shift;	break;
	case 2:
		val = (((signed long)val) >> shift);
		break;
	case 3:
 		val = (val >> shift) | (val << (32 - shift));
		break;
	}
	return val;
}

/*
 * Get value of operand 2 (in a LDR instruction)
 */
static unsigned long
ptrace_getldrop2(struct task_struct *child, unsigned long insn)
{
	unsigned long val;
	int shift;
	int type;

	val = get_user_reg(child, insn & 15);
	shift = (insn >> 7) & 31;
	type = (insn >> 5) & 3;

	switch (type) {
	case 0:	val <<= shift;	break;
	case 1:	val >>= shift;	break;
	case 2:
		val = (((signed long)val) >> shift);
		break;
	case 3:
 		val = (val >> shift) | (val << (32 - shift));
		break;
	}
	return val;
}

#define OP_MASK	0x01e00000
#define OP_AND	0x00000000
#define OP_EOR	0x00200000
#define OP_SUB	0x00400000
#define OP_RSB	0x00600000
#define OP_ADD	0x00800000
#define OP_ADC	0x00a00000
#define OP_SBC	0x00c00000
#define OP_RSC	0x00e00000
#define OP_ORR	0x01800000
#define OP_MOV	0x01a00000
#define OP_BIC	0x01c00000
#define OP_MVN	0x01e00000

static unsigned long
get_branch_address(struct task_struct *child, unsigned long pc, unsigned long insn)
{
	u32 alt = 0;

	switch (insn & 0x0e000000) {
	case 0x00000000:
	case 0x02000000: {
		/*
		 * data processing
		 */
		long aluop1, aluop2, ccbit;

	        if ((insn & 0x0fffffd0) == 0x012fff10) {
		        /*
			 * bx or blx
			 */
			alt = get_user_reg(child, insn & 15);
			break;
		}


		if ((insn & 0xf000) != 0xf000)
			break;

		aluop1 = ptrace_getrn(child, insn);
		aluop2 = ptrace_getaluop2(child, insn);
		ccbit  = get_user_reg(child, REG_PSR) & PSR_C_BIT ? 1 : 0;

		switch (insn & OP_MASK) {
		case OP_AND: alt = aluop1 & aluop2;		break;
		case OP_EOR: alt = aluop1 ^ aluop2;		break;
		case OP_SUB: alt = aluop1 - aluop2;		break;
		case OP_RSB: alt = aluop2 - aluop1;		break;
		case OP_ADD: alt = aluop1 + aluop2;		break;
		case OP_ADC: alt = aluop1 + aluop2 + ccbit;	break;
		case OP_SBC: alt = aluop1 - aluop2 + ccbit;	break;
		case OP_RSC: alt = aluop2 - aluop1 + ccbit;	break;
		case OP_ORR: alt = aluop1 | aluop2;		break;
		case OP_MOV: alt = aluop2;			break;
		case OP_BIC: alt = aluop1 & ~aluop2;		break;
		case OP_MVN: alt = ~aluop2;			break;
		}
		break;
	}

	case 0x04000000:
	case 0x06000000:
		/*
		 * ldr
		 */
		if ((insn & 0x0010f000) == 0x0010f000) {
			unsigned long base;

			base = ptrace_getrn(child, insn);
			if (insn & 1 << 24) {
				long aluop2;

				if (insn & 0x02000000)
					aluop2 = ptrace_getldrop2(child, insn);
				else
					aluop2 = insn & 0xfff;

				if (insn & 1 << 23)
					base += aluop2;
				else
					base -= aluop2;
			}
			read_u32(child, base, &alt);
		}
		break;

	case 0x08000000:
		/*
		 * ldm
		 */
		if ((insn & 0x00108000) == 0x00108000) {
			unsigned long base;
			unsigned int nr_regs;

			if (insn & (1 << 23)) {
				nr_regs = hweight16(insn & 65535) << 2;

				if (!(insn & (1 << 24)))
					nr_regs -= 4;
			} else {
				if (insn & (1 << 24))
					nr_regs = -4;
				else
					nr_regs = 0;
			}

			base = ptrace_getrn(child, insn);

			read_u32(child, base + nr_regs, &alt);
			break;
		}
		break;

	case 0x0a000000: {
		/*
		 * bl or b
		 */
		signed long displ;
		/* It's a branch/branch link: instead of trying to
		 * figure out whether the branch will be taken or not,
		 * we'll put a breakpoint at both locations.  This is
		 * simpler, more reliable, and probably not a whole lot
		 * slower than the alternative approach of emulating the
		 * branch.
		 */
		displ = (insn & 0x00ffffff) << 8;
		displ = (displ >> 6) + 8;
		if (displ != 0 && displ != 4)
			alt = pc + displ;
	    }
	    break;
	}

	return alt;
}

static int
swap_insn(struct task_struct *task, unsigned long addr,
	  void *old_insn, void *new_insn, int size)
{
	int ret;

	ret = access_process_vm(task, addr, old_insn, size, 0);
	if (ret == size)
		ret = access_process_vm(task, addr, new_insn, size, 1);
	return ret;
}

static void
add_breakpoint(struct task_struct *task, struct debug_info *dbg, unsigned long addr)
{
	int nr = dbg->nsaved;

	if (nr < 2) {
		u32 new_insn = BREAKINST_ARM;
		int res;

		res = swap_insn(task, addr, &dbg->bp[nr].insn, &new_insn, 4);

		if (res == 4) {
			dbg->bp[nr].address = addr;
			dbg->nsaved += 1;
		}
	} else
		printk(KERN_ERR "ptrace: too many breakpoints\n");
}

/*
 * Clear one breakpoint in the user program.  We copy what the hardware
 * does and use bit 0 of the address to indicate whether this is a Thumb
 * breakpoint or an ARM breakpoint.
 */
static void clear_breakpoint(struct task_struct *task, struct debug_entry *bp)
{
	unsigned long addr = bp->address;
	union debug_insn old_insn;
	int ret;

	if (addr & 1) {
		ret = swap_insn(task, addr & ~1, &old_insn.thumb,
				&bp->insn.thumb, 2);

		if (ret != 2 || old_insn.thumb != BREAKINST_THUMB)
			printk(KERN_ERR "%s:%d: corrupted Thumb breakpoint at "
				"0x%08lx (0x%04x)\n", task->comm,
				task_pid_nr(task), addr, old_insn.thumb);
	} else {
		ret = swap_insn(task, addr & ~3, &old_insn.arm,
				&bp->insn.arm, 4);

		if (ret != 4 || old_insn.arm != BREAKINST_ARM)
			printk(KERN_ERR "%s:%d: corrupted ARM breakpoint at "
				"0x%08lx (0x%08x)\n", task->comm,
				task_pid_nr(task), addr, old_insn.arm);
	}
}

void ptrace_set_bpt(struct task_struct *child)
{
	struct pt_regs *regs;
	unsigned long pc;
	u32 insn;
	int res;

	regs = task_pt_regs(child);
	pc = instruction_pointer(regs);

	if (thumb_mode(regs)) {
		printk(KERN_WARNING "ptrace: can't handle thumb mode\n");
		return;
	}

	res = read_instr(child, pc, &insn);
	if (!res) {
		struct debug_info *dbg = &child->thread.debug;
		unsigned long alt;

		dbg->nsaved = 0;

		alt = get_branch_address(child, pc, insn);
		if (alt)
			add_breakpoint(child, dbg, alt);

		/*
		 * Note that we ignore the result of setting the above
		 * breakpoint since it may fail.  When it does, this is
		 * not so much an error, but a forewarning that we may
		 * be receiving a prefetch abort shortly.
		 *
		 * If we don't set this breakpoint here, then we can
		 * lose control of the thread during single stepping.
		 */
		if (!alt || predicate(insn) != PREDICATE_ALWAYS)
			add_breakpoint(child, dbg, pc + 4);
	}
}

/*
 * Ensure no single-step breakpoint is pending.  Returns non-zero
 * value if child was being single-stepped.
 */
void ptrace_cancel_bpt(struct task_struct *child)
{
	int i, nsaved = child->thread.debug.nsaved;

	child->thread.debug.nsaved = 0;

	if (nsaved > 2) {
		printk("ptrace_cancel_bpt: bogus nsaved: %d!\n", nsaved);
		nsaved = 2;
	}

	for (i = 0; i < nsaved; i++)
		clear_breakpoint(child, &child->thread.debug.bp[i]);
}

void user_disable_single_step(struct task_struct *task)
{
	task->ptrace &= ~PT_SINGLESTEP;
	ptrace_cancel_bpt(task);
}

void user_enable_single_step(struct task_struct *task)
{
	task->ptrace |= PT_SINGLESTEP;
}

/*
 * Called by kernel/ptrace.c when detaching..
 */
void ptrace_disable(struct task_struct *child)
{
	user_disable_single_step(child);
}

/*
 * Handle hitting a breakpoint.
 */
void ptrace_break(struct task_struct *tsk, struct pt_regs *regs)
{
	siginfo_t info;

	ptrace_cancel_bpt(tsk);

	info.si_signo = SIGTRAP;
	info.si_errno = 0;
	info.si_code  = TRAP_BRKPT;
	info.si_addr  = (void __user *)instruction_pointer(regs);

	force_sig_info(SIGTRAP, &info, tsk);
}

static int break_trap(struct pt_regs *regs, unsigned int instr)
{
	ptrace_break(current, regs);
	return 0;
}

static struct undef_hook arm_break_hook = {
	.instr_mask	= 0x0fffffff,
	.instr_val	= 0x07f001f0,
	.cpsr_mask	= PSR_T_BIT,
	.cpsr_val	= 0,
	.fn		= break_trap,
};

static struct undef_hook thumb_break_hook = {
	.instr_mask	= 0xffff,
	.instr_val	= 0xde01,
	.cpsr_mask	= PSR_T_BIT,
	.cpsr_val	= PSR_T_BIT,
	.fn		= break_trap,
};

static int thumb2_break_trap(struct pt_regs *regs, unsigned int instr)
{
	unsigned int instr2;
	void __user *pc;

	/* Check the second half of the instruction.  */
	pc = (void __user *)(instruction_pointer(regs) + 2);

	if (processor_mode(regs) == SVC_MODE) {
		instr2 = *(u16 *) pc;
	} else {
		get_user(instr2, (u16 __user *)pc);
	}

	if (instr2 == 0xa000) {
		ptrace_break(current, regs);
		return 0;
	} else {
		return 1;
	}
}

static struct undef_hook thumb2_break_hook = {
	.instr_mask	= 0xffff,
	.instr_val	= 0xf7f0,
	.cpsr_mask	= PSR_T_BIT,
	.cpsr_val	= PSR_T_BIT,
	.fn		= thumb2_break_trap,
};

static int __init ptrace_break_init(void)
{
	register_undef_hook(&arm_break_hook);
	register_undef_hook(&thumb_break_hook);
	register_undef_hook(&thumb2_break_hook);
	return 0;
}

core_initcall(ptrace_break_init);

/*
 * Read the word at offset "off" into the "struct user".  We
 * actually access the pt_regs stored on the kernel stack.
 */
static int ptrace_read_user(struct task_struct *tsk, unsigned long off,
			    unsigned long __user *ret)
{
	unsigned long tmp;

	if (off & 3 || off >= sizeof(struct user))
		return -EIO;

	tmp = 0;
	if (off == PT_TEXT_ADDR)
		tmp = tsk->mm->start_code;
	else if (off == PT_DATA_ADDR)
		tmp = tsk->mm->start_data;
	else if (off == PT_TEXT_END_ADDR)
		tmp = tsk->mm->end_code;
	else if (off < sizeof(struct pt_regs))
		tmp = get_user_reg(tsk, off >> 2);

	return put_user(tmp, ret);
}

/*
 * Write the word at offset "off" into "struct user".  We
 * actually access the pt_regs stored on the kernel stack.
 */
static int ptrace_write_user(struct task_struct *tsk, unsigned long off,
			     unsigned long val)
{
	if (off & 3 || off >= sizeof(struct user))
		return -EIO;

	if (off >= sizeof(struct pt_regs))
		return 0;

	return put_user_reg(tsk, off >> 2, val);
}

/*
 * Get all user integer registers.
 */
static int ptrace_getregs(struct task_struct *tsk, void __user *uregs)
{
	struct pt_regs *regs = task_pt_regs(tsk);

	return copy_to_user(uregs, regs, sizeof(struct pt_regs)) ? -EFAULT : 0;
}

/*
 * Set all user integer registers.
 */
static int ptrace_setregs(struct task_struct *tsk, void __user *uregs)
{
	struct pt_regs newregs;
	int ret;

	ret = -EFAULT;
	if (copy_from_user(&newregs, uregs, sizeof(struct pt_regs)) == 0) {
		struct pt_regs *regs = task_pt_regs(tsk);

		ret = -EINVAL;
		if (valid_user_regs(&newregs)) {
			*regs = newregs;
			ret = 0;
		}
	}

	return ret;
}

/*
 * Get the child FPU state.
 */
static int ptrace_getfpregs(struct task_struct *tsk, void __user *ufp)
{
	return copy_to_user(ufp, &task_thread_info(tsk)->fpstate,
			    sizeof(struct user_fp)) ? -EFAULT : 0;
}

/*
 * Set the child FPU state.
 */
static int ptrace_setfpregs(struct task_struct *tsk, void __user *ufp)
{
	struct thread_info *thread = task_thread_info(tsk);
	thread->used_cp[1] = thread->used_cp[2] = 1;
	return copy_from_user(&thread->fpstate, ufp,
			      sizeof(struct user_fp)) ? -EFAULT : 0;
}

#ifdef CONFIG_IWMMXT

/*
 * Get the child iWMMXt state.
 */
static int ptrace_getwmmxregs(struct task_struct *tsk, void __user *ufp)
{
	struct thread_info *thread = task_thread_info(tsk);

	if (!test_ti_thread_flag(thread, TIF_USING_IWMMXT))
		return -ENODATA;
	iwmmxt_task_disable(thread);  /* force it to ram */
	return copy_to_user(ufp, &thread->fpstate.iwmmxt, IWMMXT_SIZE)
		? -EFAULT : 0;
}

/*
 * Set the child iWMMXt state.
 */
static int ptrace_setwmmxregs(struct task_struct *tsk, void __user *ufp)
{
	struct thread_info *thread = task_thread_info(tsk);

	if (!test_ti_thread_flag(thread, TIF_USING_IWMMXT))
		return -EACCES;
	iwmmxt_task_release(thread);  /* force a reload */
	return copy_from_user(&thread->fpstate.iwmmxt, ufp, IWMMXT_SIZE)
		? -EFAULT : 0;
}

#endif

#ifdef CONFIG_CRUNCH
/*
 * Get the child Crunch state.
 */
static int ptrace_getcrunchregs(struct task_struct *tsk, void __user *ufp)
{
	struct thread_info *thread = task_thread_info(tsk);

	crunch_task_disable(thread);  /* force it to ram */
	return copy_to_user(ufp, &thread->crunchstate, CRUNCH_SIZE)
		? -EFAULT : 0;
}

/*
 * Set the child Crunch state.
 */
static int ptrace_setcrunchregs(struct task_struct *tsk, void __user *ufp)
{
	struct thread_info *thread = task_thread_info(tsk);

	crunch_task_release(thread);  /* force a reload */
	return copy_from_user(&thread->crunchstate, ufp, CRUNCH_SIZE)
		? -EFAULT : 0;
}
#endif

#ifdef CONFIG_VFP
/*
 * Get the child VFP state.
 */
static int ptrace_getvfpregs(struct task_struct *tsk, void __user *data)
{
	struct thread_info *thread = task_thread_info(tsk);
	union vfp_state *vfp = &thread->vfpstate;
	struct user_vfp __user *ufp = data;

	vfp_sync_hwstate(thread);

	/* copy the floating point registers */
	if (copy_to_user(&ufp->fpregs, &vfp->hard.fpregs,
			 sizeof(vfp->hard.fpregs)))
		return -EFAULT;

	/* copy the status and control register */
	if (put_user(vfp->hard.fpscr, &ufp->fpscr))
		return -EFAULT;

	return 0;
}

/*
 * Set the child VFP state.
 */
static int ptrace_setvfpregs(struct task_struct *tsk, void __user *data)
{
	struct thread_info *thread = task_thread_info(tsk);
	union vfp_state *vfp = &thread->vfpstate;
	struct user_vfp __user *ufp = data;

	vfp_sync_hwstate(thread);

	/* copy the floating point registers */
	if (copy_from_user(&vfp->hard.fpregs, &ufp->fpregs,
			   sizeof(vfp->hard.fpregs)))
		return -EFAULT;

	/* copy the status and control register */
	if (get_user(vfp->hard.fpscr, &ufp->fpscr))
		return -EFAULT;

	vfp_flush_hwstate(thread);

	return 0;
}
#endif

#ifdef CONFIG_HAVE_HW_BREAKPOINT
/*
 * Convert a virtual register number into an index for a thread_info
 * breakpoint array. Breakpoints are identified using positive numbers
 * whilst watchpoints are negative. The registers are laid out as pairs
 * of (address, control), each pair mapping to a unique hw_breakpoint struct.
 * Register 0 is reserved for describing resource information.
 */
static int ptrace_hbp_num_to_idx(long num)
{
	if (num < 0)
		num = (ARM_MAX_BRP << 1) - num;
	return (num - 1) >> 1;
}

/*
 * Returns the virtual register number for the address of the
 * breakpoint at index idx.
 */
static long ptrace_hbp_idx_to_num(int idx)
{
	long mid = ARM_MAX_BRP << 1;
	long num = (idx << 1) + 1;
	return num > mid ? mid - num : num;
}

/*
 * Handle hitting a HW-breakpoint.
 */
static void ptrace_hbptriggered(struct perf_event *bp, int unused,
				     struct perf_sample_data *data,
				     struct pt_regs *regs)
{
	struct arch_hw_breakpoint *bkpt = counter_arch_bp(bp);
	long num;
	int i;
	siginfo_t info;

	for (i = 0; i < ARM_MAX_HBP_SLOTS; ++i)
		if (current->thread.debug.hbp[i] == bp)
			break;

	num = (i == ARM_MAX_HBP_SLOTS) ? 0 : ptrace_hbp_idx_to_num(i);

	info.si_signo	= SIGTRAP;
	info.si_errno	= (int)num;
	info.si_code	= TRAP_HWBKPT;
	info.si_addr	= (void __user *)(bkpt->trigger);

	force_sig_info(SIGTRAP, &info, current);
}

/*
 * Set ptrace breakpoint pointers to zero for this task.
 * This is required in order to prevent child processes from unregistering
 * breakpoints held by their parent.
 */
void clear_ptrace_hw_breakpoint(struct task_struct *tsk)
{
	memset(tsk->thread.debug.hbp, 0, sizeof(tsk->thread.debug.hbp));
}

/*
 * Unregister breakpoints from this task and reset the pointers in
 * the thread_struct.
 */
void flush_ptrace_hw_breakpoint(struct task_struct *tsk)
{
	int i;
	struct thread_struct *t = &tsk->thread;

	for (i = 0; i < ARM_MAX_HBP_SLOTS; i++) {
		if (t->debug.hbp[i]) {
			unregister_hw_breakpoint(t->debug.hbp[i]);
			t->debug.hbp[i] = NULL;
		}
	}
}

static u32 ptrace_get_hbp_resource_info(void)
{
	u8 num_brps, num_wrps, debug_arch, wp_len;
	u32 reg = 0;

	num_brps	= hw_breakpoint_slots(TYPE_INST);
	num_wrps	= hw_breakpoint_slots(TYPE_DATA);
	debug_arch	= arch_get_debug_arch();
	wp_len		= arch_get_max_wp_len();

	reg		|= debug_arch;
	reg		<<= 8;
	reg		|= wp_len;
	reg		<<= 8;
	reg		|= num_wrps;
	reg		<<= 8;
	reg		|= num_brps;

	return reg;
}

static struct perf_event *ptrace_hbp_create(struct task_struct *tsk, int type)
{
	struct perf_event_attr attr;

	ptrace_breakpoint_init(&attr);

	/* Initialise fields to sane defaults. */
	attr.bp_addr	= 0;
	attr.bp_len	= HW_BREAKPOINT_LEN_4;
	attr.bp_type	= type;
	attr.disabled	= 1;

	return register_user_hw_breakpoint(&attr, ptrace_hbptriggered, tsk);
}

static int ptrace_gethbpregs(struct task_struct *tsk, long num,
			     unsigned long  __user *data)
{
	u32 reg;
	int idx, ret = 0;
	struct perf_event *bp;
	struct arch_hw_breakpoint_ctrl arch_ctrl;

	if (num == 0) {
		reg = ptrace_get_hbp_resource_info();
	} else {
		idx = ptrace_hbp_num_to_idx(num);
		if (idx < 0 || idx >= ARM_MAX_HBP_SLOTS) {
			ret = -EINVAL;
			goto out;
		}

		bp = tsk->thread.debug.hbp[idx];
		if (!bp) {
			reg = 0;
			goto put;
		}

		arch_ctrl = counter_arch_bp(bp)->ctrl;

		/*
		 * Fix up the len because we may have adjusted it
		 * to compensate for an unaligned address.
		 */
		while (!(arch_ctrl.len & 0x1))
			arch_ctrl.len >>= 1;

		if (idx & 0x1)
			reg = encode_ctrl_reg(arch_ctrl);
		else
			reg = bp->attr.bp_addr;
	}

put:
	if (put_user(reg, data))
		ret = -EFAULT;

out:
	return ret;
}

static int ptrace_sethbpregs(struct task_struct *tsk, long num,
			     unsigned long __user *data)
{
	int idx, gen_len, gen_type, implied_type, ret = 0;
	u32 user_val;
	struct perf_event *bp;
	struct arch_hw_breakpoint_ctrl ctrl;
	struct perf_event_attr attr;

	if (num == 0)
		goto out;
	else if (num < 0)
		implied_type = HW_BREAKPOINT_RW;
	else
		implied_type = HW_BREAKPOINT_X;

	idx = ptrace_hbp_num_to_idx(num);
	if (idx < 0 || idx >= ARM_MAX_HBP_SLOTS) {
		ret = -EINVAL;
		goto out;
	}

	if (get_user(user_val, data)) {
		ret = -EFAULT;
		goto out;
	}

	bp = tsk->thread.debug.hbp[idx];
	if (!bp) {
		bp = ptrace_hbp_create(tsk, implied_type);
		if (IS_ERR(bp)) {
			ret = PTR_ERR(bp);
			goto out;
		}
		tsk->thread.debug.hbp[idx] = bp;
	}

	attr = bp->attr;

	if (num & 0x1) {
		/* Address */
		attr.bp_addr	= user_val;
	} else {
		/* Control */
		decode_ctrl_reg(user_val, &ctrl);
		ret = arch_bp_generic_fields(ctrl, &gen_len, &gen_type);
		if (ret)
			goto out;

		if ((gen_type & implied_type) != gen_type) {
				ret = -EINVAL;
				goto out;
		}

		attr.bp_len	= gen_len;
		attr.bp_type	= gen_type;
		attr.disabled	= !ctrl.enabled;
	}

	ret = modify_user_hw_breakpoint(bp, &attr);
out:
	return ret;
}
#endif

long arch_ptrace(struct task_struct *child, long request, long addr, long data)
{
	int ret;

	switch (request) {
		case PTRACE_PEEKUSR:
			ret = ptrace_read_user(child, addr, (unsigned long __user *)data);
			break;

		case PTRACE_POKEUSR:
			ret = ptrace_write_user(child, addr, data);
			break;

		case PTRACE_GETREGS:
			ret = ptrace_getregs(child, (void __user *)data);
			break;

		case PTRACE_SETREGS:
			ret = ptrace_setregs(child, (void __user *)data);
			break;

		case PTRACE_GETFPREGS:
			ret = ptrace_getfpregs(child, (void __user *)data);
			break;
		
		case PTRACE_SETFPREGS:
			ret = ptrace_setfpregs(child, (void __user *)data);
			break;

#ifdef CONFIG_IWMMXT
		case PTRACE_GETWMMXREGS:
			ret = ptrace_getwmmxregs(child, (void __user *)data);
			break;

		case PTRACE_SETWMMXREGS:
			ret = ptrace_setwmmxregs(child, (void __user *)data);
			break;
#endif

		case PTRACE_GET_THREAD_AREA:
			ret = put_user(task_thread_info(child)->tp_value,
				       (unsigned long __user *) data);
			break;

		case PTRACE_SET_SYSCALL:
			task_thread_info(child)->syscall = data;
			ret = 0;
			break;

#ifdef CONFIG_CRUNCH
		case PTRACE_GETCRUNCHREGS:
			ret = ptrace_getcrunchregs(child, (void __user *)data);
			break;

		case PTRACE_SETCRUNCHREGS:
			ret = ptrace_setcrunchregs(child, (void __user *)data);
			break;
#endif

#ifdef CONFIG_VFP
		case PTRACE_GETVFPREGS:
			ret = ptrace_getvfpregs(child, (void __user *)data);
			break;

		case PTRACE_SETVFPREGS:
			ret = ptrace_setvfpregs(child, (void __user *)data);
			break;
#endif

#ifdef CONFIG_HAVE_HW_BREAKPOINT
		case PTRACE_GETHBPREGS:
			ret = ptrace_gethbpregs(child, addr,
						(unsigned long __user *)data);
			break;
		case PTRACE_SETHBPREGS:
			ret = ptrace_sethbpregs(child, addr,
						(unsigned long __user *)data);
			break;
#endif

		default:
			ret = ptrace_request(child, request, addr, data);
			break;
	}

	return ret;
}

asmlinkage int syscall_trace(int why, struct pt_regs *regs, int scno)
{
	unsigned long ip;

	if (!test_thread_flag(TIF_SYSCALL_TRACE))
		return scno;
	if (!(current->ptrace & PT_PTRACED))
		return scno;

	/*
	 * Save IP.  IP is used to denote syscall entry/exit:
	 *  IP = 0 -> entry, = 1 -> exit
	 */
	ip = regs->ARM_ip;
	regs->ARM_ip = why;

	current_thread_info()->syscall = scno;

	/* the 0x80 provides a way for the tracing parent to distinguish
	   between a syscall stop and SIGTRAP delivery */
	ptrace_notify(SIGTRAP | ((current->ptrace & PT_TRACESYSGOOD)
				 ? 0x80 : 0));
	/*
	 * this isn't the same as continuing with a signal, but it will do
	 * for normal use.  strace only continues with a signal if the
	 * stopping signal is not SIGTRAP.  -brl
	 */
	if (current->exit_code) {
		send_sig(current->exit_code, current, 1);
		current->exit_code = 0;
	}
	regs->ARM_ip = ip;

	return current_thread_info()->syscall;
}
