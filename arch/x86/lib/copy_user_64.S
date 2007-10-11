/* Copyright 2002 Andi Kleen, SuSE Labs.
 * Subject to the GNU Public License v2.
 * 
 * Functions to copy from and to user space.		
 */		 

#include <linux/linkage.h>
#include <asm/dwarf2.h>

#define FIX_ALIGNMENT 1

#include <asm/current.h>
#include <asm/asm-offsets.h>
#include <asm/thread_info.h>
#include <asm/cpufeature.h>

	.macro ALTERNATIVE_JUMP feature,orig,alt
0:
	.byte 0xe9	/* 32bit jump */
	.long \orig-1f	/* by default jump to orig */
1:
	.section .altinstr_replacement,"ax"
2:	.byte 0xe9	             /* near jump with 32bit immediate */
	.long \alt-1b /* offset */   /* or alternatively to alt */
	.previous
	.section .altinstructions,"a"
	.align 8
	.quad  0b
	.quad  2b
	.byte  \feature		     /* when feature is set */
	.byte  5
	.byte  5
	.previous
	.endm

/* Standard copy_to_user with segment limit checking */		
ENTRY(copy_to_user)
	CFI_STARTPROC
	GET_THREAD_INFO(%rax)
	movq %rdi,%rcx
	addq %rdx,%rcx
	jc  bad_to_user
	cmpq threadinfo_addr_limit(%rax),%rcx
	jae bad_to_user
	xorl %eax,%eax	/* clear zero flag */
	ALTERNATIVE_JUMP X86_FEATURE_REP_GOOD,copy_user_generic_unrolled,copy_user_generic_string
	CFI_ENDPROC

ENTRY(copy_user_generic)
	CFI_STARTPROC
	movl $1,%ecx	/* set zero flag */
	ALTERNATIVE_JUMP X86_FEATURE_REP_GOOD,copy_user_generic_unrolled,copy_user_generic_string
	CFI_ENDPROC

ENTRY(__copy_from_user_inatomic)
	CFI_STARTPROC
	xorl %ecx,%ecx	/* clear zero flag */
	ALTERNATIVE_JUMP X86_FEATURE_REP_GOOD,copy_user_generic_unrolled,copy_user_generic_string
	CFI_ENDPROC

/* Standard copy_from_user with segment limit checking */	
ENTRY(copy_from_user)
	CFI_STARTPROC
	GET_THREAD_INFO(%rax)
	movq %rsi,%rcx
	addq %rdx,%rcx
	jc  bad_from_user
	cmpq threadinfo_addr_limit(%rax),%rcx
	jae  bad_from_user
	movl $1,%ecx	/* set zero flag */
	ALTERNATIVE_JUMP X86_FEATURE_REP_GOOD,copy_user_generic_unrolled,copy_user_generic_string
	CFI_ENDPROC
ENDPROC(copy_from_user)
	
	.section .fixup,"ax"
	/* must zero dest */
bad_from_user:
	CFI_STARTPROC
	movl %edx,%ecx
	xorl %eax,%eax
	rep
	stosb
bad_to_user:
	movl	%edx,%eax
	ret
	CFI_ENDPROC
END(bad_from_user)
	.previous
	
		
/*
 * copy_user_generic_unrolled - memory copy with exception handling.
 * This version is for CPUs like P4 that don't have efficient micro code for rep movsq
 * 	
 * Input:	
 * rdi destination
 * rsi source
 * rdx count
 * ecx zero flag -- if true zero destination on error
 *
 * Output:		
 * eax uncopied bytes or 0 if successful.
 */
ENTRY(copy_user_generic_unrolled)
	CFI_STARTPROC
	pushq %rbx
	CFI_ADJUST_CFA_OFFSET 8
	CFI_REL_OFFSET rbx, 0
	pushq %rcx
	CFI_ADJUST_CFA_OFFSET 8
	CFI_REL_OFFSET rcx, 0
	xorl %eax,%eax		/*zero for the exception handler */

