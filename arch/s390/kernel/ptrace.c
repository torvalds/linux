/*
 *  arch/s390/kernel/ptrace.c
 *
 *  S390 version
 *    Copyright (C) 1999,2000 IBM Deutschland Entwicklung GmbH, IBM Corporation
 *    Author(s): Denis Joseph Barrow (djbarrow@de.ibm.com,barrow_dj@yahoo.com),
 *               Martin Schwidefsky (schwidefsky@de.ibm.com)
 *
 *  Based on PowerPC version 
 *    Copyright (C) 1995-1996 Gary Thomas (gdt@linuxppc.org)
 *
 *  Derived from "arch/m68k/kernel/ptrace.c"
 *  Copyright (C) 1994 by Hamish Macdonald
 *  Taken from linux/kernel/ptrace.c and modified for M680x0.
 *  linux/kernel/ptrace.c is by Ross Biro 1/23/92, edited by Linus Torvalds
 *
 * Modified by Cort Dougan (cort@cs.nmt.edu) 
 *
 *
 * This file is subject to the terms and conditions of the GNU General
 * Public License.  See the file README.legal in the main directory of
 * this archive for more details.
 */

#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/mm.h>
#include <linux/smp.h>
#include <linux/smp_lock.h>
#include <linux/errno.h>
#include <linux/ptrace.h>
#include <linux/user.h>
#include <linux/security.h>
#include <linux/audit.h>
#include <linux/signal.h>
#include <linux/elf.h>
#include <linux/regset.h>

#include <asm/segment.h>
#include <asm/page.h>
#include <asm/pgtable.h>
#include <asm/pgalloc.h>
#include <asm/system.h>
#include <asm/uaccess.h>
#include <asm/unistd.h>
#include "entry.h"

#ifdef CONFIG_COMPAT
#include "compat_ptrace.h"
#endif

enum s390_regset {
	REGSET_GENERAL,
	REGSET_FP,
};

static void
FixPerRegisters(struct task_struct *task)
{
	struct pt_regs *regs;
	per_struct *per_info;

	regs = task_pt_regs(task);
	per_info = (per_struct *) &task->thread.per_info;
	per_info->control_regs.bits.em_instruction_fetch =
		per_info->single_step | per_info->instruction_fetch;
	
	if (per_info->single_step) {
		per_info->control_regs.bits.starting_addr = 0;
#ifdef CONFIG_COMPAT
		if (test_thread_flag(TIF_31BIT))
			per_info->control_regs.bits.ending_addr = 0x7fffffffUL;
		else
#endif
			per_info->control_regs.bits.ending_addr = PSW_ADDR_INSN;
	} else {
		per_info->control_regs.bits.starting_addr =
			per_info->starting_addr;
		per_info->control_regs.bits.ending_addr =
			per_info->ending_addr;
	}
	/*
	 * if any of the control reg tracing bits are on 
	 * we switch on per in the psw
	 */
	if (per_info->control_regs.words.cr[0] & PER_EM_MASK)
		regs->psw.mask |= PSW_MASK_PER;
	else
		regs->psw.mask &= ~PSW_MASK_PER;

	if (per_info->control_regs.bits.em_storage_alteration)
		per_info->control_regs.bits.storage_alt_space_ctl = 1;
	else
		per_info->control_regs.bits.storage_alt_space_ctl = 0;
}

void user_enable_single_step(struct task_struct *task)
{
	task->thread.per_info.single_step = 1;
	FixPerRegisters(task);
}

void user_disable_single_step(struct task_struct *task)
{
	task->thread.per_info.single_step = 0;
	FixPerRegisters(task);
}

/*
 * Called by kernel/ptrace.c when detaching..
 *
 * Make sure single step bits etc are not set.
 */
void
ptrace_disable(struct task_struct *child)
{
	/* make sure the single step bit is not set. */
	user_disable_single_step(child);
}

#ifndef CONFIG_64BIT
# define __ADDR_MASK 3
#else
# define __ADDR_MASK 7
#endif

