/* SPDX-License-Identifier: GPL-2.0 */
#include <linux/linkage.h>
#include <asm/asm.h>
#include <asm/asm-offsets.h>
#include <asm/bitsperlong.h>
#include <asm/kvm_vcpu_regs.h>
#include <asm/nospec-branch.h>
#include "kvm-asm-offsets.h"

#define WORD_SIZE (BITS_PER_LONG / 8)

/* Intentionally omit RAX as it's context switched by hardware */
#define VCPU_RCX	(SVM_vcpu_arch_regs + __VCPU_REGS_RCX * WORD_SIZE)
#define VCPU_RDX	(SVM_vcpu_arch_regs + __VCPU_REGS_RDX * WORD_SIZE)
#define VCPU_RBX	(SVM_vcpu_arch_regs + __VCPU_REGS_RBX * WORD_SIZE)
/* Intentionally omit RSP as it's context switched by hardware */
#define VCPU_RBP	(SVM_vcpu_arch_regs + __VCPU_REGS_RBP * WORD_SIZE)
#define VCPU_RSI	(SVM_vcpu_arch_regs + __VCPU_REGS_RSI * WORD_SIZE)
#define VCPU_RDI	(SVM_vcpu_arch_regs + __VCPU_REGS_RDI * WORD_SIZE)

#ifdef CONFIG_X86_64
#define VCPU_R8		(SVM_vcpu_arch_regs + __VCPU_REGS_R8  * WORD_SIZE)
#define VCPU_R9		(SVM_vcpu_arch_regs + __VCPU_REGS_R9  * WORD_SIZE)
#define VCPU_R10	(SVM_vcpu_arch_regs + __VCPU_REGS_R10 * WORD_SIZE)
#define VCPU_R11	(SVM_vcpu_arch_regs + __VCPU_REGS_R11 * WORD_SIZE)
#define VCPU_R12	(SVM_vcpu_arch_regs + __VCPU_REGS_R12 * WORD_SIZE)
#define VCPU_R13	(SVM_vcpu_arch_regs + __VCPU_REGS_R13 * WORD_SIZE)
#define VCPU_R14	(SVM_vcpu_arch_regs + __VCPU_REGS_R14 * WORD_SIZE)
#define VCPU_R15	(SVM_vcpu_arch_regs + __VCPU_REGS_R15 * WORD_SIZE)
#endif

#define SVM_vmcb01_pa	(SVM_vmcb01 + KVM_VMCB_pa)

.section .noinstr.text, "ax"

.macro RESTORE_GUEST_SPEC_CTRL
	/* No need to do anything if SPEC_CTRL is unset or V_SPEC_CTRL is set */
	ALTERNATIVE_2 "", \
		"jmp 800f", X86_FEATURE_MSR_SPEC_CTRL, \
		"", X86_FEATURE_V_SPEC_CTRL
801:
.endm
.macro RESTORE_GUEST_SPEC_CTRL_BODY
800:
	/*
	 * SPEC_CTRL handling: if the guest's SPEC_CTRL value differs from the
	 * host's, write the MSR.  This is kept out-of-line so that the common
	 * case does not have to jump.
	 *
	 * IMPORTANT: To avoid RSB underflow attacks and any other nastiness,
	 * there must not be any returns or indirect branches between this code
	 * and vmentry.
	 */
	movl SVM_spec_ctrl(%_ASM_DI), %eax
	cmp PER_CPU_VAR(x86_spec_ctrl_current), %eax
	je 801b
	mov $MSR_IA32_SPEC_CTRL, %ecx
	xor %edx, %edx
	wrmsr
	jmp 801b
.endm

.macro RESTORE_HOST_SPEC_CTRL
	/* No need to do anything if SPEC_CTRL is unset or V_SPEC_CTRL is set */
	ALTERNATIVE_2 "", \
		"jmp 900f", X86_FEATURE_MSR_SPEC_CTRL, \
		"", X86_FEATURE_V_SPEC_CTRL
