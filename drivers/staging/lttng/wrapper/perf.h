#ifndef _LTT_WRAPPER_PERF_H
#define _LTT_WRAPPER_PERF_H

/*
 * Copyright (C) 2011 Mathieu Desnoyers (mathieu.desnoyers@efficios.com)
 *
 * Dual LGPL v2.1/GPL v2 license.
 */

#include <linux/perf_event.h>

#if defined(CONFIG_PERF_EVENTS) && (LINUX_VERSION_CODE >= KERNEL_VERSION(3,0,99))
static inline struct perf_event *
wrapper_perf_event_create_kernel_counter(struct perf_event_attr *attr,
				int cpu,
				struct task_struct *task,
				perf_overflow_handler_t callback)
{
	return perf_event_create_kernel_counter(attr, cpu, task, callback, NULL);
}
#else
static inline struct perf_event *
wrapper_perf_event_create_kernel_counter(struct perf_event_attr *attr,
				int cpu,
				struct task_struct *task,
				perf_overflow_handler_t callback)
{
	return perf_event_create_kernel_counter(attr, cpu, task, callback);
}
#endif

#endif /* _LTT_WRAPPER_PERF_H */