/*
 * Read the word at offset addr from the user area of a process. The
 * trouble here is that the information is littered over different
 * locations. The process registers are found on the kernel stack,
 * the floating point stuff and the trace settings are stored in
 * the task structure. In addition the different structures in
 * struct user contain pad bytes that should be read as zeroes.
 * Lovely...
 */
static unsigned long __peek_user(struct task_struct *child, addr_t addr)
{
	struct user *dummy = NULL;
	addr_t offset, tmp;

	if (addr < (addr_t) &dummy->regs.acrs) {
		/*
		 * psw and gprs are stored on the stack
		 */
		tmp = *(addr_t *)((addr_t) &task_pt_regs(child)->psw + addr);
		if (addr == (addr_t) &dummy->regs.psw.mask)
			/* Remove per bit from user psw. */
			tmp &= ~PSW_MASK_PER;

	} else if (addr < (addr_t) &dummy->regs.orig_gpr2) {
		/*
		 * access registers are stored in the thread structure
		 */
		offset = addr - (addr_t) &dummy->regs.acrs;
#ifdef CONFIG_64BIT
		/*
		 * Very special case: old & broken 64 bit gdb reading
		 * from acrs[15]. Result is a 64 bit value. Read the
		 * 32 bit acrs[15] value and shift it by 32. Sick...
		 */
		if (addr == (addr_t) &dummy->regs.acrs[15])
			tmp = ((unsigned long) child->thread.acrs[15]) << 32;
		else
#endif
		tmp = *(addr_t *)((addr_t) &child->thread.acrs + offset);

	} else if (addr == (addr_t) &dummy->regs.orig_gpr2) {
		/*
		 * orig_gpr2 is stored on the kernel stack
		 */
		tmp = (addr_t) task_pt_regs(child)->orig_gpr2;

	} else if (addr < (addr_t) &dummy->regs.fp_regs) {
		/*
		 * prevent reads of padding hole between
		 * orig_gpr2 and fp_regs on s390.
		 */
		tmp = 0;

	} else if (addr < (addr_t) (&dummy->regs.fp_regs + 1)) {
		/* 
		 * floating point regs. are stored in the thread structure
		 */
		offset = addr - (addr_t) &dummy->regs.fp_regs;
		tmp = *(addr_t *)((addr_t) &child->thread.fp_regs + offset);
		if (addr == (addr_t) &dummy->regs.fp_regs.fpc)
			tmp &= (unsigned long) FPC_VALID_MASK
				<< (BITS_PER_LONG - 32);

	} else if (addr < (addr_t) (&dummy->regs.per_info + 1)) {
		/*
		 * per_info is found in the thread structure
		 */
		offset = addr - (addr_t) &dummy->regs.per_info;
		tmp = *(addr_t *)((addr_t) &child->thread.per_info + offset);

	} else
		tmp = 0;

	return tmp;
}

static int
peek_user(struct task_struct *child, addr_t addr, addr_t data)
{
	struct user *dummy = NULL;
	addr_t tmp, mask;

	/*
	 * Stupid gdb peeks/pokes the access registers in 64 bit with
	 * an alignment of 4. Programmers from hell...
	 */
	mask = __ADDR_MASK;
#ifdef CONFIG_64BIT
	if (addr >= (addr_t) &dummy->regs.acrs &&
	    addr < (addr_t) &dummy->regs.orig_gpr2)
		mask = 3;
#endif
	if ((addr & mask) || addr > sizeof(struct user) - __ADDR_MASK)
		return -EIO;

	tmp = __peek_user(child, addr);
	return put_user(tmp, (addr_t __user *) data);
}

/*
 * Write a word to the user area of a process at location addr. This
 * operation does have an additional problem compared to peek_user.
 * Stores to the program status word and on the floating point
 * control register needs to get checked for validity.
 */
