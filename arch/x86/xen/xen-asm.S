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
#include <asm/thread_info.h>
#include <asm/percpu.h>
#include <asm/processor-flags.h>
#include <asm/segment.h>

#include <xen/interface/xen.h>

#define RELOC(x, v)	.globl x##_reloc; x##_reloc=v
#define ENDPATCH(x)	.globl x##_end; x##_end=.

/* Pseudo-flag used for virtual NMI, which we don't implement yet */
#define XEN_EFLAGS_NMI	0x80000000

/*
	Enable events.  This clears the event mask and tests the pending
	event status with one and operation.  If there are pending
	events, then enter the hypervisor to get them handled.
 */
ENTRY(xen_irq_enable_direct)
	/* Unmask events */
	movb $0, PER_CPU_VAR(xen_vcpu_info)+XEN_vcpu_info_mask

	/* Preempt here doesn't matter because that will deal with
	   any pending interrupts.  The pending check may end up being
	   run on the wrong CPU, but that doesn't hurt. */

	/* Test for pending */
	testb $0xff, PER_CPU_VAR(xen_vcpu_info)+XEN_vcpu_info_pending
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
	movb $1, PER_CPU_VAR(xen_vcpu_info)+XEN_vcpu_info_mask
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
	testb $0xff, PER_CPU_VAR(xen_vcpu_info)+XEN_vcpu_info_mask
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
	setz PER_CPU_VAR(xen_vcpu_info)+XEN_vcpu_info_mask
	/* Preempt here doesn't matter because that will deal with
	   any pending interrupts.  The pending check may end up being
	   run on the wrong CPU, but that doesn't hurt. */

	/* check for unmasked and pending */
	cmpw $0x0001, PER_CPU_VAR(xen_vcpu_info)+XEN_vcpu_info_pending
	jz 1f
2:	call check_events
1:
ENDPATCH(xen_restore_fl_direct)
	ret
	ENDPROC(xen_restore_fl_direct)
	RELOC(xen_restore_fl_direct, 2b+1)

/*
	This is run where a normal iret would be run, with the same stack setup:
	      8: eflags
	      4: cs
	esp-> 0: eip

	This attempts to make sure that any pending events are dealt
	with on return to usermode, but there is a small window in
	which an event can happen just before entering usermode.  If
	the nested interrupt ends up setting one of the TIF_WORK_MASK
	pending work flags, they will not be tested again before
	returning to usermode. This means that a process can end up
	with pending work, which will be unprocessed until the process
	enters and leaves the kernel again, which could be an
	unbounded amount of time.  This means that a pending signal or
	reschedule event could be indefinitely delayed.

	The fix is to notice a nested interrupt in the critical
	window, and if one occurs, then fold the nested interrupt into
	the current interrupt stack frame, and re-process it
	iteratively rather than recursively.  This means that it will
	exit via the normal path, and all pending work will be dealt
	with appropriately.

	Because the nested interrupt handler needs to deal with the
	current stack state in whatever form its in, we keep things
	simple by only using a single register which is pushed/popped
	on the stack.

	Non-direct iret could be done in the same way, but it would
	require an annoying amount of code duplication.  We'll assume
	that direct mode will be the common case once the hypervisor
	support becomes commonplace.
 */
ENTRY(xen_iret_direct)
	/* test eflags for special cases */
	testl $(X86_EFLAGS_VM | XEN_EFLAGS_NMI), 8(%esp)
	jnz hyper_iret

	push %eax
	ESP_OFFSET=4	# bytes pushed onto stack

	/* Store vcpu_info pointer for easy access.  Do it this
	   way to avoid having to reload %fs */
#ifdef CONFIG_SMP
	GET_THREAD_INFO(%eax)
	movl TI_cpu(%eax),%eax
	movl __per_cpu_offset(,%eax,4),%eax
	lea per_cpu__xen_vcpu_info(%eax),%eax
#else
	movl $per_cpu__xen_vcpu_info, %eax
