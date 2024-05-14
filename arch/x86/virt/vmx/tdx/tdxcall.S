/* SPDX-License-Identifier: GPL-2.0 */
#include <asm/asm-offsets.h>
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
 */
.macro TDX_MODULE_CALL host:req
	/*
	 * R12 will be used as temporary storage for struct tdx_module_output
	 * pointer. Since R12-R15 registers are not used by TDCALL/SEAMCALL
	 * services supported by this function, it can be reused.
	 */

	/* Callee saved, so preserve it */
	push %r12

	/*
	 * Push output pointer to stack.
	 * After the operation, it will be fetched into R12 register.
	 */
	push %r9

	/* Mangle function call ABI into TDCALL/SEAMCALL ABI: */
	/* Move Leaf ID to RAX */
	mov %rdi, %rax
	/* Move input 4 to R9 */
	mov %r8,  %r9
	/* Move input 3 to R8 */
	mov %rcx, %r8
	/* Move input 1 to RCX */
	mov %rsi, %rcx
	/* Leave input param 2 in RDX */

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
	jnc .Lno_vmfailinvalid
	mov $TDX_SEAMCALL_VMFAILINVALID, %rax
.Lno_vmfailinvalid:

	.else
	tdcall
	.endif

	/*
	 * Fetch output pointer from stack to R12 (It is used
	 * as temporary storage)
	 */
	pop %r12

	/*
	 * Since this macro can be invoked with NULL as an output pointer,
	 * check if caller provided an output struct before storing output
	 * registers.
	 *
	 * Update output registers, even if the call failed (RAX != 0).
	 * Other registers may contain details of the failure.
	 */
	test %r12, %r12
	jz .Lno_output_struct

	/* Copy result registers to output struct: */
	movq %rcx, TDX_MODULE_rcx(%r12)
	movq %rdx, TDX_MODULE_rdx(%r12)
	movq %r8,  TDX_MODULE_r8(%r12)
	movq %r9,  TDX_MODULE_r9(%r12)
	movq %r10, TDX_MODULE_r10(%r12)
	movq %r11, TDX_MODULE_r11(%r12)

.Lno_output_struct:
	/* Restore the state of R12 register */
	pop %r12
.endm
