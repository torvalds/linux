/* $Id: traps.c,v 1.17 2004/05/02 01:46:30 sugioka Exp $
 *
 *  linux/arch/sh/traps.c
 *
 *  SuperH version: Copyright (C) 1999 Niibe Yutaka
 *                  Copyright (C) 2000 Philipp Rumpf
 *                  Copyright (C) 2000 David Howells
 *                  Copyright (C) 2002, 2003 Paul Mundt
 */

/*
 * 'Traps.c' handles hardware traps and faults after we have saved some
 * state in 'entry.S'.
 */
#include <linux/config.h>
#include <linux/sched.h>
#include <linux/kernel.h>
#include <linux/string.h>
#include <linux/errno.h>
#include <linux/ptrace.h>
#include <linux/timer.h>
#include <linux/mm.h>
#include <linux/smp.h>
#include <linux/smp_lock.h>
#include <linux/init.h>
#include <linux/delay.h>
#include <linux/spinlock.h>
#include <linux/module.h>
#include <linux/kallsyms.h>

#include <asm/system.h>
#include <asm/uaccess.h>
#include <asm/io.h>
#include <asm/atomic.h>
#include <asm/processor.h>
#include <asm/sections.h>

#ifdef CONFIG_SH_KGDB
#include <asm/kgdb.h>
#define CHK_REMOTE_DEBUG(regs)                                               \
{                                                                            \
  if ((kgdb_debug_hook != (kgdb_debug_hook_t *) NULL) && (!user_mode(regs))) \
  {                                                                          \
    (*kgdb_debug_hook)(regs);                                                \
  }                                                                          \
}
#else
#define CHK_REMOTE_DEBUG(regs)
#endif

#define DO_ERROR(trapnr, signr, str, name, tsk)				\
asmlinkage void do_##name(unsigned long r4, unsigned long r5,		\
			  unsigned long r6, unsigned long r7,		\
			  struct pt_regs regs)				\
{									\
	unsigned long error_code;					\
 									\
	/* Check if it's a DSP instruction */				\
 	if (is_dsp_inst(&regs)) {					\
		/* Enable DSP mode, and restart instruction. */		\
		regs.sr |= SR_DSP;					\
		return;							\
	}								\
									\
	asm volatile("stc	r2_bank, %0": "=r" (error_code));	\
	local_irq_enable();						\
	tsk->thread.error_code = error_code;				\
	tsk->thread.trap_no = trapnr;					\
        CHK_REMOTE_DEBUG(&regs);					\
	force_sig(signr, tsk);						\
	die_if_no_fixup(str,&regs,error_code);				\
}

#ifdef CONFIG_CPU_SH2
#define TRAP_RESERVED_INST	4
#define TRAP_ILLEGAL_SLOT_INST	6
#else
#define TRAP_RESERVED_INST	12
#define TRAP_ILLEGAL_SLOT_INST	13
#endif

/*
 * These constants are for searching for possible module text
 * segments.  VMALLOC_OFFSET comes from mm/vmalloc.c; MODULE_RANGE is
 * a guess of how much space is likely to be vmalloced.
 */
#define VMALLOC_OFFSET (8*1024*1024)
#define MODULE_RANGE (8*1024*1024)

spinlock_t die_lock;

void die(const char * str, struct pt_regs * regs, long err)
{
	static int die_counter;

	console_verbose();
	spin_lock_irq(&die_lock);
	printk("%s: %04lx [#%d]\n", str, err & 0xffff, ++die_counter);
	CHK_REMOTE_DEBUG(regs);
	show_regs(regs);
	spin_unlock_irq(&die_lock);
	do_exit(SIGSEGV);
}

static inline void die_if_kernel(const char * str, struct pt_regs * regs, long err)
{
	if (!user_mode(regs))
		die(str, regs, err);
}

static int handle_unaligned_notify_count = 10;

/*
 * try and fix up kernelspace address errors
 * - userspace errors just cause EFAULT to be returned, resulting in SEGV
 * - kernel/userspace interfaces cause a jump to an appropriate handler
 * - other kernel errors are bad
 * - return 0 if fixed-up, -EFAULT if non-fatal (to the kernel) fault
 */
