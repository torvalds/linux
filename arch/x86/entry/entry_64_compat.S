/*
 * Compatibility mode system call entry point for x86-64.
 *
 * Copyright 2000-2002 Andi Kleen, SuSE Labs.
 */
#include "calling.h"
#include <asm/asm-offsets.h>
#include <asm/current.h>
#include <asm/errno.h>
#include <asm/ia32_unistd.h>
#include <asm/thread_info.h>
#include <asm/segment.h>
#include <asm/irqflags.h>
#include <asm/asm.h>
#include <asm/smap.h>
#include <linux/linkage.h>
#include <linux/err.h>

/* Avoid __ASSEMBLER__'ifying <linux/audit.h> just for this.  */
#include <linux/elf-em.h>
#define AUDIT_ARCH_I386		(EM_386|__AUDIT_ARCH_LE)
#define __AUDIT_ARCH_LE		0x40000000

#ifndef CONFIG_AUDITSYSCALL
# define sysexit_audit		ia32_ret_from_sys_call
# define sysretl_audit		ia32_ret_from_sys_call
#endif

	.section .entry.text, "ax"

#ifdef CONFIG_PARAVIRT
ENTRY(native_usergs_sysret32)
	swapgs
	sysretl
ENDPROC(native_usergs_sysret32)
#endif

/*
 * 32-bit SYSENTER instruction entry.
 *
 * SYSENTER loads ss, rsp, cs, and rip from previously programmed MSRs.
 * IF and VM in rflags are cleared (IOW: interrupts are off).
 * SYSENTER does not save anything on the stack,
 * and does not save old rip (!!!) and rflags.
 *
 * Arguments:
 * eax  system call number
 * ebx  arg1
 * ecx  arg2
 * edx  arg3
 * esi  arg4
 * edi  arg5
 * ebp  user stack
 * 0(%ebp) arg6
 *
 * This is purely a fast path. For anything complicated we use the int 0x80
 * path below. We set up a complete hardware stack frame to share code
 * with the int 0x80 path.
 */
ENTRY(entry_SYSENTER_compat)
	/*
	 * Interrupts are off on entry.
	 * We do not frame this tiny irq-off block with TRACE_IRQS_OFF/ON,
	 * it is too small to ever cause noticeable irq latency.
	 */
	SWAPGS_UNSAFE_STACK
	movq	PER_CPU_VAR(cpu_current_top_of_stack), %rsp
	ENABLE_INTERRUPTS(CLBR_NONE)

	/* Zero-extending 32-bit regs, do not remove */
	movl	%ebp, %ebp
	movl	%eax, %eax

	movl	ASM_THREAD_INFO(TI_sysenter_return, %rsp, 0), %r10d

	/* Construct struct pt_regs on stack */
	pushq	$__USER32_DS		/* pt_regs->ss */
	pushq	%rbp			/* pt_regs->sp */
	pushfq				/* pt_regs->flags */
	pushq	$__USER32_CS		/* pt_regs->cs */
	pushq	%r10			/* pt_regs->ip = thread_info->sysenter_return */
	pushq	%rax			/* pt_regs->orig_ax */
	pushq	%rdi			/* pt_regs->di */
	pushq	%rsi			/* pt_regs->si */
	pushq	%rdx			/* pt_regs->dx */
	pushq	%rcx			/* pt_regs->cx */
	pushq	$-ENOSYS		/* pt_regs->ax */
	cld
	sub	$(10*8), %rsp /* pt_regs->r8-11, bp, bx, r12-15 not saved */

	/*
	 * no need to do an access_ok check here because rbp has been
	 * 32-bit zero extended
	 */
	ASM_STAC
1:	movl	(%rbp), %ebp
	_ASM_EXTABLE(1b, ia32_badarg)
	ASM_CLAC

	/*
	 * Sysenter doesn't filter flags, so we need to clear NT
	 * ourselves.  To save a few cycles, we can check whether
	 * NT was set instead of doing an unconditional popfq.
	 */
	testl	$X86_EFLAGS_NT, EFLAGS(%rsp)
	jnz	sysenter_fix_flags
