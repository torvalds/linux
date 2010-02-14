/*
 * linux/kernel/ptrace.c is by Ross Biro 1/23/92, edited by Linus Torvalds
 * these modifications are Copyright 2004-2009 Analog Devices Inc.
 *
 * Licensed under the GPL-2
 */

#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/mm.h>
#include <linux/smp.h>
#include <linux/errno.h>
#include <linux/ptrace.h>
#include <linux/user.h>
#include <linux/signal.h>
#include <linux/uaccess.h>

#include <asm/page.h>
#include <asm/pgtable.h>
#include <asm/system.h>
#include <asm/processor.h>
#include <asm/asm-offsets.h>
#include <asm/dma.h>
#include <asm/fixed_code.h>
#include <asm/cacheflush.h>
#include <asm/mem_map.h>

/*
 * does not yet catch signals sent when the child dies.
 * in exit.c or in signal.c.
 */

/* Find the stack offset for a register, relative to thread.esp0. */
#define PT_REG(reg)	((long)&((struct pt_regs *)0)->reg)

/*
 * Get the address of the live pt_regs for the specified task.
 * These are saved onto the top kernel stack when the process
 * is not running.
 *
 * Note: if a user thread is execve'd from kernel space, the
 * kernel stack will not be empty on entry to the kernel, so
 * ptracing these tasks will fail.
 */
static inline struct pt_regs *task_pt_regs(struct task_struct *task)
{
	return (struct pt_regs *)
	    ((unsigned long)task_stack_page(task) +
	     (THREAD_SIZE - sizeof(struct pt_regs)));
}

/*
 * Get all user integer registers.
 */
static inline int ptrace_getregs(struct task_struct *tsk, void __user *uregs)
{
	struct pt_regs regs;
	memcpy(&regs, task_pt_regs(tsk), sizeof(regs));
	regs.usp = tsk->thread.usp;
	return copy_to_user(uregs, &regs, sizeof(struct pt_regs)) ? -EFAULT : 0;
}

/* Mapping from PT_xxx to the stack offset at which the register is
 * saved.  Notice that usp has no stack-slot and needs to be treated
 * specially (see get_reg/put_reg below).
 */

/*
 * Get contents of register REGNO in task TASK.
 */
static inline long
get_reg(struct task_struct *task, long regno, unsigned long __user *datap)
{
	long tmp;
	struct pt_regs *regs = task_pt_regs(task);

	if (regno & 3 || regno > PT_LAST_PSEUDO || regno < 0)
		return -EIO;

	switch (regno) {
	case PT_TEXT_ADDR:
		tmp = task->mm->start_code;
		break;
	case PT_TEXT_END_ADDR:
		tmp = task->mm->end_code;
		break;
	case PT_DATA_ADDR:
		tmp = task->mm->start_data;
		break;
	case PT_USP:
		tmp = task->thread.usp;
		break;
	default:
		if (regno < sizeof(*regs)) {
			void *reg_ptr = regs;
			tmp = *(long *)(reg_ptr + regno);
		} else
			return -EIO;
	}

	return put_user(tmp, datap);
}

/*
 * Write contents of register REGNO in task TASK.
 */
static inline int
put_reg(struct task_struct *task, long regno, unsigned long data)
{
	struct pt_regs *regs = task_pt_regs(task);

	if (regno & 3 || regno > PT_LAST_PSEUDO || regno < 0)
		return -EIO;

	switch (regno) {
	case PT_PC:
		/*********************************************************************/
		/* At this point the kernel is most likely in exception.             */
		/* The RETX register will be used to populate the pc of the process. */
		/*********************************************************************/
		regs->retx = data;
		regs->pc = data;
		break;
	case PT_RETX:
		break;		/* regs->retx = data; break; */
	case PT_USP:
		regs->usp = data;
		task->thread.usp = data;
		break;
	case PT_SYSCFG:	/* don't let userspace screw with this */
		if ((data & ~1) != 0x6)
			pr_warning("ptrace: ignore syscfg write of %#lx\n", data);
		break;		/* regs->syscfg = data; break; */
	default:
		if (regno < sizeof(*regs)) {
			void *reg_offset = regs;
			*(long *)(reg_offset + regno) = data;
		}
		/* Ignore writes to pseudo registers */
	}

	return 0;
}

/*
 * check that an address falls within the bounds of the target process's memory mappings
 */
static inline int is_user_addr_valid(struct task_struct *child,
				     unsigned long start, unsigned long len)
{
	struct vm_area_struct *vma;
	struct sram_list_struct *sraml;

	/* overflow */
	if (start + len < start)
		return -EIO;

	vma = find_vma(child->mm, start);
	if (vma && start >= vma->vm_start && start + len <= vma->vm_end)
			return 0;

	for (sraml = child->mm->context.sram_list; sraml; sraml = sraml->next)
		if (start >= (unsigned long)sraml->addr
		    && start + len < (unsigned long)sraml->addr + sraml->length)
			return 0;

	if (start >= FIXED_CODE_START && start + len < FIXED_CODE_END)
		return 0;

	return -EIO;
}

void ptrace_enable(struct task_struct *child)
{
	struct pt_regs *regs = task_pt_regs(child);
	regs->syscfg |= SYSCFG_SSSTEP;
}

/*
 * Called by kernel/ptrace.c when detaching..
 *
 * Make sure the single step bit is not set.
 */
void ptrace_disable(struct task_struct *child)
{
	struct pt_regs *regs = task_pt_regs(child);
	regs->syscfg &= ~SYSCFG_SSSTEP;
}

