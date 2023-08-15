/* SPDX-License-Identifier: GPL-2.0 */
#include <asm/asm-offsets.h>
#include <asm/asm.h>
#include <asm/frame.h>

#include <linux/linkage.h>
#include <linux/errno.h>

#include "../../virt/vmx/tdx/tdxcall.S"

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
 * Only RCX/RDX/R8-R11 are used as input registers.
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
 * Only RCX/RDX/R8-R11 are used as input/output registers.
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
 * Transforms values in  function call argument struct tdx_module_args @args
 * into the TDCALL register ABI. After TDCALL operation, VMM output is saved
 * back in @args, if \ret is 1.
 *
 * Depends on the caller to pass TDG.VP.VMCALL as the TDCALL leaf, and set
 * @args::rcx to TDVMCALL_EXPOSE_REGS_MASK.
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
.macro TDX_HYPERCALL
	FRAME_BEGIN

	/* Save callee-saved GPRs as mandated by the x86_64 ABI */
	push %r15
	push %r14
	push %r13
	push %r12
	push %rbx

	/* Move Leaf ID to RAX */
	movq %rdi, %rax

	/* Move bitmap of shared registers to RCX */
	movq TDX_MODULE_rcx(%rsi), %rcx

	/* Copy hypercall registers from arg struct: */
	movq TDX_MODULE_r8(%rsi),  %r8
	movq TDX_MODULE_r9(%rsi),  %r9
	movq TDX_MODULE_r10(%rsi), %r10
	movq TDX_MODULE_r11(%rsi), %r11
	movq TDX_MODULE_r12(%rsi), %r12
	movq TDX_MODULE_r13(%rsi), %r13
	movq TDX_MODULE_r14(%rsi), %r14
	movq TDX_MODULE_r15(%rsi), %r15
	movq TDX_MODULE_rdi(%rsi), %rdi
	movq TDX_MODULE_rbx(%rsi), %rbx
	movq TDX_MODULE_rdx(%rsi), %rdx

	pushq %rsi
	movq TDX_MODULE_rsi(%rsi), %rsi

	tdcall

	/*
	 * Restore the pointer of the structure to save output registers.
	 *
	 * RCX is used as bitmap of shared registers and doesn't hold any
	 * value provided by the VMM, thus it can be used as spare to
	 * restore the structure pointer.
	 */
	popq %rcx
	movq %rsi, TDX_MODULE_rsi(%rcx)
	movq %rcx, %rsi

	movq %r8,  TDX_MODULE_r8(%rsi)
	movq %r9,  TDX_MODULE_r9(%rsi)
	movq %r10, TDX_MODULE_r10(%rsi)
	movq %r11, TDX_MODULE_r11(%rsi)
	movq %r12, TDX_MODULE_r12(%rsi)
	movq %r13, TDX_MODULE_r13(%rsi)
	movq %r14, TDX_MODULE_r14(%rsi)
	movq %r15, TDX_MODULE_r15(%rsi)
	movq %rdi, TDX_MODULE_rdi(%rsi)
	movq %rbx, TDX_MODULE_rbx(%rsi)
	movq %rdx, TDX_MODULE_rdx(%rsi)

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
.endm

/*
 *
 * __tdcall_hypercall() function ABI:
 *
 * @fn   (RDI)	- TDCALL leaf ID, moved to RAX
 * @args (RSI)	- struct tdx_module_args for input/output
 *
 * @fn and @args::rcx from the caller must be TDG_VP_VMCALL and
 * TDVMCALL_EXPOSE_REGS_MASK respectively.
 *
 * On successful completion, return the hypercall error code.
 */
SYM_FUNC_START(__tdcall_hypercall)
	TDX_HYPERCALL
SYM_FUNC_END(__tdcall_hypercall)