static int __poke_user(struct task_struct *child, addr_t addr, addr_t data)
{
	struct user *dummy = NULL;
	addr_t offset;

	if (addr < (addr_t) &dummy->regs.acrs) {
		/*
		 * psw and gprs are stored on the stack
		 */
		if (addr == (addr_t) &dummy->regs.psw.mask &&
#ifdef CONFIG_COMPAT
		    data != PSW_MASK_MERGE(psw_user32_bits, data) &&
#endif
		    data != PSW_MASK_MERGE(psw_user_bits, data))
			/* Invalid psw mask. */
			return -EINVAL;
#ifndef CONFIG_64BIT
		if (addr == (addr_t) &dummy->regs.psw.addr)
			/* I'd like to reject addresses without the
			   high order bit but older gdb's rely on it */
			data |= PSW_ADDR_AMODE;
#endif
		*(addr_t *)((addr_t) &task_pt_regs(child)->psw + addr) = data;

	} else if (addr < (addr_t) (&dummy->regs.orig_gpr2)) {
		/*
		 * access registers are stored in the thread structure
		 */
		offset = addr - (addr_t) &dummy->regs.acrs;
#ifdef CONFIG_64BIT
		/*
		 * Very special case: old & broken 64 bit gdb writing
		 * to acrs[15] with a 64 bit value. Ignore the lower
		 * half of the value and write the upper 32 bit to
		 * acrs[15]. Sick...
		 */
		if (addr == (addr_t) &dummy->regs.acrs[15])
			child->thread.acrs[15] = (unsigned int) (data >> 32);
		else
#endif
		*(addr_t *)((addr_t) &child->thread.acrs + offset) = data;

	} else if (addr == (addr_t) &dummy->regs.orig_gpr2) {
		/*
		 * orig_gpr2 is stored on the kernel stack
		 */
		task_pt_regs(child)->orig_gpr2 = data;

	} else if (addr < (addr_t) &dummy->regs.fp_regs) {
		/*
		 * prevent writes of padding hole between
		 * orig_gpr2 and fp_regs on s390.
		 */
		return 0;

	} else if (addr < (addr_t) (&dummy->regs.fp_regs + 1)) {
		/*
		 * floating point regs. are stored in the thread structure
		 */
		if (addr == (addr_t) &dummy->regs.fp_regs.fpc &&
		    (data & ~((unsigned long) FPC_VALID_MASK
			      << (BITS_PER_LONG - 32))) != 0)
			return -EINVAL;
		offset = addr - (addr_t) &dummy->regs.fp_regs;
		*(addr_t *)((addr_t) &child->thread.fp_regs + offset) = data;

	} else if (addr < (addr_t) (&dummy->regs.per_info + 1)) {
		/*
		 * per_info is found in the thread structure 
		 */
		offset = addr - (addr_t) &dummy->regs.per_info;
		*(addr_t *)((addr_t) &child->thread.per_info + offset) = data;

	}

	FixPerRegisters(child);
	return 0;
}

static int
poke_user(struct task_struct *child, addr_t addr, addr_t data)
{
	struct user *dummy = NULL;
	addr_t mask;

	/*
	 * Stupid gdb peeks/pokes the access registers in 64 bit with
	 * an alignment of 4. Programmers from hell indeed...
	 */
	mask = __ADDR_MASK;
#ifdef CONFIG_64BIT
	if (addr >= (addr_t) &dummy->regs.acrs &&
	    addr < (addr_t) &dummy->regs.orig_gpr2)
		mask = 3;
#endif
	if ((addr & mask) || addr > sizeof(struct user) - __ADDR_MASK)
		return -EIO;

	return __poke_user(child, addr, data);
}

