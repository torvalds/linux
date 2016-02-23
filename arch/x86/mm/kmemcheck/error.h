#ifndef ARCH__X86__MM__KMEMCHECK__ERROR_H
#define ARCH__X86__MM__KMEMCHECK__ERROR_H

#include <linux/ptrace.h>

#include "shadow.h"

void kmemcheck_error_save(enum kmemcheck_shadow state,
	unsigned long address, unsigned int size, struct pt_regs *regs);

void kmemcheck_error_save_bug(struct pt_regs *regs);

void kmemcheck_error_recall(void);

#endif
