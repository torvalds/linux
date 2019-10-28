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

.macro PUSH_AND_CLEAR_REGS rdx=%rdx rax=%rax save_ret=0
	/*
	 * Push registers and sanitize registers of values that a
	 * speculation attack might otherwise want to exploit. The
	 * lower registers are likely clobbered well before they
	 * could be put to use in a speculative execution gadget.
	 * Interleave XOR with PUSH for better uop scheduling:
	 */
	.if \save_ret
	pushq	%rsi		/* pt_regs->si */
	movq	8(%rsp), %rsi	/* temporarily store the return address in %rsi */
	movq	%rdi, 8(%rsp)	/* pt_regs->di (overwriting original return address) */
	.else
	pushq   %rdi		/* pt_regs->di */
	pushq   %rsi		/* pt_regs->si */
	.endif
	pushq	\rdx		/* pt_regs->dx */
	xorl	%edx, %edx	/* nospec   dx */
	pushq   %rcx		/* pt_regs->cx */
	xorl	%ecx, %ecx	/* nospec   cx */
	pushq   \rax		/* pt_regs->ax */
	pushq   %r8		/* pt_regs->r8 */
	xorl	%r8d, %r8d	/* nospec   r8 */
	pushq   %r9		/* pt_regs->r9 */
	xorl	%r9d, %r9d	/* nospec   r9 */
	pushq   %r10		/* pt_regs->r10 */
	xorl	%r10d, %r10d	/* nospec   r10 */
	pushq   %r11		/* pt_regs->r11 */
	xorl	%r11d, %r11d	/* nospec   r11*/
	pushq	%rbx		/* pt_regs->rbx */
	xorl    %ebx, %ebx	/* nospec   rbx*/
	pushq	%rbp		/* pt_regs->rbp */
	xorl    %ebp, %ebp	/* nospec   rbp*/
	pushq	%r12		/* pt_regs->r12 */
	xorl	%r12d, %r12d	/* nospec   r12*/
	pushq	%r13		/* pt_regs->r13 */
	xorl	%r13d, %r13d	/* nospec   r13*/
	pushq	%r14		/* pt_regs->r14 */
	xorl	%r14d, %r14d	/* nospec   r14*/
	pushq	%r15		/* pt_regs->r15 */
	xorl	%r15d, %r15d	/* nospec   r15*/
	UNWIND_HINT_REGS
	.if \save_ret
	pushq	%rsi		/* return address on top of stack */
	.endif
.endm

.macro POP_REGS pop_rdi=1 skip_r11rcx=0
	popq %r15
	popq %r14
	popq %r13
	popq %r12
	popq %rbp
	popq %rbx
	.if \skip_r11rcx
	popq %rsi
	.else
	popq %r11
	.endif
	popq %r10
	popq %r9
	popq %r8
	popq %rax
	.if \skip_r11rcx
	popq %rsi
	.else
	popq %rcx
	.endif
	popq %rdx
	popq %rsi
	.if \pop_rdi
	popq %rdi
	.endif
.endm

/*
 * This is a sneaky trick to help the unwinder find pt_regs on the stack.  The
 * frame pointer is replaced with an encoded pointer to pt_regs.  The encoding
 * is just setting the LSB, which makes it an invalid stack address and is also
 * a signal to the unwinder that it's a pt_regs pointer in disguise.
 *
 * NOTE: This macro must be used *after* PUSH_AND_CLEAR_REGS because it corrupts
 * the original rbp.
 */
.macro ENCODE_FRAME_POINTER ptregs_offset=0
#ifdef CONFIG_FRAME_POINTER
	leaq 1+\ptregs_offset(%rsp), %rbp
#endif
.endm

#ifdef CONFIG_PAGE_TABLE_ISOLATION

/*
 * PAGE_TABLE_ISOLATION PGDs are 8k.  Flip bit 12 to switch between the two
 * halves:
 */
#define PTI_USER_PGTABLE_BIT		PAGE_SHIFT
#define PTI_USER_PGTABLE_MASK		(1 << PTI_USER_PGTABLE_BIT)
#define PTI_USER_PCID_BIT		X86_CR3_PTI_PCID_USER_BIT
#define PTI_USER_PCID_MASK		(1 << PTI_USER_PCID_BIT)
#define PTI_USER_PGTABLE_AND_PCID_MASK  (PTI_USER_PCID_MASK | PTI_USER_PGTABLE_MASK)

.macro SET_NOFLUSH_BIT	reg:req
	bts	$X86_CR3_PCID_NOFLUSH_BIT, \reg
.endm

.macro ADJUST_KERNEL_CR3 reg:req
	ALTERNATIVE "", "SET_NOFLUSH_BIT \reg", X86_FEATURE_PCID
	/* Clear PCID and "PAGE_TABLE_ISOLATION bit", point CR3 at kernel pagetables: */
	andq    $(~PTI_USER_PGTABLE_AND_PCID_MASK), \reg
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
	jmp	.Lwrcr3_pcid_\@

.Lnoflush_\@:
	movq	\scratch_reg2, \scratch_reg
	SET_NOFLUSH_BIT \scratch_reg

.Lwrcr3_pcid_\@:
	/* Flip the ASID to the user version */
	orq	$(PTI_USER_PCID_MASK), \scratch_reg

.Lwrcr3_\@:
	/* Flip the PGD to the user version */
	orq     $(PTI_USER_PGTABLE_MASK), \scratch_reg
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
	 * Test the user pagetable bit. If set, then the user page tables
	 * are active. If clear CR3 already has the kernel page table
	 * active.
	 */
	bt	$PTI_USER_PGTABLE_BIT, \scratch_reg
	jnc	.Ldone_\@

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
	bt	$PTI_USER_PGTABLE_BIT, \save_reg
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

/*
 * Mitigate Spectre v1 for conditional swapgs code paths.
 *
 * FENCE_SWAPGS_USER_ENTRY is used in the user entry swapgs code path, to
 * prevent a speculative swapgs when coming from kernel space.
 *
 * FENCE_SWAPGS_KERNEL_ENTRY is used in the kernel entry non-swapgs code path,
 * to prevent the swapgs from getting speculatively skipped when coming from
 * user space.
 */
.macro FENCE_SWAPGS_USER_ENTRY
	ALTERNATIVE "", "lfence", X86_FEATURE_FENCE_SWAPGS_USER
.endm
.macro FENCE_SWAPGS_KERNEL_ENTRY
	ALTERNATIVE "", "lfence", X86_FEATURE_FENCE_SWAPGS_KERNEL
.endm

#endif /* CONFIG_X86_64 */

/*
 * This does 'call enter_from_user_mode' unless we can avoid it based on
 * kernel config or using the static jump infrastructure.
 */
.macro CALL_enter_from_user_mode
#ifdef CONFIG_CONTEXT_TRACKING
#ifdef CONFIG_JUMP_LABEL
	STATIC_JUMP_IF_FALSE .Lafter_call_\@, context_tracking_enabled, def=0
#endif
	call enter_from_user_mode
.Lafter_call_\@:
#endif
.endm
