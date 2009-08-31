#include <linux/linkage.h>
#include <linux/errno.h>
#include <asm/asm.h>
#include <asm/msr.h>

#ifdef CONFIG_X86_64
/*
 * int native_{rdmsr,wrmsr}_safe_regs(u32 gprs[8]);
 *
 * reg layout: u32 gprs[eax, ecx, edx, ebx, esp, ebp, esi, edi]
 *
 */
.macro op_safe_regs op:req
ENTRY(native_\op\()_safe_regs)
	push    %rbx
	push    %rbp
	push    $0              /* Return value */
	push    %rdi
	movl    (%rdi), %eax
	movl    4(%rdi), %ecx
	movl    8(%rdi), %edx
	movl    12(%rdi), %ebx
	movl    20(%rdi), %ebp
	movl    24(%rdi), %esi
	movl    28(%rdi), %edi
1:	\op
2:	movl    %edi, %r10d
	pop     %rdi
	movl    %eax, (%rdi)
	movl    %ecx, 4(%rdi)
	movl    %edx, 8(%rdi)
	movl    %ebx, 12(%rdi)
	movl    %ebp, 20(%rdi)
	movl    %esi, 24(%rdi)
	movl    %r10d, 28(%rdi)
	pop     %rax
	pop     %rbp
	pop     %rbx
	ret
3:
	movq    $-EIO, 8(%rsp)
	jmp     2b
	.section __ex_table,"ax"
	.balign 4
	.quad   1b, 3b
	.previous
ENDPROC(native_\op\()_safe_regs)
.endm

#else /* X86_32 */

.macro op_safe_regs op:req
ENTRY(native_\op\()_safe_regs)
	push    %ebx
	push    %ebp
	push    %esi
	push    %edi
	push    $0              /* Return value */
	push    %eax
	movl    4(%eax), %ecx
	movl    8(%eax), %edx
	movl    12(%eax), %ebx
	movl    20(%eax), %ebp
	movl    24(%eax), %esi
	movl    28(%eax), %edi
	movl    (%eax), %eax
1:	\op
2:	push    %eax
	movl    4(%esp), %eax
	pop     (%eax)
	addl    $4, %esp
	movl    %ecx, 4(%eax)
	movl    %edx, 8(%eax)
	movl    %ebx, 12(%eax)
	movl    %ebp, 20(%eax)
	movl    %esi, 24(%eax)
	movl    %edi, 28(%eax)
	pop     %eax
	pop     %edi
	pop     %esi
	pop     %ebp
	pop     %ebx
	ret
3:
	movl    $-EIO, 4(%esp)
	jmp     2b
	.section __ex_table,"ax"
	.balign 4
	.long   1b, 3b
	.previous
ENDPROC(native_\op\()_safe_regs)
.endm

#endif

op_safe_regs rdmsr
op_safe_regs wrmsr

