/*
 *  linux/arch/h8300/kernel/ptrace.c
 *
 *  Yoshinori Sato <ysato@users.sourceforge.jp>
 *
 *  Based on:
 *  linux/arch/m68k/kernel/ptrace.c
 *
 *  Copyright (C) 1994 by Hamish Macdonald
 *  Taken from linux/kernel/ptrace.c and modified for M680x0.
 *  linux/kernel/ptrace.c is by Ross Biro 1/23/92, edited by Linus Torvalds
 *
 * This file is subject to the terms and conditions of the GNU General
 * Public License.  See the file COPYING in the main directory of
 * this archive for more details.
 */

#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/mm.h>
#include <linux/smp.h>
#include <linux/errno.h>
#include <linux/ptrace.h>
#include <linux/user.h>
#include <linux/signal.h>

#include <asm/uaccess.h>
#include <asm/page.h>
#include <asm/pgtable.h>
#include <asm/system.h>
#include <asm/processor.h>
#include <asm/signal.h>

/* cpu depend functions */
extern long h8300_get_reg(struct task_struct *task, int regno);
extern int  h8300_put_reg(struct task_struct *task, int regno, unsigned long data);
extern void h8300_disable_trace(struct task_struct *child);
extern void h8300_enable_trace(struct task_struct *child);

/*
 * does not yet catch signals sent when the child dies.
 * in exit.c or in signal.c.
 */

inline
static int read_long(struct task_struct * tsk, unsigned long addr,
	unsigned long * result)
{
	*result = *(unsigned long *)addr;
	return 0;
}

void ptrace_disable(struct task_struct *child)
{
	h8300_disable_trace(child);
}

long arch_ptrace(struct task_struct *child, long request, long addr, long data)
{
	int ret;

	switch (request) {
		case PTRACE_PEEKTEXT: /* read word at location addr. */ 
		case PTRACE_PEEKDATA: {
			unsigned long tmp;

			ret = read_long(child, addr, &tmp);
			if (ret < 0)
				break ;
			ret = put_user(tmp, (unsigned long *) data);
			break ;
		}

	/* read the word at location addr in the USER area. */
		case PTRACE_PEEKUSR: {
			unsigned long tmp = 0;
			
			if ((addr & 3) || addr < 0 || addr >= sizeof(struct user)) {
				ret = -EIO;
				break ;
			}
			
		        ret = 0;  /* Default return condition */
			addr = addr >> 2; /* temporary hack. */

			if (addr < H8300_REGS_NO)
				tmp = h8300_get_reg(child, addr);
			else {
				switch(addr) {
				case 49:
					tmp = child->mm->start_code;
					break ;
				case 50:
					tmp = child->mm->start_data;
					break ;
				case 51:
					tmp = child->mm->end_code;
					break ;
				case 52:
					tmp = child->mm->end_data;
					break ;
				default:
					ret = -EIO;
				}
			}
			if (!ret)
				ret = put_user(tmp,(unsigned long *) data);
			break ;
		}

      /* when I and D space are separate, this will have to be fixed. */
		case PTRACE_POKETEXT: /* write the word at location addr. */
		case PTRACE_POKEDATA:
			ret = 0;
			if (access_process_vm(child, addr, &data, sizeof(data), 1) == sizeof(data))
				break;
			ret = -EIO;
			break;

		case PTRACE_POKEUSR: /* write the word at location addr in the USER area */
			if ((addr & 3) || addr < 0 || addr >= sizeof(struct user)) {
				ret = -EIO;
				break ;
			}
			addr = addr >> 2; /* temporary hack. */
			    
			if (addr == PT_ORIG_ER0) {
				ret = -EIO;
				break ;
			}
			if (addr < H8300_REGS_NO) {
				ret = h8300_put_reg(child, addr, data);
				break ;
			}
			ret = -EIO;
			break ;
		case PTRACE_SYSCALL: /* continue and stop at next (return from) syscall */
		case PTRACE_CONT: { /* restart after signal. */
			ret = -EIO;
			if (!valid_signal(data))
				break ;
			if (request == PTRACE_SYSCALL)
				set_tsk_thread_flag(child, TIF_SYSCALL_TRACE);
			else
				clear_tsk_thread_flag(child, TIF_SYSCALL_TRACE);
			child->exit_code = data;
			wake_up_process(child);
			/* make sure the single step bit is not set. */
			h8300_disable_trace(child);
			ret = 0;
		}

/*
 * make the child exit.  Best I can do is send it a sigkill. 
 * perhaps it should be put in the status that it wants to 
 * exit.
 */
		case PTRACE_KILL: {

			ret = 0;
			if (child->exit_state == EXIT_ZOMBIE) /* already dead */
				break;
			child->exit_code = SIGKILL;
			h8300_disable_trace(child);
			wake_up_process(child);
			break;
		}

		case PTRACE_SINGLESTEP: {  /* set the trap flag. */
			ret = -EIO;
			if (!valid_signal(data))
				break;
			clear_tsk_thread_flag(child, TIF_SYSCALL_TRACE);
			child->exit_code = data;
			h8300_enable_trace(child);
			wake_up_process(child);
			ret = 0;
			break;
		}

		case PTRACE_DETACH:	/* detach a process that was attached. */
			ret = ptrace_detach(child, data);
			break;

		case PTRACE_GETREGS: { /* Get all gp regs from the child. */
		  	int i;
			unsigned long tmp;
			for (i = 0; i < H8300_REGS_NO; i++) {
			    tmp = h8300_get_reg(child, i);
			    if (put_user(tmp, (unsigned long *) data)) {
				ret = -EFAULT;
				break;
			    }
			    data += sizeof(long);
			}
			ret = 0;
			break;
		}

		case PTRACE_SETREGS: { /* Set all gp regs in the child. */
			int i;
			unsigned long tmp;
			for (i = 0; i < H8300_REGS_NO; i++) {
			    if (get_user(tmp, (unsigned long *) data)) {
				ret = -EFAULT;
				break;
			    }
			    h8300_put_reg(child, i, tmp);
			    data += sizeof(long);
			}
			ret = 0;
			break;
		}

		default:
			ret = -EIO;
			break;
	}
	return ret;
}

asmlinkage void do_syscall_trace(void)
{
	if (!test_thread_flag(TIF_SYSCALL_TRACE))
		return;
	if (!(current->ptrace & PT_PTRACED))
		return;
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
