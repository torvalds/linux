// SPDX-License-Identifier: GPL-2.0
/*
 * 'traps.c' handles hardware traps and faults after we have saved some
 * state in 'entry.S'.
 *
 *  SuperH version: Copyright (C) 1999 Niibe Yutaka
 *                  Copyright (C) 2000 Philipp Rumpf
 *                  Copyright (C) 2000 David Howells
 *                  Copyright (C) 2002 - 2010 Paul Mundt
 */
#include <linux/kernel.h>
#include <linux/ptrace.h>
#include <linux/hardirq.h>
#include <linux/init.h>
#include <linux/spinlock.h>
#include <linux/kallsyms.h>
#include <linux/io.h>
#include <linux/bug.h>
#include <linux/debug_locks.h>
#include <linux/kdebug.h>
#include <linux/limits.h>
#include <linux/sysfs.h>
#include <linux/uaccess.h>
#include <linux/perf_event.h>
#include <linux/sched/task_stack.h>

#include <asm/alignment.h>
#include <asm/fpu.h>
#include <asm/kprobes.h>
#include <asm/traps.h>
#include <asm/bl_bit.h>

#ifdef CONFIG_CPU_SH2
# define TRAP_RESERVED_INST	4
# define TRAP_ILLEGAL_SLOT_INST	6
# define TRAP_ADDRESS_ERROR	9
# ifdef CONFIG_CPU_SH2A
#  define TRAP_UBC		12
#  define TRAP_FPU_ERROR	13
#  define TRAP_DIVZERO_ERROR	17
#  define TRAP_DIVOVF_ERROR	18
# endif
#else
#define TRAP_RESERVED_INST	12
#define TRAP_ILLEGAL_SLOT_INST	13
#endif

static inline void sign_extend(unsigned int count, unsigned char *dst)
{
#ifdef __LITTLE_ENDIAN__
	if ((count == 1) && dst[0] & 0x80) {
		dst[1] = 0xff;
		dst[2] = 0xff;
		dst[3] = 0xff;
	}
	if ((count == 2) && dst[1] & 0x80) {
		dst[2] = 0xff;
		dst[3] = 0xff;
	}
#else
	if ((count == 1) && dst[3] & 0x80) {
		dst[2] = 0xff;
		dst[1] = 0xff;
		dst[0] = 0xff;
	}
	if ((count == 2) && dst[2] & 0x80) {
		dst[1] = 0xff;
		dst[0] = 0xff;
	}
#endif
}

static struct mem_access user_mem_access = {
	copy_from_user,
	copy_to_user,
};

static unsigned long copy_from_kernel_wrapper(void *dst, const void __user *src,
					      unsigned long cnt)
{
	return copy_from_kernel_nofault(dst, (const void __force *)src, cnt);
}

static unsigned long copy_to_kernel_wrapper(void __user *dst, const void *src,
					    unsigned long cnt)
{
	return copy_to_kernel_nofault((void __force *)dst, src, cnt);
}

static struct mem_access kernel_mem_access = {
	copy_from_kernel_wrapper,
	copy_to_kernel_wrapper,
};

/*
 * handle an instruction that does an unaligned memory access by emulating the
 * desired behaviour
 * - note that PC _may not_ point to the faulting instruction
 *   (if that instruction is in a branch delay slot)
 * - return 0 if emulation okay, -EFAULT on existential error
 */
static int handle_unaligned_ins(insn_size_t instruction, struct pt_regs *regs,
				struct mem_access *ma)
{
	int ret, index, count;
	unsigned long *rm, *rn;
	unsigned char *src, *dst;
	unsigned char __user *srcu, *dstu;

	index = (instruction>>8)&15;	/* 0x0F00 */
	rn = &regs->regs[index];

	index = (instruction>>4)&15;	/* 0x00F0 */
	rm = &regs->regs[index];

	count = 1<<(instruction&3);

	switch (count) {
	case 1: inc_unaligned_byte_access(); break;
	case 2: inc_unaligned_word_access(); break;
	case 4: inc_unaligned_dword_access(); break;
	case 8: inc_unaligned_multi_access(); break;
	}