901:
.endm
.macro RESTORE_HOST_SPEC_CTRL_BODY
900:
	/* Same for after vmexit.  */
	mov $MSR_IA32_SPEC_CTRL, %ecx

	/*
	 * Load the value that the guest had written into MSR_IA32_SPEC_CTRL,
	 * if it was not intercepted during guest execution.
	 */
	cmpb $0, (%_ASM_SP)
	jnz 998f
	rdmsr
	movl %eax, SVM_spec_ctrl(%_ASM_DI)
998:

	/* Now restore the host value of the MSR if different from the guest's.  */
	movl PER_CPU_VAR(x86_spec_ctrl_current), %eax
	cmp SVM_spec_ctrl(%_ASM_DI), %eax
	je 901b
	xor %edx, %edx
	wrmsr
	jmp 901b
.endm


/**
 * __svm_vcpu_run - Run a vCPU via a transition to SVM guest mode
 * @svm:	struct vcpu_svm *
 * @spec_ctrl_intercepted: bool
 */
SYM_FUNC_START(__svm_vcpu_run)
	push %_ASM_BP
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

	/* Clobbers RAX, RCX, RDX.  */
	RESTORE_GUEST_SPEC_CTRL

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

	/* Load guest registers. */
	mov VCPU_RCX(%_ASM_DI), %_ASM_CX
	mov VCPU_RDX(%_ASM_DI), %_ASM_DX
	mov VCPU_RBX(%_ASM_DI), %_ASM_BX
	mov VCPU_RBP(%_ASM_DI), %_ASM_BP
	mov VCPU_RSI(%_ASM_DI), %_ASM_SI
#ifdef CONFIG_X86_64
	mov VCPU_R8 (%_ASM_DI),  %r8
	mov VCPU_R9 (%_ASM_DI),  %r9
	mov VCPU_R10(%_ASM_DI), %r10
	mov VCPU_R11(%_ASM_DI), %r11
	mov VCPU_R12(%_ASM_DI), %r12
	mov VCPU_R13(%_ASM_DI), %r13
	mov VCPU_R14(%_ASM_DI), %r14
	mov VCPU_R15(%_ASM_DI), %r15
#endif
	mov VCPU_RDI(%_ASM_DI), %_ASM_DI

	/* Enter guest mode */
	sti

3:	vmrun %_ASM_AX
4:
	cli

	/* Pop @svm to RAX while it's the only available register. */
	pop %_ASM_AX

	/* Save all guest registers.  */
	mov %_ASM_CX,   VCPU_RCX(%_ASM_AX)
	mov %_ASM_DX,   VCPU_RDX(%_ASM_AX)
	mov %_ASM_BX,   VCPU_RBX(%_ASM_AX)
	mov %_ASM_BP,   VCPU_RBP(%_ASM_AX)
	mov %_ASM_SI,   VCPU_RSI(%_ASM_AX)
	mov %_ASM_DI,   VCPU_RDI(%_ASM_AX)
#ifdef CONFIG_X86_64
	mov %r8,  VCPU_R8 (%_ASM_AX)
	mov %r9,  VCPU_R9 (%_ASM_AX)
	mov %r10, VCPU_R10(%_ASM_AX)
	mov %r11, VCPU_R11(%_ASM_AX)
	mov %r12, VCPU_R12(%_ASM_AX)
	mov %r13, VCPU_R13(%_ASM_AX)
	mov %r14, VCPU_R14(%_ASM_AX)
	mov %r15, VCPU_R15(%_ASM_AX)
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

#ifdef CONFIG_RETPOLINE
	/* IMPORTANT: Stuff the RSB immediately after VM-Exit, before RET! */
	FILL_RETURN_BUFFER %_ASM_AX, RSB_CLEAR_LOOPS, X86_FEATURE_RETPOLINE
