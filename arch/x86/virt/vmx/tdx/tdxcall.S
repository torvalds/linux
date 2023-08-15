/* SPDX-License-Identifier: GPL-2.0 */
#include <asm/asm-offsets.h>
#include <asm/frame.h>
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
 * RAX                 - TDCALL/SEAMCALL Leaf number.
 * RCX,RDX,R8-R11      - TDCALL/SEAMCALL Leaf specific input registers.
 *
 * Output Registers:
 *
 * RAX                 - TDCALL/SEAMCALL instruction error code.
 * RCX,RDX,R8-R11      - TDCALL/SEAMCALL Leaf specific output registers.
 *
 *-------------------------------------------------------------------------
 */
.macro TDX_MODULE_CALL host:req ret=0
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

.if \host
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
	/* Copy output registers to the structure */
	movq %rcx, TDX_MODULE_rcx(%rsi)
	movq %rdx, TDX_MODULE_rdx(%rsi)
	movq %r8,  TDX_MODULE_r8(%rsi)
	movq %r9,  TDX_MODULE_r9(%rsi)
	movq %r10, TDX_MODULE_r10(%rsi)
	movq %r11, TDX_MODULE_r11(%rsi)
.endif

.if \host
.Lout\@:
.endif
	FRAME_END
	RET

.if \host
.Lseamcall_vmfailinvalid\@:
	mov $TDX_SEAMCALL_VMFAILINVALID, %rax
	jmp .Lout\@
.endif	/* \host */

.endm
