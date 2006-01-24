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
