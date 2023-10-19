/* SPDX-License-Identifier: GPL-2.0-only */
#include <linux/linkage.h>
#include <asm/asm.h>
#include <asm/export.h>

/*
 * Most CPUs support enhanced REP MOVSB/STOSB instructions. It is
 * recommended to use this when possible and we do use them by default.
 * If enhanced REP MOVSB/STOSB is not available, try to use fast string.
 * Otherwise, use original.
 */

/*
 * Zero a page.
 * %rdi	- page
 */
SYM_FUNC_START(clear_page_rep)
	movl $4096/8,%ecx
	xorl %eax,%eax
	rep stosq
	RET
SYM_FUNC_END(clear_page_rep)
EXPORT_SYMBOL_GPL(clear_page_rep)

SYM_FUNC_START(clear_page_orig)
	xorl   %eax,%eax
	movl   $4096/64,%ecx
	.p2align 4
.Lloop:
	decl	%ecx
#define PUT(x) movq %rax,x*8(%rdi)
	movq %rax,(%rdi)
	PUT(1)
	PUT(2)
	PUT(3)
	PUT(4)
	PUT(5)
	PUT(6)
	PUT(7)
	leaq	64(%rdi),%rdi
	jnz	.Lloop
	nop
	RET
SYM_FUNC_END(clear_page_orig)
EXPORT_SYMBOL_GPL(clear_page_orig)

SYM_FUNC_START(clear_page_erms)
	movl $4096,%ecx
	xorl %eax,%eax
	rep stosb
	RET
SYM_FUNC_END(clear_page_erms)
EXPORT_SYMBOL_GPL(clear_page_erms)

/*
 * Default clear user-space.
 * Input:
 * rdi destination
 * rcx count
 *
 * Output:
 * rcx: uncleared bytes or 0 if successful.
 */
SYM_FUNC_START(clear_user_original)
	/*
	 * Copy only the lower 32 bits of size as that is enough to handle the rest bytes,
	 * i.e., no need for a 'q' suffix and thus a REX prefix.
	 */
	mov %ecx,%eax
	shr $3,%rcx
	jz .Lrest_bytes

	# do the qwords first
	.p2align 4
.Lqwords:
	movq $0,(%rdi)
	lea 8(%rdi),%rdi
	dec %rcx
	jnz .Lqwords

.Lrest_bytes:
	and $7,  %eax
	jz .Lexit

	# now do the rest bytes
.Lbytes:
	movb $0,(%rdi)
	inc %rdi
	dec %eax
	jnz .Lbytes

.Lexit:
	/*
	 * %rax still needs to be cleared in the exception case because this function is called
	 * from inline asm and the compiler expects %rax to be zero when exiting the inline asm,
	 * in case it might reuse it somewhere.
	 */
        xor %eax,%eax
        RET

.Lqwords_exception:
        # convert remaining qwords back into bytes to return to caller
        shl $3, %rcx
        and $7, %eax
        add %rax,%rcx
        jmp .Lexit

.Lbytes_exception:
        mov %eax,%ecx
        jmp .Lexit

        _ASM_EXTABLE_UA(.Lqwords, .Lqwords_exception)
        _ASM_EXTABLE_UA(.Lbytes, .Lbytes_exception)
SYM_FUNC_END(clear_user_original)
EXPORT_SYMBOL(clear_user_original)

/*
 * Alternative clear user-space when CPU feature X86_FEATURE_REP_GOOD is
 * present.
 * Input:
 * rdi destination
 * rcx count
 *
 * Output:
 * rcx: uncleared bytes or 0 if successful.
 */
SYM_FUNC_START(clear_user_rep_good)
	# call the original thing for less than a cacheline
	cmp $64, %rcx
	jb clear_user_original

.Lprep:
	# copy lower 32-bits for rest bytes
	mov %ecx, %edx
	shr $3, %rcx
	jz .Lrep_good_rest_bytes

.Lrep_good_qwords:
	rep stosq

.Lrep_good_rest_bytes:
	and $7, %edx
	jz .Lrep_good_exit

.Lrep_good_bytes:
	mov %edx, %ecx
	rep stosb

.Lrep_good_exit:
	# see .Lexit comment above
	xor %eax, %eax
	RET

.Lrep_good_qwords_exception:
	# convert remaining qwords back into bytes to return to caller
	shl $3, %rcx
	and $7, %edx
	add %rdx, %rcx
	jmp .Lrep_good_exit

	_ASM_EXTABLE_UA(.Lrep_good_qwords, .Lrep_good_qwords_exception)
	_ASM_EXTABLE_UA(.Lrep_good_bytes, .Lrep_good_exit)
SYM_FUNC_END(clear_user_rep_good)
EXPORT_SYMBOL(clear_user_rep_good)

/*
 * Alternative clear user-space when CPU feature X86_FEATURE_ERMS is present.
 * Input:
 * rdi destination
 * rcx count
 *
 * Output:
 * rcx: uncleared bytes or 0 if successful.
 *
 */
SYM_FUNC_START(clear_user_erms)
	# call the original thing for less than a cacheline
	cmp $64, %rcx
	jb clear_user_original

.Lerms_bytes:
	rep stosb

.Lerms_exit:
	xorl %eax,%eax
	RET

	_ASM_EXTABLE_UA(.Lerms_bytes, .Lerms_exit)
SYM_FUNC_END(clear_user_erms)
EXPORT_SYMBOL(clear_user_erms)