#endif

	/* Clobbers RAX, RCX, RDX.  */
	RESTORE_HOST_SPEC_CTRL

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
	xor %ecx, %ecx
	xor %edx, %edx
	xor %ebx, %ebx
	xor %ebp, %ebp
	xor %esi, %esi
	xor %edi, %edi
#ifdef CONFIG_X86_64
	xor %r8d,  %r8d
	xor %r9d,  %r9d
	xor %r10d, %r10d
	xor %r11d, %r11d
	xor %r12d, %r12d
	xor %r13d, %r13d
	xor %r14d, %r14d
	xor %r15d, %r15d
#endif

	/* "Pop" @spec_ctrl_intercepted.  */
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

	RESTORE_GUEST_SPEC_CTRL_BODY
	RESTORE_HOST_SPEC_CTRL_BODY

10:	cmpb $0, _ASM_RIP(kvm_rebooting)
	jne 2b
	ud2
30:	cmpb $0, _ASM_RIP(kvm_rebooting)
	jne 4b
	ud2
50:	cmpb $0, _ASM_RIP(kvm_rebooting)
	jne 6b
	ud2
70:	cmpb $0, _ASM_RIP(kvm_rebooting)
	jne 8b
	ud2

	_ASM_EXTABLE(1b, 10b)
	_ASM_EXTABLE(3b, 30b)
	_ASM_EXTABLE(5b, 50b)
	_ASM_EXTABLE(7b, 70b)

SYM_FUNC_END(__svm_vcpu_run)

/**
 * __svm_sev_es_vcpu_run - Run a SEV-ES vCPU via a transition to SVM guest mode
 * @svm:	struct vcpu_svm *
 * @spec_ctrl_intercepted: bool
 */
SYM_FUNC_START(__svm_sev_es_vcpu_run)
	push %_ASM_BP
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

	/* Save @svm. */
	push %_ASM_ARG1

.ifnc _ASM_ARG1, _ASM_DI
	/*
	 * Stash @svm in RDI early. On 32-bit, arguments are in RAX, RCX
	 * and RDX which are clobbered by RESTORE_GUEST_SPEC_CTRL.
	 */
	mov %_ASM_ARG1, %_ASM_DI
.endif

	/* Clobbers RAX, RCX, RDX.  */
	RESTORE_GUEST_SPEC_CTRL

	/* Get svm->current_vmcb->pa into RAX. */
	mov SVM_current_vmcb(%_ASM_DI), %_ASM_AX
	mov KVM_VMCB_pa(%_ASM_AX), %_ASM_AX

	/* Enter guest mode */
	sti

1:	vmrun %_ASM_AX

2:	cli

	/* Pop @svm to RDI, guest registers have been saved already. */
	pop %_ASM_DI

#ifdef CONFIG_RETPOLINE
	/* IMPORTANT: Stuff the RSB immediately after VM-Exit, before RET! */
	FILL_RETURN_BUFFER %_ASM_AX, RSB_CLEAR_LOOPS, X86_FEATURE_RETPOLINE
#endif

	/* Clobbers RAX, RCX, RDX.  */
	RESTORE_HOST_SPEC_CTRL

	/*
	 * Mitigate RETBleed for AMD/Hygon Zen uarch. RET should be
	 * untrained as soon as we exit the VM and are back to the
	 * kernel. This should be done before re-enabling interrupts
	 * because interrupt handlers won't sanitize RET if the return is
	 * from the kernel.
	 */
	UNTRAIN_RET_VM

	/* "Pop" @spec_ctrl_intercepted.  */
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

	RESTORE_GUEST_SPEC_CTRL_BODY
	RESTORE_HOST_SPEC_CTRL_BODY

3:	cmpb $0, _ASM_RIP(kvm_rebooting)
	jne 2b
	ud2

	_ASM_EXTABLE(1b, 3b)

SYM_FUNC_END(__svm_sev_es_vcpu_run)
