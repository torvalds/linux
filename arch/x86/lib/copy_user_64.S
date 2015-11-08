/*
 * Copyright 2008 Vitaly Mayatskikh <vmayatsk@redhat.com>
 * Copyright 2002 Andi Kleen, SuSE Labs.
 * Subject to the GNU Public License v2.
 *
 * Functions to copy from and to user space.
 */

#include <linux/linkage.h>
#include <asm/current.h>
#include <asm/asm-offsets.h>
#include <asm/thread_info.h>
#include <asm/cpufeature.h>
#include <asm/alternative-asm.h>
#include <asm/asm.h>
#include <asm/smap.h>

/* Standard copy_to_user with segment limit checking */
ENTRY(_copy_to_user)
	GET_THREAD_INFO(%rax)
	movq %rdi,%rcx
	addq %rdx,%rcx
	jc bad_to_user
	cmpq TI_addr_limit(%rax),%rcx
	ja bad_to_user
	ALTERNATIVE_2 "jmp copy_user_generic_unrolled",		\
		      "jmp copy_user_generic_string",		\
		      X86_FEATURE_REP_GOOD,			\
		      "jmp copy_user_enhanced_fast_string",	\
		      X86_FEATURE_ERMS
ENDPROC(_copy_to_user)

/* Standard copy_from_user with segment limit checking */
ENTRY(_copy_from_user)
	GET_THREAD_INFO(%rax)
	movq %rsi,%rcx
	addq %rdx,%rcx
	jc bad_from_user
	cmpq TI_addr_limit(%rax),%rcx
	ja bad_from_user
	ALTERNATIVE_2 "jmp copy_user_generic_unrolled",		\
		      "jmp copy_user_generic_string",		\
		      X86_FEATURE_REP_GOOD,			\
		      "jmp copy_user_enhanced_fast_string",	\
		      X86_FEATURE_ERMS
ENDPROC(_copy_from_user)

	.section .fixup,"ax"
	/* must zero dest */
ENTRY(bad_from_user)
bad_from_user:
	movl %edx,%ecx
	xorl %eax,%eax
	rep
	stosb
bad_to_user:
	movl %edx,%eax
	ret
ENDPROC(bad_from_user)
	.previous

/*
 * copy_user_generic_unrolled - memory copy with exception handling.
 * This version is for CPUs like P4 that don't have efficient micro
 * code for rep movsq
 *
 * Input:
 * rdi destination
 * rsi source
 * rdx count
 *
 * Output:
 * eax uncopied bytes or 0 if successful.
 */
ENTRY(copy_user_generic_unrolled)
	ASM_STAC
	cmpl $8,%edx
	jb 20f		/* less then 8 bytes, go to byte copy loop */
	ALIGN_DESTINATION
	movl %edx,%ecx
	andl $63,%edx
	shrl $6,%ecx
	jz 17f
1:	movq (%rsi),%r8
2:	movq 1*8(%rsi),%r9
3:	movq 2*8(%rsi),%r10
4:	movq 3*8(%rsi),%r11
5:	movq %r8,(%rdi)
6:	movq %r9,1*8(%rdi)
7:	movq %r10,2*8(%rdi)
8:	movq %r11,3*8(%rdi)
9:	movq 4*8(%rsi),%r8
10:	movq 5*8(%rsi),%r9
11:	movq 6*8(%rsi),%r10
12:	movq 7*8(%rsi),%r11
13:	movq %r8,4*8(%rdi)
14:	movq %r9,5*8(%rdi)
15:	movq %r10,6*8(%rdi)
16:	movq %r11,7*8(%rdi)
	leaq 64(%rsi),%rsi
	leaq 64(%rdi),%rdi
	decl %ecx
	jnz 1b
17:	movl %edx,%ecx
	andl $7,%edx
	shrl $3,%ecx
	jz 20f
18:	movq (%rsi),%r8
19:	movq %r8,(%rdi)
	leaq 8(%rsi),%rsi
	leaq 8(%rdi),%rdi
	decl %ecx
	jnz 18b
20:	andl %edx,%edx
	jz 23f
	movl %edx,%ecx
21:	movb (%rsi),%al
22:	movb %al,(%rdi)
	incq %rsi
	incq %rdi
	decl %ecx
	jnz 21b
23:	xor %eax,%eax
	ASM_CLAC
	ret

	.section .fixup,"ax"
30:	shll $6,%ecx
	addl %ecx,%edx
	jmp 60f
40:	leal (%rdx,%rcx,8),%edx
	jmp 60f
50:	movl %ecx,%edx
60:	jmp copy_user_handle_tail /* ecx is zerorest also */
	.previous

	_ASM_EXTABLE(1b,30b)
	_ASM_EXTABLE(2b,30b)
	_ASM_EXTABLE(3b,30b)
	_ASM_EXTABLE(4b,30b)
	_ASM_EXTABLE(5b,30b)
	_ASM_EXTABLE(6b,30b)
	_ASM_EXTABLE(7b,30b)
	_ASM_EXTABLE(8b,30b)
	_ASM_EXTABLE(9b,30b)
	_ASM_EXTABLE(10b,30b)
	_ASM_EXTABLE(11b,30b)
	_ASM_EXTABLE(12b,30b)
	_ASM_EXTABLE(13b,30b)
	_ASM_EXTABLE(14b,30b)
	_ASM_EXTABLE(15b,30b)
	_ASM_EXTABLE(16b,30b)
	_ASM_EXTABLE(18b,40b)
	_ASM_EXTABLE(19b,40b)
	_ASM_EXTABLE(21b,50b)
	_ASM_EXTABLE(22b,50b)