	ret = -EFAULT;
	switch (instruction>>12) {
	case 0: /* mov.[bwl] to/from memory via r0+rn */
		if (instruction & 8) {
			/* from memory */
			srcu = (unsigned char __user *)*rm;
			srcu += regs->regs[0];
			dst = (unsigned char *)rn;
			*(unsigned long *)dst = 0;

#if !defined(__LITTLE_ENDIAN__)
			dst += 4-count;
#endif
			if (ma->from(dst, srcu, count))
				goto fetch_fault;

			sign_extend(count, dst);
		} else {
			/* to memory */
			src = (unsigned char *)rm;
#if !defined(__LITTLE_ENDIAN__)
			src += 4-count;
#endif
			dstu = (unsigned char __user *)*rn;
			dstu += regs->regs[0];

			if (ma->to(dstu, src, count))
				goto fetch_fault;
		}
		ret = 0;
		break;

	case 1: /* mov.l Rm,@(disp,Rn) */
		src = (unsigned char*) rm;
		dstu = (unsigned char __user *)*rn;
		dstu += (instruction&0x000F)<<2;

		if (ma->to(dstu, src, 4))
			goto fetch_fault;
		ret = 0;
		break;

	case 2: /* mov.[bwl] to memory, possibly with pre-decrement */
		if (instruction & 4)
			*rn -= count;
		src = (unsigned char*) rm;
		dstu = (unsigned char __user *)*rn;
#if !defined(__LITTLE_ENDIAN__)
		src += 4-count;
#endif
		if (ma->to(dstu, src, count))
			goto fetch_fault;
		ret = 0;
		break;

	case 5: /* mov.l @(disp,Rm),Rn */
		srcu = (unsigned char __user *)*rm;
		srcu += (instruction & 0x000F) << 2;
		dst = (unsigned char *)rn;
		*(unsigned long *)dst = 0;

		if (ma->from(dst, srcu, 4))
			goto fetch_fault;
		ret = 0;
		break;

	case 6:	/* mov.[bwl] from memory, possibly with post-increment */
		srcu = (unsigned char __user *)*rm;
		if (instruction & 4)
			*rm += count;
		dst = (unsigned char*) rn;
		*(unsigned long*)dst = 0;

#if !defined(__LITTLE_ENDIAN__)
		dst += 4-count;
#endif
		if (ma->from(dst, srcu, count))
			goto fetch_fault;
		sign_extend(count, dst);
		ret = 0;
		break;

	case 8:
		switch ((instruction&0xFF00)>>8) {
		case 0x81: /* mov.w R0,@(disp,Rn) */
			src = (unsigned char *) &regs->regs[0];
#if !defined(__LITTLE_ENDIAN__)
			src += 2;
#endif
			dstu = (unsigned char __user *)*rm; /* called Rn in the spec */
			dstu += (instruction & 0x000F) << 1;

			if (ma->to(dstu, src, 2))
				goto fetch_fault;
			ret = 0;
			break;

		case 0x85: /* mov.w @(disp,Rm),R0 */
			srcu = (unsigned char __user *)*rm;
			srcu += (instruction & 0x000F) << 1;
			dst = (unsigned char *) &regs->regs[0];
			*(unsigned long *)dst = 0;

#if !defined(__LITTLE_ENDIAN__)
			dst += 2;
#endif
			if (ma->from(dst, srcu, 2))
				goto fetch_fault;
			sign_extend(2, dst);
			ret = 0;
			break;
		}
		break;

	case 9: /* mov.w @(disp,PC),Rn */
		srcu = (unsigned char __user *)regs->pc;
		srcu += 4;
		srcu += (instruction & 0x00FF) << 1;
		dst = (unsigned char *)rn;
		*(unsigned long *)dst = 0;

#if !defined(__LITTLE_ENDIAN__)
		dst += 2;
#endif

		if (ma->from(dst, srcu, 2))
			goto fetch_fault;
		sign_extend(2, dst);
		ret = 0;
		break;

	case 0xd: /* mov.l @(disp,PC),Rn */
		srcu = (unsigned char __user *)(regs->pc & ~0x3);
		srcu += 4;
		srcu += (instruction & 0x00FF) << 2;
		dst = (unsigned char *)rn;
		*(unsigned long *)dst = 0;

		if (ma->from(dst, srcu, 4))
			goto fetch_fault;
		ret = 0;
		break;
	}
	return ret;