static int die_if_no_fixup(const char * str, struct pt_regs * regs, long err)
{
	if (!user_mode(regs))
	{
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
		die("delay-slot-insn faulting in handle_unaligned_delayslot", regs, 0);
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

static int handle_unaligned_access(u16 instruction, struct pt_regs *regs)
{
	u_int rm;
	int ret, index;

	index = (instruction>>8)&15;	/* 0x0F00 */
	rm = regs->regs[index];

	/* shout about the first ten userspace fixups */
	if (user_mode(regs) && handle_unaligned_notify_count>0) {
		handle_unaligned_notify_count--;

		printk("Fixing up unaligned userspace access in \"%s\" pid=%d pc=0x%p ins=0x%04hx\n",
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

/*
 * Handle various address error exceptions
 */
asmlinkage void do_address_error(struct pt_regs *regs, 
				 unsigned long writeaccess,
				 unsigned long address)
{
	unsigned long error_code;
	mm_segment_t oldfs;
	u16 instruction;
	int tmp;

	asm volatile("stc       r2_bank,%0": "=r" (error_code));

	oldfs = get_fs();

	if (user_mode(regs)) {
		local_irq_enable();
		current->thread.error_code = error_code;
		current->thread.trap_no = (writeaccess) ? 8 : 7;

		/* bad PC is not something we can fix */
		if (regs->pc & 1)
			goto uspace_segv;

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

	uspace_segv:
		printk(KERN_NOTICE "Killing process \"%s\" due to unaligned access\n", current->comm);
		force_sig(SIGSEGV, current);
	} else {
		if (regs->pc & 1)
			die("unaligned program counter", regs, error_code);

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

DO_ERROR(TRAP_RESERVED_INST, SIGILL, "reserved instruction", reserved_inst, current)
DO_ERROR(TRAP_ILLEGAL_SLOT_INST, SIGILL, "illegal slot instruction", illegal_slot_inst, current)

asmlinkage void do_exception_error(unsigned long r4, unsigned long r5,
				   unsigned long r6, unsigned long r7,
				   struct pt_regs regs)
{
	long ex;
	asm volatile("stc	r2_bank, %0" : "=r" (ex));
	die_if_kernel("exception", &regs, ex);
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

void __init trap_init(void)
{
	extern void *exception_handling_table[];

	exception_handling_table[TRAP_RESERVED_INST]
		= (void *)do_reserved_inst;
	exception_handling_table[TRAP_ILLEGAL_SLOT_INST]
		= (void *)do_illegal_slot_inst;

#ifdef CONFIG_CPU_SH4
	if (!(cpu_data->flags & CPU_HAS_FPU)) {
		/* For SH-4 lacking an FPU, treat floating point instructions
		   as reserved. */
		/* entry 64 corresponds to EXPEVT=0x800 */
		exception_handling_table[64] = (void *)do_reserved_inst;
		exception_handling_table[65] = (void *)do_illegal_slot_inst;
	}
#endif
		
	/* Setup VBR for boot cpu */
	per_cpu_trap_init();
}

void show_stack(struct task_struct *tsk, unsigned long *sp)
{
	unsigned long *stack, addr;
	unsigned long module_start = VMALLOC_START;
	unsigned long module_end = VMALLOC_END;
	int i = 1;

	if (tsk && !sp) {
		sp = (unsigned long *)tsk->thread.sp;
	}

	if (!sp) {
		__asm__ __volatile__ (
			"mov r15, %0\n\t"
			"stc r7_bank, %1\n\t"
			: "=r" (module_start),
			  "=r" (module_end)
		);
		
		sp = (unsigned long *)module_start;
	}

	stack = sp;

	printk("\nCall trace: ");
#ifdef CONFIG_KALLSYMS
	printk("\n");
#endif

	while (!kstack_end(stack)) {
		addr = *stack++;
		if (((addr >= (unsigned long)_text) &&
		     (addr <= (unsigned long)_etext)) ||
		    ((addr >= module_start) && (addr <= module_end))) {
			/*
			 * For 80-columns display, 6 entry is maximum.
			 * NOTE: '[<8c00abcd>] ' consumes 13 columns .
			 */
#ifndef CONFIG_KALLSYMS
			if (i && ((i % 6) == 0))
				printk("\n       ");
#endif
			printk("[<%08lx>] ", addr);
			print_symbol("%s\n", addr);
			i++;
		}
	}

	printk("\n");
}

void show_task(unsigned long *sp)
{
	show_stack(NULL, sp);
}

void dump_stack(void)
{
	show_stack(NULL, NULL);
}
EXPORT_SYMBOL(dump_stack);