ENDPROC(copy_user_generic_unrolled)

/* Some CPUs run faster using the string copy instructions.
 * This is also a lot simpler. Use them when possible.
 *
 * Only 4GB of copy is supported. This shouldn't be a problem
 * because the kernel normally only writes from/to page sized chunks
 * even if user space passed a longer buffer.
 * And more would be dangerous because both Intel and AMD have
 * errata with rep movsq > 4GB. If someone feels the need to fix
 * this please consider this.
 *
 * Input:
 * rdi destination
 * rsi source
 * rdx count
 *
 * Output:
 * eax uncopied bytes or 0 if successful.
 */
ENTRY(copy_user_generic_string)
	ASM_STAC
	cmpl $8,%edx
	jb 2f		/* less than 8 bytes, go to byte copy loop */
	ALIGN_DESTINATION
	movl %edx,%ecx
	shrl $3,%ecx
	andl $7,%edx
1:	rep
	movsq
2:	movl %edx,%ecx
3:	rep
	movsb
	xorl %eax,%eax
	ASM_CLAC
	ret

	.section .fixup,"ax"
11:	leal (%rdx,%rcx,8),%ecx
12:	movl %ecx,%edx		/* ecx is zerorest also */
	jmp copy_user_handle_tail
	.previous

	_ASM_EXTABLE(1b,11b)
	_ASM_EXTABLE(3b,12b)
ENDPROC(copy_user_generic_string)

/*
 * Some CPUs are adding enhanced REP MOVSB/STOSB instructions.
 * It's recommended to use enhanced REP MOVSB/STOSB if it's enabled.
 *
 * Input:
 * rdi destination
 * rsi source
 * rdx count
 *
 * Output:
 * eax uncopied bytes or 0 if successful.
 */
ENTRY(copy_user_enhanced_fast_string)
	ASM_STAC
	movl %edx,%ecx
1:	rep
	movsb
	xorl %eax,%eax
	ASM_CLAC
	ret

	.section .fixup,"ax"
12:	movl %ecx,%edx		/* ecx is zerorest also */
	jmp copy_user_handle_tail
	.previous

	_ASM_EXTABLE(1b,12b)
ENDPROC(copy_user_enhanced_fast_string)

/*
 * copy_user_nocache - Uncached memory copy with exception handling
 * This will force destination/source out of cache for more performance.
 */
ENTRY(__copy_user_nocache)
	ASM_STAC
	cmpl $8,%edx
	jb 20f		/* less then 8 bytes, go to byte copy loop */
	ALIGN_DESTINATION
	movl %edx,%ecx
	andl $63,%edx
	shrl $6,%ecx
	jz 17f
1:	movq (%rsi),%r8
2:	movq 1*8(%rsi),%r9
3:	movq 2*8(%rsi),%r10
4:	movq 3*8(%rsi),%r11
5:	movnti %r8,(%rdi)
6:	movnti %r9,1*8(%rdi)
7:	movnti %r10,2*8(%rdi)
8:	movnti %r11,3*8(%rdi)
9:	movq 4*8(%rsi),%r8
10:	movq 5*8(%rsi),%r9
11:	movq 6*8(%rsi),%r10
12:	movq 7*8(%rsi),%r11
13:	movnti %r8,4*8(%rdi)
14:	movnti %r9,5*8(%rdi)
15:	movnti %r10,6*8(%rdi)
16:	movnti %r11,7*8(%rdi)
	leaq 64(%rsi),%rsi
	leaq 64(%rdi),%rdi
	decl %ecx
	jnz 1b
17:	movl %edx,%ecx
	andl $7,%edx
	shrl $3,%ecx
	jz 20f
18:	movq (%rsi),%r8
19:	movnti %r8,(%rdi)
	leaq 8(%rsi),%rsi
	leaq 8(%rdi),%rdi
	decl %ecx
	jnz 18b
20:	andl %edx,%edx
	jz 23f
	movl %edx,%ecx
21:	movb (%rsi),%al
22:	movb %al,(%rdi)
	incq %rsi
	incq %rdi
	decl %ecx
	jnz 21b
23:	xorl %eax,%eax
	ASM_CLAC
	sfence
	ret

	.section .fixup,"ax"
30:	shll $6,%ecx
	addl %ecx,%edx
	jmp 60f
40:	lea (%rdx,%rcx,8),%rdx
	jmp 60f
50:	movl %ecx,%edx
60:	sfence
	jmp copy_user_handle_tail
	.previous

	_ASM_EXTABLE(1b,30b)
	_ASM_EXTABLE(2b,30b)
	_ASM_EXTABLE(3b,30b)
	_ASM_EXTABLE(4b,30b)
	_ASM_EXTABLE(5b,30b)
	_ASM_EXTABLE(6b,30b)
	_ASM_EXTABLE(7b,30b)
	_ASM_EXTABLE(8b,30b)
	_ASM_EXTABLE(9b,30b)
	_ASM_EXTABLE(10b,30b)
	_ASM_EXTABLE(11b,30b)
	_ASM_EXTABLE(12b,30b)
	_ASM_EXTABLE(13b,30b)
	_ASM_EXTABLE(14b,30b)
	_ASM_EXTABLE(15b,30b)
	_ASM_EXTABLE(16b,30b)
	_ASM_EXTABLE(18b,40b)
	_ASM_EXTABLE(19b,40b)
	_ASM_EXTABLE(21b,50b)
	_ASM_EXTABLE(22b,50b)
ENDPROC(__copy_user_nocache)