 fetch_fault:
	/* Argh. Address not only misaligned but also non-existent.
	 * Raise an EFAULT and see if it's trapped
	 */
	die_if_no_fixup("Fault in unaligned fixup", regs, 0);
	return -EFAULT;
}

/*
 * emulate the instruction in the delay slot
 * - fetches the instruction from PC+2
 */
static inline int handle_delayslot(struct pt_regs *regs,
				   insn_size_t old_instruction,
				   struct mem_access *ma)
{
	insn_size_t instruction;
	void __user *addr = (void __user *)(regs->pc +
		instruction_size(old_instruction));

	if (copy_from_user(&instruction, addr, sizeof(instruction))) {
		/* the instruction-fetch faulted */
		if (user_mode(regs))
			return -EFAULT;

		/* kernel */
		die("delay-slot-insn faulting in handle_unaligned_delayslot",
		    regs, 0);
	}

	return handle_unaligned_ins(instruction, regs, ma);
}

/*
 * handle an instruction that does an unaligned memory access
 * - have to be careful of branch delay-slot instructions that fault
 *  SH3:
 *   - if the branch would be taken PC points to the branch
 *   - if the branch would not be taken, PC points to delay-slot
 *  SH4:
 *   - PC always points to delayed branch
 * - return 0 if handled, -EFAULT if failed (may not return if in kernel)
 */

/* Macros to determine offset from current PC for branch instructions */
/* Explicit type coercion is used to force sign extension where needed */
#define SH_PC_8BIT_OFFSET(instr) ((((signed char)(instr))*2) + 4)
#define SH_PC_12BIT_OFFSET(instr) ((((signed short)(instr<<4))>>3) + 4)

