/* SPDX-License-Identifier: GPL-2.0 */
#include <linux/jump_label.h>
#include <asm/unwind_hints.h>
#include <asm/cpufeatures.h>
#include <asm/page_types.h>
#include <asm/percpu.h>
#include <asm/asm-offsets.h>
#include <asm/processor-flags.h>

/*

 x86 function call convention, 64-bit:
 -------------------------------------
  arguments           |  callee-saved      | extra caller-saved | return
 [callee-clobbered]   |                    | [callee-clobbered] |
 ---------------------------------------------------------------------------
 rdi rsi rdx rcx r8-9 | rbx rbp [*] r12-15 | r10-11             | rax, rdx [**]

 ( rsp is obviously invariant across normal function calls. (gcc can 'merge'
   functions when it sees tail-call optimization possibilities) rflags is
   clobbered. Leftover arguments are passed over the stack frame.)

 [*]  In the frame-pointers case rbp is fixed to the stack frame.

 [**] for struct return values wider than 64 bits the return convention is a
      bit more complex: up to 128 bits width we return small structures
      straight in rax, rdx. For structures larger than that (3 words or
      larger) the caller puts a pointer to an on-stack return struct
      [allocated in the caller's stack frame] into the first argument - i.e.
      into rdi. All other arguments shift up by one in this case.
      Fortunately this case is rare in the kernel.

For 32-bit we have the following conventions - kernel is built with
-mregparm=3 and -freg-struct-return:

 x86 function calling convention, 32-bit:
 ----------------------------------------
  arguments         | callee-saved        | extra caller-saved | return
 [callee-clobbered] |                     | [callee-clobbered] |
 -------------------------------------------------------------------------
 eax edx ecx        | ebx edi esi ebp [*] | <none>             | eax, edx [**]

 ( here too esp is obviously invariant across normal function calls. eflags
   is clobbered. Leftover arguments are passed over the stack frame. )

 [*]  In the frame-pointers case ebp is fixed to the stack frame.

 [**] We build with -freg-struct-return, which on 32-bit means similar
      semantics as on 64-bit: edx can be used for a second return value
      (i.e. covering integer and structure sizes up to 64 bits) - after that
      it gets more complex and more expensive: 3-word or larger struct returns
      get done in the caller's frame and the pointer to the return struct goes
      into regparm0, i.e. eax - the other arguments shift up and the
      function's register parameters degenerate to regparm=2 in essence.

*/

#ifdef CONFIG_X86_64

/*
 * 64-bit system call stack frame layout defines and helpers,
 * for assembly code:
 */

/* The layout forms the "struct pt_regs" on the stack: */
/*
 * C ABI says these regs are callee-preserved. They aren't saved on kernel entry
 * unless syscall needs a complete, fully filled "struct pt_regs".
 */
#define R15		0*8
#define R14		1*8
#define R13		2*8
#define R12		3*8
#define RBP		4*8
#define RBX		5*8
/* These regs are callee-clobbered. Always saved on kernel entry. */
#define R11		6*8
#define R10		7*8
#define R9		8*8
#define R8		9*8
#define RAX		10*8
#define RCX		11*8
#define RDX		12*8
#define RSI		13*8
#define RDI		14*8
/*
 * On syscall entry, this is syscall#. On CPU exception, this is error code.
 * On hw interrupt, it's IRQ number:
 */
#define ORIG_RAX	15*8
/* Return frame for iretq */
#define RIP		16*8
#define CS		17*8
#define EFLAGS		18*8
#define RSP		19*8
#define SS		20*8

