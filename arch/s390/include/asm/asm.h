/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_S390_ASM_H
#define _ASM_S390_ASM_H

#include <linux/stringify.h>

/*
 * Helper macros to be used for flag output operand handling.
 * Inline assemblies must use four of the five supplied macros:
 *
 * Use CC_IPM(sym) at the end of the inline assembly; this extracts the
 * condition code and program mask with the ipm instruction and writes it to
 * the variable with symbolic name [sym] if the compiler has no support for
 * flag output operands. If the compiler has support for flag output operands
 * this generates no code.
 *
 * Use CC_OUT(sym, var) at the output operand list of an inline assembly. This
 * defines an output operand with symbolic name [sym] for the variable
 * [var]. [var] must be an int variable and [sym] must be identical with [sym]
 * used with CC_IPM().
 *
 * Use either CC_CLOBBER or CC_CLOBBER_LIST() for the clobber list. Use
 * CC_CLOBBER if the clobber list contains only "cc", otherwise use
 * CC_CLOBBER_LIST() and add all clobbers as argument to the macro.
 *
 * Use CC_TRANSFORM() to convert the variable [var] which contains the
 * extracted condition code. If the condition code is extracted with ipm, the
 * [var] also contains the program mask. CC_TRANSFORM() moves the condition
 * code to the two least significant bits and sets all other bits to zero.
 */
#if defined(__GCC_ASM_FLAG_OUTPUTS__) && !(IS_ENABLED(CONFIG_CC_ASM_FLAG_OUTPUT_BROKEN))

#define __HAVE_ASM_FLAG_OUTPUTS__

#define CC_IPM(sym)
#define CC_OUT(sym, var)	"=@cc" (var)
#define CC_TRANSFORM(cc)	({ cc; })
#define CC_CLOBBER
#define CC_CLOBBER_LIST(...)	__VA_ARGS__

#else

#define CC_IPM(sym)		"	ipm	%[" __stringify(sym) "]\n"
#define CC_OUT(sym, var)	[sym] "=d" (var)
#define CC_TRANSFORM(cc)	({ (cc) >> 28; })
#define CC_CLOBBER		"cc"
#define CC_CLOBBER_LIST(...)	"cc", __VA_ARGS__

#endif

#endif /* _ASM_S390_ASM_H */