#endif

	/* check IF state we're restoring */
	testb $X86_EFLAGS_IF>>8, 8+1+ESP_OFFSET(%esp)

	/* Maybe enable events.  Once this happens we could get a
	   recursive event, so the critical region starts immediately
	   afterwards.  However, if that happens we don't end up
	   resuming the code, so we don't have to be worried about
	   being preempted to another CPU. */
	setz XEN_vcpu_info_mask(%eax)
xen_iret_start_crit:

	/* check for unmasked and pending */
	cmpw $0x0001, XEN_vcpu_info_pending(%eax)

	/* If there's something pending, mask events again so we
	   can jump back into xen_hypervisor_callback */
	sete XEN_vcpu_info_mask(%eax)

	popl %eax

	/* From this point on the registers are restored and the stack
	   updated, so we don't need to worry about it if we're preempted */
iret_restore_end:

	/* Jump to hypervisor_callback after fixing up the stack.
	   Events are masked, so jumping out of the critical
	   region is OK. */
	je xen_hypervisor_callback

	iret
xen_iret_end_crit:

hyper_iret:
	/* put this out of line since its very rarely used */
	jmp hypercall_page + __HYPERVISOR_iret * 32

	.globl xen_iret_start_crit, xen_iret_end_crit

/*
   This is called by xen_hypervisor_callback in entry.S when it sees
   that the EIP at the time of interrupt was between xen_iret_start_crit
   and xen_iret_end_crit.  We're passed the EIP in %eax so we can do
   a more refined determination of what to do.

   The stack format at this point is:
	----------------
	 ss		: (ss/esp may be present if we came from usermode)
	 esp		:
	 eflags		}  outer exception info
	 cs		}
	 eip		}
	---------------- <- edi (copy dest)
	 eax		:  outer eax if it hasn't been restored
	----------------
	 eflags		}  nested exception info
	 cs		}   (no ss/esp because we're nested
	 eip		}    from the same ring)
	 orig_eax	}<- esi (copy src)
	 - - - - - - - -
	 fs		}
	 es		}
	 ds		}  SAVE_ALL state
	 eax		}
	  :		:
	 ebx		}
	----------------
	 return addr	 <- esp
	----------------

   In order to deliver the nested exception properly, we need to shift
   everything from the return addr up to the error code so it
   sits just under the outer exception info.  This means that when we
   handle the exception, we do it in the context of the outer exception
   rather than starting a new one.

   The only caveat is that if the outer eax hasn't been
   restored yet (ie, it's still on stack), we need to insert
   its value into the SAVE_ALL state before going on, since
   it's usermode state which we eventually need to restore.
 */
ENTRY(xen_iret_crit_fixup)
	/* offsets +4 for return address */

	/*
	   Paranoia: Make sure we're really coming from userspace.
	   One could imagine a case where userspace jumps into the
	   critical range address, but just before the CPU delivers a GP,
	   it decides to deliver an interrupt instead.  Unlikely?
	   Definitely.  Easy to avoid?  Yes.  The Intel documents
	   explicitly say that the reported EIP for a bad jump is the
	   jump instruction itself, not the destination, but some virtual
	   environments get this wrong.
	 */
	movl PT_CS+4(%esp), %ecx
	andl $SEGMENT_RPL_MASK, %ecx
	cmpl $USER_RPL, %ecx
	je 2f

	lea PT_ORIG_EAX+4(%esp), %esi
	lea PT_EFLAGS+4(%esp), %edi

	/* If eip is before iret_restore_end then stack
	   hasn't been restored yet. */
	cmp $iret_restore_end, %eax
	jae 1f

	movl 0+4(%edi),%eax		/* copy EAX */
	movl %eax, PT_EAX+4(%esp)

	lea ESP_OFFSET(%edi),%edi	/* move dest up over saved regs */

	/* set up the copy */
1:	std
	mov $(PT_EIP+4) / 4, %ecx	/* copy ret+saved regs up to orig_eax */
	rep movsl
	cld

	lea 4(%edi),%esp		/* point esp to new frame */
2:	ret


/*
	Force an event check by making a hypercall,
	but preserve regs before making the call.
 */
check_events:
	push %eax
	push %ecx
	push %edx
	call force_evtchn_callback
	pop %edx
	pop %ecx
	pop %eax
	ret
