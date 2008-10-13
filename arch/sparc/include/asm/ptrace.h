#ifndef ___ASM_SPARC_PTRACE_H
#define ___ASM_SPARC_PTRACE_H
#if defined(__sparc__) && defined(__arch64__)
#include <asm/ptrace_64.h>
#else
#include <asm/ptrace_32.h>
#endif
#endif
