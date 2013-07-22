/*
 *  Ptrace user space interface.
 *
 *    Copyright IBM Corp. 1999, 2010
 *    Author(s): Denis Joseph Barrow
 *               Martin Schwidefsky (schwidefsky@de.ibm.com)
 */

#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/mm.h>
#include <linux/smp.h>
#include <linux/errno.h>
#include <linux/ptrace.h>
#include <linux/user.h>
#include <linux/security.h>
#include <linux/audit.h>
#include <linux/signal.h>
#include <linux/elf.h>
#include <linux/regset.h>
#include <linux/tracehook.h>
#include <linux/seccomp.h>
#include <linux/compat.h>
#include <trace/syscall.h>
#include <asm/segment.h>
#include <asm/page.h>
#include <asm/pgtable.h>
#include <asm/pgalloc.h>
#include <asm/uaccess.h>
#include <asm/unistd.h>
#include <asm/switch_to.h>
#include "entry.h"

#ifdef CONFIG_COMPAT
#include "compat_ptrace.h"
#endif

#define CREATE_TRACE_POINTS
#include <trace/events/syscalls.h>

enum s390_regset {
	REGSET_GENERAL,
	REGSET_FP,
	REGSET_LAST_BREAK,
	REGSET_TDB,
	REGSET_SYSTEM_CALL,
	REGSET_GENERAL_EXTENDED,
};

void update_cr_regs(struct task_struct *task)
{
	struct pt_regs *regs = task_pt_regs(task);
	struct thread_struct *thread = &task->thread;
	struct per_regs old, new;

#ifdef CONFIG_64BIT
	/* Take care of the enable/disable of transactional execution. */
	if (MACHINE_HAS_TE) {
		unsigned long cr[3], cr_new[3];

		__ctl_store(cr, 0, 2);
		cr_new[1] = cr[1];
		/* Set or clear transaction execution TXC/PIFO bits 8 and 9. */
		if (task->thread.per_flags & PER_FLAG_NO_TE)
			cr_new[0] = cr[0] & ~(3UL << 54);
		else
			cr_new[0] = cr[0] | (3UL << 54);
		/* Set or clear transaction execution TDC bits 62 and 63. */
		cr_new[2] = cr[2] & ~3UL;
		if (task->thread.per_flags & PER_FLAG_TE_ABORT_RAND) {
			if (task->thread.per_flags & PER_FLAG_TE_ABORT_RAND_TEND)
				cr_new[2] |= 1UL;
			else
				cr_new[2] |= 2UL;
		}
		if (memcmp(&cr_new, &cr, sizeof(cr)))
			__ctl_load(cr_new, 0, 2);
	}
#endif
	/* Copy user specified PER registers */
	new.control = thread->per_user.control;
	new.start = thread->per_user.start;
	new.end = thread->per_user.end;

	/* merge TIF_SINGLE_STEP into user specified PER registers. */
	if (test_tsk_thread_flag(task, TIF_SINGLE_STEP)) {
		new.control |= PER_EVENT_IFETCH;
#ifdef CONFIG_64BIT
		new.control |= PER_CONTROL_SUSPENSION;
		new.control |= PER_EVENT_TRANSACTION_END;
#endif
		new.start = 0;
		new.end = PSW_ADDR_INSN;
	}

	/* Take care of the PER enablement bit in the PSW. */
	if (!(new.control & PER_EVENT_MASK)) {
		regs->psw.mask &= ~PSW_MASK_PER;
		return;
	}
	regs->psw.mask |= PSW_MASK_PER;
	__ctl_store(old, 9, 11);
	if (memcmp(&new, &old, sizeof(struct per_regs)) != 0)
		__ctl_load(new, 9, 11);
}

void user_enable_single_step(struct task_struct *task)
{
	set_tsk_thread_flag(task, TIF_SINGLE_STEP);
	if (task == current)
		update_cr_regs(task);
}

void user_disable_single_step(struct task_struct *task)
{
	clear_tsk_thread_flag(task, TIF_SINGLE_STEP);
	if (task == current)
		update_cr_regs(task);
}

/*
 * Called by kernel/ptrace.c when detaching..
 *
 * Clear all debugging related fields.
 */
