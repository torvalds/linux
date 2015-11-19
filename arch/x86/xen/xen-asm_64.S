/*
 * Asm versions of Xen pv-ops, suitable for either direct use or
 * inlining.  The inline versions are the same as the direct-use
 * versions, with the pre- and post-amble chopped off.
 *
 * This code is encoded for size rather than absolute efficiency, with
 * a view to being able to inline as much as possible.
 *
 * We only bother with direct forms (ie, vcpu in pda) of the
 * operations here; the indirect forms are better handled in C, since
 * they're generally too large to inline anyway.
 */

#include <asm/errno.h>
#include <asm/percpu.h>
#include <asm/processor-flags.h>
#include <asm/segment.h>
#include <asm/asm-offsets.h>
#include <asm/thread_info.h>

#include <xen/interface/xen.h>

#include "xen-asm.h"

ENTRY(xen_adjust_exception_frame)
	mov 8+0(%rsp), %rcx
	mov 8+8(%rsp), %r11
	ret $16

hypercall_iret = hypercall_page + __HYPERVISOR_iret * 32
/*
 * Xen64 iret frame:
 *
 *	ss
 *	rsp
 *	rflags
 *	cs
 *	rip		<-- standard iret frame
 *
 *	flags
 *
 *	rcx		}
 *	r11		}<-- pushed by hypercall page
 * rsp->rax		}
 */
ENTRY(xen_iret)
	pushq $0
1:	jmp hypercall_iret
ENDPATCH(xen_iret)
RELOC(xen_iret, 1b+1)

ENTRY(xen_sysret64)
	/*
	 * We're already on the usermode stack at this point, but
	 * still with the kernel gs, so we can easily switch back
	 */
	movq %rsp, PER_CPU_VAR(rsp_scratch)
	movq PER_CPU_VAR(cpu_current_top_of_stack), %rsp

	pushq $__USER_DS
	pushq PER_CPU_VAR(rsp_scratch)
	pushq %r11
	pushq $__USER_CS
	pushq %rcx

	pushq $VGCF_in_syscall
1:	jmp hypercall_iret
ENDPATCH(xen_sysret64)
RELOC(xen_sysret64, 1b+1)

/*
 * Xen handles syscall callbacks much like ordinary exceptions, which
 * means we have:
 * - kernel gs
 * - kernel rsp
 * - an iret-like stack frame on the stack (including rcx and r11):
 *	ss
 *	rsp
 *	rflags
 *	cs
 *	rip
 *	r11
 * rsp->rcx
 *
 * In all the entrypoints, we undo all that to make it look like a
 * CPU-generated syscall/sysenter and jump to the normal entrypoint.
 */

.macro undo_xen_syscall
	mov 0*8(%rsp), %rcx
	mov 1*8(%rsp), %r11
	mov 5*8(%rsp), %rsp
.endm

/* Normal 64-bit system call target */
ENTRY(xen_syscall_target)
	undo_xen_syscall
	jmp entry_SYSCALL_64_after_swapgs
ENDPROC(xen_syscall_target)

#ifdef CONFIG_IA32_EMULATION

/* 32-bit compat syscall target */
ENTRY(xen_syscall32_target)
	undo_xen_syscall
	jmp entry_SYSCALL_compat
ENDPROC(xen_syscall32_target)

/* 32-bit compat sysenter target */
ENTRY(xen_sysenter_target)
	undo_xen_syscall
	jmp entry_SYSENTER_compat
ENDPROC(xen_sysenter_target)

#else /* !CONFIG_IA32_EMULATION */

ENTRY(xen_syscall32_target)
ENTRY(xen_sysenter_target)
	lea 16(%rsp), %rsp	/* strip %rcx, %r11 */
	mov $-ENOSYS, %rax
	pushq $0
	jmp hypercall_iret
ENDPROC(xen_syscall32_target)
ENDPROC(xen_sysenter_target)

#endif	/* CONFIG_IA32_EMULATION */
