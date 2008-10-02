/*
	Asm versions of Xen pv-ops, suitable for either direct use or inlining.
	The inline versions are the same as the direct-use versions, with the
	pre- and post-amble chopped off.

	This code is encoded for size rather than absolute efficiency,
	with a view to being able to inline as much as possible.

	We only bother with direct forms (ie, vcpu in pda) of the operations
	here; the indirect forms are better handled in C, since they're
	generally too large to inline anyway.
 */

#include <linux/linkage.h>

#include <asm/asm-offsets.h>
#include <asm/processor-flags.h>
#include <asm/errno.h>
#include <asm/segment.h>

#include <xen/interface/xen.h>

#define RELOC(x, v)	.globl x##_reloc; x##_reloc=v
#define ENDPATCH(x)	.globl x##_end; x##_end=.

/* Pseudo-flag used for virtual NMI, which we don't implement yet */
#define XEN_EFLAGS_NMI	0x80000000

#if 0
#include <asm/percpu.h>

/*
	Enable events.  This clears the event mask and tests the pending
	event status with one and operation.  If there are pending
	events, then enter the hypervisor to get them handled.
 */
ENTRY(xen_irq_enable_direct)
	/* Unmask events */
	movb $0, PER_CPU_VAR(xen_vcpu_info, XEN_vcpu_info_mask)

	/* Preempt here doesn't matter because that will deal with
	   any pending interrupts.  The pending check may end up being
	   run on the wrong CPU, but that doesn't hurt. */

	/* Test for pending */
	testb $0xff, PER_CPU_VAR(xen_vcpu_info, XEN_vcpu_info_pending)
	jz 1f

2:	call check_events
1:
ENDPATCH(xen_irq_enable_direct)
	ret
	ENDPROC(xen_irq_enable_direct)
	RELOC(xen_irq_enable_direct, 2b+1)

/*
	Disabling events is simply a matter of making the event mask
	non-zero.
 */
ENTRY(xen_irq_disable_direct)
	movb $1, PER_CPU_VAR(xen_vcpu_info, XEN_vcpu_info_mask)
ENDPATCH(xen_irq_disable_direct)
	ret
	ENDPROC(xen_irq_disable_direct)
	RELOC(xen_irq_disable_direct, 0)

/*
	(xen_)save_fl is used to get the current interrupt enable status.
	Callers expect the status to be in X86_EFLAGS_IF, and other bits
	may be set in the return value.  We take advantage of this by
	making sure that X86_EFLAGS_IF has the right value (and other bits
	in that byte are 0), but other bits in the return value are
	undefined.  We need to toggle the state of the bit, because
	Xen and x86 use opposite senses (mask vs enable).
 */
ENTRY(xen_save_fl_direct)
	testb $0xff, PER_CPU_VAR(xen_vcpu_info, XEN_vcpu_info_mask)
	setz %ah
	addb %ah,%ah
ENDPATCH(xen_save_fl_direct)
	ret
	ENDPROC(xen_save_fl_direct)
	RELOC(xen_save_fl_direct, 0)

/*
	In principle the caller should be passing us a value return
	from xen_save_fl_direct, but for robustness sake we test only
	the X86_EFLAGS_IF flag rather than the whole byte. After
	setting the interrupt mask state, it checks for unmasked
	pending events and enters the hypervisor to get them delivered
	if so.
 */
ENTRY(xen_restore_fl_direct)
	testb $X86_EFLAGS_IF>>8, %ah
	setz PER_CPU_VAR(xen_vcpu_info, XEN_vcpu_info_mask)
	/* Preempt here doesn't matter because that will deal with
	   any pending interrupts.  The pending check may end up being
	   run on the wrong CPU, but that doesn't hurt. */

	/* check for unmasked and pending */
	cmpw $0x0001, PER_CPU_VAR(xen_vcpu_info, XEN_vcpu_info_pending)
	jz 1f
2:	call check_events
1:
ENDPATCH(xen_restore_fl_direct)
	ret
	ENDPROC(xen_restore_fl_direct)
	RELOC(xen_restore_fl_direct, 2b+1)


/*
	Force an event check by making a hypercall,
	but preserve regs before making the call.
 */
