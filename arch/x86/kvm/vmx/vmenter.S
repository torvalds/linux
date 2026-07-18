/* SPDX-License-Identifier: GPL-2.0 */
#include <linux/linkage.h>
#include <asm/asm.h>
#include <asm/bitsperlong.h>
#include <asm/nospec-branch.h>
#include <asm/percpu.h>
#include <asm/segment.h>
#include "kvm-asm-offsets.h"
#include "vmenter.h"

.section .noinstr.text, "ax"

/**
 * __vmx_vcpu_run - Run a vCPU via a transition to VMX guest mode
 * @vmx:	struct vcpu_vmx *
 * @flags:	KVM_ENTER_VMRESUME:	use VMRESUME instead of VMLAUNCH
 *		KVM_ENTER_SAVE_SPEC_CTRL: save guest SPEC_CTRL into vmx->spec_ctrl
 *		KVM_ENTER_CLEAR_CPU_BUFFERS_FOR_MMIO: vCPU can access host MMIO
 *
 * Returns:
 *	0 on VM-Exit, 1 on VM-Fail
 */
SYM_FUNC_START(__vmx_vcpu_run)
	push %_ASM_BP
	mov  %_ASM_SP, %_ASM_BP
#ifdef CONFIG_X86_64
	push %r15
	push %r14
	push %r13
	push %r12
#else
	push %edi
	push %esi
#endif
	push %_ASM_BX

	/* Save @vmx for SPEC_CTRL handling */
	push %_ASM_ARG1

	/* Save @flags (used for VMLAUNCH vs. VMRESUME and mitigations). */
	push %_ASM_ARG2

	lea (%_ASM_SP), %_ASM_ARG2
	call vmx_update_host_rsp

	/* Reload @vmx, _ASM_ARG1 may be modified by vmx_update_host_rsp().  */
	mov WORD_SIZE(%_ASM_SP), %_ASM_DI

	/*
	 * Unlike AMD there's no V_SPEC_CTRL here, so do not leave the body
	 * out of line.  Clobbers RAX, RCX, RDX, RSI.
	 */
	ALTERNATIVE "jmp .Lspec_ctrl_guest_done", "", X86_FEATURE_MSR_SPEC_CTRL
	RESTORE_GUEST_SPEC_CTRL_BODY VMX_spec_ctrl(%_ASM_DI), .Lspec_ctrl_guest_done
.Lspec_ctrl_guest_done:

	/*
	 * Since vmentry is serializing on affected CPUs, there's no need for
	 * an LFENCE to stop speculation from skipping the wrmsr.
	 */

	/*
	 * Load guest registers.  Don't clobber flags. Intentionally omit
	 * %_ASM_SP as it's context switched by hardware
	 */
	LOAD_REGS %_ASM_DI, VMX_vcpu_arch_regs, \
		  %_ASM_AX, %_ASM_CX, %_ASM_DX, %_ASM_BX, %_ASM_BP, %_ASM_SI
#ifdef CONFIG_X86_64
	LOAD_REGS %_ASM_DI, VMX_vcpu_arch_regs, \
		  %r8, %r9, %r10, %r11, %r12, %r13, %r14, %r15
#endif
	/* Load guest RDI.  This kills the @vmx pointer! */
	LOAD_REGS %_ASM_DI, VMX_vcpu_arch_regs, %_ASM_DI

	/*
	 * Note, ALTERNATIVE_2 works in reverse order.  If CLEAR_CPU_BUF_VM is
	 * enabled, do VERW unconditionally.  If CPU_BUF_VM_MMIO is enabled,
	 * check @flags to see if the vCPU has access to host MMIO, and if so,
	 * do VERW.  Else, do nothing (no mitigations needed/enabled).
	 */
	ALTERNATIVE_2 "",									  \
		      __stringify(testl $KVM_ENTER_CLEAR_CPU_BUFFERS_FOR_MMIO, (%_ASM_SP);	  \
				  jz .Lskip_mmio_verw;						  \
				  VERW;								  \
				  .Lskip_mmio_verw:),					  	  \
		      X86_FEATURE_CLEAR_CPU_BUF_VM_MMIO,					  \
		      __stringify(VERW), X86_FEATURE_CLEAR_CPU_BUF_VM

	/* Check @flags to see if VMLAUNCH or VMRESUME is needed. */
	testl $KVM_ENTER_VMRESUME, (%_ASM_SP)
	jz .Lvmlaunch

	/*
	 * After a successful VMRESUME/VMLAUNCH, control flow "magically"
	 * resumes below at 'vmx_vmexit' due to the VMCS HOST_RIP setting.
	 * So this isn't a typical function and objtool needs to be told to
	 * save the unwind state here and restore it below.
	 */
	UNWIND_HINT_SAVE

/*
 * If VMRESUME/VMLAUNCH and corresponding vmexit succeed, execution resumes at
 * the 'vmx_vmexit' label below.
 */
.Lvmresume:
	vmresume
	jmp .Lvmfail

.Lvmlaunch:
	vmlaunch
	jmp .Lvmfail

	_ASM_EXTABLE(.Lvmresume, .Lfixup)
	_ASM_EXTABLE(.Lvmlaunch, .Lfixup)

SYM_INNER_LABEL_ALIGN(vmx_vmexit, SYM_L_GLOBAL)

	/* Restore unwind state from before the VMRESUME/VMLAUNCH. */
	UNWIND_HINT_RESTORE
	ENDBR

	/* Temporarily save guest's RDI. */
	push %_ASM_DI

	/* Reload @vmx to RDI. */
	mov 2*WORD_SIZE(%_ASM_SP), %_ASM_DI

	/*
	 * Save all guest registers, including RDI from the stack. Intentionally
	 * omit %_ASM_SP as it's context switched by hardware
	 */
	STORE_REGS %_ASM_DI, VMX_vcpu_arch_regs, \
		   %_ASM_AX, %_ASM_CX, %_ASM_DX, %_ASM_BX, %_ASM_BP, %_ASM_SI
	POP_REGS   %_ASM_DI, VMX_vcpu_arch_regs, %_ASM_DI
