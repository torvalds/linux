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

	.text
ENTRY(__get_user_1)
	GET_THREAD_INFO(%_ASM_DX)
	cmp TI_addr_limit(%_ASM_DX),%_ASM_AX
	jae bad_get_user
	sbb %_ASM_DX, %_ASM_DX		/* array_index_mask_nospec() */
	and %_ASM_DX, %_ASM_AX
	ASM_STAC
1:	movzbl (%_ASM_AX),%edx
	xor %eax,%eax
	ASM_CLAC
	ret
ENDPROC(__get_user_1)

ENTRY(__get_user_2)
	add $1,%_ASM_AX
	jc bad_get_user
	GET_THREAD_INFO(%_ASM_DX)
	cmp TI_addr_limit(%_ASM_DX),%_ASM_AX
	jae bad_get_user
	sbb %_ASM_DX, %_ASM_DX		/* array_index_mask_nospec() */
	and %_ASM_DX, %_ASM_AX
	ASM_STAC
2:	movzwl -1(%_ASM_AX),%edx
	xor %eax,%eax
	ASM_CLAC
	ret
ENDPROC(__get_user_2)

ENTRY(__get_user_4)
	add $3,%_ASM_AX
	jc bad_get_user
	GET_THREAD_INFO(%_ASM_DX)
	cmp TI_addr_limit(%_ASM_DX),%_ASM_AX
	jae bad_get_user
	sbb %_ASM_DX, %_ASM_DX		/* array_index_mask_nospec() */
	and %_ASM_DX, %_ASM_AX
	ASM_STAC
3:	movl -3(%_ASM_AX),%edx
	xor %eax,%eax
	ASM_CLAC
	ret
ENDPROC(__get_user_4)

ENTRY(__get_user_8)
#ifdef CONFIG_X86_64
	add $7,%_ASM_AX
	jc bad_get_user
	GET_THREAD_INFO(%_ASM_DX)
	cmp TI_addr_limit(%_ASM_DX),%_ASM_AX
	jae bad_get_user
	sbb %_ASM_DX, %_ASM_DX		/* array_index_mask_nospec() */
	and %_ASM_DX, %_ASM_AX
	ASM_STAC
4:	movq -7(%_ASM_AX),%rdx
	xor %eax,%eax
	ASM_CLAC
	ret
#else
	add $7,%_ASM_AX
	jc bad_get_user_8
	GET_THREAD_INFO(%_ASM_DX)
	cmp TI_addr_limit(%_ASM_DX),%_ASM_AX
	jae bad_get_user_8
	sbb %_ASM_DX, %_ASM_DX		/* array_index_mask_nospec() */
	and %_ASM_DX, %_ASM_AX
	ASM_STAC
4:	movl -7(%_ASM_AX),%edx
5:	movl -3(%_ASM_AX),%ecx
	xor %eax,%eax
	ASM_CLAC
	ret
#endif
ENDPROC(__get_user_8)


bad_get_user:
	xor %edx,%edx
	mov $(-EFAULT),%_ASM_AX
	ASM_CLAC
	ret
END(bad_get_user)

#ifdef CONFIG_X86_32
bad_get_user_8:
	xor %edx,%edx
	xor %ecx,%ecx
	mov $(-EFAULT),%_ASM_AX
	ASM_CLAC
	ret
END(bad_get_user_8)
#endif

	_ASM_EXTABLE(1b,bad_get_user)
	_ASM_EXTABLE(2b,bad_get_user)
	_ASM_EXTABLE(3b,bad_get_user)
#ifdef CONFIG_X86_64
	_ASM_EXTABLE(4b,bad_get_user)
#else
	_ASM_EXTABLE(4b,bad_get_user_8)
	_ASM_EXTABLE(5b,bad_get_user_8)
#endif