#ifdef FIX_ALIGNMENT
	/* check for bad alignment of destination */
	movl %edi,%ecx
	andl $7,%ecx
	jnz  .Lbad_alignment
.Lafter_bad_alignment:
#endif

	movq %rdx,%rcx

	movl $64,%ebx
	shrq $6,%rdx
	decq %rdx
	js   .Lhandle_tail

	.p2align 4
.Lloop:
.Ls1:	movq (%rsi),%r11
.Ls2:	movq 1*8(%rsi),%r8
.Ls3:	movq 2*8(%rsi),%r9
.Ls4:	movq 3*8(%rsi),%r10
.Ld1:	movq %r11,(%rdi)
.Ld2:	movq %r8,1*8(%rdi)
.Ld3:	movq %r9,2*8(%rdi)
.Ld4:	movq %r10,3*8(%rdi)

.Ls5:	movq 4*8(%rsi),%r11
.Ls6:	movq 5*8(%rsi),%r8
.Ls7:	movq 6*8(%rsi),%r9
.Ls8:	movq 7*8(%rsi),%r10
.Ld5:	movq %r11,4*8(%rdi)
.Ld6:	movq %r8,5*8(%rdi)
.Ld7:	movq %r9,6*8(%rdi)
.Ld8:	movq %r10,7*8(%rdi)

	decq %rdx

	leaq 64(%rsi),%rsi
	leaq 64(%rdi),%rdi

	jns  .Lloop

	.p2align 4
.Lhandle_tail:
	movl %ecx,%edx
	andl $63,%ecx
	shrl $3,%ecx
	jz   .Lhandle_7
	movl $8,%ebx
	.p2align 4
.Lloop_8:
.Ls9:	movq (%rsi),%r8
.Ld9:	movq %r8,(%rdi)
	decl %ecx
	leaq 8(%rdi),%rdi
	leaq 8(%rsi),%rsi
	jnz .Lloop_8

.Lhandle_7:
	movl %edx,%ecx
	andl $7,%ecx
	jz   .Lende
	.p2align 4
.Lloop_1:
.Ls10:	movb (%rsi),%bl
.Ld10:	movb %bl,(%rdi)
	incq %rdi
	incq %rsi
	decl %ecx
	jnz .Lloop_1

	CFI_REMEMBER_STATE
.Lende:
	popq %rcx
	CFI_ADJUST_CFA_OFFSET -8
	CFI_RESTORE rcx
	popq %rbx
	CFI_ADJUST_CFA_OFFSET -8
	CFI_RESTORE rbx
	ret
	CFI_RESTORE_STATE

#ifdef FIX_ALIGNMENT
	/* align destination */
	.p2align 4
.Lbad_alignment:
	movl $8,%r9d
	subl %ecx,%r9d
	movl %r9d,%ecx
	cmpq %r9,%rdx
	jz   .Lhandle_7
	js   .Lhandle_7
.Lalign_1:
.Ls11:	movb (%rsi),%bl
.Ld11:	movb %bl,(%rdi)
	incq %rsi
	incq %rdi
	decl %ecx
	jnz .Lalign_1
	subq %r9,%rdx
	jmp .Lafter_bad_alignment
#endif

	/* table sorted by exception address */
	.section __ex_table,"a"
	.align 8
	.quad .Ls1,.Ls1e
	.quad .Ls2,.Ls2e
	.quad .Ls3,.Ls3e
	.quad .Ls4,.Ls4e
	.quad .Ld1,.Ls1e
	.quad .Ld2,.Ls2e
	.quad .Ld3,.Ls3e
	.quad .Ld4,.Ls4e
	.quad .Ls5,.Ls5e
	.quad .Ls6,.Ls6e
	.quad .Ls7,.Ls7e
	.quad .Ls8,.Ls8e
	.quad .Ld5,.Ls5e
	.quad .Ld6,.Ls6e
	.quad .Ld7,.Ls7e
	.quad .Ld8,.Ls8e
	.quad .Ls9,.Le_quad
	.quad .Ld9,.Le_quad
	.quad .Ls10,.Le_byte
	.quad .Ld10,.Le_byte
