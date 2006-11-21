/*
 * 'traps.c' handles hardware traps and faults after we have saved some
 * state in 'entry.S'.
 *
 *  SuperH version: Copyright (C) 1999 Niibe Yutaka
 *                  Copyright (C) 2000 Philipp Rumpf
 *                  Copyright (C) 2000 David Howells
 *                  Copyright (C) 2002 - 2006 Paul Mundt
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 */
#include <linux/kernel.h>
#include <linux/ptrace.h>
#include <linux/init.h>
#include <linux/spinlock.h>
#include <linux/module.h>
#include <linux/kallsyms.h>
#include <linux/io.h>
#include <asm/system.h>
#include <asm/uaccess.h>

#ifdef CONFIG_SH_KGDB
#include <asm/kgdb.h>
#define CHK_REMOTE_DEBUG(regs)			\
{						\
	if (kgdb_debug_hook && !user_mode(regs))\
		(*kgdb_debug_hook)(regs);       \
}
#else
#define CHK_REMOTE_DEBUG(regs)
#endif

#ifdef CONFIG_CPU_SH2
# define TRAP_RESERVED_INST	4
# define TRAP_ILLEGAL_SLOT_INST	6
# define TRAP_ADDRESS_ERROR	9
# ifdef CONFIG_CPU_SH2A
#  define TRAP_DIVZERO_ERROR	17
#  define TRAP_DIVOVF_ERROR	18
# endif
#else
#define TRAP_RESERVED_INST	12
#define TRAP_ILLEGAL_SLOT_INST	13
#endif

static void dump_mem(const char *str, unsigned long bottom, unsigned long top)
{
	unsigned long p;
	int i;

	printk("%s(0x%08lx to 0x%08lx)\n", str, bottom, top);

	for (p = bottom & ~31; p < top; ) {
		printk("%04lx: ", p & 0xffff);

		for (i = 0; i < 8; i++, p += 4) {
			unsigned int val;

			if (p < bottom || p >= top)
				printk("         ");
			else {
				if (__get_user(val, (unsigned int __user *)p)) {
					printk("\n");
					return;
				}
				printk("%08x ", val);
			}
		}
		printk("\n");
	}
}

DEFINE_SPINLOCK(die_lock);

void die(const char * str, struct pt_regs * regs, long err)
{
	static int die_counter;

	console_verbose();
	spin_lock_irq(&die_lock);
	bust_spinlocks(1);

	printk("%s: %04lx [#%d]\n", str, err & 0xffff, ++die_counter);

	CHK_REMOTE_DEBUG(regs);
	print_modules();
	show_regs(regs);

	printk("Process: %s (pid: %d, stack limit = %p)\n",
	       current->comm, current->pid, task_stack_page(current) + 1);

	if (!user_mode(regs) || in_interrupt())
		dump_mem("Stack: ", regs->regs[15], THREAD_SIZE +
			 (unsigned long)task_stack_page(current));

	bust_spinlocks(0);
	spin_unlock_irq(&die_lock);
	do_exit(SIGSEGV);
}

static inline void die_if_kernel(const char *str, struct pt_regs *regs,
				 long err)
{
	if (!user_mode(regs))
		die(str, regs, err);
}

/*
 * try and fix up kernelspace address errors
 * - userspace errors just cause EFAULT to be returned, resulting in SEGV
 * - kernel/userspace interfaces cause a jump to an appropriate handler
 * - other kernel errors are bad
 * - return 0 if fixed-up, -EFAULT if non-fatal (to the kernel) fault
 */
static int die_if_no_fixup(const char * str, struct pt_regs * regs, long err)
{
	if (!user_mode(regs)) {
		const struct exception_table_entry *fixup;
		fixup = search_exception_tables(regs->pc);
		if (fixup) {
			regs->pc = fixup->fixup;
			return 0;
		}
		die(str, regs, err);
	}
	return -EFAULT;
}

/*
 * handle an instruction that does an unaligned memory access by emulating the
 * desired behaviour
 * - note that PC _may not_ point to the faulting instruction
 *   (if that instruction is in a branch delay slot)
 * - return 0 if emulation okay, -EFAULT on existential error
 */
