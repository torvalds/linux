/* SPDX-License-Identifier: GPL-2.0 */
#include <asm/asm-offsets.h>
#include <asm/frame.h>
#include <asm/asm.h>
#include <asm/tdx.h>

/*
 * TDCALL and SEAMCALL are supported in Binutils >= 2.36.
 */
#define tdcall		.byte 0x66,0x0f,0x01,0xcc
#define seamcall	.byte 0x66,0x0f,0x01,0xcf

/*
 * TDX_MODULE_CALL - common helper macro for both
 *                 TDCALL and SEAMCALL instructions.
 *
 * TDCALL   - used by TDX guests to make requests to the
 *            TDX module and hypercalls to the VMM.
 * SEAMCALL - used by TDX hosts to make requests to the
 *            TDX module.
 *
 *-------------------------------------------------------------------------
 * TDCALL/SEAMCALL ABI:
 *-------------------------------------------------------------------------
 * Input Registers:
 *
 * RAX                        - TDCALL/SEAMCALL Leaf number.
 * RCX,RDX,RDI,RSI,RBX,R8-R15 - TDCALL/SEAMCALL Leaf specific input registers.
 *
 * Output Registers:
 *
 * RAX                        - TDCALL/SEAMCALL instruction error code.
 * RCX,RDX,RDI,RSI,RBX,R8-R15 - TDCALL/SEAMCALL Leaf specific output registers.
 *
 *-------------------------------------------------------------------------
 *
 * So while the common core (RAX,RCX,RDX,R8-R11) fits nicely in the
 * callee-clobbered registers and even leaves RDI,RSI free to act as a
 * base pointer, some leafs (e.g., VP.ENTER) make a giant mess of things.
 *
 * For simplicity, assume that anything that needs the callee-saved regs
 * also tramples on RDI,RSI.  This isn't strictly true, see for example
 * TDH.EXPORT.MEM.
 */
.macro TDX_MODULE_CALL host:req ret=0 saved=0
	FRAME_BEGIN

	/* Move Leaf ID to RAX */
	mov %rdi, %rax

	/* Move other input regs from 'struct tdx_module_args' */
	movq	TDX_MODULE_rcx(%rsi), %rcx
	movq	TDX_MODULE_rdx(%rsi), %rdx
	movq	TDX_MODULE_r8(%rsi),  %r8
	movq	TDX_MODULE_r9(%rsi),  %r9
	movq	TDX_MODULE_r10(%rsi), %r10
	movq	TDX_MODULE_r11(%rsi), %r11

.if \saved
	/*
	 * Move additional input regs from the structure.  For simplicity
	 * assume that anything needs the callee-saved regs also tramples
	 * on RDI/RSI (see VP.ENTER).
	 */
	/* Save those callee-saved GPRs as mandated by the x86_64 ABI */
	pushq	%rbx
	pushq	%r12
	pushq	%r13
	pushq	%r14
	pushq	%r15

	movq	TDX_MODULE_r12(%rsi), %r12
	movq	TDX_MODULE_r13(%rsi), %r13
	movq	TDX_MODULE_r14(%rsi), %r14
	movq	TDX_MODULE_r15(%rsi), %r15
	movq	TDX_MODULE_rbx(%rsi), %rbx

.if \ret
	/* Save the structure pointer as RSI is about to be clobbered */
	pushq	%rsi
.endif

	movq	TDX_MODULE_rdi(%rsi), %rdi
	/* RSI needs to be done at last */
	movq	TDX_MODULE_rsi(%rsi), %rsi
.endif	/* \saved */

.if \host
.Lseamcall\@:
	seamcall
	/*
	 * SEAMCALL instruction is essentially a VMExit from VMX root
	 * mode to SEAM VMX root mode.  VMfailInvalid (CF=1) indicates
	 * that the targeted SEAM firmware is not loaded or disabled,
	 * or P-SEAMLDR is busy with another SEAMCALL.  %rax is not
	 * changed in this case.
	 *
	 * Set %rax to TDX_SEAMCALL_VMFAILINVALID for VMfailInvalid.
	 * This value will never be used as actual SEAMCALL error code as
	 * it is from the Reserved status code class.
	 */
	jc .Lseamcall_vmfailinvalid\@
.else
	tdcall
