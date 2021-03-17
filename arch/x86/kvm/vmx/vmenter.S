/* SPDX-License-Identifier: GPL-2.0 */
#include <linux/linkage.h>
#include <asm/asm.h>
#include <asm/bitsperlong.h>
#include <asm/kvm_vcpu_regs.h>
#include <asm/nospec-branch.h>
#include <asm/segment.h>

#define WORD_SIZE (BITS_PER_LONG / 8)

#define VCPU_RAX	__VCPU_REGS_RAX * WORD_SIZE
#define VCPU_RCX	__VCPU_REGS_RCX * WORD_SIZE
#define VCPU_RDX	__VCPU_REGS_RDX * WORD_SIZE
#define VCPU_RBX	__VCPU_REGS_RBX * WORD_SIZE
/* Intentionally omit RSP as it's context switched by hardware */
#define VCPU_RBP	__VCPU_REGS_RBP * WORD_SIZE
#define VCPU_RSI	__VCPU_REGS_RSI * WORD_SIZE
#define VCPU_RDI	__VCPU_REGS_RDI * WORD_SIZE

#ifdef CONFIG_X86_64
#define VCPU_R8		__VCPU_REGS_R8  * WORD_SIZE
#define VCPU_R9		__VCPU_REGS_R9  * WORD_SIZE
#define VCPU_R10	__VCPU_REGS_R10 * WORD_SIZE
#define VCPU_R11	__VCPU_REGS_R11 * WORD_SIZE
#define VCPU_R12	__VCPU_REGS_R12 * WORD_SIZE
#define VCPU_R13	__VCPU_REGS_R13 * WORD_SIZE
#define VCPU_R14	__VCPU_REGS_R14 * WORD_SIZE
#define VCPU_R15	__VCPU_REGS_R15 * WORD_SIZE
#endif

.section .noinstr.text, "ax"

/**
 * vmx_vmenter - VM-Enter the current loaded VMCS
 *
 * %RFLAGS.ZF:	!VMCS.LAUNCHED, i.e. controls VMLAUNCH vs. VMRESUME
 *
 * Returns:
 *	%RFLAGS.CF is set on VM-Fail Invalid
 *	%RFLAGS.ZF is set on VM-Fail Valid
 *	%RFLAGS.{CF,ZF} are cleared on VM-Success, i.e. VM-Exit
 *
 * Note that VMRESUME/VMLAUNCH fall-through and return directly if
 * they VM-Fail, whereas a successful VM-Enter + VM-Exit will jump
 * to vmx_vmexit.
 */
SYM_FUNC_START(vmx_vmenter)
	/* EFLAGS.ZF is set if VMCS.LAUNCHED == 0 */
	je 2f

1:	vmresume
	ret

2:	vmlaunch
	ret

3:	cmpb $0, kvm_rebooting
	je 4f
	ret
4:	ud2

	_ASM_EXTABLE(1b, 3b)
	_ASM_EXTABLE(2b, 3b)

SYM_FUNC_END(vmx_vmenter)

/**
 * vmx_vmexit - Handle a VMX VM-Exit
 *
 * Returns:
 *	%RFLAGS.{CF,ZF} are cleared on VM-Success, i.e. VM-Exit
 *
 * This is vmx_vmenter's partner in crime.  On a VM-Exit, control will jump
 * here after hardware loads the host's state, i.e. this is the destination
 * referred to by VMCS.HOST_RIP.
 */
SYM_FUNC_START(vmx_vmexit)
#ifdef CONFIG_RETPOLINE
	ALTERNATIVE "jmp .Lvmexit_skip_rsb", "", X86_FEATURE_RETPOLINE
	/* Preserve guest's RAX, it's used to stuff the RSB. */
	push %_ASM_AX

	/* IMPORTANT: Stuff the RSB immediately after VM-Exit, before RET! */
	FILL_RETURN_BUFFER %_ASM_AX, RSB_CLEAR_LOOPS, X86_FEATURE_RETPOLINE

	/* Clear RFLAGS.CF and RFLAGS.ZF to preserve VM-Exit, i.e. !VM-Fail. */
	or $1, %_ASM_AX

	pop %_ASM_AX
.Lvmexit_skip_rsb:
#endif
	ret
SYM_FUNC_END(vmx_vmexit)

/**
 * __vmx_vcpu_run - Run a vCPU via a transition to VMX guest mode
 * @vmx:	struct vcpu_vmx * (forwarded to vmx_update_host_rsp)
 * @regs:	unsigned long * (to guest registers)
 * @launched:	%true if the VMCS has been launched
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

	/*
	 * Save @regs, _ASM_ARG2 may be modified by vmx_update_host_rsp() and
	 * @regs is needed after VM-Exit to save the guest's register values.
	 */
	push %_ASM_ARG2

	/* Copy @launched to BL, _ASM_ARG3 is volatile. */
	mov %_ASM_ARG3B, %bl

	/* Adjust RSP to account for the CALL to vmx_vmenter(). */
	lea -WORD_SIZE(%_ASM_SP), %_ASM_ARG2
	call vmx_update_host_rsp

	/* Load @regs to RAX. */
	mov (%_ASM_SP), %_ASM_AX

	/* Check if vmlaunch or vmresume is needed */
	cmpb $0, %bl

	/* Load guest registers.  Don't clobber flags. */
	mov VCPU_RCX(%_ASM_AX), %_ASM_CX
	mov VCPU_RDX(%_ASM_AX), %_ASM_DX
	mov VCPU_RBX(%_ASM_AX), %_ASM_BX
	mov VCPU_RBP(%_ASM_AX), %_ASM_BP
	mov VCPU_RSI(%_ASM_AX), %_ASM_SI
	mov VCPU_RDI(%_ASM_AX), %_ASM_DI