long arch_ptrace(struct task_struct *child, long request, long addr, long data)
{
	ptrace_area parea; 
	int copied, ret;

	switch (request) {
	case PTRACE_PEEKTEXT:
	case PTRACE_PEEKDATA:
		/* Remove high order bit from address (only for 31 bit). */
		addr &= PSW_ADDR_INSN;
		/* read word at location addr. */
		return generic_ptrace_peekdata(child, addr, data);

	case PTRACE_PEEKUSR:
		/* read the word at location addr in the USER area. */
		return peek_user(child, addr, data);

	case PTRACE_POKETEXT:
	case PTRACE_POKEDATA:
		/* Remove high order bit from address (only for 31 bit). */
		addr &= PSW_ADDR_INSN;
		/* write the word at location addr. */
		return generic_ptrace_pokedata(child, addr, data);

	case PTRACE_POKEUSR:
		/* write the word at location addr in the USER area */
		return poke_user(child, addr, data);

	case PTRACE_PEEKUSR_AREA:
	case PTRACE_POKEUSR_AREA:
		if (copy_from_user(&parea, (void __force __user *) addr,
							sizeof(parea)))
			return -EFAULT;
		addr = parea.kernel_addr;
		data = parea.process_addr;
		copied = 0;
		while (copied < parea.len) {
			if (request == PTRACE_PEEKUSR_AREA)
				ret = peek_user(child, addr, data);
			else {
				addr_t utmp;
				if (get_user(utmp,
					     (addr_t __force __user *) data))
					return -EFAULT;
				ret = poke_user(child, addr, utmp);
			}
			if (ret)
				return ret;
			addr += sizeof(unsigned long);
			data += sizeof(unsigned long);
			copied += sizeof(unsigned long);
		}
		return 0;
	}
	return ptrace_request(child, request, addr, data);
}

#ifdef CONFIG_COMPAT
/*
 * Now the fun part starts... a 31 bit program running in the
 * 31 bit emulation tracing another program. PTRACE_PEEKTEXT,
 * PTRACE_PEEKDATA, PTRACE_POKETEXT and PTRACE_POKEDATA are easy
 * to handle, the difference to the 64 bit versions of the requests
 * is that the access is done in multiples of 4 byte instead of
 * 8 bytes (sizeof(unsigned long) on 31/64 bit).
 * The ugly part are PTRACE_PEEKUSR, PTRACE_PEEKUSR_AREA,
 * PTRACE_POKEUSR and PTRACE_POKEUSR_AREA. If the traced program
 * is a 31 bit program too, the content of struct user can be
 * emulated. A 31 bit program peeking into the struct user of
 * a 64 bit program is a no-no.
 */

/*
 * Same as peek_user but for a 31 bit program.
 */
static u32 __peek_user_compat(struct task_struct *child, addr_t addr)
{
	struct user32 *dummy32 = NULL;
	per_struct32 *dummy_per32 = NULL;
	addr_t offset;
	__u32 tmp;

	if (addr < (addr_t) &dummy32->regs.acrs) {
		/*
		 * psw and gprs are stored on the stack
		 */
		if (addr == (addr_t) &dummy32->regs.psw.mask) {
			/* Fake a 31 bit psw mask. */
			tmp = (__u32)(task_pt_regs(child)->psw.mask >> 32);
			tmp = PSW32_MASK_MERGE(psw32_user_bits, tmp);
		} else if (addr == (addr_t) &dummy32->regs.psw.addr) {
			/* Fake a 31 bit psw address. */
			tmp = (__u32) task_pt_regs(child)->psw.addr |
				PSW32_ADDR_AMODE31;
		} else {
			/* gpr 0-15 */
			tmp = *(__u32 *)((addr_t) &task_pt_regs(child)->psw +
					 addr*2 + 4);
		}
	} else if (addr < (addr_t) (&dummy32->regs.orig_gpr2)) {
		/*
		 * access registers are stored in the thread structure
		 */
		offset = addr - (addr_t) &dummy32->regs.acrs;
		tmp = *(__u32*)((addr_t) &child->thread.acrs + offset);

	} else if (addr == (addr_t) (&dummy32->regs.orig_gpr2)) {
		/*
		 * orig_gpr2 is stored on the kernel stack
		 */
		tmp = *(__u32*)((addr_t) &task_pt_regs(child)->orig_gpr2 + 4);

	} else if (addr < (addr_t) &dummy32->regs.fp_regs) {
		/*
		 * prevent reads of padding hole between
		 * orig_gpr2 and fp_regs on s390.
		 */
		tmp = 0;

	} else if (addr < (addr_t) (&dummy32->regs.fp_regs + 1)) {
		/*
		 * floating point regs. are stored in the thread structure 
		 */
	        offset = addr - (addr_t) &dummy32->regs.fp_regs;
		tmp = *(__u32 *)((addr_t) &child->thread.fp_regs + offset);

	} else if (addr < (addr_t) (&dummy32->regs.per_info + 1)) {
		/*
		 * per_info is found in the thread structure
		 */
		offset = addr - (addr_t) &dummy32->regs.per_info;
		/* This is magic. See per_struct and per_struct32. */
		if ((offset >= (addr_t) &dummy_per32->control_regs &&
		     offset < (addr_t) (&dummy_per32->control_regs + 1)) ||
		    (offset >= (addr_t) &dummy_per32->starting_addr &&
		     offset <= (addr_t) &dummy_per32->ending_addr) ||
		    offset == (addr_t) &dummy_per32->lowcore.words.address)
			offset = offset*2 + 4;
		else
			offset = offset*2;
		tmp = *(__u32 *)((addr_t) &child->thread.per_info + offset);

	} else
		tmp = 0;

	return tmp;
}