check_events:
	push %rax
	push %rcx
	push %rdx
	push %rsi
	push %rdi
	push %r8
	push %r9
	push %r10
	push %r11
	call force_evtchn_callback
	pop %r11
	pop %r10
	pop %r9
	pop %r8
	pop %rdi
	pop %rsi
	pop %rdx
	pop %rcx
	pop %rax
	ret
#endif

ENTRY(xen_adjust_exception_frame)
	mov 8+0(%rsp),%rcx
	mov 8+8(%rsp),%r11
	ret $16

hypercall_iret = hypercall_page + __HYPERVISOR_iret * 32
/*
	Xen64 iret frame:

	ss
	rsp
	rflags
	cs
	rip		<-- standard iret frame

	flags

	rcx		}
	r11		}<-- pushed by hypercall page
rsp ->	rax		}
 */
ENTRY(xen_iret)
	pushq $0
1:	jmp hypercall_iret
ENDPATCH(xen_iret)
RELOC(xen_iret, 1b+1)

/*
	sysexit is not used for 64-bit processes, so it's
	only ever used to return to 32-bit compat userspace.
 */
ENTRY(xen_sysexit)
	pushq $__USER32_DS
	pushq %rcx
	pushq $X86_EFLAGS_IF
	pushq $__USER32_CS
	pushq %rdx

	pushq $0
1:	jmp hypercall_iret
ENDPATCH(xen_sysexit)
RELOC(xen_sysexit, 1b+1)

ENTRY(xen_sysret64)
	/* We're already on the usermode stack at this point, but still
	   with the kernel gs, so we can easily switch back */
	movq %rsp, %gs:pda_oldrsp
	movq %gs:pda_kernelstack,%rsp

	pushq $__USER_DS
	pushq %gs:pda_oldrsp
	pushq %r11
	pushq $__USER_CS
	pushq %rcx

	pushq $VGCF_in_syscall
1:	jmp hypercall_iret
ENDPATCH(xen_sysret64)
RELOC(xen_sysret64, 1b+1)

ENTRY(xen_sysret32)
	/* We're already on the usermode stack at this point, but still
	   with the kernel gs, so we can easily switch back */
	movq %rsp, %gs:pda_oldrsp
	movq %gs:pda_kernelstack, %rsp

	pushq $__USER32_DS
	pushq %gs:pda_oldrsp
	pushq %r11
	pushq $__USER32_CS
	pushq %rcx

	pushq $VGCF_in_syscall
1:	jmp hypercall_iret
ENDPATCH(xen_sysret32)
RELOC(xen_sysret32, 1b+1)

/*
	Xen handles syscall callbacks much like ordinary exceptions,
	which means we have:
	 - kernel gs
	 - kernel rsp
	 - an iret-like stack frame on the stack (including rcx and r11):
		ss
		rsp
		rflags
		cs
		rip
		r11
	rsp->	rcx

	In all the entrypoints, we undo all that to make it look
	like a CPU-generated syscall/sysenter and jump to the normal
	entrypoint.
 */

.macro undo_xen_syscall
	mov 0*8(%rsp),%rcx
	mov 1*8(%rsp),%r11
	mov 5*8(%rsp),%rsp
.endm

/* Normal 64-bit system call target */
ENTRY(xen_syscall_target)
	undo_xen_syscall
	jmp system_call_after_swapgs
ENDPROC(xen_syscall_target)

#ifdef CONFIG_IA32_EMULATION

/* 32-bit compat syscall target */
ENTRY(xen_syscall32_target)
	undo_xen_syscall
	jmp ia32_cstar_target
ENDPROC(xen_syscall32_target)

/* 32-bit compat sysenter target */
ENTRY(xen_sysenter_target)
	undo_xen_syscall
	jmp ia32_sysenter_target
ENDPROC(xen_sysenter_target)

#else /* !CONFIG_IA32_EMULATION */

ENTRY(xen_syscall32_target)
ENTRY(xen_sysenter_target)
	lea 16(%rsp), %rsp	/* strip %rcx,%r11 */
	mov $-ENOSYS, %rax
	pushq $VGCF_in_syscall
	jmp hypercall_iret
ENDPROC(xen_syscall32_target)
ENDPROC(xen_sysenter_target)

#endif	/* CONFIG_IA32_EMULATION */
