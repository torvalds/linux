/* SPDX-License-Identifier: GPL-2.0 */
#include <asm/asm-offsets.h>
#include <asm/asm.h>
#include <asm/frame.h>
#include <asm/unwind_hints.h>

#include <linux/linkage.h>
#include <linux/bits.h>
#include <linux/errno.h>

#include "../../virt/vmx/tdx/tdxcall.S"

/*
 * Bitmasks of exposed registers (with VMM).
 */
#define TDX_RDX		BIT(2)
#define TDX_RBX		BIT(3)
#define TDX_RSI		BIT(6)
#define TDX_RDI		BIT(7)
#define TDX_R8		BIT(8)
#define TDX_R9		BIT(9)
#define TDX_R10		BIT(10)
#define TDX_R11		BIT(11)
#define TDX_R12		BIT(12)
#define TDX_R13		BIT(13)
#define TDX_R14		BIT(14)
#define TDX_R15		BIT(15)

/*
 * These registers are clobbered to hold arguments for each
 * TDVMCALL. They are safe to expose to the VMM.
 * Each bit in this mask represents a register ID. Bit field
 * details can be found in TDX GHCI specification, section
 * titled "TDCALL [TDG.VP.VMCALL] leaf".
 */
#define TDVMCALL_EXPOSE_REGS_MASK	\
	( TDX_RDX | TDX_RBX | TDX_RSI | TDX_RDI | TDX_R8  | TDX_R9  | \
	  TDX_R10 | TDX_R11 | TDX_R12 | TDX_R13 | TDX_R14 | TDX_R15 )

.section .noinstr.text, "ax"

/*
 * __tdcall()  - Used by TDX guests to request services from the TDX
 * module (does not include VMM services) using TDCALL instruction.
 *
 * __tdcall() function ABI:
 *
 * @fn   (RDI)	- TDCALL Leaf ID, moved to RAX
 * @args (RSI)	- struct tdx_module_args for input
 *
 * Return status of TDCALL via RAX.
 */
SYM_FUNC_START(__tdcall)
	TDX_MODULE_CALL host=0
SYM_FUNC_END(__tdcall)

/*
 * __tdcall_ret() - Used by TDX guests to request services from the TDX
 * module (does not include VMM services) using TDCALL instruction, with
 * saving output registers to the 'struct tdx_module_args' used as input.
 *
 * __tdcall_ret() function ABI:
 *
 * @fn   (RDI)	- TDCALL Leaf ID, moved to RAX
 * @args (RSI)	- struct tdx_module_args for input and output
 *
 * Return status of TDCALL via RAX.
 */
SYM_FUNC_START(__tdcall_ret)
	TDX_MODULE_CALL host=0 ret=1
SYM_FUNC_END(__tdcall_ret)

/*
 * TDX_HYPERCALL - Make hypercalls to a TDX VMM using TDVMCALL leaf of TDCALL
 * instruction
 *
 * Transforms values in  function call argument struct tdx_hypercall_args @args
 * into the TDCALL register ABI. After TDCALL operation, VMM output is saved
 * back in @args, if \ret is 1.
 *
 *-------------------------------------------------------------------------
 * TD VMCALL ABI:
 *-------------------------------------------------------------------------
 *
 * Input Registers:
 *
 * RAX                 - TDCALL instruction leaf number (0 - TDG.VP.VMCALL)
 * RCX                 - BITMAP which controls which part of TD Guest GPR
 *                       is passed as-is to the VMM and back.
 * R10                 - Set 0 to indicate TDCALL follows standard TDX ABI
 *                       specification. Non zero value indicates vendor
 *                       specific ABI.
 * R11                 - VMCALL sub function number
 * RBX, RDX, RDI, RSI  - Used to pass VMCALL sub function specific arguments.
 * R8-R9, R12-R15      - Same as above.
 *
 * Output Registers:
 *
 * RAX                 - TDCALL instruction status (Not related to hypercall
 *                        output).
 * RBX, RDX, RDI, RSI  - Hypercall sub function specific output values.
 * R8-R15              - Same as above.
 *
 */
