#include <linux/linkage.h>
#include <asm/dwarf2.h>
#include <asm/alternative-asm.h>

/*
 * Zero a page. 	
 * rdi	page
 */			
ENTRY(clear_page_c)
	CFI_STARTPROC
	movl $4096/8,%ecx
	xorl %eax,%eax
	rep stosq
	ret
	CFI_ENDPROC
ENDPROC(clear_page_c)

ENTRY(clear_page_c_e)
	CFI_STARTPROC
	movl $4096,%ecx
	xorl %eax,%eax
	rep stosb
	ret
	CFI_ENDPROC
ENDPROC(clear_page_c_e)

ENTRY(clear_page)
	CFI_STARTPROC
	xorl   %eax,%eax
	movl   $4096/64,%ecx
	.p2align 4
.Lloop:
	decl	%ecx
#define PUT(x) movq %rax,x*8(%rdi)
	movq %rax,(%rdi)
	PUT(1)
	PUT(2)
	PUT(3)
	PUT(4)
	PUT(5)
	PUT(6)
	PUT(7)
	leaq	64(%rdi),%rdi
	jnz	.Lloop
	nop
	ret
	CFI_ENDPROC
.Lclear_page_end:
ENDPROC(clear_page)

	/*
	 * Some CPUs support enhanced REP MOVSB/STOSB instructions.
	 * It is recommended to use this when possible.
	 * If enhanced REP MOVSB/STOSB is not available, try to use fast string.
	 * Otherwise, use original function.
	 *
	 */

#include <asm/cpufeature.h>

	.section .altinstr_replacement,"ax"
1:	.byte 0xeb					/* jmp <disp8> */
	.byte (clear_page_c - clear_page) - (2f - 1b)	/* offset */
2:	.byte 0xeb					/* jmp <disp8> */
	.byte (clear_page_c_e - clear_page) - (3f - 2b)	/* offset */
3:
	.previous
	.section .altinstructions,"a"
	altinstruction_entry clear_page,1b,X86_FEATURE_REP_GOOD,\
			     .Lclear_page_end-clear_page, 2b-1b, 0
	altinstruction_entry clear_page,2b,X86_FEATURE_ERMS,   \
			     .Lclear_page_end-clear_page,3b-2b, 0
	.previous
