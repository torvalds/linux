/* SPDX-License-Identifier: GPL-2.0 */
/*
 * __get_user functions.
 *
 * (C) Copyright 1998 Linus Torvalds
 * (C) Copyright 2005 Andi Kleen
 * (C) Copyright 2008 Glauber Costa
 *
 * These functions have a non-standard call interface
 * to make them more efficient, especially as they
 * return an error value in addition to the "real"
 * return value.
 */

/*
 * __get_user_X
 *
 * Inputs:	%[r|e]ax contains the address.
 *
 * Outputs:	%[r|e]ax is error code (0 or -EFAULT)
 *		%[r|e]dx contains zero-extended value
 *		%ecx contains the high half for 32-bit __get_user_8
 *
 *
 * These functions should not modify any other registers,
 * as they get called from within inline assembly.
 */

#include <linux/linkage.h>
#include <asm/page_types.h>
#include <asm/errno.h>
#include <asm/asm-offsets.h>
#include <asm/thread_info.h>
#include <asm/asm.h>
#include <asm/smap.h>
#include <asm/export.h>

#define ASM_BARRIER_NOSPEC ALTERNATIVE "", "lfence", X86_FEATURE_LFENCE_RDTSC

.macro check_range size:req
.if IS_ENABLED(CONFIG_X86_64)
	mov %rax, %rdx
	sar $63, %rdx
	or %rdx, %rax
.else
	cmp $TASK_SIZE_MAX-\size+1, %eax
	jae .Lbad_get_user
	sbb %edx, %edx		/* array_index_mask_nospec() */
	and %edx, %eax
.endif
.endm

	.text
SYM_FUNC_START(__get_user_1)
	check_range size=1
	ASM_STAC
1:	movzbl (%_ASM_AX),%edx
	xor %eax,%eax
	ASM_CLAC
	RET
SYM_FUNC_END(__get_user_1)
EXPORT_SYMBOL(__get_user_1)

SYM_FUNC_START(__get_user_2)
	check_range size=2
	ASM_STAC
2:	movzwl (%_ASM_AX),%edx
	xor %eax,%eax
	ASM_CLAC
	RET
SYM_FUNC_END(__get_user_2)
EXPORT_SYMBOL(__get_user_2)

SYM_FUNC_START(__get_user_4)
	check_range size=4
	ASM_STAC
3:	movl (%_ASM_AX),%edx
	xor %eax,%eax
	ASM_CLAC
	RET
SYM_FUNC_END(__get_user_4)
EXPORT_SYMBOL(__get_user_4)

SYM_FUNC_START(__get_user_8)
	check_range size=8
	ASM_STAC
#ifdef CONFIG_X86_64
4:	movq (%_ASM_AX),%rdx
#else
4:	movl (%_ASM_AX),%edx
5:	movl 4(%_ASM_AX),%ecx
#endif
	xor %eax,%eax
	ASM_CLAC
	RET
SYM_FUNC_END(__get_user_8)
EXPORT_SYMBOL(__get_user_8)

/* .. and the same for __get_user, just without the range checks */
SYM_FUNC_START(__get_user_nocheck_1)
	ASM_STAC
	ASM_BARRIER_NOSPEC
6:	movzbl (%_ASM_AX),%edx
	xor %eax,%eax
	ASM_CLAC
	RET
SYM_FUNC_END(__get_user_nocheck_1)
EXPORT_SYMBOL(__get_user_nocheck_1)

SYM_FUNC_START(__get_user_nocheck_2)
	ASM_STAC
	ASM_BARRIER_NOSPEC
7:	movzwl (%_ASM_AX),%edx
	xor %eax,%eax
	ASM_CLAC
	RET
SYM_FUNC_END(__get_user_nocheck_2)
EXPORT_SYMBOL(__get_user_nocheck_2)

SYM_FUNC_START(__get_user_nocheck_4)
	ASM_STAC
	ASM_BARRIER_NOSPEC
8:	movl (%_ASM_AX),%edx
	xor %eax,%eax
	ASM_CLAC
	RET
SYM_FUNC_END(__get_user_nocheck_4)
EXPORT_SYMBOL(__get_user_nocheck_4)

SYM_FUNC_START(__get_user_nocheck_8)
	ASM_STAC
	ASM_BARRIER_NOSPEC
#ifdef CONFIG_X86_64
9:	movq (%_ASM_AX),%rdx
#else
9:	movl (%_ASM_AX),%edx
10:	movl 4(%_ASM_AX),%ecx
#endif
	xor %eax,%eax
	ASM_CLAC
	RET
SYM_FUNC_END(__get_user_nocheck_8)
EXPORT_SYMBOL(__get_user_nocheck_8)


SYM_CODE_START_LOCAL(.Lbad_get_user_clac)
	ASM_CLAC
.Lbad_get_user:
	xor %edx,%edx
	mov $(-EFAULT),%_ASM_AX
	RET
SYM_CODE_END(.Lbad_get_user_clac)

#ifdef CONFIG_X86_32
SYM_CODE_START_LOCAL(.Lbad_get_user_8_clac)
	ASM_CLAC
bad_get_user_8:
	xor %edx,%edx
	xor %ecx,%ecx
	mov $(-EFAULT),%_ASM_AX
	RET
SYM_CODE_END(.Lbad_get_user_8_clac)
#endif

/* get_user */
	_ASM_EXTABLE(1b, .Lbad_get_user_clac)
	_ASM_EXTABLE(2b, .Lbad_get_user_clac)
	_ASM_EXTABLE(3b, .Lbad_get_user_clac)
#ifdef CONFIG_X86_64
	_ASM_EXTABLE(4b, .Lbad_get_user_clac)
#else
	_ASM_EXTABLE(4b, .Lbad_get_user_8_clac)
	_ASM_EXTABLE(5b, .Lbad_get_user_8_clac)
#endif

/* __get_user */
	_ASM_EXTABLE(6b, .Lbad_get_user_clac)
	_ASM_EXTABLE(7b, .Lbad_get_user_clac)
	_ASM_EXTABLE(8b, .Lbad_get_user_clac)
#ifdef CONFIG_X86_64
	_ASM_EXTABLE(9b, .Lbad_get_user_clac)
#else
	_ASM_EXTABLE(9b, .Lbad_get_user_8_clac)
	_ASM_EXTABLE(10b, .Lbad_get_user_8_clac)
#endif
