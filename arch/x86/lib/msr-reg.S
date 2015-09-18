#include <linux/linkage.h>
#include <linux/errno.h>
#include <asm/asm.h>
#include <asm/msr.h>

#ifdef CONFIG_X86_64
/*
 * int {rdmsr,wrmsr}_safe_regs(u32 gprs[8]);
 *
 * reg layout: u32 gprs[eax, ecx, edx, ebx, esp, ebp, esi, edi]
 *
 */
.macro op_safe_regs op
ENTRY(\op\()_safe_regs)
	pushq %rbx
	pushq %rbp
	movq	%rdi, %r10	/* Save pointer */
	xorl	%r11d, %r11d	/* Return value */
	movl    (%rdi), %eax
	movl    4(%rdi), %ecx
	movl    8(%rdi), %edx
	movl    12(%rdi), %ebx
	movl    20(%rdi), %ebp
	movl    24(%rdi), %esi
	movl    28(%rdi), %edi
1:	\op
2:	movl    %eax, (%r10)
	movl	%r11d, %eax	/* Return value */
	movl    %ecx, 4(%r10)
	movl    %edx, 8(%r10)
	movl    %ebx, 12(%r10)
	movl    %ebp, 20(%r10)
	movl    %esi, 24(%r10)
	movl    %edi, 28(%r10)
	popq %rbp
	popq %rbx
	ret
3:
	movl    $-EIO, %r11d
	jmp     2b

	_ASM_EXTABLE(1b, 3b)
ENDPROC(\op\()_safe_regs)
.endm

#else /* X86_32 */

.macro op_safe_regs op
ENTRY(\op\()_safe_regs)
	pushl %ebx
	pushl %ebp
	pushl %esi
	pushl %edi
	pushl $0              /* Return value */
	pushl %eax
	movl    4(%eax), %ecx
	movl    8(%eax), %edx
	movl    12(%eax), %ebx
	movl    20(%eax), %ebp
	movl    24(%eax), %esi
	movl    28(%eax), %edi
	movl    (%eax), %eax
1:	\op
2:	pushl %eax
	movl    4(%esp), %eax
	popl (%eax)
	addl    $4, %esp
	movl    %ecx, 4(%eax)
	movl    %edx, 8(%eax)
	movl    %ebx, 12(%eax)
	movl    %ebp, 20(%eax)
	movl    %esi, 24(%eax)
	movl    %edi, 28(%eax)
	popl %eax
	popl %edi
	popl %esi
	popl %ebp
	popl %ebx
	ret
3:
	movl    $-EIO, 4(%esp)
	jmp     2b

	_ASM_EXTABLE(1b, 3b)
ENDPROC(\op\()_safe_regs)
.endm

#endif

op_safe_regs rdmsr
op_safe_regs wrmsr