#ifdef CONFIG_X86_64
	mov VCPU_R8 (%_ASM_AX),  %r8
	mov VCPU_R9 (%_ASM_AX),  %r9
	mov VCPU_R10(%_ASM_AX), %r10
	mov VCPU_R11(%_ASM_AX), %r11
	mov VCPU_R12(%_ASM_AX), %r12
	mov VCPU_R13(%_ASM_AX), %r13
	mov VCPU_R14(%_ASM_AX), %r14
	mov VCPU_R15(%_ASM_AX), %r15
#endif
	/* Load guest RAX.  This kills the @regs pointer! */
	mov VCPU_RAX(%_ASM_AX), %_ASM_AX

	/* Enter guest mode */
	call vmx_vmenter

	/* Jump on VM-Fail. */
	jbe 2f

	/* Temporarily save guest's RAX. */
	push %_ASM_AX

	/* Reload @regs to RAX. */
	mov WORD_SIZE(%_ASM_SP), %_ASM_AX

	/* Save all guest registers, including RAX from the stack */
	pop           VCPU_RAX(%_ASM_AX)
	mov %_ASM_CX, VCPU_RCX(%_ASM_AX)
	mov %_ASM_DX, VCPU_RDX(%_ASM_AX)
	mov %_ASM_BX, VCPU_RBX(%_ASM_AX)
	mov %_ASM_BP, VCPU_RBP(%_ASM_AX)
	mov %_ASM_SI, VCPU_RSI(%_ASM_AX)
	mov %_ASM_DI, VCPU_RDI(%_ASM_AX)
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

	/* Clear RAX to indicate VM-Exit (as opposed to VM-Fail). */
	xor %eax, %eax

	/*
	 * Clear all general purpose registers except RSP and RAX to prevent
	 * speculative use of the guest's values, even those that are reloaded
	 * via the stack.  In theory, an L1 cache miss when restoring registers
	 * could lead to speculative execution with the guest's values.
	 * Zeroing XORs are dirt cheap, i.e. the extra paranoia is essentially
	 * free.  RSP and RAX are exempt as RSP is restored by hardware during
	 * VM-Exit and RAX is explicitly loaded with 0 or 1 to return VM-Fail.
	 */
1:	xor %ecx, %ecx
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

	/* "POP" @regs. */
	add $WORD_SIZE, %_ASM_SP
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
	ret

	/* VM-Fail.  Out-of-line to avoid a taken Jcc after VM-Exit. */
2:	mov $1, %eax
	jmp 1b
SYM_FUNC_END(__vmx_vcpu_run)


.section .text, "ax"

/**
 * vmread_error_trampoline - Trampoline from inline asm to vmread_error()
 * @field:	VMCS field encoding that failed
 * @fault:	%true if the VMREAD faulted, %false if it failed

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
#ifdef CONFIG_X86_64
	/* Load @field and @fault to arg1 and arg2 respectively. */
	mov 3*WORD_SIZE(%rbp), %_ASM_ARG2
	mov 2*WORD_SIZE(%rbp), %_ASM_ARG1
#else
	/* Parameters are passed on the stack for 32-bit (see asmlinkage). */
	push 3*WORD_SIZE(%ebp)
	push 2*WORD_SIZE(%ebp)
#endif

	call vmread_error

#ifndef CONFIG_X86_64
	add $8, %esp
#endif

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

	ret
SYM_FUNC_END(vmread_error_trampoline)

SYM_FUNC_START(vmx_do_interrupt_nmi_irqoff)
	/*
	 * Unconditionally create a stack frame, getting the correct RSP on the
	 * stack (for x86-64) would take two instructions anyways, and RBP can
	 * be used to restore RSP to make objtool happy (see below).
	 */
	push %_ASM_BP
	mov %_ASM_SP, %_ASM_BP

#ifdef CONFIG_X86_64
	/*
	 * Align RSP to a 16-byte boundary (to emulate CPU behavior) before
	 * creating the synthetic interrupt stack frame for the IRQ/NMI.
	 */
	and  $-16, %rsp
	push $__KERNEL_DS
	push %rbp
#endif
	pushf
	push $__KERNEL_CS
	CALL_NOSPEC _ASM_ARG1

	/*
	 * "Restore" RSP from RBP, even though IRET has already unwound RSP to
	 * the correct value.  objtool doesn't know the callee will IRET and,
	 * without the explicit restore, thinks the stack is getting walloped.
	 * Using an unwind hint is problematic due to x86-64's dynamic alignment.
	 */
	mov %_ASM_BP, %_ASM_SP
	pop %_ASM_BP
	ret
SYM_FUNC_END(vmx_do_interrupt_nmi_irqoff)