static int handle_unaligned_ins(u16 instruction, struct pt_regs *regs)
{
	int ret, index, count;
	unsigned long *rm, *rn;
	unsigned char *src, *dst;

	index = (instruction>>8)&15;	/* 0x0F00 */
	rn = &regs->regs[index];

	index = (instruction>>4)&15;	/* 0x00F0 */
	rm = &regs->regs[index];

	count = 1<<(instruction&3);

	ret = -EFAULT;
	switch (instruction>>12) {
	case 0: /* mov.[bwl] to/from memory via r0+rn */
		if (instruction & 8) {
			/* from memory */
			src = (unsigned char*) *rm;
			src += regs->regs[0];
			dst = (unsigned char*) rn;
			*(unsigned long*)dst = 0;

#ifdef __LITTLE_ENDIAN__
			if (copy_from_user(dst, src, count))
				goto fetch_fault;

			if ((count == 2) && dst[1] & 0x80) {
				dst[2] = 0xff;
				dst[3] = 0xff;
			}
#else
			dst += 4-count;

			if (__copy_user(dst, src, count))
				goto fetch_fault;

			if ((count == 2) && dst[2] & 0x80) {
				dst[0] = 0xff;
				dst[1] = 0xff;
			}
#endif
		} else {
			/* to memory */
			src = (unsigned char*) rm;
#if !defined(__LITTLE_ENDIAN__)
			src += 4-count;
#endif
			dst = (unsigned char*) *rn;
			dst += regs->regs[0];

			if (copy_to_user(dst, src, count))
				goto fetch_fault;
		}
		ret = 0;
		break;

	case 1: /* mov.l Rm,@(disp,Rn) */
		src = (unsigned char*) rm;
		dst = (unsigned char*) *rn;
		dst += (instruction&0x000F)<<2;

		if (copy_to_user(dst,src,4))
			goto fetch_fault;
		ret = 0;
		break;

	case 2: /* mov.[bwl] to memory, possibly with pre-decrement */
		if (instruction & 4)
			*rn -= count;
		src = (unsigned char*) rm;
		dst = (unsigned char*) *rn;
#if !defined(__LITTLE_ENDIAN__)
		src += 4-count;
#endif
		if (copy_to_user(dst, src, count))
			goto fetch_fault;
		ret = 0;
		break;

	case 5: /* mov.l @(disp,Rm),Rn */
		src = (unsigned char*) *rm;
		src += (instruction&0x000F)<<2;
		dst = (unsigned char*) rn;
		*(unsigned long*)dst = 0;

		if (copy_from_user(dst,src,4))
			goto fetch_fault;
		ret = 0;
		break;

	case 6:	/* mov.[bwl] from memory, possibly with post-increment */
		src = (unsigned char*) *rm;
		if (instruction & 4)
			*rm += count;
		dst = (unsigned char*) rn;
		*(unsigned long*)dst = 0;

#ifdef __LITTLE_ENDIAN__
		if (copy_from_user(dst, src, count))
			goto fetch_fault;

		if ((count == 2) && dst[1] & 0x80) {
			dst[2] = 0xff;
			dst[3] = 0xff;
		}
#else
		dst += 4-count;

		if (copy_from_user(dst, src, count))
			goto fetch_fault;

		if ((count == 2) && dst[2] & 0x80) {
			dst[0] = 0xff;
			dst[1] = 0xff;
		}
#endif
		ret = 0;
		break;

	case 8:
		switch ((instruction&0xFF00)>>8) {
		case 0x81: /* mov.w R0,@(disp,Rn) */
			src = (unsigned char*) &regs->regs[0];
#if !defined(__LITTLE_ENDIAN__)
			src += 2;
#endif
			dst = (unsigned char*) *rm; /* called Rn in the spec */
			dst += (instruction&0x000F)<<1;

			if (copy_to_user(dst, src, 2))
				goto fetch_fault;
			ret = 0;
			break;

		case 0x85: /* mov.w @(disp,Rm),R0 */
			src = (unsigned char*) *rm;
			src += (instruction&0x000F)<<1;
			dst = (unsigned char*) &regs->regs[0];
			*(unsigned long*)dst = 0;

#if !defined(__LITTLE_ENDIAN__)
			dst += 2;
#endif

			if (copy_from_user(dst, src, 2))
				goto fetch_fault;

#ifdef __LITTLE_ENDIAN__
			if (dst[1] & 0x80) {
				dst[2] = 0xff;
				dst[3] = 0xff;
			}
#else
			if (dst[2] & 0x80) {
				dst[0] = 0xff;
				dst[1] = 0xff;
			}
#endif
			ret = 0;
			break;
		}
		break;
	}
	return ret;

 fetch_fault:
	/* Argh. Address not only misaligned but also non-existent.
	 * Raise an EFAULT and see if it's trapped
	 */
	return die_if_no_fixup("Fault in unaligned fixup", regs, 0);
}

/*
 * emulate the instruction in the delay slot
 * - fetches the instruction from PC+2
 */