int handle_unaligned_access(insn_size_t instruction, struct pt_regs *regs,
			    struct mem_access *ma, int expected,
			    unsigned long address)
{
	u_int rm;
	int ret, index;

	/*
	 * XXX: We can't handle mixed 16/32-bit instructions yet
	 */
	if (instruction_size(instruction) != 2)
		return -EINVAL;

	index = (instruction>>8)&15;	/* 0x0F00 */
	rm = regs->regs[index];

	/*
	 * Log the unexpected fixups, and then pass them on to perf.
	 *
	 * We intentionally don't report the expected cases to perf as
	 * otherwise the trapped I/O case will skew the results too much
	 * to be useful.
	 */
	if (!expected) {
		unaligned_fixups_notify(current, instruction, regs);
		perf_sw_event(PERF_COUNT_SW_ALIGNMENT_FAULTS, 1,
			      regs, address);
	}

	ret = -EFAULT;
	switch (instruction&0xF000) {
	case 0x0000:
		if (instruction==0x000B) {
			/* rts */
			ret = handle_delayslot(regs, instruction, ma);
			if (ret==0)
				regs->pc = regs->pr;
		}
		else if ((instruction&0x00FF)==0x0023) {
			/* braf @Rm */
			ret = handle_delayslot(regs, instruction, ma);
			if (ret==0)
				regs->pc += rm + 4;
		}
		else if ((instruction&0x00FF)==0x0003) {
			/* bsrf @Rm */
			ret = handle_delayslot(regs, instruction, ma);
			if (ret==0) {
				regs->pr = regs->pc + 4;
				regs->pc += rm + 4;
			}
		}
		else {
			/* mov.[bwl] to/from memory via r0+rn */
			goto simple;
		}
		break;

	case 0x1000: /* mov.l Rm,@(disp,Rn) */
		goto simple;

	case 0x2000: /* mov.[bwl] to memory, possibly with pre-decrement */
		goto simple;

	case 0x4000:
		if ((instruction&0x00FF)==0x002B) {
			/* jmp @Rm */
			ret = handle_delayslot(regs, instruction, ma);
			if (ret==0)
				regs->pc = rm;
		}
		else if ((instruction&0x00FF)==0x000B) {
			/* jsr @Rm */
			ret = handle_delayslot(regs, instruction, ma);
			if (ret==0) {
				regs->pr = regs->pc + 4;
				regs->pc = rm;
			}
		}
		else {
			/* mov.[bwl] to/from memory via r0+rn */
			goto simple;
		}
		break;

	case 0x5000: /* mov.l @(disp,Rm),Rn */
		goto simple;

	case 0x6000: /* mov.[bwl] from memory, possibly with post-increment */
		goto simple;

	case 0x8000: /* bf lab, bf/s lab, bt lab, bt/s lab */
		switch (instruction&0x0F00) {
		case 0x0100: /* mov.w R0,@(disp,Rm) */
			goto simple;
		case 0x0500: /* mov.w @(disp,Rm),R0 */
			goto simple;
		case 0x0B00: /* bf   lab - no delayslot*/
			ret = 0;
			break;
		case 0x0F00: /* bf/s lab */
			ret = handle_delayslot(regs, instruction, ma);
			if (ret==0) {
#if defined(CONFIG_CPU_SH4) || defined(CONFIG_SH7705_CACHE_32KB)
				if ((regs->sr & 0x00000001) != 0)
					regs->pc += 4; /* next after slot */
				else
#endif
					regs->pc += SH_PC_8BIT_OFFSET(instruction);
			}
			break;
		case 0x0900: /* bt   lab - no delayslot */
			ret = 0;
			break;
		case 0x0D00: /* bt/s lab */
			ret = handle_delayslot(regs, instruction, ma);
			if (ret==0) {
#if defined(CONFIG_CPU_SH4) || defined(CONFIG_SH7705_CACHE_32KB)
				if ((regs->sr & 0x00000001) == 0)
					regs->pc += 4; /* next after slot */
				else
#endif
					regs->pc += SH_PC_8BIT_OFFSET(instruction);
			}
			break;
		}
		break;

	case 0x9000: /* mov.w @(disp,Rm),Rn */
		goto simple;

	case 0xA000: /* bra label */
		ret = handle_delayslot(regs, instruction, ma);
		if (ret==0)
			regs->pc += SH_PC_12BIT_OFFSET(instruction);
		break;

	case 0xB000: /* bsr label */
		ret = handle_delayslot(regs, instruction, ma);
		if (ret==0) {
			regs->pr = regs->pc + 4;
			regs->pc += SH_PC_12BIT_OFFSET(instruction);
		}
		break;

	case 0xD000: /* mov.l @(disp,Rm),Rn */
		goto simple;
	}
	return ret;

	/* handle non-delay-slot instruction */
 simple:
	ret = handle_unaligned_ins(instruction, regs, ma);
	if (ret==0)
		regs->pc += instruction_size(instruction);
	return ret;
}

/*
 * Handle various address error exceptions:
 *  - instruction address error:
 *       misaligned PC
 *       PC >= 0x80000000 in user mode
 *  - data address error (read and write)
 *       misaligned data access
 *       access to >= 0x80000000 is user mode
 * Unfortuntaly we can't distinguish between instruction address error
 * and data address errors caused by read accesses.
 */
asmlinkage void do_address_error(struct pt_regs *regs,
				 unsigned long writeaccess,
				 unsigned long address)
{
	unsigned long error_code = 0;
	insn_size_t instruction;
	int tmp;

	/* Intentional ifdef */
#ifdef CONFIG_CPU_HAS_SR_RB
	error_code = lookup_exception_vector();
#endif

	if (user_mode(regs)) {
		int si_code = BUS_ADRERR;
		unsigned int user_action;

		local_irq_enable();
		inc_unaligned_user_access();

		if (copy_from_user(&instruction, (insn_size_t __user *)(regs->pc & ~1),
				   sizeof(instruction))) {
			goto uspace_segv;
		}

		/* shout about userspace fixups */
		unaligned_fixups_notify(current, instruction, regs);

		user_action = unaligned_user_action();
		if (user_action & UM_FIXUP)
			goto fixup;
		if (user_action & UM_SIGNAL)
			goto uspace_segv;
		else {
			/* ignore */
			regs->pc += instruction_size(instruction);
			return;
		}

fixup:
		/* bad PC is not something we can fix */
		if (regs->pc & 1) {
			si_code = BUS_ADRALN;
			goto uspace_segv;
		}

		tmp = handle_unaligned_access(instruction, regs,
					      &user_mem_access, 0,
					      address);

		if (tmp == 0)
			return; /* sorted */
uspace_segv:
		printk(KERN_NOTICE "Sending SIGBUS to \"%s\" due to unaligned "
		       "access (PC %lx PR %lx)\n", current->comm, regs->pc,
		       regs->pr);

		force_sig_fault(SIGBUS, si_code, (void __user *)address);
	} else {
		inc_unaligned_kernel_access();

		if (regs->pc & 1)
			die("unaligned program counter", regs, error_code);

		if (copy_from_kernel_nofault(&instruction, (void *)(regs->pc),
				   sizeof(instruction))) {
			/* Argh. Fault on the instruction itself.
			   This should never happen non-SMP
			*/
			die("insn faulting in do_address_error", regs, 0);
		}

		unaligned_fixups_notify(current, instruction, regs);

		handle_unaligned_access(instruction, regs, &kernel_mem_access,
					0, address);
	}
}