sysenter_flags_fixed:

	orl	$TS_COMPAT, ASM_THREAD_INFO(TI_status, %rsp, SIZEOF_PTREGS)
	testl	$_TIF_WORK_SYSCALL_ENTRY, ASM_THREAD_INFO(TI_flags, %rsp, SIZEOF_PTREGS)
	jnz	sysenter_tracesys

sysenter_do_call:
	/* 32-bit syscall -> 64-bit C ABI argument conversion */
	movl	%edi, %r8d		/* arg5 */
	movl	%ebp, %r9d		/* arg6 */
	xchg	%ecx, %esi		/* rsi:arg2, rcx:arg4 */
	movl	%ebx, %edi		/* arg1 */
	movl	%edx, %edx		/* arg3 (zero extension) */
sysenter_dispatch:
	cmpq	$(IA32_NR_syscalls-1), %rax
	ja	1f
	call	*ia32_sys_call_table(, %rax, 8)
	movq	%rax, RAX(%rsp)
1:
	DISABLE_INTERRUPTS(CLBR_NONE)
	TRACE_IRQS_OFF
	testl	$_TIF_ALLWORK_MASK, ASM_THREAD_INFO(TI_flags, %rsp, SIZEOF_PTREGS)
	jnz	sysexit_audit
sysexit_from_sys_call:
	/*
	 * NB: SYSEXIT is not obviously safe for 64-bit kernels -- an
	 * NMI between STI and SYSEXIT has poorly specified behavior,
	 * and and NMI followed by an IRQ with usergs is fatal.  So
	 * we just pretend we're using SYSEXIT but we really use
	 * SYSRETL instead.
	 *
	 * This code path is still called 'sysexit' because it pairs
	 * with 'sysenter' and it uses the SYSENTER calling convention.
	 */
	andl	$~TS_COMPAT, ASM_THREAD_INFO(TI_status, %rsp, SIZEOF_PTREGS)
	movl	RIP(%rsp), %ecx		/* User %eip */
	RESTORE_RSI_RDI
	xorl	%edx, %edx		/* Do not leak kernel information */
	xorq	%r8, %r8
	xorq	%r9, %r9
	xorq	%r10, %r10
	movl	EFLAGS(%rsp), %r11d	/* User eflags */
	TRACE_IRQS_ON

	/*
	 * SYSRETL works even on Intel CPUs.  Use it in preference to SYSEXIT,
	 * since it avoids a dicey window with interrupts enabled.
	 */
	movl	RSP(%rsp), %esp

	/*
	 * USERGS_SYSRET32 does:
	 *  gsbase = user's gs base
	 *  eip = ecx
	 *  rflags = r11
	 *  cs = __USER32_CS
	 *  ss = __USER_DS
	 *
	 * The prologue set RIP(%rsp) to VDSO32_SYSENTER_RETURN, which does:
	 *
	 *  pop %ebp
	 *  pop %edx
	 *  pop %ecx
	 *
	 * Therefore, we invoke SYSRETL with EDX and R8-R10 zeroed to
	 * avoid info leaks.  R11 ends up with VDSO32_SYSENTER_RETURN's
	 * address (already known to user code), and R12-R15 are
	 * callee-saved and therefore don't contain any interesting
	 * kernel data.
	 */
	USERGS_SYSRET32

