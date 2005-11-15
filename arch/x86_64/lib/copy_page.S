/* Written 2003 by Andi Kleen, based on a kernel by Evandro Menezes */
	
/* Don't use streaming store because it's better when the target
   ends up in cache. */
	    
/* Could vary the prefetch distance based on SMP/UP */

	.globl copy_page
	.p2align 4
copy_page:
	movl $4096/8,%ecx
	rep 
	movsq 
	ret