#define SIZEOF_PTREGS	21*8

	.macro ALLOC_PT_GPREGS_ON_STACK
	addq	$-(15*8), %rsp
	.endm

	.macro SAVE_C_REGS_HELPER offset=0 rax=1 rcx=1 r8910=1 r11=1
	.if \r11
	movq %r11, 6*8+\offset(%rsp)
	.endif
	.if \r8910
	movq %r10, 7*8+\offset(%rsp)
	movq %r9,  8*8+\offset(%rsp)
	movq %r8,  9*8+\offset(%rsp)
	.endif
	.if \rax
	movq %rax, 10*8+\offset(%rsp)
	.endif
	.if \rcx
	movq %rcx, 11*8+\offset(%rsp)
	.endif
	movq %rdx, 12*8+\offset(%rsp)
	movq %rsi, 13*8+\offset(%rsp)
	movq %rdi, 14*8+\offset(%rsp)
	UNWIND_HINT_REGS offset=\offset extra=0
	.endm
	.macro SAVE_C_REGS offset=0
	SAVE_C_REGS_HELPER \offset, 1, 1, 1, 1
	.endm
	.macro SAVE_C_REGS_EXCEPT_RAX_RCX offset=0
	SAVE_C_REGS_HELPER \offset, 0, 0, 1, 1
	.endm
	.macro SAVE_C_REGS_EXCEPT_R891011
	SAVE_C_REGS_HELPER 0, 1, 1, 0, 0
	.endm
	.macro SAVE_C_REGS_EXCEPT_RCX_R891011
	SAVE_C_REGS_HELPER 0, 1, 0, 0, 0
	.endm
	.macro SAVE_C_REGS_EXCEPT_RAX_RCX_R11
	SAVE_C_REGS_HELPER 0, 0, 0, 1, 0
	.endm

	.macro SAVE_EXTRA_REGS offset=0
	movq %r15, 0*8+\offset(%rsp)
	movq %r14, 1*8+\offset(%rsp)
	movq %r13, 2*8+\offset(%rsp)
	movq %r12, 3*8+\offset(%rsp)
	movq %rbp, 4*8+\offset(%rsp)
	movq %rbx, 5*8+\offset(%rsp)
	UNWIND_HINT_REGS offset=\offset
	.endm

	.macro POP_EXTRA_REGS
	popq %r15
	popq %r14
	popq %r13
	popq %r12
	popq %rbp
	popq %rbx
	.endm

	.macro POP_C_REGS
	popq %r11
	popq %r10
	popq %r9
	popq %r8
	popq %rax
	popq %rcx
	popq %rdx
	popq %rsi
	popq %rdi
	.endm

	.macro icebp
	.byte 0xf1
	.endm

/*
 * This is a sneaky trick to help the unwinder find pt_regs on the stack.  The
 * frame pointer is replaced with an encoded pointer to pt_regs.  The encoding
 * is just setting the LSB, which makes it an invalid stack address and is also
 * a signal to the unwinder that it's a pt_regs pointer in disguise.
 *
 * NOTE: This macro must be used *after* SAVE_EXTRA_REGS because it corrupts
 * the original rbp.
 */
.macro ENCODE_FRAME_POINTER ptregs_offset=0
#ifdef CONFIG_FRAME_POINTER
	.if \ptregs_offset
		leaq \ptregs_offset(%rsp), %rbp
	.else
		mov %rsp, %rbp
	.endif
	orq	$0x1, %rbp
#endif
.endm

#ifdef CONFIG_PAGE_TABLE_ISOLATION

/*
 * PAGE_TABLE_ISOLATION PGDs are 8k.  Flip bit 12 to switch between the two
 * halves:
 */
#define PTI_SWITCH_PGTABLES_MASK	(1<<PAGE_SHIFT)
#define PTI_SWITCH_MASK		(PTI_SWITCH_PGTABLES_MASK|(1<<X86_CR3_PTI_SWITCH_BIT))

.macro SET_NOFLUSH_BIT	reg:req
	bts	$X86_CR3_PCID_NOFLUSH_BIT, \reg
.endm

.macro ADJUST_KERNEL_CR3 reg:req
	ALTERNATIVE "", "SET_NOFLUSH_BIT \reg", X86_FEATURE_PCID
	/* Clear PCID and "PAGE_TABLE_ISOLATION bit", point CR3 at kernel pagetables: */
	andq    $(~PTI_SWITCH_MASK), \reg
.endm

.macro SWITCH_TO_KERNEL_CR3 scratch_reg:req
	ALTERNATIVE "jmp .Lend_\@", "", X86_FEATURE_PTI
	mov	%cr3, \scratch_reg
	ADJUST_KERNEL_CR3 \scratch_reg
	mov	\scratch_reg, %cr3
.Lend_\@:
.endm

#define THIS_CPU_user_pcid_flush_mask   \
	PER_CPU_VAR(cpu_tlbstate) + TLB_STATE_user_pcid_flush_mask