void ptrace_disable(struct task_struct *task)
{
	memset(&task->thread.per_user, 0, sizeof(task->thread.per_user));
	memset(&task->thread.per_event, 0, sizeof(task->thread.per_event));
	clear_tsk_thread_flag(task, TIF_SINGLE_STEP);
	clear_tsk_thread_flag(task, TIF_PER_TRAP);
	task->thread.per_flags = 0;
}

#ifndef CONFIG_64BIT
# define __ADDR_MASK 3
#else
# define __ADDR_MASK 7
#endif

static inline unsigned long __peek_user_per(struct task_struct *child,
					    addr_t addr)
{
	struct per_struct_kernel *dummy = NULL;

	if (addr == (addr_t) &dummy->cr9)
		/* Control bits of the active per set. */
		return test_thread_flag(TIF_SINGLE_STEP) ?
			PER_EVENT_IFETCH : child->thread.per_user.control;
	else if (addr == (addr_t) &dummy->cr10)
		/* Start address of the active per set. */
		return test_thread_flag(TIF_SINGLE_STEP) ?
			0 : child->thread.per_user.start;
	else if (addr == (addr_t) &dummy->cr11)
		/* End address of the active per set. */
		return test_thread_flag(TIF_SINGLE_STEP) ?
			PSW_ADDR_INSN : child->thread.per_user.end;
	else if (addr == (addr_t) &dummy->bits)
		/* Single-step bit. */
		return test_thread_flag(TIF_SINGLE_STEP) ?
			(1UL << (BITS_PER_LONG - 1)) : 0;
	else if (addr == (addr_t) &dummy->starting_addr)
		/* Start address of the user specified per set. */
		return child->thread.per_user.start;
	else if (addr == (addr_t) &dummy->ending_addr)
		/* End address of the user specified per set. */
		return child->thread.per_user.end;
	else if (addr == (addr_t) &dummy->perc_atmid)
		/* PER code, ATMID and AI of the last PER trap */
		return (unsigned long)
			child->thread.per_event.cause << (BITS_PER_LONG - 16);
	else if (addr == (addr_t) &dummy->address)
		/* Address of the last PER trap */
		return child->thread.per_event.address;
	else if (addr == (addr_t) &dummy->access_id)
		/* Access id of the last PER trap */
		return (unsigned long)
			child->thread.per_event.paid << (BITS_PER_LONG - 8);
	return 0;
}

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
			/* Return a clean psw mask. */
			tmp = psw_user_bits | (tmp & PSW_MASK_USER);

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
		 * Handle access to the per_info structure.
		 */
		addr -= (addr_t) &dummy->regs.per_info;
		tmp = __peek_user_per(child, addr);

	} else
		tmp = 0;

	return tmp;
}

static int
peek_user(struct task_struct *child, addr_t addr, addr_t data)
{
	addr_t tmp, mask;

	/*
	 * Stupid gdb peeks/pokes the access registers in 64 bit with
	 * an alignment of 4. Programmers from hell...
	 */
	mask = __ADDR_MASK;
#ifdef CONFIG_64BIT
	if (addr >= (addr_t) &((struct user *) NULL)->regs.acrs &&
	    addr < (addr_t) &((struct user *) NULL)->regs.orig_gpr2)
		mask = 3;
#endif
	if ((addr & mask) || addr > sizeof(struct user) - __ADDR_MASK)
		return -EIO;

	tmp = __peek_user(child, addr);
	return put_user(tmp, (addr_t __user *) data);
}