#ifdef CONFIG_AUDITSYSCALL
	.macro auditsys_entry_common
	/*
	 * At this point, registers hold syscall args in the 32-bit syscall ABI:
	 * EAX is syscall number, the 6 args are in EBX,ECX,EDX,ESI,EDI,EBP.
	 *
	 * We want to pass them to __audit_syscall_entry(), which is a 64-bit
	 * C function with 5 parameters, so shuffle them to match what
	 * the function expects: RDI,RSI,RDX,RCX,R8.
	 */
	movl	%esi, %r8d		/* arg5 (R8 ) <= 4th syscall arg (ESI) */
	xchg	%ecx, %edx		/* arg4 (RCX) <= 3rd syscall arg (EDX) */
					/* arg3 (RDX) <= 2nd syscall arg (ECX) */
	movl	%ebx, %esi		/* arg2 (RSI) <= 1st syscall arg (EBX) */
	movl	%eax, %edi		/* arg1 (RDI) <= syscall number  (EAX) */
	call	__audit_syscall_entry

	/*
	 * We are going to jump back to the syscall dispatch code.
	 * Prepare syscall args as required by the 64-bit C ABI.
	 * Registers clobbered by __audit_syscall_entry() are
	 * loaded from pt_regs on stack:
	 */
	movl	ORIG_RAX(%rsp), %eax	/* syscall number */
	movl	%ebx, %edi		/* arg1 */
	movl	RCX(%rsp), %esi		/* arg2 */
	movl	RDX(%rsp), %edx		/* arg3 */
	movl	RSI(%rsp), %ecx		/* arg4 */
	movl	RDI(%rsp), %r8d		/* arg5 */
	.endm

	.macro auditsys_exit exit
	testl	$(_TIF_ALLWORK_MASK & ~_TIF_SYSCALL_AUDIT), ASM_THREAD_INFO(TI_flags, %rsp, SIZEOF_PTREGS)
	jnz	ia32_ret_from_sys_call
	TRACE_IRQS_ON
	ENABLE_INTERRUPTS(CLBR_NONE)
	movl	%eax, %esi		/* second arg, syscall return value */
	cmpl	$-MAX_ERRNO, %eax	/* is it an error ? */
	jbe	1f
	movslq	%eax, %rsi		/* if error sign extend to 64 bits */
1:	setbe	%al			/* 1 if error, 0 if not */
	movzbl	%al, %edi		/* zero-extend that into %edi */
	call	__audit_syscall_exit
	movq	RAX(%rsp), %rax		/* reload syscall return value */
	movl	$(_TIF_ALLWORK_MASK & ~_TIF_SYSCALL_AUDIT), %edi
	DISABLE_INTERRUPTS(CLBR_NONE)
	TRACE_IRQS_OFF
	testl	%edi, ASM_THREAD_INFO(TI_flags, %rsp, SIZEOF_PTREGS)
	jz	\exit
	xorl	%eax, %eax		/* Do not leak kernel information */
	movq	%rax, R11(%rsp)
	movq	%rax, R10(%rsp)
	movq	%rax, R9(%rsp)
	movq	%rax, R8(%rsp)
	jmp	int_with_check
	.endm

sysenter_auditsys:
	auditsys_entry_common
	movl	%ebp, %r9d		/* reload 6th syscall arg */
	jmp	sysenter_dispatch

sysexit_audit:
	auditsys_exit sysexit_from_sys_call
#endif

sysenter_fix_flags:
	pushq	$(X86_EFLAGS_IF|X86_EFLAGS_FIXED)
	popfq
	jmp	sysenter_flags_fixed

sysenter_tracesys:
#ifdef CONFIG_AUDITSYSCALL
	testl	$(_TIF_WORK_SYSCALL_ENTRY & ~_TIF_SYSCALL_AUDIT), ASM_THREAD_INFO(TI_flags, %rsp, SIZEOF_PTREGS)
	jz	sysenter_auditsys
#endif
	SAVE_EXTRA_REGS
	xorl	%eax, %eax		/* Do not leak kernel information */
	movq	%rax, R11(%rsp)
	movq	%rax, R10(%rsp)
	movq	%rax, R9(%rsp)
	movq	%rax, R8(%rsp)
	movq	%rsp, %rdi		/* &pt_regs -> arg1 */
	call	syscall_trace_enter

	/* Reload arg registers from stack. (see sysenter_tracesys) */
	movl	RCX(%rsp), %ecx
	movl	RDX(%rsp), %edx
	movl	RSI(%rsp), %esi
	movl	RDI(%rsp), %edi
	movl	%eax, %eax		/* zero extension */

	RESTORE_EXTRA_REGS
	jmp	sysenter_do_call
ENDPROC(entry_SYSENTER_compat)

