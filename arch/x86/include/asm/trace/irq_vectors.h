#undef TRACE_SYSTEM
#define TRACE_SYSTEM irq_vectors

#if !defined(_TRACE_IRQ_VECTORS_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_IRQ_VECTORS_H

#include <linux/tracepoint.h>

extern void trace_irq_vector_regfunc(void);
extern void trace_irq_vector_unregfunc(void);

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
	TP_ARGS(vector),			\
	trace_irq_vector_regfunc,		\
	trace_irq_vector_unregfunc);		\
DEFINE_EVENT_FN(x86_irq_vector, name##_exit,	\
	TP_PROTO(int vector),			\
	TP_ARGS(vector),			\
	trace_irq_vector_regfunc,		\
	trace_irq_vector_unregfunc);


/*
 * local_timer - called when entering/exiting a local timer interrupt
 * vector handler
 */
DEFINE_IRQ_VECTOR_EVENT(local_timer);

/*
 * reschedule - called when entering/exiting a reschedule vector handler
 */
DEFINE_IRQ_VECTOR_EVENT(reschedule);

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

/*
 * threshold_apic - called when entering/exiting a threshold apic interrupt
 * vector handler
 */
DEFINE_IRQ_VECTOR_EVENT(threshold_apic);

/*
 * thermal_apic - called when entering/exiting a thermal apic interrupt
 * vector handler
 */
DEFINE_IRQ_VECTOR_EVENT(thermal_apic);

#undef TRACE_INCLUDE_PATH
#define TRACE_INCLUDE_PATH .
#define TRACE_INCLUDE_FILE irq_vectors
#endif /*  _TRACE_IRQ_VECTORS_H */

/* This part must be outside protection */
#include <trace/define_trace.h>
