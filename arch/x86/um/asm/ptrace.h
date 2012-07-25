#ifndef __UM_X86_PTRACE_H
#define __UM_X86_PTRACE_H

#ifdef CONFIG_X86_32
# include "ptrace_32.h"
#else
# include "ptrace_64.h"
#endif

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
#define UPT_SET_SYSCALL_RETURN(r, res) (UPT_AX(r) = (res))

static inline long regs_return_value(struct uml_pt_regs *regs)
{
	return UPT_AX(regs);
}
#endif /* __UM_X86_PTRACE_H */
