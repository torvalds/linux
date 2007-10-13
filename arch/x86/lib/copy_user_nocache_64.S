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

/*
 * copy_user_nocache - Uncached memory copy with exception handling
 * This will force destination/source out of cache for more performance.
 *
 * Input:
 * rdi destination
 * rsi source
 * rdx count
 * rcx zero flag	when 1 zero on exception
 *
 * Output:
 * eax uncopied bytes or 0 if successful.
 */
ENTRY(__copy_user_nocache)
	CFI_STARTPROC
	pushq %rbx
	CFI_ADJUST_CFA_OFFSET 8
	CFI_REL_OFFSET rbx, 0
	pushq %rcx		/* save zero flag */
	CFI_ADJUST_CFA_OFFSET 8
	CFI_REL_OFFSET rcx, 0

	xorl %eax,%eax		/* zero for the exception handler */

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
.Ld1:	movnti %r11,(%rdi)
.Ld2:	movnti %r8,1*8(%rdi)
.Ld3:	movnti %r9,2*8(%rdi)
.Ld4:	movnti %r10,3*8(%rdi)

.Ls5:	movq 4*8(%rsi),%r11
.Ls6:	movq 5*8(%rsi),%r8
.Ls7:	movq 6*8(%rsi),%r9
.Ls8:	movq 7*8(%rsi),%r10
.Ld5:	movnti %r11,4*8(%rdi)
.Ld6:	movnti %r8,5*8(%rdi)
.Ld7:	movnti %r9,6*8(%rdi)
.Ld8:	movnti %r10,7*8(%rdi)

	dec  %rdx

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
.Ld9:	movnti %r8,(%rdi)
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
	CFI_RESTORE %rcx
	popq %rbx
	CFI_ADJUST_CFA_OFFSET -8
	CFI_RESTORE rbx
	sfence
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
	cmpl $0,(%rsp)	/* zero flag set? */
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
ENDPROC(__copy_user_nocache)


