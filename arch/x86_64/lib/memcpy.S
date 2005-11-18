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
 * 
 * TODO: check best memcpy for PSC
 */	

 	.globl __memcpy
	.globl memcpy
	.p2align 4
__memcpy:
memcpy:		
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