/*
 * 32-bit SYSCALL instruction entry.
 *
 * 32-bit SYSCALL saves rip to rcx, clears rflags.RF, then saves rflags to r11,
 * then loads new ss, cs, and rip from previously programmed MSRs.
 * rflags gets masked by a value from another MSR (so CLD and CLAC
 * are not needed). SYSCALL does not save anything on the stack
 * and does not change rsp.
 *
 * Note: rflags saving+masking-with-MSR happens only in Long mode
 * (in legacy 32-bit mode, IF, RF and VM bits are cleared and that's it).
 * Don't get confused: rflags saving+masking depends on Long Mode Active bit
 * (EFER.LMA=1), NOT on bitness of userspace where SYSCALL executes
 * or target CS descriptor's L bit (SYSCALL does not read segment descriptors).
 *
 * Arguments:
 * eax  system call number
 * ecx  return address
 * ebx  arg1
 * ebp  arg2	(note: not saved in the stack frame, should not be touched)
 * edx  arg3
 * esi  arg4
 * edi  arg5
 * esp  user stack
 * 0(%esp) arg6
 *
 * This is purely a fast path. For anything complicated we use the int 0x80
 * path below. We set up a complete hardware stack frame to share code
 * with the int 0x80 path.
 */
ENTRY(entry_SYSCALL_compat)
	/*
	 * Interrupts are off on entry.
	 * We do not frame this tiny irq-off block with TRACE_IRQS_OFF/ON,
	 * it is too small to ever cause noticeable irq latency.
	 */
	SWAPGS_UNSAFE_STACK
	movl	%esp, %r8d
	movq	PER_CPU_VAR(cpu_current_top_of_stack), %rsp
	ENABLE_INTERRUPTS(CLBR_NONE)

	/* Zero-extending 32-bit regs, do not remove */
	movl	%eax, %eax

	/* Construct struct pt_regs on stack */
	pushq	$__USER32_DS		/* pt_regs->ss */
	pushq	%r8			/* pt_regs->sp */
	pushq	%r11			/* pt_regs->flags */
	pushq	$__USER32_CS		/* pt_regs->cs */
	pushq	%rcx			/* pt_regs->ip */
	pushq	%rax			/* pt_regs->orig_ax */
	pushq	%rdi			/* pt_regs->di */
	pushq	%rsi			/* pt_regs->si */
	pushq	%rdx			/* pt_regs->dx */
	pushq	%rbp			/* pt_regs->cx */
	movl	%ebp, %ecx
	pushq	$-ENOSYS		/* pt_regs->ax */
	sub	$(10*8), %rsp		/* pt_regs->r8-11, bp, bx, r12-15 not saved */

	/*
	 * No need to do an access_ok check here because r8 has been
	 * 32-bit zero extended:
	 */
	ASM_STAC
1:	movl	(%r8), %r9d
	_ASM_EXTABLE(1b, ia32_badarg)
	ASM_CLAC
	orl	$TS_COMPAT, ASM_THREAD_INFO(TI_status, %rsp, SIZEOF_PTREGS)
	testl	$_TIF_WORK_SYSCALL_ENTRY, ASM_THREAD_INFO(TI_flags, %rsp, SIZEOF_PTREGS)
	jnz	cstar_tracesys

cstar_do_call:
	/* 32-bit syscall -> 64-bit C ABI argument conversion */
	movl	%edi, %r8d		/* arg5 */
	/* r9 already loaded */		/* arg6 */
	xchg	%ecx, %esi		/* rsi:arg2, rcx:arg4 */
	movl	%ebx, %edi		/* arg1 */
	movl	%edx, %edx		/* arg3 (zero extension) */

cstar_dispatch:
	cmpq	$(IA32_NR_syscalls-1), %rax
	ja	1f

	call	*ia32_sys_call_table(, %rax, 8)
	movq	%rax, RAX(%rsp)
1:
	DISABLE_INTERRUPTS(CLBR_NONE)
	TRACE_IRQS_OFF
	testl	$_TIF_ALLWORK_MASK, ASM_THREAD_INFO(TI_flags, %rsp, SIZEOF_PTREGS)
	jnz	sysretl_audit

