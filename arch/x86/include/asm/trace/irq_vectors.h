#undef TRACE_SYSTEM
#define TRACE_SYSTEM irq_vectors

#if !defined(_TRACE_IRQ_VECTORS_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_IRQ_VECTORS_H

#include <linux/tracepoint.h>
#include <asm/trace/common.h>

#ifdef CONFIG_X86_LOCAL_APIC

extern int trace_resched_ipi_reg(void);
extern void trace_resched_ipi_unreg(void);

DECLARE_EVENT_CLASS(x86_irq_vector,

	TP_PROTO(int vector),

	TP_ARGS(vector),

	TP_STRUCT__entry(
		__field(		int,	vector	)
	),

	TP_fast_assign(
		__entry->vector = vector;
	),

	TP_printk("vector=%d", __entry->vector) );

#define DEFINE_IRQ_VECTOR_EVENT(name)		\
DEFINE_EVENT_FN(x86_irq_vector, name##_entry,	\
	TP_PROTO(int vector),			\
	TP_ARGS(vector), NULL, NULL);		\
DEFINE_EVENT_FN(x86_irq_vector, name##_exit,	\
	TP_PROTO(int vector),			\
	TP_ARGS(vector), NULL, NULL);

#define DEFINE_RESCHED_IPI_EVENT(name)		\
DEFINE_EVENT_FN(x86_irq_vector, name##_entry,	\
	TP_PROTO(int vector),			\
	TP_ARGS(vector),			\
	trace_resched_ipi_reg,			\
	trace_resched_ipi_unreg);		\
DEFINE_EVENT_FN(x86_irq_vector, name##_exit,	\
	TP_PROTO(int vector),			\
	TP_ARGS(vector),			\
	trace_resched_ipi_reg,			\
	trace_resched_ipi_unreg);

/*
 * local_timer - called when entering/exiting a local timer interrupt
 * vector handler
 */
DEFINE_IRQ_VECTOR_EVENT(local_timer);

/*
 * spurious_apic - called when entering/exiting a spurious apic vector handler
 */
DEFINE_IRQ_VECTOR_EVENT(spurious_apic);

/*
 * error_apic - called when entering/exiting an error apic vector handler
 */
DEFINE_IRQ_VECTOR_EVENT(error_apic);

/*
 * x86_platform_ipi - called when entering/exiting a x86 platform ipi interrupt
 * vector handler
 */
DEFINE_IRQ_VECTOR_EVENT(x86_platform_ipi);

#ifdef CONFIG_IRQ_WORK
/*
 * irq_work - called when entering/exiting a irq work interrupt
 * vector handler
 */
DEFINE_IRQ_VECTOR_EVENT(irq_work);

/*
 * We must dis-allow sampling irq_work_exit() because perf event sampling
 * itself can cause irq_work, which would lead to an infinite loop;
 *
 *  1) irq_work_exit happens
 *  2) generates perf sample
 *  3) generates irq_work
 *  4) goto 1
 */
TRACE_EVENT_PERF_PERM(irq_work_exit, is_sampling_event(p_event) ? -EPERM : 0);
#endif

/*
 * The ifdef is required because that tracepoint macro hell emits tracepoint
 * code in files which include this header even if the tracepoint is not
 * enabled. Brilliant stuff that.
 */
#ifdef CONFIG_SMP
/*
 * reschedule - called when entering/exiting a reschedule vector handler
 */
DEFINE_RESCHED_IPI_EVENT(reschedule);

/*
 * call_function - called when entering/exiting a call function interrupt
 * vector handler
 */
DEFINE_IRQ_VECTOR_EVENT(call_function);

/*
 * call_function_single - called when entering/exiting a call function
 * single interrupt vector handler
 */
DEFINE_IRQ_VECTOR_EVENT(call_function_single);
#endif

#ifdef CONFIG_X86_MCE_THRESHOLD
/*
 * threshold_apic - called when entering/exiting a threshold apic interrupt
 * vector handler
 */
DEFINE_IRQ_VECTOR_EVENT(threshold_apic);
#endif

#ifdef CONFIG_X86_MCE_AMD
/*
 * deferred_error_apic - called when entering/exiting a deferred apic interrupt
 * vector handler
 */
DEFINE_IRQ_VECTOR_EVENT(deferred_error_apic);
#endif

#ifdef CONFIG_X86_THERMAL_VECTOR
/*
 * thermal_apic - called when entering/exiting a thermal apic interrupt
 * vector handler
 */
DEFINE_IRQ_VECTOR_EVENT(thermal_apic);
#endif

#endif /* CONFIG_X86_LOCAL_APIC */

#undef TRACE_INCLUDE_PATH
#define TRACE_INCLUDE_PATH .
#define TRACE_INCLUDE_FILE irq_vectors
#endif /*  _TRACE_IRQ_VECTORS_H */

/* This part must be outside protection */
#include <trace/define_trace.h>
