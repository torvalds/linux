/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __UM_X86_PTRACE_H
#define __UM_X86_PTRACE_H

#include <linux/compiler.h>
#ifndef CONFIG_X86_32
#define __FRAME_OFFSETS /* Needed to get the R* macros */
#endif
#include <asm/ptrace-generic.h>

#define user_mode(r) UPT_IS_USER(&(r)->regs)

#define PT_REGS_AX(r) UPT_AX(&(r)->regs)
#define PT_REGS_BX(r) UPT_BX(&(r)->regs)
#define PT_REGS_CX(r) UPT_CX(&(r)->regs)
#define PT_REGS_DX(r) UPT_DX(&(r)->regs)

#define PT_REGS_SI(r) UPT_SI(&(r)->regs)
#define PT_REGS_DI(r) UPT_DI(&(r)->regs)
#define PT_REGS_BP(r) UPT_BP(&(r)->regs)
#define PT_REGS_EFLAGS(r) UPT_EFLAGS(&(r)->regs)

#define PT_REGS_CS(r) UPT_CS(&(r)->regs)
#define PT_REGS_SS(r) UPT_SS(&(r)->regs)
#define PT_REGS_DS(r) UPT_DS(&(r)->regs)
#define PT_REGS_ES(r) UPT_ES(&(r)->regs)

#define PT_REGS_ORIG_SYSCALL(r) PT_REGS_AX(r)
#define PT_REGS_SYSCALL_RET(r) PT_REGS_AX(r)

#define PT_FIX_EXEC_STACK(sp) do ; while(0)

#define profile_pc(regs) PT_REGS_IP(regs)

#define UPT_RESTART_SYSCALL(r) (UPT_IP(r) -= 2)
#define PT_REGS_SET_SYSCALL_RETURN(r, res) (PT_REGS_AX(r) = (res))

static inline long regs_return_value(struct pt_regs *regs)
{
	return PT_REGS_AX(regs);
}

/*
 * Forward declaration to avoid including sysdep/tls.h, which causes a
 * circular include, and compilation failures.
 */
struct user_desc;

#ifdef CONFIG_X86_32

extern int ptrace_get_thread_area(struct task_struct *child, int idx,
                                  struct user_desc __user *user_desc);

extern int ptrace_set_thread_area(struct task_struct *child, int idx,
                                  struct user_desc __user *user_desc);

#else

#define PT_REGS_R8(r) UPT_R8(&(r)->regs)
#define PT_REGS_R9(r) UPT_R9(&(r)->regs)
#define PT_REGS_R10(r) UPT_R10(&(r)->regs)
#define PT_REGS_R11(r) UPT_R11(&(r)->regs)
#define PT_REGS_R12(r) UPT_R12(&(r)->regs)
#define PT_REGS_R13(r) UPT_R13(&(r)->regs)
#define PT_REGS_R14(r) UPT_R14(&(r)->regs)
#define PT_REGS_R15(r) UPT_R15(&(r)->regs)

#include <asm/errno.h>

static inline int ptrace_get_thread_area(struct task_struct *child, int idx,
                                         struct user_desc __user *user_desc)
{
        return -ENOSYS;
}

static inline int ptrace_set_thread_area(struct task_struct *child, int idx,
                                         struct user_desc __user *user_desc)
{
        return -ENOSYS;
}

extern long arch_prctl(struct task_struct *task, int option,
		       unsigned long __user *addr);

#endif
#define user_stack_pointer(regs) PT_REGS_SP(regs)
#endif /* __UM_X86_PTRACE_H */