sysretl_from_sys_call:
	andl	$~TS_COMPAT, ASM_THREAD_INFO(TI_status, %rsp, SIZEOF_PTREGS)
	RESTORE_RSI_RDI_RDX
	movl	RIP(%rsp), %ecx
	movl	EFLAGS(%rsp), %r11d
	xorq	%r10, %r10
	xorq	%r9, %r9
	xorq	%r8, %r8
	TRACE_IRQS_ON
	movl	RSP(%rsp), %esp
	/*
	 * 64-bit->32-bit SYSRET restores eip from ecx,
	 * eflags from r11 (but RF and VM bits are forced to 0),
	 * cs and ss are loaded from MSRs.
	 * (Note: 32-bit->32-bit SYSRET is different: since r11
	 * does not exist, it merely sets eflags.IF=1).
	 *
	 * NB: On AMD CPUs with the X86_BUG_SYSRET_SS_ATTRS bug, the ss
	 * descriptor is not reinitialized.  This means that we must
	 * avoid SYSRET with SS == NULL, which could happen if we schedule,
	 * exit the kernel, and re-enter using an interrupt vector.  (All
	 * interrupt entries on x86_64 set SS to NULL.)  We prevent that
	 * from happening by reloading SS in __switch_to.
	 */
	USERGS_SYSRET32

#ifdef CONFIG_AUDITSYSCALL
cstar_auditsys:
	movl	%r9d, R9(%rsp)		/* register to be clobbered by call */
	auditsys_entry_common
	movl	R9(%rsp), %r9d		/* reload 6th syscall arg */
	jmp	cstar_dispatch

sysretl_audit:
	auditsys_exit sysretl_from_sys_call
#endif

cstar_tracesys:
#ifdef CONFIG_AUDITSYSCALL
	testl	$(_TIF_WORK_SYSCALL_ENTRY & ~_TIF_SYSCALL_AUDIT), ASM_THREAD_INFO(TI_flags, %rsp, SIZEOF_PTREGS)
	jz	cstar_auditsys
#endif
	xchgl	%r9d, %ebp
	SAVE_EXTRA_REGS
	xorl	%eax, %eax		/* Do not leak kernel information */
	movq	%rax, R11(%rsp)
	movq	%rax, R10(%rsp)
	movq	%r9, R9(%rsp)
	movq	%rax, R8(%rsp)
	movq	%rsp, %rdi		/* &pt_regs -> arg1 */
	call	syscall_trace_enter
	movl	R9(%rsp), %r9d

	/* Reload arg registers from stack. (see sysenter_tracesys) */
	movl	RCX(%rsp), %ecx
	movl	RDX(%rsp), %edx
	movl	RSI(%rsp), %esi
	movl	RDI(%rsp), %edi
	movl	%eax, %eax		/* zero extension */

	RESTORE_EXTRA_REGS
	xchgl	%ebp, %r9d
	jmp	cstar_do_call
END(entry_SYSCALL_compat)

ia32_badarg:
	ASM_CLAC
	movq	$-EFAULT, RAX(%rsp)
ia32_ret_from_sys_call:
	xorl	%eax, %eax		/* Do not leak kernel information */
	movq	%rax, R11(%rsp)
	movq	%rax, R10(%rsp)
	movq	%rax, R9(%rsp)
	movq	%rax, R8(%rsp)
	jmp	int_ret_from_sys_call

/*
 * Emulated IA32 system calls via int 0x80.
 *
 * Arguments:
 * eax  system call number
 * ebx  arg1
 * ecx  arg2
 * edx  arg3
 * esi  arg4
 * edi  arg5
 * ebp  arg6	(note: not saved in the stack frame, should not be touched)
 *
 * Notes:
 * Uses the same stack frame as the x86-64 version.
 * All registers except eax must be saved (but ptrace may violate that).
 * Arguments are zero extended. For system calls that want sign extension and
 * take long arguments a wrapper is needed. Most calls can just be called
 * directly.
 * Assumes it is only called from user space and entered with interrupts off.
 */

