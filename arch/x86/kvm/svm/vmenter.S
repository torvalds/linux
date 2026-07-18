/* SPDX-License-Identifier: GPL-2.0 */
#include <linux/linkage.h>
#include <asm/asm.h>
#include <asm/asm-offsets.h>
#include <asm/bitsperlong.h>
#include <asm/frame.h>
#include <asm/nospec-branch.h>
#include "kvm-asm-offsets.h"
#include "vmenter.h"

#define SVM_vmcb01_pa	(SVM_vmcb01 + KVM_VMCB_pa)

.section .noinstr.text, "ax"

.macro RESTORE_GUEST_SPEC_CTRL
	/* No need to do anything if SPEC_CTRL is unset or V_SPEC_CTRL is set */
	ALTERNATIVE_2 "", \
		"jmp 800f", X86_FEATURE_MSR_SPEC_CTRL, \
		"", X86_FEATURE_V_SPEC_CTRL
.endm

.macro RESTORE_HOST_SPEC_CTRL
	/* No need to do anything if SPEC_CTRL is unset or V_SPEC_CTRL is set */
	ALTERNATIVE_2 "", \
		"jmp 900f", X86_FEATURE_MSR_SPEC_CTRL, \
		"", X86_FEATURE_V_SPEC_CTRL
.endm

#define SVM_CLEAR_CPU_BUFFERS \
	ALTERNATIVE "", __CLEAR_CPU_BUFFERS, X86_FEATURE_CLEAR_CPU_BUF_VM

/**
 * __svm_vcpu_run - Run a vCPU via a transition to SVM guest mode
 * @svm:	 struct vcpu_svm *
 * @enter_flags: u32
 */
SYM_FUNC_START(__svm_vcpu_run)
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

	/*
	 * Save variables needed after vmexit on the stack, in inverse
	 * order compared to when they are needed.
	 */

	/* Accessed directly from the stack in RESTORE_HOST_SPEC_CTRL.  */
	push %_ASM_ARG2

	/* Needed to restore access to percpu variables.  */
	__ASM_SIZE(push) PER_CPU_VAR(svm_data + SD_save_area_pa)

	/* Finally save @svm. */
	push %_ASM_ARG1

.ifnc _ASM_ARG1, _ASM_DI
	/*
	 * Stash @svm in RDI early. On 32-bit, arguments are in RAX, RCX
	 * and RDX which are clobbered by RESTORE_GUEST_SPEC_CTRL.
	 */
	mov %_ASM_ARG1, %_ASM_DI
.endif

	/* Clobbers RAX, RCX, RDX (and ESI on 32-bit), consumes RDI (@svm). */
	RESTORE_GUEST_SPEC_CTRL
801:

	/*
	 * Use a single vmcb (vmcb01 because it's always valid) for
	 * context switching guest state via VMLOAD/VMSAVE, that way
	 * the state doesn't need to be copied between vmcb01 and
	 * vmcb02 when switching vmcbs for nested virtualization.
	 */
	mov SVM_vmcb01_pa(%_ASM_DI), %_ASM_AX
1:	vmload %_ASM_AX
2:

	/* Get svm->current_vmcb->pa into RAX. */
	mov SVM_current_vmcb(%_ASM_DI), %_ASM_AX
	mov KVM_VMCB_pa(%_ASM_AX), %_ASM_AX

	/*
	 * Load guest registers. Intentionally omit %_ASM_AX and %_ASM_SP as
	 * context switched by hardware
	 */
	LOAD_REGS %_ASM_DI, SVM_vcpu_arch_regs, \
		  %_ASM_CX, %_ASM_DX, %_ASM_BX, %_ASM_BP, %_ASM_SI
#ifdef CONFIG_X86_64
	LOAD_REGS %_ASM_DI, SVM_vcpu_arch_regs, \
		  %r8, %r9, %r10, %r11, %r12, %r13, %r14, %r15
#endif
	LOAD_REGS %_ASM_DI, SVM_vcpu_arch_regs, %_ASM_DI

	/* Clobbers EFLAGS.ZF */
	SVM_CLEAR_CPU_BUFFERS

	/* Enter guest mode */
3:	vmrun %_ASM_AX
4:
	/* Pop @svm to RAX while it's the only available register. */
	pop %_ASM_AX

	/*
	 * Save all guest registers. Intentionally omit %_ASM_AX and %_ASM_SP as
	 * context switched by hardware
	 */
	STORE_REGS %_ASM_AX, SVM_vcpu_arch_regs, \
		   %_ASM_CX, %_ASM_DX, %_ASM_BX, %_ASM_BP, %_ASM_SI, %_ASM_DI
#ifdef CONFIG_X86_64
	STORE_REGS %_ASM_AX, SVM_vcpu_arch_regs, \
		   %r8, %r9, %r10, %r11, %r12, %r13, %r14, %r15
#endif

	/* @svm can stay in RDI from now on.  */
	mov %_ASM_AX, %_ASM_DI

	mov SVM_vmcb01_pa(%_ASM_DI), %_ASM_AX
5:	vmsave %_ASM_AX
6:

	/* Restores GSBASE among other things, allowing access to percpu data.  */
	pop %_ASM_AX
