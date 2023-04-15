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
