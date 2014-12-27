/* Copyright 2002 Andi Kleen, SuSE Labs */

#include <linux/linkage.h>
#include <asm/dwarf2.h>
#include <asm/cpufeature.h>
#include <asm/alternative-asm.h>

/*
 * ISO C memset - set a memory block to a byte value. This function uses fast
 * string to get better performance than the original function. The code is
 * simpler and shorter than the orignal function as well.
 *	
 * rdi   destination
 * rsi   value (char) 
 * rdx   count (bytes) 
 * 
 * rax   original destination
 */	
	.section .altinstr_replacement, "ax", @progbits
.Lmemset_c:
	movq %rdi,%r9
	movq %rdx,%rcx
	andl $7,%edx
	shrq $3,%rcx
	/* expand byte value  */
	movzbl %sil,%esi
	movabs $0x0101010101010101,%rax
	imulq %rsi,%rax
	rep stosq
	movl %edx,%ecx
	rep stosb
	movq %r9,%rax
	ret
.Lmemset_e:
	.previous

/*
 * ISO C memset - set a memory block to a byte value. This function uses
 * enhanced rep stosb to override the fast string function.
 * The code is simpler and shorter than the fast string function as well.
 *
 * rdi   destination
 * rsi   value (char)
 * rdx   count (bytes)
 *
 * rax   original destination
 */
	.section .altinstr_replacement, "ax", @progbits
.Lmemset_c_e:
	movq %rdi,%r9
	movb %sil,%al
	movq %rdx,%rcx
	rep stosb
	movq %r9,%rax
	ret
.Lmemset_e_e:
	.previous

.weak memset

ENTRY(memset)
ENTRY(__memset)
	CFI_STARTPROC
	movq %rdi,%r10

	/* expand byte value  */
	movzbl %sil,%ecx
	movabs $0x0101010101010101,%rax
	imulq  %rcx,%rax

	/* align dst */
	movl  %edi,%r9d
	andl  $7,%r9d
	jnz  .Lbad_alignment
	CFI_REMEMBER_STATE
.Lafter_bad_alignment:

	movq  %rdx,%rcx
	shrq  $6,%rcx
	jz	 .Lhandle_tail

	.p2align 4
.Lloop_64:
	decq  %rcx
	movq  %rax,(%rdi)
	movq  %rax,8(%rdi)
	movq  %rax,16(%rdi)
	movq  %rax,24(%rdi)
	movq  %rax,32(%rdi)
	movq  %rax,40(%rdi)
	movq  %rax,48(%rdi)
	movq  %rax,56(%rdi)
	leaq  64(%rdi),%rdi
	jnz    .Lloop_64

	/* Handle tail in loops. The loops should be faster than hard
	   to predict jump tables. */
	.p2align 4
.Lhandle_tail:
	movl	%edx,%ecx
	andl    $63&(~7),%ecx
	jz 		.Lhandle_7
	shrl	$3,%ecx
	.p2align 4
.Lloop_8:
	decl   %ecx
	movq  %rax,(%rdi)
	leaq  8(%rdi),%rdi
	jnz    .Lloop_8

.Lhandle_7:
	andl	$7,%edx
	jz      .Lende
	.p2align 4
.Lloop_1:
	decl    %edx
	movb 	%al,(%rdi)
	leaq	1(%rdi),%rdi
	jnz     .Lloop_1

.Lende:
	movq	%r10,%rax
	ret

	CFI_RESTORE_STATE
.Lbad_alignment:
	cmpq $7,%rdx
	jbe	.Lhandle_7
	movq %rax,(%rdi)	/* unaligned store */
	movq $8,%r8
	subq %r9,%r8
	addq %r8,%rdi
	subq %r8,%rdx
	jmp .Lafter_bad_alignment
.Lfinal:
	CFI_ENDPROC
ENDPROC(memset)
ENDPROC(__memset)

	/* Some CPUs support enhanced REP MOVSB/STOSB feature.
	 * It is recommended to use this when possible.
	 *
	 * If enhanced REP MOVSB/STOSB feature is not available, use fast string
	 * instructions.
	 *
	 * Otherwise, use original memset function.
	 *
	 * In .altinstructions section, ERMS feature is placed after REG_GOOD
         * feature to implement the right patch order.
	 */
	.section .altinstructions,"a"
	altinstruction_entry __memset,.Lmemset_c,X86_FEATURE_REP_GOOD,\
			     .Lfinal-__memset,.Lmemset_e-.Lmemset_c,0
	altinstruction_entry __memset,.Lmemset_c_e,X86_FEATURE_ERMS, \
			     .Lfinal-__memset,.Lmemset_e_e-.Lmemset_c_e,0
	.previous
