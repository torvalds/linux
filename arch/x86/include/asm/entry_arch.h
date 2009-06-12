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

BUILD_INTERRUPT3(invalidate_interrupt0,INVALIDATE_TLB_VECTOR_START+0,
		 smp_invalidate_interrupt)
BUILD_INTERRUPT3(invalidate_interrupt1,INVALIDATE_TLB_VECTOR_START+1,
		 smp_invalidate_interrupt)
BUILD_INTERRUPT3(invalidate_interrupt2,INVALIDATE_TLB_VECTOR_START+2,
		 smp_invalidate_interrupt)
BUILD_INTERRUPT3(invalidate_interrupt3,INVALIDATE_TLB_VECTOR_START+3,
		 smp_invalidate_interrupt)
BUILD_INTERRUPT3(invalidate_interrupt4,INVALIDATE_TLB_VECTOR_START+4,
		 smp_invalidate_interrupt)
BUILD_INTERRUPT3(invalidate_interrupt5,INVALIDATE_TLB_VECTOR_START+5,
		 smp_invalidate_interrupt)
BUILD_INTERRUPT3(invalidate_interrupt6,INVALIDATE_TLB_VECTOR_START+6,
		 smp_invalidate_interrupt)
BUILD_INTERRUPT3(invalidate_interrupt7,INVALIDATE_TLB_VECTOR_START+7,
		 smp_invalidate_interrupt)
#endif

BUILD_INTERRUPT(generic_interrupt, GENERIC_INTERRUPT_VECTOR)

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

#ifdef CONFIG_PERF_COUNTERS
BUILD_INTERRUPT(perf_pending_interrupt, LOCAL_PENDING_VECTOR)
#endif

#ifdef CONFIG_X86_MCE_P4THERMAL
BUILD_INTERRUPT(thermal_interrupt,THERMAL_APIC_VECTOR)
#endif

#endif