static int peek_user_compat(struct task_struct *child,
			    addr_t addr, addr_t data)
{
	__u32 tmp;

	if (!test_thread_flag(TIF_31BIT) ||
	    (addr & 3) || addr > sizeof(struct user) - 3)
		return -EIO;

	tmp = __peek_user_compat(child, addr);
	return put_user(tmp, (__u32 __user *) data);
}

/*
 * Same as poke_user but for a 31 bit program.
 */
static int __poke_user_compat(struct task_struct *child,
			      addr_t addr, addr_t data)
{
	struct user32 *dummy32 = NULL;
	per_struct32 *dummy_per32 = NULL;
	__u32 tmp = (__u32) data;
	addr_t offset;

	if (addr < (addr_t) &dummy32->regs.acrs) {
		/*
		 * psw, gprs, acrs and orig_gpr2 are stored on the stack
		 */
		if (addr == (addr_t) &dummy32->regs.psw.mask) {
			/* Build a 64 bit psw mask from 31 bit mask. */
			if (tmp != PSW32_MASK_MERGE(psw32_user_bits, tmp))
				/* Invalid psw mask. */
				return -EINVAL;
			task_pt_regs(child)->psw.mask =
				PSW_MASK_MERGE(psw_user32_bits, (__u64) tmp << 32);
		} else if (addr == (addr_t) &dummy32->regs.psw.addr) {
			/* Build a 64 bit psw address from 31 bit address. */
			task_pt_regs(child)->psw.addr =
				(__u64) tmp & PSW32_ADDR_INSN;
		} else {
			/* gpr 0-15 */
			*(__u32*)((addr_t) &task_pt_regs(child)->psw
				  + addr*2 + 4) = tmp;
		}
	} else if (addr < (addr_t) (&dummy32->regs.orig_gpr2)) {
		/*
		 * access registers are stored in the thread structure
		 */
		offset = addr - (addr_t) &dummy32->regs.acrs;
		*(__u32*)((addr_t) &child->thread.acrs + offset) = tmp;

	} else if (addr == (addr_t) (&dummy32->regs.orig_gpr2)) {
		/*
		 * orig_gpr2 is stored on the kernel stack
		 */
		*(__u32*)((addr_t) &task_pt_regs(child)->orig_gpr2 + 4) = tmp;

	} else if (addr < (addr_t) &dummy32->regs.fp_regs) {
		/*
		 * prevent writess of padding hole between
		 * orig_gpr2 and fp_regs on s390.
		 */
		return 0;

	} else if (addr < (addr_t) (&dummy32->regs.fp_regs + 1)) {
		/*
		 * floating point regs. are stored in the thread structure 
		 */
		if (addr == (addr_t) &dummy32->regs.fp_regs.fpc &&
		    (tmp & ~FPC_VALID_MASK) != 0)
			/* Invalid floating point control. */
			return -EINVAL;
	        offset = addr - (addr_t) &dummy32->regs.fp_regs;
		*(__u32 *)((addr_t) &child->thread.fp_regs + offset) = tmp;

	} else if (addr < (addr_t) (&dummy32->regs.per_info + 1)) {
		/*
		 * per_info is found in the thread structure.
		 */
		offset = addr - (addr_t) &dummy32->regs.per_info;
		/*
		 * This is magic. See per_struct and per_struct32.
		 * By incident the offsets in per_struct are exactly
		 * twice the offsets in per_struct32 for all fields.
		 * The 8 byte fields need special handling though,
		 * because the second half (bytes 4-7) is needed and
		 * not the first half.
		 */
		if ((offset >= (addr_t) &dummy_per32->control_regs &&
		     offset < (addr_t) (&dummy_per32->control_regs + 1)) ||
		    (offset >= (addr_t) &dummy_per32->starting_addr &&
		     offset <= (addr_t) &dummy_per32->ending_addr) ||
		    offset == (addr_t) &dummy_per32->lowcore.words.address)
			offset = offset*2 + 4;
		else
			offset = offset*2;
		*(__u32 *)((addr_t) &child->thread.per_info + offset) = tmp;

	}

	FixPerRegisters(child);
	return 0;
}