ENTRY(entry_INT80_compat)
	/*
	 * Interrupts are off on entry.
	 * We do not frame this tiny irq-off block with TRACE_IRQS_OFF/ON,
	 * it is too small to ever cause noticeable irq latency.
	 */
	PARAVIRT_ADJUST_EXCEPTION_FRAME
	SWAPGS
	ENABLE_INTERRUPTS(CLBR_NONE)

	/* Zero-extending 32-bit regs, do not remove */
	movl	%eax, %eax

	/* Construct struct pt_regs on stack (iret frame is already on stack) */
	pushq	%rax			/* pt_regs->orig_ax */
	pushq	%rdi			/* pt_regs->di */
	pushq	%rsi			/* pt_regs->si */
	pushq	%rdx			/* pt_regs->dx */
	pushq	%rcx			/* pt_regs->cx */
	pushq	$-ENOSYS		/* pt_regs->ax */
	pushq	$0			/* pt_regs->r8 */
	pushq	$0			/* pt_regs->r9 */
	pushq	$0			/* pt_regs->r10 */
	pushq	$0			/* pt_regs->r11 */
	cld
	sub	$(6*8), %rsp /* pt_regs->bp, bx, r12-15 not saved */

	orl	$TS_COMPAT, ASM_THREAD_INFO(TI_status, %rsp, SIZEOF_PTREGS)
	testl	$_TIF_WORK_SYSCALL_ENTRY, ASM_THREAD_INFO(TI_flags, %rsp, SIZEOF_PTREGS)
	jnz	ia32_tracesys

ia32_do_call:
	/* 32-bit syscall -> 64-bit C ABI argument conversion */
	movl	%edi, %r8d		/* arg5 */
	movl	%ebp, %r9d		/* arg6 */
	xchg	%ecx, %esi		/* rsi:arg2, rcx:arg4 */
	movl	%ebx, %edi		/* arg1 */
	movl	%edx, %edx		/* arg3 (zero extension) */
	cmpq	$(IA32_NR_syscalls-1), %rax
	ja	1f

	call	*ia32_sys_call_table(, %rax, 8)
	movq	%rax, RAX(%rsp)
1:
	jmp	int_ret_from_sys_call

ia32_tracesys:
	SAVE_EXTRA_REGS
	movq	%rsp, %rdi			/* &pt_regs -> arg1 */
	call	syscall_trace_enter
	/*
	 * Reload arg registers from stack in case ptrace changed them.
	 * Don't reload %eax because syscall_trace_enter() returned
	 * the %rax value we should see.  But do truncate it to 32 bits.
	 * If it's -1 to make us punt the syscall, then (u32)-1 is still
	 * an appropriately invalid value.
	 */
	movl	RCX(%rsp), %ecx
	movl	RDX(%rsp), %edx
	movl	RSI(%rsp), %esi
	movl	RDI(%rsp), %edi
	movl	%eax, %eax		/* zero extension */
	RESTORE_EXTRA_REGS
	jmp	ia32_do_call
END(entry_INT80_compat)

	.macro PTREGSCALL label, func
	ALIGN
GLOBAL(\label)
	leaq	\func(%rip), %rax
	jmp	ia32_ptregs_common
	.endm

	PTREGSCALL stub32_rt_sigreturn,	sys32_rt_sigreturn
	PTREGSCALL stub32_sigreturn,	sys32_sigreturn
	PTREGSCALL stub32_fork,		sys_fork
	PTREGSCALL stub32_vfork,	sys_vfork

	ALIGN
GLOBAL(stub32_clone)
	leaq	sys_clone(%rip), %rax
	/*
	 * The 32-bit clone ABI is: clone(..., int tls_val, int *child_tidptr).
	 * The 64-bit clone ABI is: clone(..., int *child_tidptr, int tls_val).
	 *
	 * The native 64-bit kernel's sys_clone() implements the latter,
	 * so we need to swap arguments here before calling it:
	 */
	xchg	%r8, %rcx
	jmp	ia32_ptregs_common

	ALIGN
ia32_ptregs_common:
	SAVE_EXTRA_REGS 8
	call	*%rax
	RESTORE_EXTRA_REGS 8
	ret
END(ia32_ptregs_common)