#ifdef CONFIG_X86_64
	STORE_REGS %_ASM_DI, VMX_vcpu_arch_regs, \
		   %r8, %r9, %r10, %r11, %r12, %r13, %r14, %r15
#endif

	/* Clear return value to indicate VM-Exit (as opposed to VM-Fail). */
	xor %ebx, %ebx

.Lclear_regs:
	/*
	 * Clear all general purpose registers except RSP and RBX to prevent
	 * speculative use of the guest's values, even those that are reloaded
	 * via the stack.  In theory, an L1 cache miss when restoring registers
	 * could lead to speculative execution with the guest's values.
	 * Zeroing XORs are dirt cheap, i.e. the extra paranoia is essentially
	 * free.  RSP and RBX are exempt as RSP is restored by hardware during
	 * VM-Exit and RBX is explicitly loaded with 0 or 1 to hold the return
	 * value.
	 */
	CLEAR_REGS %eax, %ecx, %edx, %ebp, %esi, %edi
#ifdef CONFIG_X86_64
	CLEAR_REGS %r8d, %r9d, %r10d, %r11d, %r12d, %r13d, %r14d, %r15d
#endif

	/*
	 * IMPORTANT: RSB filling and SPEC_CTRL handling must be done before
	 * the first unbalanced RET after vmexit!
	 *
	 * For retpoline or IBRS, RSB filling is needed to prevent poisoned RSB
	 * entries and (in some cases) RSB underflow.
	 *
	 * eIBRS has its own protection against poisoned RSB, so it doesn't
	 * need the RSB filling sequence.  But it does need to be enabled, and a
	 * single call to retire, before the first unbalanced RET.
	 */

	FILL_RETURN_BUFFER %_ASM_CX, RSB_CLEAR_LOOPS, X86_FEATURE_RSB_VMEXIT,\
			   X86_FEATURE_RSB_VMEXIT_LITE

	/* Clobbers RAX, RCX, RDX, RSI.  */
	ALTERNATIVE "jmp .Lspec_ctrl_host_done", "", X86_FEATURE_MSR_SPEC_CTRL
	mov WORD_SIZE(%_ASM_SP), %_ASM_DI
	RESTORE_HOST_SPEC_CTRL_BODY VMX_spec_ctrl(%_ASM_DI), (%_ASM_SP), .Lspec_ctrl_host_done
.Lspec_ctrl_host_done:

	/*
	 * Halt speculation past a conditional wrmsr.  Intel's eIBRS
	 * guarantees that the guest cannot control the RSB "once IBRS is
	 * set", but in the eIBRS case speculative execution past the 'je'
	 * can go all the way to the RET below while MSR_IA32_SPEC_CTRL
	 * still holds the guest value.
	 */
	ALTERNATIVE_2 "", "lfence", X86_FEATURE_MSR_SPEC_CTRL, \
		"", X86_FEATURE_KERNEL_IBRS

	CLEAR_BRANCH_HISTORY_VMEXIT

	/* Put return value in AX */
	mov %_ASM_BX, %_ASM_AX

	/* Pop our saved arguments from the stack */
	pop %_ASM_BX
	pop %_ASM_BX

	/* ... and then the callee-save registers */
	pop %_ASM_BX
#ifdef CONFIG_X86_64
	pop %r12
	pop %r13
	pop %r14
	pop %r15
#else
	pop %esi
	pop %edi
#endif
	pop %_ASM_BP
	RET

.Lfixup:
	cmpb $0, _ASM_RIP(virt_rebooting)
	jne .Lvmfail
	ud2
.Lvmfail:
	/* VM-Fail: set return value to 1 */
	mov $1, %_ASM_BX
	jmp .Lclear_regs

SYM_FUNC_END(__vmx_vcpu_run)

#ifndef CONFIG_CC_HAS_ASM_GOTO_OUTPUT

/**
 * vmread_error_trampoline - Trampoline from inline asm to vmread_error()
 * @field:	VMCS field encoding that failed
 * @fault:	%true if the VMREAD faulted, %false if it failed
 *
 * Save and restore volatile registers across a call to vmread_error().  Note,
 * all parameters are passed on the stack.
 */
SYM_FUNC_START(vmread_error_trampoline)
	push %_ASM_BP
	mov  %_ASM_SP, %_ASM_BP

	push %_ASM_AX
	push %_ASM_CX
	push %_ASM_DX
#ifdef CONFIG_X86_64
	push %rdi
	push %rsi
	push %r8
	push %r9
	push %r10
	push %r11
#endif

	/* Load @field and @fault to arg1 and arg2 respectively. */
	mov 3*WORD_SIZE(%_ASM_BP), %_ASM_ARG2
	mov 2*WORD_SIZE(%_ASM_BP), %_ASM_ARG1

	call vmread_error_trampoline2

	/* Zero out @fault, which will be popped into the result register. */
	_ASM_MOV $0, 3*WORD_SIZE(%_ASM_BP)

#ifdef CONFIG_X86_64
	pop %r11
	pop %r10
	pop %r9
	pop %r8
	pop %rsi
	pop %rdi
#endif
	pop %_ASM_DX
	pop %_ASM_CX
	pop %_ASM_AX
	pop %_ASM_BP

	RET
SYM_FUNC_END(vmread_error_trampoline)
#endif
