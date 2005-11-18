/*
 * Zero a page. 	
 * rdi	page
 */			
	.globl clear_page
	.p2align 4
clear_page:
	movl $4096/8,%ecx
	xorl %eax,%eax
	rep 
	stosq
	ret