static int poke_user_compat(struct task_struct *child,
			    addr_t addr, addr_t data)
{
	if (!test_thread_flag(TIF_31BIT) ||
	    (addr & 3) || addr > sizeof(struct user32) - 3)
		return -EIO;

	return __poke_user_compat(child, addr, data);
}

long compat_arch_ptrace(struct task_struct *child, compat_long_t request,
			compat_ulong_t caddr, compat_ulong_t cdata)
{
	unsigned long addr = caddr;
	unsigned long data = cdata;
	ptrace_area_emu31 parea; 
	int copied, ret;

	switch (request) {
	case PTRACE_PEEKUSR:
		/* read the word at location addr in the USER area. */
		return peek_user_compat(child, addr, data);

	case PTRACE_POKEUSR:
		/* write the word at location addr in the USER area */
		return poke_user_compat(child, addr, data);

	case PTRACE_PEEKUSR_AREA:
	case PTRACE_POKEUSR_AREA:
		if (copy_from_user(&parea, (void __force __user *) addr,
							sizeof(parea)))
			return -EFAULT;
		addr = parea.kernel_addr;
		data = parea.process_addr;
		copied = 0;
		while (copied < parea.len) {
			if (request == PTRACE_PEEKUSR_AREA)
				ret = peek_user_compat(child, addr, data);
			else {
				__u32 utmp;
				if (get_user(utmp,
					     (__u32 __force __user *) data))
					return -EFAULT;
				ret = poke_user_compat(child, addr, utmp);
			}
			if (ret)
				return ret;
			addr += sizeof(unsigned int);
			data += sizeof(unsigned int);
			copied += sizeof(unsigned int);
		}
		return 0;
	}
	return compat_ptrace_request(child, request, addr, data);
}
#endif

