/*
 * This file is designed to contain the BUILD_INTERRUPT specifications for
 * all of the extra named interrupt vectors used by the architecture.
 * Usually this is the Inter Process Interrupts (IPIs)
 */

/*
 * The following vectors are part of the Linux architecture, there
 * is no hardware IRQ pin equivalent for them, they are triggered
 * through the ICC by us (IPIs)
 */
#ifdef CONFIG_SMP
BUILD_INTERRUPT(reschedule_interrupt,RESCHEDULE_VECTOR)
BUILD_INTERRUPT(call_function_interrupt,CALL_FUNCTION_VECTOR)
BUILD_INTERRUPT(call_function_single_interrupt,CALL_FUNCTION_SINGLE_VECTOR)
BUILD_INTERRUPT(irq_move_cleanup_interrupt,IRQ_MOVE_CLEANUP_VECTOR)
BUILD_INTERRUPT(reboot_interrupt,REBOOT_VECTOR)

.irp idx,0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15, \
	16,17,18,19,20,21,22,23,24,25,26,27,28,29,30,31
.if NUM_INVALIDATE_TLB_VECTORS > \idx
BUILD_INTERRUPT3(invalidate_interrupt\idx,
		 (INVALIDATE_TLB_VECTOR_START)+\idx,
		 smp_invalidate_interrupt)
.endif
.endr
#endif

BUILD_INTERRUPT(x86_platform_ipi, X86_PLATFORM_IPI_VECTOR)

/*
 * every pentium local APIC has two 'local interrupts', with a
 * soft-definable vector attached to both interrupts, one of
 * which is a timer interrupt, the other one is error counter
 * overflow. Linux uses the local APIC timer interrupt to get
 * a much simpler SMP time architecture:
 */
#ifdef CONFIG_X86_LOCAL_APIC

BUILD_INTERRUPT(apic_timer_interrupt,LOCAL_TIMER_VECTOR)
BUILD_INTERRUPT(error_interrupt,ERROR_APIC_VECTOR)
BUILD_INTERRUPT(spurious_interrupt,SPURIOUS_APIC_VECTOR)

#ifdef CONFIG_IRQ_WORK
BUILD_INTERRUPT(irq_work_interrupt, IRQ_WORK_VECTOR)
#endif

#ifdef CONFIG_X86_THERMAL_VECTOR
BUILD_INTERRUPT(thermal_interrupt,THERMAL_APIC_VECTOR)
#endif

#ifdef CONFIG_X86_MCE_THRESHOLD
BUILD_INTERRUPT(threshold_interrupt,THRESHOLD_APIC_VECTOR)
#endif

#ifdef CONFIG_X86_MCE
BUILD_INTERRUPT(mce_self_interrupt,MCE_SELF_VECTOR)
#endif

#endif