static inline void __poke_user_per(struct task_struct *child,
				   addr_t addr, addr_t data)
{
	struct per_struct_kernel *dummy = NULL;

	/*
	 * There are only three fields in the per_info struct that the
	 * debugger user can write to.
	 * 1) cr9: the debugger wants to set a new PER event mask
	 * 2) starting_addr: the debugger wants to set a new starting
	 *    address to use with the PER event mask.
	 * 3) ending_addr: the debugger wants to set a new ending
	 *    address to use with the PER event mask.
	 * The user specified PER event mask and the start and end
	 * addresses are used only if single stepping is not in effect.
	 * Writes to any other field in per_info are ignored.
	 */
	if (addr == (addr_t) &dummy->cr9)
		/* PER event mask of the user specified per set. */
		child->thread.per_user.control =
			data & (PER_EVENT_MASK | PER_CONTROL_MASK);
	else if (addr == (addr_t) &dummy->starting_addr)
		/* Starting address of the user specified per set. */
		child->thread.per_user.start = data;
	else if (addr == (addr_t) &dummy->ending_addr)
		/* Ending address of the user specified per set. */
		child->thread.per_user.end = data;
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
		    ((data & ~PSW_MASK_USER) != psw_user_bits ||
		     ((data & PSW_MASK_EA) && !(data & PSW_MASK_BA))))
			/* Invalid psw mask. */
			return -EINVAL;
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
		 * Handle access to the per_info structure.
		 */
		addr -= (addr_t) &dummy->regs.per_info;
		__poke_user_per(child, addr, data);

	}

	return 0;
}

static int poke_user(struct task_struct *child, addr_t addr, addr_t data)
{
	addr_t mask;

	/*
	 * Stupid gdb peeks/pokes the access registers in 64 bit with
	 * an alignment of 4. Programmers from hell indeed...
	 */
	mask = __ADDR_MASK;
#ifdef CONFIG_64BIT
	if (addr >= (addr_t) &((struct user *) NULL)->regs.acrs &&
	    addr < (addr_t) &((struct user *) NULL)->regs.orig_gpr2)
		mask = 3;
#endif
	if ((addr & mask) || addr > sizeof(struct user) - __ADDR_MASK)
		return -EIO;

	return __poke_user(child, addr, data);
}

