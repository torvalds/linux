#ifndef _ASM_POWERPC_SIGNAL_H
#define _ASM_POWERPC_SIGNAL_H

#include <uapi/asm/signal.h>

struct pt_regs;
#define ptrace_signal_deliver(regs, cookie) do { } while (0)
#endif /* _ASM_POWERPC_SIGNAL_H */
