/* SPDX-License-Identifier: GPL-2.0 */
#include <linux/jump_label.h>
#include <asm/unwind_hints.h>

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
