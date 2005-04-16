/* Copyright 2002 Andi Kleen, SuSE Labs */
/*
 * ISO C memset - set a memory block to a byte value.
 *	
 * rdi   destination
 * rsi   value (char) 
 * rdx   count (bytes) 
 * 
 * rax   original destination
 */	
 	.globl __memset
	.globl memset
	.p2align 4
memset:	
__memset:
	movq %rdi,%r10
	movq %rdx,%r11

	/* expand byte value  */
	movzbl %sil,%ecx
	movabs $0x0101010101010101,%rax
	mul    %rcx		/* with rax, clobbers rdx */

	/* align dst */
	movl  %edi,%r9d		
	andl  $7,%r9d	
	jnz  .Lbad_alignment
.Lafter_bad_alignment:
	
	movl %r11d,%ecx
	shrl $6,%ecx
	jz	 .Lhandle_tail

	.p2align 4
.Lloop_64:	
	decl   %ecx
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
	movl	%r11d,%ecx
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
	movl	%r11d,%ecx
	andl	$7,%ecx
	jz      .Lende
	.p2align 4
.Lloop_1:
	decl    %ecx
	movb 	%al,(%rdi)
	leaq	1(%rdi),%rdi
	jnz     .Lloop_1
	
.Lende:	
	movq	%r10,%rax
	ret

.Lbad_alignment:
	cmpq $7,%r11
	jbe	.Lhandle_7
	movq %rax,(%rdi)	/* unaligned store */
	movq $8,%r8			
	subq %r9,%r8 
	addq %r8,%rdi
	subq %r8,%r11
	jmp .Lafter_bad_alignment

	/* C stepping K8 run faster using the string instructions.
	   It is also a lot simpler. Use this when possible */

#include <asm/cpufeature.h>	
		
	.section .altinstructions,"a"
	.align 8
	.quad  memset
	.quad  memset_c
	.byte  X86_FEATURE_K8_C
	.byte  memset_c_end-memset_c
	.byte  memset_c_end-memset_c
	.previous

	.section .altinstr_replacement,"ax"
 /* rdi	destination
  * rsi value
  * rdx count
  */			
memset_c:	
	movq %rdi,%r9
	movl %edx,%r8d
	andl $7,%r8d		
	movl %edx,%ecx
	shrl $3,%ecx		
	/* expand byte value  */
	movzbl %sil,%esi
	movabs $0x0101010101010101,%rax
	mulq   %rsi		/* with rax, clobbers rdx */
	rep
	stosq	
	movl %r8d,%ecx
	rep
	stosb
	movq %r9,%rax
	ret
memset_c_end:
	.previous