#ifdef CONFIG_SH_DSP
/*
 *	SH-DSP support gerg@snapgear.com.
 */
int is_dsp_inst(struct pt_regs *regs)
{
	unsigned short inst = 0;

	/*
	 * Safe guard if DSP mode is already enabled or we're lacking
	 * the DSP altogether.
	 */
	if (!(current_cpu_data.flags & CPU_HAS_DSP) || (regs->sr & SR_DSP))
		return 0;

	get_user(inst, ((unsigned short *) regs->pc));

	inst &= 0xf000;

	/* Check for any type of DSP or support instruction */
	if ((inst == 0xf000) || (inst == 0x4000))
		return 1;

	return 0;
}
#else
#define is_dsp_inst(regs)	(0)
#endif /* CONFIG_SH_DSP */

#ifdef CONFIG_CPU_SH2A
asmlinkage void do_divide_error(unsigned long r4)
{
	int code;

	switch (r4) {
	case TRAP_DIVZERO_ERROR:
		code = FPE_INTDIV;
		break;
	case TRAP_DIVOVF_ERROR:
		code = FPE_INTOVF;
		break;
	default:
		/* Let gcc know unhandled cases don't make it past here */
		return;
	}
	force_sig_fault(SIGFPE, code, NULL);
}
#endif

asmlinkage void do_reserved_inst(void)
{
	struct pt_regs *regs = current_pt_regs();
	unsigned long error_code;

#ifdef CONFIG_SH_FPU_EMU
	unsigned short inst = 0;
	int err;

	get_user(inst, (unsigned short __user *)regs->pc);

	err = do_fpu_inst(inst, regs);
	if (!err) {
		regs->pc += instruction_size(inst);
		return;
	}
	/* not a FPU inst. */
#endif

#ifdef CONFIG_SH_DSP
	/* Check if it's a DSP instruction */
	if (is_dsp_inst(regs)) {
		/* Enable DSP mode, and restart instruction. */
		regs->sr |= SR_DSP;
		/* Save DSP mode */
		current->thread.dsp_status.status |= SR_DSP;
		return;
	}
#endif

	error_code = lookup_exception_vector();

	local_irq_enable();
	force_sig(SIGILL);
	die_if_no_fixup("reserved instruction", regs, error_code);
}

