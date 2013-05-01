#ifndef _ASM_METAG_BUG_H
#define _ASM_METAG_BUG_H

#include <asm-generic/bug.h>

struct pt_regs;

extern const char *trap_name(int trapno);
extern void die(const char *str, struct pt_regs *regs, long err,
		unsigned long addr) __attribute__ ((noreturn));

#endif
