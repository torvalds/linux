#ifdef __i386__
#include "ptrace_32.h"
#else
#include "ptrace_64.h"
#endif

static inline long regs_return_value(struct uml_pt_regs *regs)
{
	return UPT_SYSCALL_RET(regs);
}
