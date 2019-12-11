/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Asm versions of Xen pv-ops, suitable for direct use.
 *
 * We only bother with direct forms (ie, vcpu in pda) of the
 * operations here; the indirect forms are better handled in C.
 */

#include <asm/thread_info.h>
#include <asm/processor-flags.h>
#include <asm/segment.h>
#include <asm/asm.h>

#include <xen/interface/xen.h>

#include <linux/linkage.h>

/* Pseudo-flag used for virtual NMI, which we don't implement yet */
#define XEN_EFLAGS_NMI  0x80000000

/*
 * This is run where a normal iret would be run, with the same stack setup:
 *	8: eflags
 *	4: cs
 *	esp-> 0: eip
 *
 * This attempts to make sure that any pending events are dealt with
 * on return to usermode, but there is a small window in which an
 * event can happen just before entering usermode.  If the nested
 * interrupt ends up setting one of the TIF_WORK_MASK pending work
 * flags, they will not be tested again before returning to
 * usermode. This means that a process can end up with pending work,
 * which will be unprocessed until the process enters and leaves the
 * kernel again, which could be an unbounded amount of time.  This
 * means that a pending signal or reschedule event could be
 * indefinitely delayed.
 *
 * The fix is to notice a nested interrupt in the critical window, and
 * if one occurs, then fold the nested interrupt into the current
 * interrupt stack frame, and re-process it iteratively rather than
 * recursively.  This means that it will exit via the normal path, and
 * all pending work will be dealt with appropriately.
 *
 * Because the nested interrupt handler needs to deal with the current
 * stack state in whatever form its in, we keep things simple by only
 * using a single register which is pushed/popped on the stack.
 */

.macro POP_FS
1:
	popw %fs
.pushsection .fixup, "ax"
2:	movw $0, (%esp)
	jmp 1b
.popsection
	_ASM_EXTABLE(1b,2b)
.endm

SYM_CODE_START(xen_iret)
	/* test eflags for special cases */
	testl $(X86_EFLAGS_VM | XEN_EFLAGS_NMI), 8(%esp)
	jnz hyper_iret

	push %eax
	ESP_OFFSET=4	# bytes pushed onto stack

	/* Store vcpu_info pointer for easy access */
#ifdef CONFIG_SMP
	pushw %fs
	movl $(__KERNEL_PERCPU), %eax
	movl %eax, %fs
	movl %fs:xen_vcpu, %eax
	POP_FS
#else
	movl %ss:xen_vcpu, %eax
#endif

	/* check IF state we're restoring */
	testb $X86_EFLAGS_IF>>8, 8+1+ESP_OFFSET(%esp)

	/*
	 * Maybe enable events.  Once this happens we could get a
	 * recursive event, so the critical region starts immediately
	 * afterwards.  However, if that happens we don't end up
	 * resuming the code, so we don't have to be worried about
	 * being preempted to another CPU.
	 */
	setz %ss:XEN_vcpu_info_mask(%eax)
xen_iret_start_crit:

	/* check for unmasked and pending */
	cmpw $0x0001, %ss:XEN_vcpu_info_pending(%eax)

	/*
	 * If there's something pending, mask events again so we can
	 * jump back into xen_hypervisor_callback. Otherwise do not
	 * touch XEN_vcpu_info_mask.
	 */
	jne 1f
	movb $1, %ss:XEN_vcpu_info_mask(%eax)

1:	popl %eax

	/*
	 * From this point on the registers are restored and the stack
	 * updated, so we don't need to worry about it if we're
	 * preempted
	 */
iret_restore_end:

	/*
	 * Jump to hypervisor_callback after fixing up the stack.
	 * Events are masked, so jumping out of the critical region is
	 * OK.
	 */
	je xen_hypervisor_callback

1:	iret
xen_iret_end_crit:
	_ASM_EXTABLE(1b, iret_exc)

hyper_iret:
	/* put this out of line since its very rarely used */
	jmp hypercall_page + __HYPERVISOR_iret * 32
SYM_CODE_END(xen_iret)

	.globl xen_iret_start_crit, xen_iret_end_crit

/*
 * This is called by xen_hypervisor_callback in entry_32.S when it sees
 * that the EIP at the time of interrupt was between
 * xen_iret_start_crit and xen_iret_end_crit.
 *
 * The stack format at this point is:
 *	----------------
 *	 ss		: (ss/esp may be present if we came from usermode)
 *	 esp		:
 *	 eflags		}  outer exception info
 *	 cs		}
 *	 eip		}
 *	----------------
 *	 eax		:  outer eax if it hasn't been restored
 *	----------------
 *	 eflags		}
 *	 cs		}  nested exception info
 *	 eip		}
 *	 return address	: (into xen_hypervisor_callback)
 *
 * In order to deliver the nested exception properly, we need to discard the
 * nested exception frame such that when we handle the exception, we do it
 * in the context of the outer exception rather than starting a new one.
 *
 * The only caveat is that if the outer eax hasn't been restored yet (i.e.
 * it's still on stack), we need to restore its value here.
 */
SYM_CODE_START(xen_iret_crit_fixup)
	/*
	 * Paranoia: Make sure we're really coming from kernel space.
	 * One could imagine a case where userspace jumps into the
	 * critical range address, but just before the CPU delivers a
	 * PF, it decides to deliver an interrupt instead.  Unlikely?
	 * Definitely.  Easy to avoid?  Yes.
	 */
	testb $2, 2*4(%esp)		/* nested CS */
	jnz 2f

	/*
	 * If eip is before iret_restore_end then stack
	 * hasn't been restored yet.
	 */
	cmpl $iret_restore_end, 1*4(%esp)
	jae 1f

	movl 4*4(%esp), %eax		/* load outer EAX */
	ret $4*4			/* discard nested EIP, CS, and EFLAGS as
					 * well as the just restored EAX */

1:
	ret $3*4			/* discard nested EIP, CS, and EFLAGS */

2:
	ret
SYM_CODE_END(xen_iret_crit_fixup)