asmlinkage void
syscall_trace(struct pt_regs *regs, int entryexit)
{
	if (unlikely(current->audit_context) && entryexit)
		audit_syscall_exit(AUDITSC_RESULT(regs->gprs[2]), regs->gprs[2]);

	if (!test_thread_flag(TIF_SYSCALL_TRACE))
		goto out;
	if (!(current->ptrace & PT_PTRACED))
		goto out;
	ptrace_notify(SIGTRAP | ((current->ptrace & PT_TRACESYSGOOD)
				 ? 0x80 : 0));

	/*
	 * If the debuffer has set an invalid system call number,
	 * we prepare to skip the system call restart handling.
	 */
	if (!entryexit && regs->gprs[2] >= NR_syscalls)
		regs->trap = -1;

	/*
	 * this isn't the same as continuing with a signal, but it will do
	 * for normal use.  strace only continues with a signal if the
	 * stopping signal is not SIGTRAP.  -brl
	 */
	if (current->exit_code) {
		send_sig(current->exit_code, current, 1);
		current->exit_code = 0;
	}
 out:
	if (unlikely(current->audit_context) && !entryexit)
		audit_syscall_entry(test_thread_flag(TIF_31BIT)?AUDIT_ARCH_S390:AUDIT_ARCH_S390X,
				    regs->gprs[2], regs->orig_gpr2, regs->gprs[3],
				    regs->gprs[4], regs->gprs[5]);
}

/*
 * user_regset definitions.
 */

static int s390_regs_get(struct task_struct *target,
			 const struct user_regset *regset,
			 unsigned int pos, unsigned int count,
			 void *kbuf, void __user *ubuf)
{
	if (target == current)
		save_access_regs(target->thread.acrs);

	if (kbuf) {
		unsigned long *k = kbuf;
		while (count > 0) {
			*k++ = __peek_user(target, pos);
			count -= sizeof(*k);
			pos += sizeof(*k);
		}
	} else {
		unsigned long __user *u = ubuf;
		while (count > 0) {
			if (__put_user(__peek_user(target, pos), u++))
				return -EFAULT;
			count -= sizeof(*u);
			pos += sizeof(*u);
		}
	}
	return 0;
}

static int s390_regs_set(struct task_struct *target,
			 const struct user_regset *regset,
			 unsigned int pos, unsigned int count,
			 const void *kbuf, const void __user *ubuf)
{
	int rc = 0;

	if (target == current)
		save_access_regs(target->thread.acrs);

	if (kbuf) {
		const unsigned long *k = kbuf;
		while (count > 0 && !rc) {
			rc = __poke_user(target, pos, *k++);
			count -= sizeof(*k);
			pos += sizeof(*k);
		}
	} else {
		const unsigned long  __user *u = ubuf;
		while (count > 0 && !rc) {
			unsigned long word;
			rc = __get_user(word, u++);
			if (rc)
				break;
			rc = __poke_user(target, pos, word);
			count -= sizeof(*u);
			pos += sizeof(*u);
		}
	}

	if (rc == 0 && target == current)
		restore_access_regs(target->thread.acrs);

	return rc;
}

static int s390_fpregs_get(struct task_struct *target,
			   const struct user_regset *regset, unsigned int pos,
			   unsigned int count, void *kbuf, void __user *ubuf)
{
	if (target == current)
		save_fp_regs(&target->thread.fp_regs);

	return user_regset_copyout(&pos, &count, &kbuf, &ubuf,
				   &target->thread.fp_regs, 0, -1);
}

static int s390_fpregs_set(struct task_struct *target,
			   const struct user_regset *regset, unsigned int pos,
			   unsigned int count, const void *kbuf,
			   const void __user *ubuf)
{
	int rc = 0;

	if (target == current)
		save_fp_regs(&target->thread.fp_regs);

	/* If setting FPC, must validate it first. */
	if (count > 0 && pos < offsetof(s390_fp_regs, fprs)) {
		u32 fpc[2] = { target->thread.fp_regs.fpc, 0 };
		rc = user_regset_copyin(&pos, &count, &kbuf, &ubuf, &fpc,
					0, offsetof(s390_fp_regs, fprs));
		if (rc)
			return rc;
		if ((fpc[0] & ~FPC_VALID_MASK) != 0 || fpc[1] != 0)
			return -EINVAL;
		target->thread.fp_regs.fpc = fpc[0];
	}

	if (rc == 0 && count > 0)
		rc = user_regset_copyin(&pos, &count, &kbuf, &ubuf,
					target->thread.fp_regs.fprs,
					offsetof(s390_fp_regs, fprs), -1);

	if (rc == 0 && target == current)
		restore_fp_regs(&target->thread.fp_regs);

	return rc;
}

