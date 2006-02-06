/* Copyright 2002 Andi Kleen */
	
	#include <asm/cpufeature.h>		
/*
 * memcpy - Copy a memory block.
 *
 * Input:	
 * rdi destination
 * rsi source
 * rdx count
 * 
 * Output:
 * rax original destination
 */	

 	.globl __memcpy
	.globl memcpy
	.p2align 4
__memcpy:
memcpy:		
	pushq %rbx
	movq %rdi,%rax

	movl %edx,%ecx
	shrl $6,%ecx
	jz .Lhandle_tail

	.p2align 4
.Lloop_64:
	decl %ecx

	movq (%rsi),%r11
	movq 8(%rsi),%r8

	movq %r11,(%rdi)
	movq %r8,1*8(%rdi)

	movq 2*8(%rsi),%r9
	movq 3*8(%rsi),%r10

	movq %r9,2*8(%rdi)
	movq %r10,3*8(%rdi)

	movq 4*8(%rsi),%r11
	movq 5*8(%rsi),%r8

	movq %r11,4*8(%rdi)
	movq %r8,5*8(%rdi)

	movq 6*8(%rsi),%r9
	movq 7*8(%rsi),%r10

	movq %r9,6*8(%rdi)
	movq %r10,7*8(%rdi)

	leaq 64(%rsi),%rsi
	leaq 64(%rdi),%rdi
	jnz  .Lloop_64

.Lhandle_tail:
	movl %edx,%ecx
	andl $63,%ecx
	shrl $3,%ecx
	jz   .Lhandle_7
	.p2align 4
.Lloop_8:
	decl %ecx
	movq (%rsi),%r8
	movq %r8,(%rdi)
	leaq 8(%rdi),%rdi
	leaq 8(%rsi),%rsi
	jnz  .Lloop_8

.Lhandle_7:
	movl %edx,%ecx
	andl $7,%ecx
	jz .Lende
	.p2align 4
.Lloop_1:
	movb (%rsi),%r8b
	movb %r8b,(%rdi)
	incq %rdi
	incq %rsi
	decl %ecx
	jnz .Lloop_1

.Lende:
	popq %rbx
	ret
.Lfinal:

	/* Some CPUs run faster using the string copy instructions.
	   It is also a lot simpler. Use this when possible */

	.section .altinstructions,"a"
	.align 8
	.quad  memcpy
	.quad  memcpy_c
	.byte  X86_FEATURE_REP_GOOD
	.byte  .Lfinal-memcpy
	.byte  memcpy_c_end-memcpy_c
	.previous

	.section .altinstr_replacement,"ax"
 /* rdi	destination
  * rsi source
  * rdx count
  */
memcpy_c:
	movq %rdi,%rax
	movl %edx,%ecx
	shrl $3,%ecx
	andl $7,%edx	
	rep 
	movsq 
	movl %edx,%ecx
	rep
	movsb
	ret
memcpy_c_end:
	.previous
