/*
 * arch/arm64/include/asm/ftrace.h
 *
 * Copyright (C) 2013 Linaro Limited
 * Author: AKASHI Takahiro <takahiro.akashi@linaro.org>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#ifndef __ASM_FTRACE_H
#define __ASM_FTRACE_H

#include <asm/insn.h>

#define MCOUNT_ADDR		((unsigned long)_mcount)
#define MCOUNT_INSN_SIZE	AARCH64_INSN_SIZE

#ifndef __ASSEMBLY__
extern void _mcount(unsigned long);
#endif /* __ASSEMBLY__ */

#endif /* __ASM_FTRACE_H */
