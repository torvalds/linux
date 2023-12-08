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
#define TDVMCALL_EXPOSE_REGS_MASK	( TDX_R10 | TDX_R11 | \
					  TDX_R12 | TDX_R13 | \
					  TDX_R14 | TDX_R15 )

/*
 * __tdx_module_call()  - Used by TDX guests to request services from
 * the TDX module (does not include VMM services) using TDCALL instruction.
 *
 * Transforms function call register arguments into the TDCALL register ABI.
 * After TDCALL operation, TDX module output is saved in @out (if it is
 * provided by the user).
 *
 *-------------------------------------------------------------------------
 * TDCALL ABI:
 *-------------------------------------------------------------------------
 * Input Registers:
 *
 * RAX                 - TDCALL Leaf number.
 * RCX,RDX,R8-R9       - TDCALL Leaf specific input registers.
 *
 * Output Registers:
 *
 * RAX                 - TDCALL instruction error code.
 * RCX,RDX,R8-R11      - TDCALL Leaf specific output registers.
 *
 *-------------------------------------------------------------------------
 *
 * __tdx_module_call() function ABI:
 *
 * @fn  (RDI)          - TDCALL Leaf ID,    moved to RAX
 * @rcx (RSI)          - Input parameter 1, moved to RCX
 * @rdx (RDX)          - Input parameter 2, moved to RDX
 * @r8  (RCX)          - Input parameter 3, moved to R8
 * @r9  (R8)           - Input parameter 4, moved to R9
 *
 * @out (R9)           - struct tdx_module_output pointer
 *                       stored temporarily in R12 (not
 *                       shared with the TDX module). It
 *                       can be NULL.
 *
 * Return status of TDCALL via RAX.
 */
SYM_FUNC_START(__tdx_module_call)
	FRAME_BEGIN
	TDX_MODULE_CALL host=0
	FRAME_END
	RET
SYM_FUNC_END(__tdx_module_call)

/*
 * __tdx_hypercall() - Make hypercalls to a TDX VMM using TDVMCALL leaf
 * of TDCALL instruction
 *
 * Transforms values in  function call argument struct tdx_hypercall_args @args
 * into the TDCALL register ABI. After TDCALL operation, VMM output is saved
 * back in @args.
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
 * RBX, RBP, RDI, RSI  - Used to pass VMCALL sub function specific arguments.
 * R8-R9, R12-R15      - Same as above.
 *
 * Output Registers:
 *
 * RAX                 - TDCALL instruction status (Not related to hypercall
 *                        output).
 * R10                 - Hypercall output error code.
 * R11-R15             - Hypercall sub function specific output values.
 *
 *-------------------------------------------------------------------------
 *
 * __tdx_hypercall() function ABI:
 *
 * @args  (RDI)        - struct tdx_hypercall_args for input and output
 * @flags (RSI)        - TDX_HCALL_* flags
 *
 * On successful completion, return the hypercall error code.
 */
SYM_FUNC_START(__tdx_hypercall)
	FRAME_BEGIN

	/* Save callee-saved GPRs as mandated by the x86_64 ABI */
	push %r15
	push %r14
	push %r13
	push %r12

	/* Mangle function call ABI into TDCALL ABI: */
	/* Set TDCALL leaf ID (TDVMCALL (0)) in RAX */
	xor %eax, %eax

	/* Copy hypercall registers from arg struct: */
	movq TDX_HYPERCALL_r10(%rdi), %r10
	movq TDX_HYPERCALL_r11(%rdi), %r11
	movq TDX_HYPERCALL_r12(%rdi), %r12
	movq TDX_HYPERCALL_r13(%rdi), %r13
	movq TDX_HYPERCALL_r14(%rdi), %r14
	movq TDX_HYPERCALL_r15(%rdi), %r15

	movl $TDVMCALL_EXPOSE_REGS_MASK, %ecx

	/*
	 * For the idle loop STI needs to be called directly before the TDCALL
	 * that enters idle (EXIT_REASON_HLT case). STI instruction enables
	 * interrupts only one instruction later. If there is a window between
	 * STI and the instruction that emulates the HALT state, there is a
	 * chance for interrupts to happen in this window, which can delay the
	 * HLT operation indefinitely. Since this is the not the desired
	 * result, conditionally call STI before TDCALL.
	 */
	testq $TDX_HCALL_ISSUE_STI, %rsi
	jz .Lskip_sti
	sti
.Lskip_sti:
	tdcall

	/*
	 * RAX==0 indicates a failure of the TDVMCALL mechanism itself and that
	 * something has gone horribly wrong with the TDX module.
	 *
	 * The return status of the hypercall operation is in a separate
	 * register (in R10). Hypercall errors are a part of normal operation
	 * and are handled by callers.
	 */
	testq %rax, %rax
	jne .Lpanic

	/* TDVMCALL leaf return code is in R10 */
	movq %r10, %rax

	/* Copy hypercall result registers to arg struct if needed */
	testq $TDX_HCALL_HAS_OUTPUT, %rsi
	jz .Lout

	movq %r10, TDX_HYPERCALL_r10(%rdi)
	movq %r11, TDX_HYPERCALL_r11(%rdi)
	movq %r12, TDX_HYPERCALL_r12(%rdi)
	movq %r13, TDX_HYPERCALL_r13(%rdi)
	movq %r14, TDX_HYPERCALL_r14(%rdi)
	movq %r15, TDX_HYPERCALL_r15(%rdi)
.Lout:
	/*
	 * Zero out registers exposed to the VMM to avoid speculative execution
	 * with VMM-controlled values. This needs to include all registers
	 * present in TDVMCALL_EXPOSE_REGS_MASK (except R12-R15). R12-R15
	 * context will be restored.
	 */
	xor %r10d, %r10d
	xor %r11d, %r11d

	/* Restore callee-saved GPRs as mandated by the x86_64 ABI */
	pop %r12
	pop %r13
	pop %r14
	pop %r15

	FRAME_END

	RET
.Lpanic:
	call __tdx_hypercall_failed
	/* __tdx_hypercall_failed never returns */
	REACHABLE
	jmp .Lpanic
SYM_FUNC_END(__tdx_hypercall)