.macro SWITCH_TO_USER_CR3_NOSTACK scratch_reg:req scratch_reg2:req
	ALTERNATIVE "jmp .Lend_\@", "", X86_FEATURE_PTI
	mov	%cr3, \scratch_reg

	ALTERNATIVE "jmp .Lwrcr3_\@", "", X86_FEATURE_PCID

	/*
	 * Test if the ASID needs a flush.
	 */
	movq	\scratch_reg, \scratch_reg2
	andq	$(0x7FF), \scratch_reg		/* mask ASID */
	bt	\scratch_reg, THIS_CPU_user_pcid_flush_mask
	jnc	.Lnoflush_\@

	/* Flush needed, clear the bit */
	btr	\scratch_reg, THIS_CPU_user_pcid_flush_mask
	movq	\scratch_reg2, \scratch_reg
	jmp	.Lwrcr3_\@

.Lnoflush_\@:
	movq	\scratch_reg2, \scratch_reg
	SET_NOFLUSH_BIT \scratch_reg

.Lwrcr3_\@:
	/* Flip the PGD and ASID to the user version */
	orq     $(PTI_SWITCH_MASK), \scratch_reg
	mov	\scratch_reg, %cr3
.Lend_\@:
.endm

.macro SWITCH_TO_USER_CR3_STACK	scratch_reg:req
	pushq	%rax
	SWITCH_TO_USER_CR3_NOSTACK scratch_reg=\scratch_reg scratch_reg2=%rax
	popq	%rax
.endm

.macro SAVE_AND_SWITCH_TO_KERNEL_CR3 scratch_reg:req save_reg:req
	ALTERNATIVE "jmp .Ldone_\@", "", X86_FEATURE_PTI
	movq	%cr3, \scratch_reg
	movq	\scratch_reg, \save_reg
	/*
	 * Is the "switch mask" all zero?  That means that both of
	 * these are zero:
	 *
	 *	1. The user/kernel PCID bit, and
	 *	2. The user/kernel "bit" that points CR3 to the
	 *	   bottom half of the 8k PGD
	 *
	 * That indicates a kernel CR3 value, not a user CR3.
	 */
	testq	$(PTI_SWITCH_MASK), \scratch_reg
	jz	.Ldone_\@

	ADJUST_KERNEL_CR3 \scratch_reg
	movq	\scratch_reg, %cr3

.Ldone_\@:
.endm

.macro RESTORE_CR3 scratch_reg:req save_reg:req
	ALTERNATIVE "jmp .Lend_\@", "", X86_FEATURE_PTI

	ALTERNATIVE "jmp .Lwrcr3_\@", "", X86_FEATURE_PCID

	/*
	 * KERNEL pages can always resume with NOFLUSH as we do
	 * explicit flushes.
	 */
	bt	$X86_CR3_PTI_SWITCH_BIT, \save_reg
	jnc	.Lnoflush_\@

	/*
	 * Check if there's a pending flush for the user ASID we're
	 * about to set.
	 */
	movq	\save_reg, \scratch_reg
	andq	$(0x7FF), \scratch_reg
	bt	\scratch_reg, THIS_CPU_user_pcid_flush_mask
	jnc	.Lnoflush_\@

	btr	\scratch_reg, THIS_CPU_user_pcid_flush_mask
	jmp	.Lwrcr3_\@

.Lnoflush_\@:
	SET_NOFLUSH_BIT \save_reg

.Lwrcr3_\@:
	/*
	 * The CR3 write could be avoided when not changing its value,
	 * but would require a CR3 read *and* a scratch register.
	 */
	movq	\save_reg, %cr3
.Lend_\@:
.endm

#else /* CONFIG_PAGE_TABLE_ISOLATION=n: */

.macro SWITCH_TO_KERNEL_CR3 scratch_reg:req
.endm
.macro SWITCH_TO_USER_CR3_NOSTACK scratch_reg:req scratch_reg2:req
.endm
.macro SWITCH_TO_USER_CR3_STACK scratch_reg:req
.endm
.macro SAVE_AND_SWITCH_TO_KERNEL_CR3 scratch_reg:req save_reg:req
.endm
.macro RESTORE_CR3 scratch_reg:req save_reg:req
.endm

#endif

#endif /* CONFIG_X86_64 */

/*
 * This does 'call enter_from_user_mode' unless we can avoid it based on
 * kernel config or using the static jump infrastructure.
 */
.macro CALL_enter_from_user_mode
#ifdef CONFIG_CONTEXT_TRACKING
#ifdef HAVE_JUMP_LABEL
	STATIC_JUMP_IF_FALSE .Lafter_call_\@, context_tracking_enabled, def=0
#endif
	call enter_from_user_mode
.Lafter_call_\@:
#endif
.endm
