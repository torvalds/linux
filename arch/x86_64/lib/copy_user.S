/* Copyright 2002 Andi Kleen, SuSE Labs.
 * Subject to the GNU Public License v2.
 * 
 * Functions to copy from and to user space.		
 */		 

#define FIX_ALIGNMENT 1
		
	#include <asm/current.h>
	#include <asm/offset.h>
	#include <asm/thread_info.h>
	#include <asm/cpufeature.h>

/* Standard copy_to_user with segment limit checking */		
	.globl copy_to_user
	.p2align 4	
copy_to_user:
	GET_THREAD_INFO(%rax)
	movq %rdi,%rcx
	addq %rdx,%rcx
	jc  bad_to_user
	cmpq threadinfo_addr_limit(%rax),%rcx
	jae bad_to_user
2:	
	.byte 0xe9	/* 32bit jump */
	.long .Lcug-1f
1:

	.section .altinstr_replacement,"ax"
3:	.byte 0xe9			/* replacement jmp with 8 bit immediate */
	.long copy_user_generic_c-1b	/* offset */
	.previous
	.section .altinstructions,"a"
	.align 8
	.quad  2b
	.quad  3b
	.byte  X86_FEATURE_K8_C
	.byte  5
	.byte  5
	.previous

/* Standard copy_from_user with segment limit checking */	
	.globl copy_from_user
	.p2align 4	
copy_from_user:
	GET_THREAD_INFO(%rax)
	movq %rsi,%rcx
	addq %rdx,%rcx
	jc  bad_from_user
	cmpq threadinfo_addr_limit(%rax),%rcx
	jae  bad_from_user
	/* FALL THROUGH to copy_user_generic */
	
	.section .fixup,"ax"
	/* must zero dest */
bad_from_user:
	movl %edx,%ecx
	xorl %eax,%eax
	rep
	stosb
bad_to_user:
	movl	%edx,%eax
	ret
	.previous
	
		
/*
 * copy_user_generic - memory copy with exception handling.
 * 	
 * Input:	
 * rdi destination
 * rsi source
 * rdx count
 *
 * Output:		
 * eax uncopied bytes or 0 if successful.
 */
	.globl copy_user_generic	
	.p2align 4
copy_user_generic:	
	.byte 0x66,0x66,0x90	/* 5 byte nop for replacement jump */	
	.byte 0x66,0x90
1:		
	.section .altinstr_replacement,"ax"
2:	.byte 0xe9	             /* near jump with 32bit immediate */
	.long copy_user_generic_c-1b /* offset */
	.previous
	.section .altinstructions,"a"
	.align 8
	.quad  copy_user_generic
	.quad  2b
	.byte  X86_FEATURE_K8_C
	.byte  5
	.byte  5
	.previous
.Lcug:	
	pushq %rbx
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
			
.Lende:
	popq %rbx
	ret	

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
	movq %rdx,%rcx
.Le_byte:
	xorl %eax,%eax
.Le5:	rep 
	stosb
	/* when there is another exception while zeroing the rest just return */
.Le_zero:		
	movq %rdx,%rax
	jmp .Lende

	/* C stepping K8 run faster using the string copy instructions.
	   This is also a lot simpler. Use them when possible.
	   Patch in jmps to this code instead of copying it fully
	   to avoid unwanted aliasing in the exception tables. */
		
 /* rdi	destination
  * rsi source
  * rdx count
  *
  * Output:		
  * eax uncopied bytes or 0 if successfull.
  */			
copy_user_generic_c:
	movl %edx,%ecx
	shrl $3,%ecx
	andl $7,%edx	
1:	rep 
	movsq 
	movl %edx,%ecx
2:	rep
	movsb
4:	movl %ecx,%eax
	ret
3:	lea (%rdx,%rcx,8),%rax
	ret
	
	.section __ex_table,"a"
	.quad 1b,3b
	.quad 2b,4b
	.previous