#ifdef FIX_ALIGNMENT
	.quad .Ls11,.Lzero_rest
	.quad .Ld11,.Lzero_rest
#endif
	.quad .Le5,.Le_zero
	.previous

	/* compute 64-offset for main loop. 8 bytes accuracy with error on the
	   pessimistic side. this is gross. it would be better to fix the
	   interface. */
	/* eax: zero, ebx: 64 */
.Ls1e: 	addl $8,%eax
.Ls2e: 	addl $8,%eax
.Ls3e: 	addl $8,%eax
.Ls4e: 	addl $8,%eax
.Ls5e: 	addl $8,%eax
.Ls6e: 	addl $8,%eax
.Ls7e: 	addl $8,%eax
.Ls8e: 	addl $8,%eax
	addq %rbx,%rdi	/* +64 */
	subq %rax,%rdi  /* correct destination with computed offset */

	shlq $6,%rdx	/* loop counter * 64 (stride length) */
	addq %rax,%rdx	/* add offset to loopcnt */
	andl $63,%ecx	/* remaining bytes */
	addq %rcx,%rdx	/* add them */
	jmp .Lzero_rest

	/* exception on quad word loop in tail handling */
	/* ecx:	loopcnt/8, %edx: length, rdi: correct */
.Le_quad:
	shll $3,%ecx
	andl $7,%edx
	addl %ecx,%edx
	/* edx: bytes to zero, rdi: dest, eax:zero */
.Lzero_rest:
	cmpl $0,(%rsp)
	jz   .Le_zero
	movq %rdx,%rcx
.Le_byte:
	xorl %eax,%eax
.Le5:	rep
	stosb
	/* when there is another exception while zeroing the rest just return */
.Le_zero:
	movq %rdx,%rax
	jmp .Lende
	CFI_ENDPROC
ENDPROC(copy_user_generic)


	/* Some CPUs run faster using the string copy instructions.
	   This is also a lot simpler. Use them when possible.
	   Patch in jmps to this code instead of copying it fully
	   to avoid unwanted aliasing in the exception tables. */

 /* rdi	destination
  * rsi source
  * rdx count
  * ecx zero flag
  *
  * Output:
  * eax uncopied bytes or 0 if successfull.
  *
  * Only 4GB of copy is supported. This shouldn't be a problem
  * because the kernel normally only writes from/to page sized chunks
  * even if user space passed a longer buffer.
  * And more would be dangerous because both Intel and AMD have
  * errata with rep movsq > 4GB. If someone feels the need to fix
  * this please consider this.
  */
ENTRY(copy_user_generic_string)
	CFI_STARTPROC
	movl %ecx,%r8d		/* save zero flag */
	movl %edx,%ecx
	shrl $3,%ecx
	andl $7,%edx	
	jz   10f
1:	rep 
	movsq 
	movl %edx,%ecx
2:	rep
	movsb
9:	movl %ecx,%eax
	ret

	/* multiple of 8 byte */
10:	rep
	movsq
	xor %eax,%eax
	ret

	/* exception handling */
3:      lea (%rdx,%rcx,8),%rax	/* exception on quad loop */
	jmp 6f
5:	movl %ecx,%eax		/* exception on byte loop */
	/* eax: left over bytes */
6:	testl %r8d,%r8d		/* zero flag set? */
	jz 7f
	movl %eax,%ecx		/* initialize x86 loop counter */
	push %rax
	xorl %eax,%eax
8:	rep
	stosb 			/* zero the rest */
11:	pop %rax
7:	ret
	CFI_ENDPROC
END(copy_user_generic_c)

	.section __ex_table,"a"
	.quad 1b,3b
	.quad 2b,5b
	.quad 8b,11b
	.quad 10b,3b
	.previous
