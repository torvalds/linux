/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Device core Trace Support
 * Copyright (C) 2021, Intel Corporation
 *
 * Author: Andy Shevchenko <andriy.shevchenko@linux.intel.com>
 */

#undef TRACE_SYSTEM
#define TRACE_SYSTEM dev

#if !defined(__DEV_TRACE_H) || defined(TRACE_HEADER_MULTI_READ)
#define __DEV_TRACE_H

#include <linux/device.h>
#include <linux/tracepoint.h>
#include <linux/types.h>

DECLARE_EVENT_CLASS(devres,
	TP_PROTO(struct device *dev, const char *op, void *node, const char *name, size_t size),
	TP_ARGS(dev, op, node, name, size),
	TP_STRUCT__entry(
		__string(devname, dev_name(dev))
		__field(struct device *, dev)
		__field(const char *, op)
		__field(void *, node)
		__field(const char *, name)
		__field(size_t, size)
	),
	TP_fast_assign(
		__assign_str(devname);
		__entry->op = op;
		__entry->node = node;
		__entry->name = name;
		__entry->size = size;
	),
	TP_printk("%s %3s %p %s (%zu bytes)", __get_str(devname),
		  __entry->op, __entry->node, __entry->name, __entry->size)
);

DEFINE_EVENT(devres, devres_log,
	TP_PROTO(struct device *dev, const char *op, void *node, const char *name, size_t size),
	TP_ARGS(dev, op, node, name, size)
);

#endif /* __DEV_TRACE_H */

/* this part has to be here */

#undef TRACE_INCLUDE_PATH
#define TRACE_INCLUDE_PATH .

#undef TRACE_INCLUDE_FILE
#define TRACE_INCLUDE_FILE trace

#include <trace/define_trace.h>