7:	vmload %_ASM_AX
8:

	/* IMPORTANT: Stuff the RSB immediately after VM-Exit, before RET! */
	FILL_RETURN_BUFFER %_ASM_AX, RSB_CLEAR_LOOPS, X86_FEATURE_RSB_VMEXIT

	/*
	 * Clobbers RAX, RCX, RDX (and ESI, EDI on 32-bit), consumes RDI (@svm)
	 * and RSP (pointer to @spec_ctrl_intercepted).
	 */
	RESTORE_HOST_SPEC_CTRL
901:

	/*
	 * Mitigate RETBleed for AMD/Hygon Zen uarch. RET should be
	 * untrained as soon as we exit the VM and are back to the
	 * kernel. This should be done before re-enabling interrupts
	 * because interrupt handlers won't sanitize 'ret' if the return is
	 * from the kernel.
	 */
	UNTRAIN_RET_VM

	/*
	 * Clear all general purpose registers except RSP and RAX to prevent
	 * speculative use of the guest's values, even those that are reloaded
	 * via the stack.  In theory, an L1 cache miss when restoring registers
	 * could lead to speculative execution with the guest's values.
	 * Zeroing XORs are dirt cheap, i.e. the extra paranoia is essentially
	 * free.  RSP and RAX are exempt as they are restored by hardware
	 * during VM-Exit.
	 */
	CLEAR_REGS %ecx, %edx, %ebx, %ebp, %esi, %edi
#ifdef CONFIG_X86_64
	CLEAR_REGS %r8d, %r9d, %r10d, %r11d, %r12d, %r13d, %r14d, %r15d
#endif

	/* "Pop" @enter_flags.  */
	pop %_ASM_BX

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

800:
	RESTORE_GUEST_SPEC_CTRL_BODY SVM_spec_ctrl(%_ASM_DI), 801b
	jmp 801b
900:
	RESTORE_HOST_SPEC_CTRL_BODY SVM_spec_ctrl(%_ASM_DI), (%_ASM_SP), 901b
	jmp 901b

10:	cmpb $0, _ASM_RIP(virt_rebooting)
	jne 2b
	ud2
30:	cmpb $0, _ASM_RIP(virt_rebooting)
	jne 4b
	ud2
50:	cmpb $0, _ASM_RIP(virt_rebooting)
	jne 6b
	ud2
70:	cmpb $0, _ASM_RIP(virt_rebooting)
	jne 8b
	ud2

	_ASM_EXTABLE(1b, 10b)
	_ASM_EXTABLE(3b, 30b)
	_ASM_EXTABLE(5b, 50b)
	_ASM_EXTABLE(7b, 70b)

SYM_FUNC_END(__svm_vcpu_run)

#ifdef CONFIG_KVM_AMD_SEV

#define SEV_ES_GPRS_BASE 0x300

/**
 * __svm_sev_es_vcpu_run - Run a SEV-ES vCPU via a transition to SVM guest mode
 * @svm:	 struct vcpu_svm *
 * @enter_flags: u32
 */
SYM_FUNC_START(__svm_sev_es_vcpu_run)
	FRAME_BEGIN

	/*
	 * Save non-volatile (callee-saved) registers to the host save area.
	 * Except for RAX and RSP, all GPRs are restored on #VMEXIT, but not
	 * saved on VMRUN.
	 */
	STORE_REGS %rdx, SEV_ES_GPRS_BASE, %rbp, %r15, %r14, %r13, %r12, %rbx

	/*
	 * Save volatile registers that hold arguments that are needed after
	 * #VMEXIT (RDI=@svm and RSI=@enter_flags).
	 */
	STORE_REGS %rdx, SEV_ES_GPRS_BASE, %rdi, %rsi

	/* Clobbers RAX, RCX, and RDX (@hostsa), consumes RDI (@svm). */
	RESTORE_GUEST_SPEC_CTRL
801:

	/* Get svm->current_vmcb->pa into RAX. */
	mov SVM_current_vmcb(%rdi), %rax
	mov KVM_VMCB_pa(%rax), %rax

	/* Clobbers EFLAGS.ZF */
	SVM_CLEAR_CPU_BUFFERS

	/* Enter guest mode */
1:	vmrun %rax
2:
	/* IMPORTANT: Stuff the RSB immediately after VM-Exit, before RET! */
	FILL_RETURN_BUFFER %rax, RSB_CLEAR_LOOPS, X86_FEATURE_RSB_VMEXIT

	/* Clobbers RAX, RCX, RDX, consumes RDI (@svm) and RSI (@enter_flags). */
	RESTORE_HOST_SPEC_CTRL
901:

	/*
	 * Mitigate RETBleed for AMD/Hygon Zen uarch. RET should be
	 * untrained as soon as we exit the VM and are back to the
	 * kernel. This should be done before re-enabling interrupts
	 * because interrupt handlers won't sanitize RET if the return is
	 * from the kernel.
	 */
	UNTRAIN_RET_VM

	FRAME_END
	RET

800:
	RESTORE_GUEST_SPEC_CTRL_BODY SVM_spec_ctrl(%_ASM_DI), 801b
	jmp 801b
900:
	RESTORE_HOST_SPEC_CTRL_BODY SVM_spec_ctrl(%_ASM_DI), %esi, 901b
	jmp 901b

3:	cmpb $0, virt_rebooting(%rip)
	jne 2b
	ud2

	_ASM_EXTABLE(1b, 3b)

SYM_FUNC_END(__svm_sev_es_vcpu_run)
#endif /* CONFIG_KVM_AMD_SEV */