.endif

.if \ret
.if \saved
	/*
	 * Restore the structure from stack to save the output registers
	 *
	 * In case of VP.ENTER returns due to TDVMCALL, all registers are
	 * valid thus no register can be used as spare to restore the
	 * structure from the stack (see "TDH.VP.ENTER Output Operands
	 * Definition on TDCALL(TDG.VP.VMCALL) Following a TD Entry").
	 * For this case, need to make one register as spare by saving it
	 * to the stack and then manually load the structure pointer to
	 * the spare register.
	 *
	 * Note for other TDCALLs/SEAMCALLs there are spare registers
	 * thus no need for such hack but just use this for all.
	 */
	pushq	%rax		/* save the TDCALL/SEAMCALL return code */
	movq	8(%rsp), %rax	/* restore the structure pointer */
	movq	%rsi, TDX_MODULE_rsi(%rax)	/* save RSI */
	popq	%rax		/* restore the return code */
	popq	%rsi		/* pop the structure pointer */

	/* Copy additional output regs to the structure  */
	movq %r12, TDX_MODULE_r12(%rsi)
	movq %r13, TDX_MODULE_r13(%rsi)
	movq %r14, TDX_MODULE_r14(%rsi)
	movq %r15, TDX_MODULE_r15(%rsi)
	movq %rbx, TDX_MODULE_rbx(%rsi)
	movq %rdi, TDX_MODULE_rdi(%rsi)
.endif	/* \saved */

	/* Copy output registers to the structure */
	movq %rcx, TDX_MODULE_rcx(%rsi)
	movq %rdx, TDX_MODULE_rdx(%rsi)
	movq %r8,  TDX_MODULE_r8(%rsi)
	movq %r9,  TDX_MODULE_r9(%rsi)
	movq %r10, TDX_MODULE_r10(%rsi)
	movq %r11, TDX_MODULE_r11(%rsi)
.endif	/* \ret */

.if \saved && \ret
	/*
	 * Clear registers shared by guest for VP.VMCALL/VP.ENTER to prevent
	 * speculative use of guest's/VMM's values, including those are
	 * restored from the stack.
	 *
	 * See arch/x86/kvm/vmx/vmenter.S:
	 *
	 * In theory, a L1 cache miss when restoring register from stack
	 * could lead to speculative execution with guest's values.
	 *
	 * Note: RBP/RSP are not used as shared register.  RSI has been
	 * restored already.
	 *
	 * XOR is cheap, thus unconditionally do for all leafs.
	 */
	xorl %ecx,  %ecx
	xorl %edx,  %edx
	xorl %r8d,  %r8d
	xorl %r9d,  %r9d
	xorl %r10d, %r10d
	xorl %r11d, %r11d
	xorl %r12d, %r12d
	xorl %r13d, %r13d
	xorl %r14d, %r14d
	xorl %r15d, %r15d
	xorl %ebx,  %ebx
	xorl %edi,  %edi
.endif	/* \ret && \host */

.if \host
.Lout\@:
.endif

.if \saved
	/* Restore callee-saved GPRs as mandated by the x86_64 ABI */
	popq	%r15
	popq	%r14
	popq	%r13
	popq	%r12
	popq	%rbx
.endif	/* \saved */

	FRAME_END
	RET

.if \host
.Lseamcall_vmfailinvalid\@:
	mov $TDX_SEAMCALL_VMFAILINVALID, %rax
	jmp .Lseamcall_fail\@

.Lseamcall_trap\@:
	/*
	 * SEAMCALL caused #GP or #UD.  By reaching here RAX contains
	 * the trap number.  Convert the trap number to the TDX error
	 * code by setting TDX_SW_ERROR to the high 32-bits of RAX.
	 *
	 * Note cannot OR TDX_SW_ERROR directly to RAX as OR instruction
	 * only accepts 32-bit immediate at most.
	 */
	movq $TDX_SW_ERROR, %rdi
	orq  %rdi, %rax

.Lseamcall_fail\@:
.if \ret && \saved
	/* pop the unused structure pointer back to RSI */
	popq %rsi
.endif
	jmp .Lout\@

	_ASM_EXTABLE_FAULT(.Lseamcall\@, .Lseamcall_trap\@)
.endif	/* \host */

.endm