long arch_ptrace(struct task_struct *child, long request, long addr, long data)
{
	int ret;
	unsigned long __user *datap = (unsigned long __user *)data;
	void *paddr = (void *)addr;

	switch (request) {
		/* when I and D space are separate, these will need to be fixed. */
	case PTRACE_PEEKDATA:
		pr_debug("ptrace: PEEKDATA\n");
		/* fall through */
	case PTRACE_PEEKTEXT:	/* read word at location addr. */
		{
			unsigned long tmp = 0;
			int copied = 0, to_copy = sizeof(tmp);

			ret = -EIO;
			pr_debug("ptrace: PEEKTEXT at addr 0x%08lx + %i\n", addr, to_copy);
			if (is_user_addr_valid(child, addr, to_copy) < 0)
				break;
			pr_debug("ptrace: user address is valid\n");

			switch (bfin_mem_access_type(addr, to_copy)) {
			case BFIN_MEM_ACCESS_CORE:
			case BFIN_MEM_ACCESS_CORE_ONLY:
				copied = access_process_vm(child, addr, &tmp,
				                           to_copy, 0);
				if (copied)
					break;

				/* hrm, why didn't that work ... maybe no mapping */
				if (addr >= FIXED_CODE_START &&
				    addr + to_copy <= FIXED_CODE_END) {
					copy_from_user_page(0, 0, 0, &tmp, paddr, to_copy);
					copied = to_copy;
				} else if (addr >= BOOT_ROM_START) {
					memcpy(&tmp, paddr, to_copy);
					copied = to_copy;
				}

				break;
			case BFIN_MEM_ACCESS_DMA:
				if (safe_dma_memcpy(&tmp, paddr, to_copy))
					copied = to_copy;
				break;
			case BFIN_MEM_ACCESS_ITEST:
				if (isram_memcpy(&tmp, paddr, to_copy))
					copied = to_copy;
				break;
			default:
				copied = 0;
				break;
			}

			pr_debug("ptrace: copied size %d [0x%08lx]\n", copied, tmp);
			if (copied == to_copy)
				ret = put_user(tmp, datap);
			break;
		}

#ifdef CONFIG_BINFMT_ELF_FDPIC
	case PTRACE_GETFDPIC: {
		unsigned long tmp = 0;

		switch (addr) {
		case_PTRACE_GETFDPIC_EXEC:
		case PTRACE_GETFDPIC_EXEC:
			tmp = child->mm->context.exec_fdpic_loadmap;
			break;
		case_PTRACE_GETFDPIC_INTERP:
		case PTRACE_GETFDPIC_INTERP:
			tmp = child->mm->context.interp_fdpic_loadmap;
			break;
		default:
			break;
		}

		ret = put_user(tmp, datap);
		break;
	}
#endif

		/* when I and D space are separate, this will have to be fixed. */
	case PTRACE_POKEDATA:
		pr_debug("ptrace: PTRACE_PEEKDATA\n");
		/* fall through */
	case PTRACE_POKETEXT:	/* write the word at location addr. */
		{
			int copied = 0, to_copy = sizeof(data);

			ret = -EIO;
			pr_debug("ptrace: POKETEXT at addr 0x%08lx + %i bytes %lx\n",
			         addr, to_copy, data);
			if (is_user_addr_valid(child, addr, to_copy) < 0)
				break;
			pr_debug("ptrace: user address is valid\n");

			switch (bfin_mem_access_type(addr, to_copy)) {
			case BFIN_MEM_ACCESS_CORE:
			case BFIN_MEM_ACCESS_CORE_ONLY:
				copied = access_process_vm(child, addr, &data,
				                           to_copy, 1);
				break;
			case BFIN_MEM_ACCESS_DMA:
				if (safe_dma_memcpy(paddr, &data, to_copy))
					copied = to_copy;
				break;
			case BFIN_MEM_ACCESS_ITEST:
				if (isram_memcpy(paddr, &data, to_copy))
					copied = to_copy;
				break;
			default:
				copied = 0;
				break;
			}

			pr_debug("ptrace: copied size %d\n", copied);
			if (copied == to_copy)
				ret = 0;
			break;
		}

	case PTRACE_PEEKUSR:
		switch (addr) {
#ifdef CONFIG_BINFMT_ELF_FDPIC	/* backwards compat */
		case PT_FDPIC_EXEC:   goto case_PTRACE_GETFDPIC_EXEC;
		case PT_FDPIC_INTERP: goto case_PTRACE_GETFDPIC_INTERP;
#endif
		default:
			ret = get_reg(child, addr, datap);
		}
		pr_debug("ptrace: PEEKUSR reg %li with %#lx = %i\n", addr, data, ret);
		break;

	case PTRACE_POKEUSR:
		ret = put_reg(child, addr, data);
		pr_debug("ptrace: POKEUSR reg %li with %li = %i\n", addr, data, ret);
		break;

	case PTRACE_GETREGS:
		/* Get all gp regs from the child. */
		ret = ptrace_getregs(child, datap);
		break;

	case PTRACE_SETREGS:
		printk(KERN_WARNING "ptrace: SETREGS: **** NOT IMPLEMENTED ***\n");
		/* Set all gp regs in the child. */
		ret = 0;
		break;

	default:
		ret = ptrace_request(child, request, addr, data);
		break;
	}

	return ret;
}

asmlinkage void syscall_trace(void)
{
	if (!test_thread_flag(TIF_SYSCALL_TRACE))
		return;

	if (!(current->ptrace & PT_PTRACED))
		return;

	/* the 0x80 provides a way for the tracing parent to distinguish
	 * between a syscall stop and SIGTRAP delivery
	 */
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
}