long arch_ptrace(struct task_struct *child, long request,
		 unsigned long addr, unsigned long data)
{
	ptrace_area parea; 
	int copied, ret;

	switch (request) {
	case PTRACE_PEEKUSR:
		/* read the word at location addr in the USER area. */
		return peek_user(child, addr, data);

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
	case PTRACE_GET_LAST_BREAK:
		put_user(task_thread_info(child)->last_break,
			 (unsigned long __user *) data);
		return 0;
	case PTRACE_ENABLE_TE:
		if (!MACHINE_HAS_TE)
			return -EIO;
		child->thread.per_flags &= ~PER_FLAG_NO_TE;
		return 0;
	case PTRACE_DISABLE_TE:
		if (!MACHINE_HAS_TE)
			return -EIO;
		child->thread.per_flags |= PER_FLAG_NO_TE;
		child->thread.per_flags &= ~PER_FLAG_TE_ABORT_RAND;
		return 0;
	case PTRACE_TE_ABORT_RAND:
		if (!MACHINE_HAS_TE || (child->thread.per_flags & PER_FLAG_NO_TE))
			return -EIO;
		switch (data) {
		case 0UL:
			child->thread.per_flags &= ~PER_FLAG_TE_ABORT_RAND;
			break;
		case 1UL:
			child->thread.per_flags |= PER_FLAG_TE_ABORT_RAND;
			child->thread.per_flags |= PER_FLAG_TE_ABORT_RAND_TEND;
			break;
		case 2UL:
			child->thread.per_flags |= PER_FLAG_TE_ABORT_RAND;
			child->thread.per_flags &= ~PER_FLAG_TE_ABORT_RAND_TEND;
			break;
		default:
			return -EINVAL;
		}
		return 0;
	default:
		/* Removing high order bit from addr (only for 31 bit). */
		addr &= PSW_ADDR_INSN;
		return ptrace_request(child, request, addr, data);
	}
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
 * Same as peek_user_per but for a 31 bit program.
 */
static inline __u32 __peek_user_per_compat(struct task_struct *child,
					   addr_t addr)
{
	struct compat_per_struct_kernel *dummy32 = NULL;

	if (addr == (addr_t) &dummy32->cr9)
		/* Control bits of the active per set. */
		return (__u32) test_thread_flag(TIF_SINGLE_STEP) ?
			PER_EVENT_IFETCH : child->thread.per_user.control;
	else if (addr == (addr_t) &dummy32->cr10)
		/* Start address of the active per set. */
		return (__u32) test_thread_flag(TIF_SINGLE_STEP) ?
			0 : child->thread.per_user.start;
	else if (addr == (addr_t) &dummy32->cr11)
		/* End address of the active per set. */
		return test_thread_flag(TIF_SINGLE_STEP) ?
			PSW32_ADDR_INSN : child->thread.per_user.end;
	else if (addr == (addr_t) &dummy32->bits)
		/* Single-step bit. */
		return (__u32) test_thread_flag(TIF_SINGLE_STEP) ?
			0x80000000 : 0;
	else if (addr == (addr_t) &dummy32->starting_addr)
		/* Start address of the user specified per set. */
		return (__u32) child->thread.per_user.start;
	else if (addr == (addr_t) &dummy32->ending_addr)
		/* End address of the user specified per set. */
		return (__u32) child->thread.per_user.end;
	else if (addr == (addr_t) &dummy32->perc_atmid)
		/* PER code, ATMID and AI of the last PER trap */
		return (__u32) child->thread.per_event.cause << 16;
	else if (addr == (addr_t) &dummy32->address)
		/* Address of the last PER trap */
		return (__u32) child->thread.per_event.address;
	else if (addr == (addr_t) &dummy32->access_id)
		/* Access id of the last PER trap */
		return (__u32) child->thread.per_event.paid << 24;
	return 0;
}

/*
 * Same as peek_user but for a 31 bit program.
 */
static u32 __peek_user_compat(struct task_struct *child, addr_t addr)
{
	struct compat_user *dummy32 = NULL;
	addr_t offset;
	__u32 tmp;

	if (addr < (addr_t) &dummy32->regs.acrs) {
		struct pt_regs *regs = task_pt_regs(child);
		/*
		 * psw and gprs are stored on the stack
		 */
		if (addr == (addr_t) &dummy32->regs.psw.mask) {
			/* Fake a 31 bit psw mask. */
			tmp = (__u32)(regs->psw.mask >> 32);
			tmp = psw32_user_bits | (tmp & PSW32_MASK_USER);
		} else if (addr == (addr_t) &dummy32->regs.psw.addr) {
			/* Fake a 31 bit psw address. */
			tmp = (__u32) regs->psw.addr |
				(__u32)(regs->psw.mask & PSW_MASK_BA);
		} else {
			/* gpr 0-15 */
			tmp = *(__u32 *)((addr_t) &regs->psw + addr*2 + 4);
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
		 * Handle access to the per_info structure.
		 */
		addr -= (addr_t) &dummy32->regs.per_info;
		tmp = __peek_user_per_compat(child, addr);

	} else
		tmp = 0;

	return tmp;
}

static int peek_user_compat(struct task_struct *child,
			    addr_t addr, addr_t data)
{
	__u32 tmp;

	if (!is_compat_task() || (addr & 3) || addr > sizeof(struct user) - 3)
		return -EIO;

	tmp = __peek_user_compat(child, addr);
	return put_user(tmp, (__u32 __user *) data);
}

/*
 * Same as poke_user_per but for a 31 bit program.
 */
static inline void __poke_user_per_compat(struct task_struct *child,
					  addr_t addr, __u32 data)
{
	struct compat_per_struct_kernel *dummy32 = NULL;

	if (addr == (addr_t) &dummy32->cr9)
		/* PER event mask of the user specified per set. */
		child->thread.per_user.control =
			data & (PER_EVENT_MASK | PER_CONTROL_MASK);
	else if (addr == (addr_t) &dummy32->starting_addr)
		/* Starting address of the user specified per set. */
		child->thread.per_user.start = data;
	else if (addr == (addr_t) &dummy32->ending_addr)
		/* Ending address of the user specified per set. */
		child->thread.per_user.end = data;
}

/*
 * Same as poke_user but for a 31 bit program.
 */
static int __poke_user_compat(struct task_struct *child,
			      addr_t addr, addr_t data)
{
	struct compat_user *dummy32 = NULL;
	__u32 tmp = (__u32) data;
	addr_t offset;

	if (addr < (addr_t) &dummy32->regs.acrs) {
		struct pt_regs *regs = task_pt_regs(child);
		/*
		 * psw, gprs, acrs and orig_gpr2 are stored on the stack
		 */
		if (addr == (addr_t) &dummy32->regs.psw.mask) {
			/* Build a 64 bit psw mask from 31 bit mask. */
			if ((tmp & ~PSW32_MASK_USER) != psw32_user_bits)
				/* Invalid psw mask. */
				return -EINVAL;
			regs->psw.mask = (regs->psw.mask & ~PSW_MASK_USER) |
				(regs->psw.mask & PSW_MASK_BA) |
				(__u64)(tmp & PSW32_MASK_USER) << 32;
		} else if (addr == (addr_t) &dummy32->regs.psw.addr) {
			/* Build a 64 bit psw address from 31 bit address. */
			regs->psw.addr = (__u64) tmp & PSW32_ADDR_INSN;
			/* Transfer 31 bit amode bit to psw mask. */
			regs->psw.mask = (regs->psw.mask & ~PSW_MASK_BA) |
				(__u64)(tmp & PSW32_ADDR_AMODE);
		} else {
			/* gpr 0-15 */
			*(__u32*)((addr_t) &regs->psw + addr*2 + 4) = tmp;
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
		 * Handle access to the per_info structure.
		 */
		addr -= (addr_t) &dummy32->regs.per_info;
		__poke_user_per_compat(child, addr, data);
	}

	return 0;
}

static int poke_user_compat(struct task_struct *child,
			    addr_t addr, addr_t data)
{
	if (!is_compat_task() || (addr & 3) ||
	    addr > sizeof(struct compat_user) - 3)
		return -EIO;

	return __poke_user_compat(child, addr, data);
}

long compat_arch_ptrace(struct task_struct *child, compat_long_t request,
			compat_ulong_t caddr, compat_ulong_t cdata)
{
	unsigned long addr = caddr;
	unsigned long data = cdata;
	compat_ptrace_area parea;
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
	case PTRACE_GET_LAST_BREAK:
		put_user(task_thread_info(child)->last_break,
			 (unsigned int __user *) data);
		return 0;
	}
	return compat_ptrace_request(child, request, addr, data);
}
#endif

asmlinkage long do_syscall_trace_enter(struct pt_regs *regs)
{
	long ret = 0;

	/* Do the secure computing check first. */
	if (secure_computing(regs->gprs[2])) {
		/* seccomp failures shouldn't expose any additional code. */
		ret = -1;
		goto out;
	}

	/*
	 * The sysc_tracesys code in entry.S stored the system
	 * call number to gprs[2].
	 */
	if (test_thread_flag(TIF_SYSCALL_TRACE) &&
	    (tracehook_report_syscall_entry(regs) ||
	     regs->gprs[2] >= NR_syscalls)) {
		/*
		 * Tracing decided this syscall should not happen or the
		 * debugger stored an invalid system call number. Skip
		 * the system call and the system call restart handling.
		 */
		clear_thread_flag(TIF_SYSCALL);
		ret = -1;
	}

	if (unlikely(test_thread_flag(TIF_SYSCALL_TRACEPOINT)))
		trace_sys_enter(regs, regs->gprs[2]);

	audit_syscall_entry(is_compat_task() ?
				AUDIT_ARCH_S390 : AUDIT_ARCH_S390X,
			    regs->gprs[2], regs->orig_gpr2,
			    regs->gprs[3], regs->gprs[4],
			    regs->gprs[5]);
out:
	return ret ?: regs->gprs[2];
}

asmlinkage void do_syscall_trace_exit(struct pt_regs *regs)
{
	audit_syscall_exit(regs);

	if (unlikely(test_thread_flag(TIF_SYSCALL_TRACEPOINT)))
		trace_sys_exit(regs, regs->gprs[2]);

	if (test_thread_flag(TIF_SYSCALL_TRACE))
		tracehook_report_syscall_exit(regs, 0);
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

#ifdef CONFIG_64BIT

static int s390_last_break_get(struct task_struct *target,
			       const struct user_regset *regset,
			       unsigned int pos, unsigned int count,
			       void *kbuf, void __user *ubuf)
{
	if (count > 0) {
		if (kbuf) {
			unsigned long *k = kbuf;
			*k = task_thread_info(target)->last_break;
		} else {
			unsigned long  __user *u = ubuf;
			if (__put_user(task_thread_info(target)->last_break, u))
				return -EFAULT;
		}
	}
	return 0;
}

static int s390_last_break_set(struct task_struct *target,
			       const struct user_regset *regset,
			       unsigned int pos, unsigned int count,
			       const void *kbuf, const void __user *ubuf)
{
	return 0;
}

static int s390_tdb_get(struct task_struct *target,
			const struct user_regset *regset,
			unsigned int pos, unsigned int count,
			void *kbuf, void __user *ubuf)
{
	struct pt_regs *regs = task_pt_regs(target);
	unsigned char *data;

	if (!(regs->int_code & 0x200))
		return -ENODATA;
	data = target->thread.trap_tdb;
	return user_regset_copyout(&pos, &count, &kbuf, &ubuf, data, 0, 256);
}

static int s390_tdb_set(struct task_struct *target,
			const struct user_regset *regset,
			unsigned int pos, unsigned int count,
			const void *kbuf, const void __user *ubuf)
{
	return 0;
}

#endif

static int s390_system_call_get(struct task_struct *target,
				const struct user_regset *regset,
				unsigned int pos, unsigned int count,
				void *kbuf, void __user *ubuf)
{
	unsigned int *data = &task_thread_info(target)->system_call;
	return user_regset_copyout(&pos, &count, &kbuf, &ubuf,
				   data, 0, sizeof(unsigned int));
}

static int s390_system_call_set(struct task_struct *target,
				const struct user_regset *regset,
				unsigned int pos, unsigned int count,
				const void *kbuf, const void __user *ubuf)
{
	unsigned int *data = &task_thread_info(target)->system_call;
	return user_regset_copyin(&pos, &count, &kbuf, &ubuf,
				  data, 0, sizeof(unsigned int));
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
#ifdef CONFIG_64BIT
	[REGSET_LAST_BREAK] = {
		.core_note_type = NT_S390_LAST_BREAK,
		.n = 1,
		.size = sizeof(long),
		.align = sizeof(long),
		.get = s390_last_break_get,
		.set = s390_last_break_set,
	},
	[REGSET_TDB] = {
		.core_note_type = NT_S390_TDB,
		.n = 1,
		.size = 256,
		.align = 1,
		.get = s390_tdb_get,
		.set = s390_tdb_set,
	},
#endif
	[REGSET_SYSTEM_CALL] = {
		.core_note_type = NT_S390_SYSTEM_CALL,
		.n = 1,
		.size = sizeof(unsigned int),
		.align = sizeof(unsigned int),
		.get = s390_system_call_get,
		.set = s390_system_call_set,
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

static int s390_compat_regs_high_get(struct task_struct *target,
				     const struct user_regset *regset,
				     unsigned int pos, unsigned int count,
				     void *kbuf, void __user *ubuf)
{
	compat_ulong_t *gprs_high;

	gprs_high = (compat_ulong_t *)
		&task_pt_regs(target)->gprs[pos / sizeof(compat_ulong_t)];
	if (kbuf) {
		compat_ulong_t *k = kbuf;
		while (count > 0) {
			*k++ = *gprs_high;
			gprs_high += 2;
			count -= sizeof(*k);
		}
	} else {
		compat_ulong_t __user *u = ubuf;
		while (count > 0) {
			if (__put_user(*gprs_high, u++))
				return -EFAULT;
			gprs_high += 2;
			count -= sizeof(*u);
		}
	}
	return 0;
}

static int s390_compat_regs_high_set(struct task_struct *target,
				     const struct user_regset *regset,
				     unsigned int pos, unsigned int count,
				     const void *kbuf, const void __user *ubuf)
{
	compat_ulong_t *gprs_high;
	int rc = 0;

	gprs_high = (compat_ulong_t *)
		&task_pt_regs(target)->gprs[pos / sizeof(compat_ulong_t)];
	if (kbuf) {
		const compat_ulong_t *k = kbuf;
		while (count > 0) {
			*gprs_high = *k++;
			*gprs_high += 2;
			count -= sizeof(*k);
		}
	} else {
		const compat_ulong_t  __user *u = ubuf;
		while (count > 0 && !rc) {
			unsigned long word;
			rc = __get_user(word, u++);
			if (rc)
				break;
			*gprs_high = word;
			*gprs_high += 2;
			count -= sizeof(*u);
		}
	}

	return rc;
}

static int s390_compat_last_break_get(struct task_struct *target,
				      const struct user_regset *regset,
				      unsigned int pos, unsigned int count,
				      void *kbuf, void __user *ubuf)
{
	compat_ulong_t last_break;

	if (count > 0) {
		last_break = task_thread_info(target)->last_break;
		if (kbuf) {
			unsigned long *k = kbuf;
			*k = last_break;
		} else {
			unsigned long  __user *u = ubuf;
			if (__put_user(last_break, u))
				return -EFAULT;
		}
	}
	return 0;
}

static int s390_compat_last_break_set(struct task_struct *target,
				      const struct user_regset *regset,
				      unsigned int pos, unsigned int count,
				      const void *kbuf, const void __user *ubuf)
{
	return 0;
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
	[REGSET_LAST_BREAK] = {
		.core_note_type = NT_S390_LAST_BREAK,
		.n = 1,
		.size = sizeof(long),
		.align = sizeof(long),
		.get = s390_compat_last_break_get,
		.set = s390_compat_last_break_set,
	},
	[REGSET_TDB] = {
		.core_note_type = NT_S390_TDB,
		.n = 1,
		.size = 256,
		.align = 1,
		.get = s390_tdb_get,
		.set = s390_tdb_set,
	},
	[REGSET_SYSTEM_CALL] = {
		.core_note_type = NT_S390_SYSTEM_CALL,
		.n = 1,
		.size = sizeof(compat_uint_t),
		.align = sizeof(compat_uint_t),
		.get = s390_system_call_get,
		.set = s390_system_call_set,
	},
	[REGSET_GENERAL_EXTENDED] = {
		.core_note_type = NT_S390_HIGH_GPRS,
		.n = sizeof(s390_compat_regs_high) / sizeof(compat_long_t),
		.size = sizeof(compat_long_t),
		.align = sizeof(compat_long_t),
		.get = s390_compat_regs_high_get,
		.set = s390_compat_regs_high_set,
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

static const char *gpr_names[NUM_GPRS] = {
	"r0", "r1",  "r2",  "r3",  "r4",  "r5",  "r6",  "r7",
	"r8", "r9", "r10", "r11", "r12", "r13", "r14", "r15",
};

unsigned long regs_get_register(struct pt_regs *regs, unsigned int offset)
{
	if (offset >= NUM_GPRS)
		return 0;
	return regs->gprs[offset];
}

int regs_query_register_offset(const char *name)
{
	unsigned long offset;

	if (!name || *name != 'r')
		return -EINVAL;
	if (kstrtoul(name + 1, 10, &offset))
		return -EINVAL;
	if (offset >= NUM_GPRS)
		return -EINVAL;
	return offset;
}

const char *regs_query_register_name(unsigned int offset)
{
	if (offset >= NUM_GPRS)
		return NULL;
	return gpr_names[offset];
}

static int regs_within_kernel_stack(struct pt_regs *regs, unsigned long addr)
{
	unsigned long ksp = kernel_stack_pointer(regs);

	return (addr & ~(THREAD_SIZE - 1)) == (ksp & ~(THREAD_SIZE - 1));
}

/**
 * regs_get_kernel_stack_nth() - get Nth entry of the stack
 * @regs:pt_regs which contains kernel stack pointer.
 * @n:stack entry number.
 *
 * regs_get_kernel_stack_nth() returns @n th entry of the kernel stack which
 * is specifined by @regs. If the @n th entry is NOT in the kernel stack,
 * this returns 0.
 */
unsigned long regs_get_kernel_stack_nth(struct pt_regs *regs, unsigned int n)
{
	unsigned long addr;

	addr = kernel_stack_pointer(regs) + n * sizeof(long);
	if (!regs_within_kernel_stack(regs, addr))
		return 0;
	return *(unsigned long *)addr;
}