static const struct user_regset s390_regsets[] = {
	[REGSET_GENERAL] = {
		.core_note_type = NT_PRSTATUS,
		.n = sizeof(s390_regs) / sizeof(long),
		.size = sizeof(long),
		.align = sizeof(long),
		.get = s390_regs_get,
		.set = s390_regs_set,
	},
	[REGSET_FP] = {
		.core_note_type = NT_PRFPREG,
		.n = sizeof(s390_fp_regs) / sizeof(long),
		.size = sizeof(long),
		.align = sizeof(long),
		.get = s390_fpregs_get,
		.set = s390_fpregs_set,
	},
};

static const struct user_regset_view user_s390_view = {
	.name = UTS_MACHINE,
	.e_machine = EM_S390,
	.regsets = s390_regsets,
	.n = ARRAY_SIZE(s390_regsets)
};

#ifdef CONFIG_COMPAT
static int s390_compat_regs_get(struct task_struct *target,
				const struct user_regset *regset,
				unsigned int pos, unsigned int count,
				void *kbuf, void __user *ubuf)
{
	if (target == current)
		save_access_regs(target->thread.acrs);

	if (kbuf) {
		compat_ulong_t *k = kbuf;
		while (count > 0) {
			*k++ = __peek_user_compat(target, pos);
			count -= sizeof(*k);
			pos += sizeof(*k);
		}
	} else {
		compat_ulong_t __user *u = ubuf;
		while (count > 0) {
			if (__put_user(__peek_user_compat(target, pos), u++))
				return -EFAULT;
			count -= sizeof(*u);
			pos += sizeof(*u);
		}
	}
	return 0;
}

static int s390_compat_regs_set(struct task_struct *target,
				const struct user_regset *regset,
				unsigned int pos, unsigned int count,
				const void *kbuf, const void __user *ubuf)
{
	int rc = 0;

	if (target == current)
		save_access_regs(target->thread.acrs);

	if (kbuf) {
		const compat_ulong_t *k = kbuf;
		while (count > 0 && !rc) {
			rc = __poke_user_compat(target, pos, *k++);
			count -= sizeof(*k);
			pos += sizeof(*k);
		}
	} else {
		const compat_ulong_t  __user *u = ubuf;
		while (count > 0 && !rc) {
			compat_ulong_t word;
			rc = __get_user(word, u++);
			if (rc)
				break;
			rc = __poke_user_compat(target, pos, word);
			count -= sizeof(*u);
			pos += sizeof(*u);
		}
	}

	if (rc == 0 && target == current)
		restore_access_regs(target->thread.acrs);

	return rc;
}

static const struct user_regset s390_compat_regsets[] = {
	[REGSET_GENERAL] = {
		.core_note_type = NT_PRSTATUS,
		.n = sizeof(s390_compat_regs) / sizeof(compat_long_t),
		.size = sizeof(compat_long_t),
		.align = sizeof(compat_long_t),
		.get = s390_compat_regs_get,
		.set = s390_compat_regs_set,
	},
	[REGSET_FP] = {
		.core_note_type = NT_PRFPREG,
		.n = sizeof(s390_fp_regs) / sizeof(compat_long_t),
		.size = sizeof(compat_long_t),
		.align = sizeof(compat_long_t),
		.get = s390_fpregs_get,
		.set = s390_fpregs_set,
	},
};

static const struct user_regset_view user_s390_compat_view = {
	.name = "s390",
	.e_machine = EM_S390,
	.regsets = s390_compat_regsets,
	.n = ARRAY_SIZE(s390_compat_regsets)
};
#endif

const struct user_regset_view *task_user_regset_view(struct task_struct *task)
{
#ifdef CONFIG_COMPAT
	if (test_tsk_thread_flag(task, TIF_31BIT))
		return &user_s390_compat_view;
#endif
	return &user_s390_view;
}