#ifdef CONFIG_SH_FPU_EMU
static int emulate_branch(unsigned short inst, struct pt_regs *regs)
{
	/*
	 * bfs: 8fxx: PC+=d*2+4;
	 * bts: 8dxx: PC+=d*2+4;
	 * bra: axxx: PC+=D*2+4;
	 * bsr: bxxx: PC+=D*2+4  after PR=PC+4;
	 * braf:0x23: PC+=Rn*2+4;
	 * bsrf:0x03: PC+=Rn*2+4 after PR=PC+4;
	 * jmp: 4x2b: PC=Rn;
	 * jsr: 4x0b: PC=Rn      after PR=PC+4;
	 * rts: 000b: PC=PR;
	 */
	if (((inst & 0xf000) == 0xb000)  ||	/* bsr */
	    ((inst & 0xf0ff) == 0x0003)  ||	/* bsrf */
	    ((inst & 0xf0ff) == 0x400b))	/* jsr */
		regs->pr = regs->pc + 4;

	if ((inst & 0xfd00) == 0x8d00) {	/* bfs, bts */
		regs->pc += SH_PC_8BIT_OFFSET(inst);
		return 0;
	}

	if ((inst & 0xe000) == 0xa000) {	/* bra, bsr */
		regs->pc += SH_PC_12BIT_OFFSET(inst);
		return 0;
	}

	if ((inst & 0xf0df) == 0x0003) {	/* braf, bsrf */
		regs->pc += regs->regs[(inst & 0x0f00) >> 8] + 4;
		return 0;
	}

	if ((inst & 0xf0df) == 0x400b) {	/* jmp, jsr */
		regs->pc = regs->regs[(inst & 0x0f00) >> 8];
		return 0;
	}

	if ((inst & 0xffff) == 0x000b) {	/* rts */
		regs->pc = regs->pr;
		return 0;
	}

	return 1;
}
#endif

asmlinkage void do_illegal_slot_inst(void)
{
	struct pt_regs *regs = current_pt_regs();
	unsigned long inst;

	if (kprobe_handle_illslot(regs->pc) == 0)
		return;

#ifdef CONFIG_SH_FPU_EMU
	get_user(inst, (unsigned short __user *)regs->pc + 1);
	if (!do_fpu_inst(inst, regs)) {
		get_user(inst, (unsigned short __user *)regs->pc);
		if (!emulate_branch(inst, regs))
			return;
		/* fault in branch.*/
	}
	/* not a FPU inst. */
#endif

	inst = lookup_exception_vector();

	local_irq_enable();
	force_sig(SIGILL);
	die_if_no_fixup("illegal slot instruction", regs, inst);
}

asmlinkage void do_exception_error(void)
{
	long ex;

	ex = lookup_exception_vector();
	die_if_kernel("exception", current_pt_regs(), ex);
}

void per_cpu_trap_init(void)
{
	extern void *vbr_base;

	/* NOTE: The VBR value should be at P1
	   (or P2, virtural "fixed" address space).
	   It's definitely should not in physical address.  */

	asm volatile("ldc	%0, vbr"
		     : /* no output */
		     : "r" (&vbr_base)
		     : "memory");

	/* disable exception blocking now when the vbr has been setup */
	clear_bl_bit();
}

void *set_exception_table_vec(unsigned int vec, void *handler)
{
	extern void *exception_handling_table[];
	void *old_handler;

	old_handler = exception_handling_table[vec];
	exception_handling_table[vec] = handler;
	return old_handler;
}

void __init trap_init(void)
{
	set_exception_table_vec(TRAP_RESERVED_INST, do_reserved_inst);
	set_exception_table_vec(TRAP_ILLEGAL_SLOT_INST, do_illegal_slot_inst);

#if defined(CONFIG_CPU_SH4) && !defined(CONFIG_SH_FPU) || \
    defined(CONFIG_SH_FPU_EMU)
	/*
	 * For SH-4 lacking an FPU, treat floating point instructions as
	 * reserved. They'll be handled in the math-emu case, or faulted on
	 * otherwise.
	 */
	set_exception_table_evt(0x800, do_reserved_inst);
	set_exception_table_evt(0x820, do_illegal_slot_inst);
#elif defined(CONFIG_SH_FPU)
	set_exception_table_evt(0x800, fpu_state_restore_trap_handler);
	set_exception_table_evt(0x820, fpu_state_restore_trap_handler);
#endif

#ifdef CONFIG_CPU_SH2
	set_exception_table_vec(TRAP_ADDRESS_ERROR, address_error_trap_handler);
#endif
#ifdef CONFIG_CPU_SH2A
	set_exception_table_vec(TRAP_DIVZERO_ERROR, do_divide_error);
	set_exception_table_vec(TRAP_DIVOVF_ERROR, do_divide_error);
#ifdef CONFIG_SH_FPU
	set_exception_table_vec(TRAP_FPU_ERROR, fpu_error_trap_handler);
#endif
#endif

#ifdef TRAP_UBC
	set_exception_table_vec(TRAP_UBC, breakpoint_trap_handler);
#endif
}
