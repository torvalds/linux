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

#ifdef CONFIG_X86_5LEVEL
#define LOAD_TASK_SIZE_MINUS_N(n) \
	ALTERNATIVE __stringify(mov $((1 << 47) - 4096 - (n)),%rdx), \
		    __stringify(mov $((1 << 56) - 4096 - (n)),%rdx), X86_FEATURE_LA57
#else
#define LOAD_TASK_SIZE_MINUS_N(n) \
	mov $(TASK_SIZE_MAX - (n)),%_ASM_DX
#endif

	.text
SYM_FUNC_START(__get_user_1)
	LOAD_TASK_SIZE_MINUS_N(0)
	cmp %_ASM_DX,%_ASM_AX
	jae bad_get_user
	sbb %_ASM_DX, %_ASM_DX		/* array_index_mask_nospec() */
	and %_ASM_DX, %_ASM_AX
	ASM_STAC
1:	movzbl (%_ASM_AX),%edx
	xor %eax,%eax
	ASM_CLAC
	ret
SYM_FUNC_END(__get_user_1)
EXPORT_SYMBOL(__get_user_1)

SYM_FUNC_START(__get_user_2)
	LOAD_TASK_SIZE_MINUS_N(1)
	cmp %_ASM_DX,%_ASM_AX
	jae bad_get_user
	sbb %_ASM_DX, %_ASM_DX		/* array_index_mask_nospec() */
	and %_ASM_DX, %_ASM_AX
	ASM_STAC
2:	movzwl (%_ASM_AX),%edx
	xor %eax,%eax
	ASM_CLAC
	ret
SYM_FUNC_END(__get_user_2)
EXPORT_SYMBOL(__get_user_2)

SYM_FUNC_START(__get_user_4)
	LOAD_TASK_SIZE_MINUS_N(3)
	cmp %_ASM_DX,%_ASM_AX
	jae bad_get_user
	sbb %_ASM_DX, %_ASM_DX		/* array_index_mask_nospec() */
	and %_ASM_DX, %_ASM_AX
	ASM_STAC
3:	movl (%_ASM_AX),%edx
	xor %eax,%eax
	ASM_CLAC
	ret
SYM_FUNC_END(__get_user_4)
EXPORT_SYMBOL(__get_user_4)

SYM_FUNC_START(__get_user_8)
#ifdef CONFIG_X86_64
	LOAD_TASK_SIZE_MINUS_N(7)
	cmp %_ASM_DX,%_ASM_AX
	jae bad_get_user
	sbb %_ASM_DX, %_ASM_DX		/* array_index_mask_nospec() */
	and %_ASM_DX, %_ASM_AX
	ASM_STAC
4:	movq (%_ASM_AX),%rdx
	xor %eax,%eax
	ASM_CLAC
	ret
#else
	LOAD_TASK_SIZE_MINUS_N(7)
	cmp %_ASM_DX,%_ASM_AX
	jae bad_get_user_8
	sbb %_ASM_DX, %_ASM_DX		/* array_index_mask_nospec() */
	and %_ASM_DX, %_ASM_AX
	ASM_STAC
4:	movl (%_ASM_AX),%edx
5:	movl 4(%_ASM_AX),%ecx
	xor %eax,%eax
	ASM_CLAC
	ret
#endif
SYM_FUNC_END(__get_user_8)
EXPORT_SYMBOL(__get_user_8)

/* .. and the same for __get_user, just without the range checks */
SYM_FUNC_START(__get_user_nocheck_1)
	ASM_STAC
	ASM_BARRIER_NOSPEC
6:	movzbl (%_ASM_AX),%edx
	xor %eax,%eax
	ASM_CLAC
	ret
SYM_FUNC_END(__get_user_nocheck_1)
EXPORT_SYMBOL(__get_user_nocheck_1)

SYM_FUNC_START(__get_user_nocheck_2)
	ASM_STAC
	ASM_BARRIER_NOSPEC
7:	movzwl (%_ASM_AX),%edx
	xor %eax,%eax
	ASM_CLAC
	ret
SYM_FUNC_END(__get_user_nocheck_2)
EXPORT_SYMBOL(__get_user_nocheck_2)

SYM_FUNC_START(__get_user_nocheck_4)
	ASM_STAC
	ASM_BARRIER_NOSPEC
8:	movl (%_ASM_AX),%edx
	xor %eax,%eax
	ASM_CLAC
	ret
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
	ret
SYM_FUNC_END(__get_user_nocheck_8)
EXPORT_SYMBOL(__get_user_nocheck_8)


SYM_CODE_START_LOCAL(.Lbad_get_user_clac)
	ASM_CLAC
bad_get_user:
	xor %edx,%edx
	mov $(-EFAULT),%_ASM_AX
	ret
SYM_CODE_END(.Lbad_get_user_clac)

#ifdef CONFIG_X86_32
SYM_CODE_START_LOCAL(.Lbad_get_user_8_clac)
	ASM_CLAC
bad_get_user_8:
	xor %edx,%edx
	xor %ecx,%ecx
	mov $(-EFAULT),%_ASM_AX
	ret
SYM_CODE_END(.Lbad_get_user_8_clac)
#endif

/* get_user */
	_ASM_EXTABLE_UA(1b, .Lbad_get_user_clac)
	_ASM_EXTABLE_UA(2b, .Lbad_get_user_clac)
	_ASM_EXTABLE_UA(3b, .Lbad_get_user_clac)
#ifdef CONFIG_X86_64
	_ASM_EXTABLE_UA(4b, .Lbad_get_user_clac)
#else
	_ASM_EXTABLE_UA(4b, .Lbad_get_user_8_clac)
	_ASM_EXTABLE_UA(5b, .Lbad_get_user_8_clac)
#endif

/* __get_user */
	_ASM_EXTABLE_UA(6b, .Lbad_get_user_clac)
	_ASM_EXTABLE_UA(7b, .Lbad_get_user_clac)
	_ASM_EXTABLE_UA(8b, .Lbad_get_user_clac)
#ifdef CONFIG_X86_64
	_ASM_EXTABLE_UA(9b, .Lbad_get_user_clac)
#else
	_ASM_EXTABLE_UA(9b, .Lbad_get_user_8_clac)
	_ASM_EXTABLE_UA(10b, .Lbad_get_user_8_clac)
#endif
