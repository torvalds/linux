/* Written 2003 by Andi Kleen, based on a kernel by Evandro Menezes */

#include <linux/linkage.h>
#include <asm/dwarf2.h>
#include <asm/alternative-asm.h>

	ALIGN
copy_page_c:
	CFI_STARTPROC
	movl $4096/8,%ecx
	rep movsq
	ret
	CFI_ENDPROC
ENDPROC(copy_page_c)

/* Don't use streaming store because it's better when the target
   ends up in cache. */
	    
/* Could vary the prefetch distance based on SMP/UP */

ENTRY(copy_page)
	CFI_STARTPROC
	subq	$2*8,%rsp
	CFI_ADJUST_CFA_OFFSET 2*8
	movq	%rbx,(%rsp)
	CFI_REL_OFFSET rbx, 0
	movq	%r12,1*8(%rsp)
	CFI_REL_OFFSET r12, 1*8

	movl	$(4096/64)-5,%ecx
	.p2align 4
.Loop64:
  	dec     %rcx

	movq        (%rsi), %rax
	movq      8 (%rsi), %rbx
	movq     16 (%rsi), %rdx
	movq     24 (%rsi), %r8
	movq     32 (%rsi), %r9
	movq     40 (%rsi), %r10
	movq     48 (%rsi), %r11
	movq     56 (%rsi), %r12

	prefetcht0 5*64(%rsi)

	movq     %rax,    (%rdi)
	movq     %rbx,  8 (%rdi)
	movq     %rdx, 16 (%rdi)
	movq     %r8,  24 (%rdi)
	movq     %r9,  32 (%rdi)
	movq     %r10, 40 (%rdi)
	movq     %r11, 48 (%rdi)
	movq     %r12, 56 (%rdi)

	leaq    64 (%rsi), %rsi
	leaq    64 (%rdi), %rdi

	jnz     .Loop64

	movl	$5,%ecx
	.p2align 4
.Loop2:
	decl   %ecx

	movq        (%rsi), %rax
	movq      8 (%rsi), %rbx
	movq     16 (%rsi), %rdx
	movq     24 (%rsi), %r8
	movq     32 (%rsi), %r9
	movq     40 (%rsi), %r10
	movq     48 (%rsi), %r11
	movq     56 (%rsi), %r12

	movq     %rax,    (%rdi)
	movq     %rbx,  8 (%rdi)
	movq     %rdx, 16 (%rdi)
	movq     %r8,  24 (%rdi)
	movq     %r9,  32 (%rdi)
	movq     %r10, 40 (%rdi)
	movq     %r11, 48 (%rdi)
	movq     %r12, 56 (%rdi)

	leaq	64(%rdi),%rdi
	leaq	64(%rsi),%rsi

	jnz	.Loop2

	movq	(%rsp),%rbx
	CFI_RESTORE rbx
	movq	1*8(%rsp),%r12
	CFI_RESTORE r12
	addq	$2*8,%rsp
	CFI_ADJUST_CFA_OFFSET -2*8
	ret
.Lcopy_page_end:
	CFI_ENDPROC
ENDPROC(copy_page)

	/* Some CPUs run faster using the string copy instructions.
	   It is also a lot simpler. Use this when possible */

#include <asm/cpufeature.h>

	.section .altinstr_replacement,"ax"
1:	.byte 0xeb					/* jmp <disp8> */
	.byte (copy_page_c - copy_page) - (2f - 1b)	/* offset */
2:
	.previous
	.section .altinstructions,"a"
	altinstruction_entry copy_page, 1b, X86_FEATURE_REP_GOOD,	\
		.Lcopy_page_end-copy_page, 2b-1b
	.previous
