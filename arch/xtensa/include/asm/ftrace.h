/*
 * arch/xtensa/include/asm/ftrace.h
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2013 Tensilica Inc.
 */
#ifndef _XTENSA_FTRACE_H
#define _XTENSA_FTRACE_H

#include <asm/processor.h>

#ifndef __ASSEMBLY__
extern unsigned long return_address(unsigned level);
#define ftrace_return_address(n) return_address(n)
#endif /* __ASSEMBLY__ */

#ifdef CONFIG_FUNCTION_TRACER

#define MCOUNT_ADDR ((unsigned long)(_mcount))
#define MCOUNT_INSN_SIZE 3

#ifndef __ASSEMBLY__
extern void _mcount(void);
#define mcount _mcount
#endif /* __ASSEMBLY__ */
#endif /* CONFIG_FUNCTION_TRACER */

#endif /* _XTENSA_FTRACE_H */