static inline int handle_unaligned_delayslot(struct pt_regs *regs)
{
	u16 instruction;

	if (copy_from_user(&instruction, (u16 *)(regs->pc+2), 2)) {
		/* the instruction-fetch faulted */
		if (user_mode(regs))
			return -EFAULT;

		/* kernel */
		die("delay-slot-insn faulting in handle_unaligned_delayslot",
		    regs, 0);
	}

	return handle_unaligned_ins(instruction,regs);
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

/*
 * XXX: SH-2A needs this too, but it needs an overhaul thanks to mixed 32-bit
 * opcodes..
 */
#ifndef CONFIG_CPU_SH2A
static int handle_unaligned_notify_count = 10;

static int handle_unaligned_access(u16 instruction, struct pt_regs *regs)
{
	u_int rm;
	int ret, index;

	index = (instruction>>8)&15;	/* 0x0F00 */
	rm = regs->regs[index];

	/* shout about the first ten userspace fixups */
	if (user_mode(regs) && handle_unaligned_notify_count>0) {
		handle_unaligned_notify_count--;

		printk(KERN_NOTICE "Fixing up unaligned userspace access "
		       "in \"%s\" pid=%d pc=0x%p ins=0x%04hx\n",
		       current->comm,current->pid,(u16*)regs->pc,instruction);
	}

	ret = -EFAULT;
	switch (instruction&0xF000) {
	case 0x0000:
		if (instruction==0x000B) {
			/* rts */
			ret = handle_unaligned_delayslot(regs);
			if (ret==0)
				regs->pc = regs->pr;
		}
		else if ((instruction&0x00FF)==0x0023) {
			/* braf @Rm */
			ret = handle_unaligned_delayslot(regs);
			if (ret==0)
				regs->pc += rm + 4;
		}
		else if ((instruction&0x00FF)==0x0003) {
			/* bsrf @Rm */
			ret = handle_unaligned_delayslot(regs);
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
			ret = handle_unaligned_delayslot(regs);
			if (ret==0)
				regs->pc = rm;
		}
		else if ((instruction&0x00FF)==0x000B) {
			/* jsr @Rm */
			ret = handle_unaligned_delayslot(regs);
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
			break;
		case 0x0F00: /* bf/s lab */
			ret = handle_unaligned_delayslot(regs);
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
			break;
		case 0x0D00: /* bt/s lab */
			ret = handle_unaligned_delayslot(regs);
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

	case 0xA000: /* bra label */
		ret = handle_unaligned_delayslot(regs);
		if (ret==0)
			regs->pc += SH_PC_12BIT_OFFSET(instruction);
		break;

	case 0xB000: /* bsr label */
		ret = handle_unaligned_delayslot(regs);
		if (ret==0) {
			regs->pr = regs->pc + 4;
			regs->pc += SH_PC_12BIT_OFFSET(instruction);
		}
		break;
	}
	return ret;

	/* handle non-delay-slot instruction */
 simple:
	ret = handle_unaligned_ins(instruction,regs);
	if (ret==0)
		regs->pc += 2;
	return ret;
}
#endif /* CONFIG_CPU_SH2A */

#ifdef CONFIG_CPU_HAS_SR_RB
#define lookup_exception_vector(x)	\
	__asm__ __volatile__ ("stc r2_bank, %0\n\t" : "=r" ((x)))
#else
#define lookup_exception_vector(x)	\
	__asm__ __volatile__ ("mov r4, %0\n\t" : "=r" ((x)))
#endif

/*
 * Handle various address error exceptions:
 *  - instruction address error:
 *       misaligned PC
 *       PC >= 0x80000000 in user mode
 *  - data address error (read and write)
 *       misaligned data access
 *       access to >= 0x80000000 is user mode
 * Unfortuntaly we can't distinguish between instruction address error
 * and data address errors caused by read acceses.
 */
asmlinkage void do_address_error(struct pt_regs *regs,
				 unsigned long writeaccess,
				 unsigned long address)
{
	unsigned long error_code = 0;
	mm_segment_t oldfs;
	siginfo_t info;
#ifndef CONFIG_CPU_SH2A
	u16 instruction;
	int tmp;
#endif

	/* Intentional ifdef */
#ifdef CONFIG_CPU_HAS_SR_RB
	lookup_exception_vector(error_code);
#endif

	oldfs = get_fs();

	if (user_mode(regs)) {
		int si_code = BUS_ADRERR;

		local_irq_enable();

		/* bad PC is not something we can fix */
		if (regs->pc & 1) {
			si_code = BUS_ADRALN;
			goto uspace_segv;
		}

#ifndef CONFIG_CPU_SH2A
		set_fs(USER_DS);
		if (copy_from_user(&instruction, (u16 *)(regs->pc), 2)) {
			/* Argh. Fault on the instruction itself.
			   This should never happen non-SMP
			*/
			set_fs(oldfs);
			goto uspace_segv;
		}

		tmp = handle_unaligned_access(instruction, regs);
		set_fs(oldfs);

		if (tmp==0)
			return; /* sorted */
#endif

uspace_segv:
		printk(KERN_NOTICE "Sending SIGBUS to \"%s\" due to unaligned "
		       "access (PC %lx PR %lx)\n", current->comm, regs->pc,
		       regs->pr);

		info.si_signo = SIGBUS;
		info.si_errno = 0;
		info.si_code = si_code;
		info.si_addr = (void *) address;
		force_sig_info(SIGBUS, &info, current);
	} else {
		if (regs->pc & 1)
			die("unaligned program counter", regs, error_code);

#ifndef CONFIG_CPU_SH2A
		set_fs(KERNEL_DS);
		if (copy_from_user(&instruction, (u16 *)(regs->pc), 2)) {
			/* Argh. Fault on the instruction itself.
			   This should never happen non-SMP
			*/
			set_fs(oldfs);
			die("insn faulting in do_address_error", regs, 0);
		}

		handle_unaligned_access(instruction, regs);
		set_fs(oldfs);
#else
		printk(KERN_NOTICE "Killing process \"%s\" due to unaligned "
		       "access\n", current->comm);

		force_sig(SIGSEGV, current);
#endif
	}
}

#ifdef CONFIG_SH_DSP
/*
 *	SH-DSP support gerg@snapgear.com.
 */
int is_dsp_inst(struct pt_regs *regs)
{
	unsigned short inst;

	/*
	 * Safe guard if DSP mode is already enabled or we're lacking
	 * the DSP altogether.
	 */
	if (!(cpu_data->flags & CPU_HAS_DSP) || (regs->sr & SR_DSP))
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
asmlinkage void do_divide_error(unsigned long r4, unsigned long r5,
				unsigned long r6, unsigned long r7,
				struct pt_regs __regs)
{
	struct pt_regs *regs = RELOC_HIDE(&__regs, 0);
	siginfo_t info;

	switch (r4) {
	case TRAP_DIVZERO_ERROR:
		info.si_code = FPE_INTDIV;
		break;
	case TRAP_DIVOVF_ERROR:
		info.si_code = FPE_INTOVF;
		break;
	}

	force_sig_info(SIGFPE, &info, current);
}
#endif

/* arch/sh/kernel/cpu/sh4/fpu.c */
extern int do_fpu_inst(unsigned short, struct pt_regs *);
extern asmlinkage void do_fpu_state_restore(unsigned long r4, unsigned long r5,
		unsigned long r6, unsigned long r7, struct pt_regs __regs);

asmlinkage void do_reserved_inst(unsigned long r4, unsigned long r5,
				unsigned long r6, unsigned long r7,
				struct pt_regs __regs)
{
	struct pt_regs *regs = RELOC_HIDE(&__regs, 0);
	unsigned long error_code;
	struct task_struct *tsk = current;

#ifdef CONFIG_SH_FPU_EMU
	unsigned short inst = 0;
	int err;

	get_user(inst, (unsigned short*)regs->pc);

	err = do_fpu_inst(inst, regs);
	if (!err) {
		regs->pc += 2;
		return;
	}
	/* not a FPU inst. */
#endif

#ifdef CONFIG_SH_DSP
	/* Check if it's a DSP instruction */
	if (is_dsp_inst(regs)) {
		/* Enable DSP mode, and restart instruction. */
		regs->sr |= SR_DSP;
		return;
	}
#endif

	lookup_exception_vector(error_code);

	local_irq_enable();
	CHK_REMOTE_DEBUG(regs);
	force_sig(SIGILL, tsk);
	die_if_no_fixup("reserved instruction", regs, error_code);
}

#ifdef CONFIG_SH_FPU_EMU
static int emulate_branch(unsigned short inst, struct pt_regs* regs)
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
	if ((inst & 0xfd00) == 0x8d00) {
		regs->pc += SH_PC_8BIT_OFFSET(inst);
		return 0;
	}

	if ((inst & 0xe000) == 0xa000) {
		regs->pc += SH_PC_12BIT_OFFSET(inst);
		return 0;
	}

	if ((inst & 0xf0df) == 0x0003) {
		regs->pc += regs->regs[(inst & 0x0f00) >> 8] + 4;
		return 0;
	}

	if ((inst & 0xf0df) == 0x400b) {
		regs->pc = regs->regs[(inst & 0x0f00) >> 8];
		return 0;
	}

	if ((inst & 0xffff) == 0x000b) {
		regs->pc = regs->pr;
		return 0;
	}

	return 1;
}
#endif

asmlinkage void do_illegal_slot_inst(unsigned long r4, unsigned long r5,
				unsigned long r6, unsigned long r7,
				struct pt_regs __regs)
{
	struct pt_regs *regs = RELOC_HIDE(&__regs, 0);
	unsigned long error_code;
	struct task_struct *tsk = current;
#ifdef CONFIG_SH_FPU_EMU
	unsigned short inst = 0;

	get_user(inst, (unsigned short *)regs->pc + 1);
	if (!do_fpu_inst(inst, regs)) {
		get_user(inst, (unsigned short *)regs->pc);
		if (!emulate_branch(inst, regs))
			return;
		/* fault in branch.*/
	}
	/* not a FPU inst. */
#endif

	lookup_exception_vector(error_code);

	local_irq_enable();
	CHK_REMOTE_DEBUG(regs);
	force_sig(SIGILL, tsk);
	die_if_no_fixup("illegal slot instruction", regs, error_code);
}

asmlinkage void do_exception_error(unsigned long r4, unsigned long r5,
				   unsigned long r6, unsigned long r7,
				   struct pt_regs __regs)
{
	struct pt_regs *regs = RELOC_HIDE(&__regs, 0);
	long ex;

	lookup_exception_vector(ex);
	die_if_kernel("exception", regs, ex);
}

#if defined(CONFIG_SH_STANDARD_BIOS)
void *gdb_vbr_vector;

static inline void __init gdb_vbr_init(void)
{
	register unsigned long vbr;

	/*
	 * Read the old value of the VBR register to initialise
	 * the vector through which debug and BIOS traps are
	 * delegated by the Linux trap handler.
	 */
	asm volatile("stc vbr, %0" : "=r" (vbr));

	gdb_vbr_vector = (void *)(vbr + 0x100);
	printk("Setting GDB trap vector to 0x%08lx\n",
	       (unsigned long)gdb_vbr_vector);
}
#endif

void __init per_cpu_trap_init(void)
{
	extern void *vbr_base;

#ifdef CONFIG_SH_STANDARD_BIOS
	gdb_vbr_init();
#endif

	/* NOTE: The VBR value should be at P1
	   (or P2, virtural "fixed" address space).
	   It's definitely should not in physical address.  */

	asm volatile("ldc	%0, vbr"
		     : /* no output */
		     : "r" (&vbr_base)
		     : "memory");
}

void *set_exception_table_vec(unsigned int vec, void *handler)
{
	extern void *exception_handling_table[];
	void *old_handler;

	old_handler = exception_handling_table[vec];
	exception_handling_table[vec] = handler;
	return old_handler;
}

extern asmlinkage void address_error_handler(unsigned long r4, unsigned long r5,
					     unsigned long r6, unsigned long r7,
					     struct pt_regs __regs);

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
	set_exception_table_evt(0x800, do_fpu_state_restore);
	set_exception_table_evt(0x820, do_fpu_state_restore);
#endif

#ifdef CONFIG_CPU_SH2
	set_exception_table_vec(TRAP_ADDRESS_ERROR, address_error_handler);
#endif
#ifdef CONFIG_CPU_SH2A
	set_exception_table_vec(TRAP_DIVZERO_ERROR, do_divide_error);
	set_exception_table_vec(TRAP_DIVOVF_ERROR, do_divide_error);
#endif

	/* Setup VBR for boot cpu */
	per_cpu_trap_init();
}

void show_trace(struct task_struct *tsk, unsigned long *sp,
		struct pt_regs *regs)
{
	unsigned long addr;

	if (regs && user_mode(regs))
		return;

	printk("\nCall trace: ");
#ifdef CONFIG_KALLSYMS
	printk("\n");
#endif

	while (!kstack_end(sp)) {
		addr = *sp++;
		if (kernel_text_address(addr))
			print_ip_sym(addr);
	}

	printk("\n");
}

void show_stack(struct task_struct *tsk, unsigned long *sp)
{
	unsigned long stack;

	if (!tsk)
		tsk = current;
	if (tsk == current)
		sp = (unsigned long *)current_stack_pointer;
	else
		sp = (unsigned long *)tsk->thread.sp;

	stack = (unsigned long)sp;
	dump_mem("Stack: ", stack, THREAD_SIZE +
		 (unsigned long)task_stack_page(tsk));
	show_trace(tsk, sp, NULL);
}

void dump_stack(void)
{
	show_stack(NULL, NULL);
}
EXPORT_SYMBOL(dump_stack);
