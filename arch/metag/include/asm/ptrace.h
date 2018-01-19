/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _METAG_PTRACE_H
#define _METAG_PTRACE_H

#include <linux/compiler.h>
#include <uapi/asm/ptrace.h>
#include <asm/tbx.h>

#ifndef __ASSEMBLY__

/* this struct defines the way the registers are stored on the
   stack during a system call. */

struct pt_regs {
	TBICTX ctx;
	TBICTXEXTCB0 extcb0[5];
};

#define user_mode(regs) (((regs)->ctx.SaveMask & TBICTX_PRIV_BIT) > 0)

#define instruction_pointer(regs) ((unsigned long)(regs)->ctx.CurrPC)
#define profile_pc(regs) instruction_pointer(regs)

#define task_pt_regs(task) \
	((struct pt_regs *)(task_stack_page(task) + \
			    sizeof(struct thread_info)))

#define current_pt_regs() \
	((struct pt_regs *)((char *)current_thread_info() + \
			    sizeof(struct thread_info)))

int syscall_trace_enter(struct pt_regs *regs);
void syscall_trace_leave(struct pt_regs *regs);

/* copy a struct user_gp_regs out to user */
int metag_gp_regs_copyout(const struct pt_regs *regs,
			  unsigned int pos, unsigned int count,
			  void *kbuf, void __user *ubuf);
/* copy a struct user_gp_regs in from user */
int metag_gp_regs_copyin(struct pt_regs *regs,
			 unsigned int pos, unsigned int count,
			 const void *kbuf, const void __user *ubuf);
/* copy a struct user_cb_regs out to user */
int metag_cb_regs_copyout(const struct pt_regs *regs,
			  unsigned int pos, unsigned int count,
			  void *kbuf, void __user *ubuf);
/* copy a struct user_cb_regs in from user */
int metag_cb_regs_copyin(struct pt_regs *regs,
			 unsigned int pos, unsigned int count,
			 const void *kbuf, const void __user *ubuf);
/* copy a struct user_rp_state out to user */
int metag_rp_state_copyout(const struct pt_regs *regs,
			   unsigned int pos, unsigned int count,
			   void *kbuf, void __user *ubuf);
/* copy a struct user_rp_state in from user */
int metag_rp_state_copyin(struct pt_regs *regs,
			  unsigned int pos, unsigned int count,
			  const void *kbuf, const void __user *ubuf);

#endif /* __ASSEMBLY__ */
#endif /* _METAG_PTRACE_H */
