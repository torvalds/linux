/* SPDX-License-Identifier: GPL-2.0 */
#include <linux/jump_label.h>
#include <asm/unwind_hints.h>
#include <asm/cpufeatures.h>
#include <asm/page_types.h>
#include <asm/percpu.h>
#include <asm/asm-offsets.h>
#include <asm/processor-flags.h>
#include <asm/msr.h>
#include <asm/nospec-branch.h>

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
	.if \save_ret
	pushq	%rsi		/* pt_regs->si */
	movq	8(%rsp), %rsi	/* temporarily store the return address in %rsi */
	movq	%rdi, 8(%rsp)	/* pt_regs->di (overwriting original return address) */
	.else
	pushq   %rdi		/* pt_regs->di */
	pushq   %rsi		/* pt_regs->si */
	.endif
	pushq	\rdx		/* pt_regs->dx */
	pushq   %rcx		/* pt_regs->cx */
	pushq   \rax		/* pt_regs->ax */
	pushq   %r8		/* pt_regs->r8 */
	pushq   %r9		/* pt_regs->r9 */
	pushq   %r10		/* pt_regs->r10 */
	pushq   %r11		/* pt_regs->r11 */
	pushq	%rbx		/* pt_regs->rbx */
	pushq	%rbp		/* pt_regs->rbp */
	pushq	%r12		/* pt_regs->r12 */
	pushq	%r13		/* pt_regs->r13 */
	pushq	%r14		/* pt_regs->r14 */
	pushq	%r15		/* pt_regs->r15 */
	UNWIND_HINT_REGS

	.if \save_ret
	pushq	%rsi		/* return address on top of stack */
	.endif

	/*
	 * Sanitize registers of values that a speculation attack might
	 * otherwise want to exploit. The lower registers are likely clobbered
	 * well before they could be put to use in a speculative execution
	 * gadget.
	 */
	xorl	%edx,  %edx	/* nospec dx  */
	xorl	%ecx,  %ecx	/* nospec cx  */
	xorl	%r8d,  %r8d	/* nospec r8  */
	xorl	%r9d,  %r9d	/* nospec r9  */
	xorl	%r10d, %r10d	/* nospec r10 */
	xorl	%r11d, %r11d	/* nospec r11 */
	xorl	%ebx,  %ebx	/* nospec rbx */
	xorl	%ebp,  %ebp	/* nospec rbp */
	xorl	%r12d, %r12d	/* nospec r12 */
	xorl	%r13d, %r13d	/* nospec r13 */
	xorl	%r14d, %r14d	/* nospec r14 */
	xorl	%r15d, %r15d	/* nospec r15 */

.endm

.macro POP_REGS pop_rdi=1
	popq %r15
	popq %r14
	popq %r13
	popq %r12
	popq %rbp
	popq %rbx
	popq %r11
	popq %r10
	popq %r9
	popq %r8
	popq %rax
	popq %rcx
	popq %rdx
	popq %rsi
	.if \pop_rdi
	popq %rdi
	.endif
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
 * IBRS kernel mitigation for Spectre_v2.
 *
 * Assumes full context is established (PUSH_REGS, CR3 and GS) and it clobbers
 * the regs it uses (AX, CX, DX). Must be called before the first RET
 * instruction (NOTE! UNTRAIN_RET includes a RET instruction)
 *
 * The optional argument is used to save/restore the current value,
 * which is used on the paranoid paths.
 *
 * Assumes x86_spec_ctrl_{base,current} to have SPEC_CTRL_IBRS set.
 */
.macro IBRS_ENTER save_reg
#ifdef CONFIG_CPU_IBRS_ENTRY
	ALTERNATIVE "jmp .Lend_\@", "", X86_FEATURE_KERNEL_IBRS
	movl	$MSR_IA32_SPEC_CTRL, %ecx

.ifnb \save_reg
	rdmsr
	shl	$32, %rdx
	or	%rdx, %rax
	mov	%rax, \save_reg
	test	$SPEC_CTRL_IBRS, %eax
	jz	.Ldo_wrmsr_\@
	lfence
	jmp	.Lend_\@
.Ldo_wrmsr_\@:
.endif

	movq	PER_CPU_VAR(x86_spec_ctrl_current), %rdx
	movl	%edx, %eax
	shr	$32, %rdx
	wrmsr
.Lend_\@:
#endif
.endm

/*
 * Similar to IBRS_ENTER, requires KERNEL GS,CR3 and clobbers (AX, CX, DX)
 * regs. Must be called after the last RET.
 */
.macro IBRS_EXIT save_reg
#ifdef CONFIG_CPU_IBRS_ENTRY
	ALTERNATIVE "jmp .Lend_\@", "", X86_FEATURE_KERNEL_IBRS
	movl	$MSR_IA32_SPEC_CTRL, %ecx

.ifnb \save_reg
	mov	\save_reg, %rdx
.else
	movq	PER_CPU_VAR(x86_spec_ctrl_current), %rdx
	andl	$(~SPEC_CTRL_IBRS), %edx
.endif

	movl	%edx, %eax
	shr	$32, %rdx
	wrmsr
.Lend_\@:
#endif
.endm

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

.macro STACKLEAK_ERASE_NOCLOBBER
#ifdef CONFIG_GCC_PLUGIN_STACKLEAK
	PUSH_AND_CLEAR_REGS
	call stackleak_erase
	POP_REGS
#endif
.endm

.macro SAVE_AND_SET_GSBASE scratch_reg:req save_reg:req
	rdgsbase \save_reg
	GET_PERCPU_BASE \scratch_reg
	wrgsbase \scratch_reg
.endm

#else /* CONFIG_X86_64 */
# undef		UNWIND_HINT_IRET_REGS
# define	UNWIND_HINT_IRET_REGS
#endif /* !CONFIG_X86_64 */

.macro STACKLEAK_ERASE
#ifdef CONFIG_GCC_PLUGIN_STACKLEAK
	call stackleak_erase
#endif
.endm

#ifdef CONFIG_SMP

/*
 * CPU/node NR is loaded from the limit (size) field of a special segment
 * descriptor entry in GDT.
 */
.macro LOAD_CPU_AND_NODE_SEG_LIMIT reg:req
	movq	$__CPUNODE_SEG, \reg
#ifdef __clang__
	.long 0xc0030f48
#else
	lsl	\reg, \reg
#endif
.endm

/*
 * Fetch the per-CPU GSBASE value for this processor and put it in @reg.
 * We normally use %gs for accessing per-CPU data, but we are setting up
 * %gs here and obviously can not use %gs itself to access per-CPU data.
 *
 * Do not use RDPID, because KVM loads guest's TSC_AUX on vm-entry and
 * may not restore the host's value until the CPU returns to userspace.
 * Thus the kernel would consume a guest's TSC_AUX if an NMI arrives
 * while running KVM's run loop.
 */
.macro GET_PERCPU_BASE reg:req
	LOAD_CPU_AND_NODE_SEG_LIMIT \reg
	andq	$VDSO_CPUNODE_MASK, \reg
	movq	__per_cpu_offset(, \reg, 8), \reg
.endm

#else

.macro GET_PERCPU_BASE reg:req
	movq	pcpu_unit_offsets(%rip), \reg
.endm

#endif /* CONFIG_SMP */
