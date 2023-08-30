/* SPDX-License-Identifier: GPL-2.0 */
/*
 * __put_user functions.
 *
 * (C) Copyright 2005 Linus Torvalds
 * (C) Copyright 2005 Andi Kleen
 * (C) Copyright 2008 Glauber Costa
 *
 * These functions have a non-standard call interface
 * to make them more efficient, especially as they
 * return an error value in addition to the "real"
 * return value.
 */
#include <linux/linkage.h>
#include <asm/thread_info.h>
#include <asm/errno.h>
#include <asm/asm.h>
#include <asm/smap.h>
#include <asm/export.h>


/*
 * __put_user_X
 *
 * Inputs:	%eax[:%edx] contains the data
 *		%ecx contains the address
 *
 * Outputs:	%ecx is error code (0 or -EFAULT)
 *
 * Clobbers:	%ebx needed for task pointer
 *
 * These functions should not modify any other registers,
 * as they get called from within inline assembly.
 */

.macro check_range size:req
.if IS_ENABLED(CONFIG_X86_64)
	mov %rcx, %rbx
	sar $63, %rbx
	or %rbx, %rcx
.else
	cmp $TASK_SIZE_MAX-\size+1, %ecx
	jae .Lbad_put_user
.endif
.endm

.text
SYM_FUNC_START(__put_user_1)
	check_range size=1
	ASM_STAC
1:	movb %al,(%_ASM_CX)
	xor %ecx,%ecx
	ASM_CLAC
	RET
SYM_FUNC_END(__put_user_1)
EXPORT_SYMBOL(__put_user_1)

SYM_FUNC_START(__put_user_nocheck_1)
	ENDBR
	ASM_STAC
2:	movb %al,(%_ASM_CX)
	xor %ecx,%ecx
	ASM_CLAC
	RET
SYM_FUNC_END(__put_user_nocheck_1)
EXPORT_SYMBOL(__put_user_nocheck_1)

SYM_FUNC_START(__put_user_2)
	check_range size=2
	ASM_STAC
3:	movw %ax,(%_ASM_CX)
	xor %ecx,%ecx
	ASM_CLAC
	RET
SYM_FUNC_END(__put_user_2)
EXPORT_SYMBOL(__put_user_2)

SYM_FUNC_START(__put_user_nocheck_2)
	ENDBR
	ASM_STAC
4:	movw %ax,(%_ASM_CX)
	xor %ecx,%ecx
	ASM_CLAC
	RET
SYM_FUNC_END(__put_user_nocheck_2)
EXPORT_SYMBOL(__put_user_nocheck_2)

SYM_FUNC_START(__put_user_4)
	check_range size=4
	ASM_STAC
5:	movl %eax,(%_ASM_CX)
	xor %ecx,%ecx
	ASM_CLAC
	RET
SYM_FUNC_END(__put_user_4)
EXPORT_SYMBOL(__put_user_4)

SYM_FUNC_START(__put_user_nocheck_4)
	ENDBR
	ASM_STAC
6:	movl %eax,(%_ASM_CX)
	xor %ecx,%ecx
	ASM_CLAC
	RET
SYM_FUNC_END(__put_user_nocheck_4)
EXPORT_SYMBOL(__put_user_nocheck_4)

SYM_FUNC_START(__put_user_8)
	check_range size=8
	ASM_STAC
7:	mov %_ASM_AX,(%_ASM_CX)
#ifdef CONFIG_X86_32
8:	movl %edx,4(%_ASM_CX)
#endif
	xor %ecx,%ecx
	ASM_CLAC
	RET
SYM_FUNC_END(__put_user_8)
EXPORT_SYMBOL(__put_user_8)

SYM_FUNC_START(__put_user_nocheck_8)
	ENDBR
	ASM_STAC
9:	mov %_ASM_AX,(%_ASM_CX)
#ifdef CONFIG_X86_32
10:	movl %edx,4(%_ASM_CX)
#endif
	xor %ecx,%ecx
	ASM_CLAC
	RET
SYM_FUNC_END(__put_user_nocheck_8)
EXPORT_SYMBOL(__put_user_nocheck_8)

SYM_CODE_START_LOCAL(.Lbad_put_user_clac)
	ASM_CLAC
.Lbad_put_user:
	movl $-EFAULT,%ecx
	RET
SYM_CODE_END(.Lbad_put_user_clac)

	_ASM_EXTABLE(1b, .Lbad_put_user_clac)
	_ASM_EXTABLE(2b, .Lbad_put_user_clac)
	_ASM_EXTABLE(3b, .Lbad_put_user_clac)
	_ASM_EXTABLE(4b, .Lbad_put_user_clac)
	_ASM_EXTABLE(5b, .Lbad_put_user_clac)
	_ASM_EXTABLE(6b, .Lbad_put_user_clac)
	_ASM_EXTABLE(7b, .Lbad_put_user_clac)
	_ASM_EXTABLE(9b, .Lbad_put_user_clac)
#ifdef CONFIG_X86_32
	_ASM_EXTABLE(8b, .Lbad_put_user_clac)
	_ASM_EXTABLE(10b, .Lbad_put_user_clac)
#endif