.macro TDX_HYPERCALL ret:req
	FRAME_BEGIN

	/* Save callee-saved GPRs as mandated by the x86_64 ABI */
	push %r15
	push %r14
	push %r13
	push %r12
	push %rbx

	/* Free RDI to be used as TDVMCALL arguments */
	movq %rdi, %rax

	/* Copy hypercall registers from arg struct: */
	movq TDX_HYPERCALL_r8(%rax),  %r8
	movq TDX_HYPERCALL_r9(%rax),  %r9
	movq TDX_HYPERCALL_r10(%rax), %r10
	movq TDX_HYPERCALL_r11(%rax), %r11
	movq TDX_HYPERCALL_r12(%rax), %r12
	movq TDX_HYPERCALL_r13(%rax), %r13
	movq TDX_HYPERCALL_r14(%rax), %r14
	movq TDX_HYPERCALL_r15(%rax), %r15
	movq TDX_HYPERCALL_rdi(%rax), %rdi
	movq TDX_HYPERCALL_rsi(%rax), %rsi
	movq TDX_HYPERCALL_rbx(%rax), %rbx
	movq TDX_HYPERCALL_rdx(%rax), %rdx

	push %rax

	/* Mangle function call ABI into TDCALL ABI: */
	/* Set TDCALL leaf ID (TDVMCALL (0)) in RAX */
	xor %eax, %eax

	movl $TDVMCALL_EXPOSE_REGS_MASK, %ecx

	tdcall

	/*
	 * RAX!=0 indicates a failure of the TDVMCALL mechanism itself and that
	 * something has gone horribly wrong with the TDX module.
	 *
	 * The return status of the hypercall operation is in a separate
	 * register (in R10). Hypercall errors are a part of normal operation
	 * and are handled by callers.
	 */
	testq %rax, %rax
	jne .Lpanic\@

	pop %rax

	.if \ret
	movq %r8,  TDX_HYPERCALL_r8(%rax)
	movq %r9,  TDX_HYPERCALL_r9(%rax)
	movq %r10, TDX_HYPERCALL_r10(%rax)
	movq %r11, TDX_HYPERCALL_r11(%rax)
	movq %r12, TDX_HYPERCALL_r12(%rax)
	movq %r13, TDX_HYPERCALL_r13(%rax)
	movq %r14, TDX_HYPERCALL_r14(%rax)
	movq %r15, TDX_HYPERCALL_r15(%rax)
	movq %rdi, TDX_HYPERCALL_rdi(%rax)
	movq %rsi, TDX_HYPERCALL_rsi(%rax)
	movq %rbx, TDX_HYPERCALL_rbx(%rax)
	movq %rdx, TDX_HYPERCALL_rdx(%rax)
	.endif

	/* TDVMCALL leaf return code is in R10 */
	movq %r10, %rax

	/*
	 * Zero out registers exposed to the VMM to avoid speculative execution
	 * with VMM-controlled values. This needs to include all registers
	 * present in TDVMCALL_EXPOSE_REGS_MASK, except RBX, and R12-R15 which
	 * will be restored.
	 */
	xor %r8d,  %r8d
	xor %r9d,  %r9d
	xor %r10d, %r10d
	xor %r11d, %r11d
	xor %rdi,  %rdi
	xor %rsi,  %rsi
	xor %rdx,  %rdx

	/* Restore callee-saved GPRs as mandated by the x86_64 ABI */
	pop %rbx
	pop %r12
	pop %r13
	pop %r14
	pop %r15

	FRAME_END

	RET
.Lpanic\@:
	call __tdx_hypercall_failed
	/* __tdx_hypercall_failed never returns */
	REACHABLE
	jmp .Lpanic\@
.endm

/*
 *
 * __tdx_hypercall() function ABI:
 *
 * @args  (RDI)        - struct tdx_hypercall_args for input
 *
 * On successful completion, return the hypercall error code.
 */
SYM_FUNC_START(__tdx_hypercall)
	TDX_HYPERCALL ret=0
SYM_FUNC_END(__tdx_hypercall)

/*
 *
 * __tdx_hypercall_ret() function ABI:
 *
 * @args  (RDI)        - struct tdx_hypercall_args for input and output
 *
 * On successful completion, return the hypercall error code.
 */
SYM_FUNC_START(__tdx_hypercall_ret)
	TDX_HYPERCALL ret=1
SYM_FUNC_END(__tdx_hypercall_ret)
