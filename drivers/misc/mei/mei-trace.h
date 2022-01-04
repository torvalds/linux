/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (c) 2015-2016, Intel Corporation. All rights reserved.
 * Intel Management Engine Interface (Intel MEI) Linux driver
 */

#if !defined(_MEI_TRACE_H_) || defined(TRACE_HEADER_MULTI_READ)
#define _MEI_TRACE_H_

#include <linux/stringify.h>
#include <linux/types.h>
#include <linux/tracepoint.h>

#include <linux/device.h>

#undef TRACE_SYSTEM
#define TRACE_SYSTEM mei

TRACE_EVENT(mei_reg_read,
	TP_PROTO(const struct device *dev, const char *reg, u32 offs, u32 val),
	TP_ARGS(dev, reg, offs, val),
	TP_STRUCT__entry(
		__string(dev, dev_name(dev))
		__field(const char *, reg)
		__field(u32, offs)
		__field(u32, val)
	),
	TP_fast_assign(
		__assign_str(dev, dev_name(dev));
		__entry->reg  = reg;
		__entry->offs = offs;
		__entry->val = val;
	),
	TP_printk("[%s] read %s:[%#x] = %#x",
		  __get_str(dev), __entry->reg, __entry->offs, __entry->val)
);

TRACE_EVENT(mei_reg_write,
	TP_PROTO(const struct device *dev, const char *reg, u32 offs, u32 val),
	TP_ARGS(dev, reg, offs, val),
	TP_STRUCT__entry(
		__string(dev, dev_name(dev))
		__field(const char *, reg)
		__field(u32, offs)
		__field(u32, val)
	),
	TP_fast_assign(
		__assign_str(dev, dev_name(dev));
		__entry->reg = reg;
		__entry->offs = offs;
		__entry->val = val;
	),
	TP_printk("[%s] write %s[%#x] = %#x",
		  __get_str(dev), __entry->reg,  __entry->offs, __entry->val)
);

TRACE_EVENT(mei_pci_cfg_read,
	TP_PROTO(const struct device *dev, const char *reg, u32 offs, u32 val),
	TP_ARGS(dev, reg, offs, val),
	TP_STRUCT__entry(
		__string(dev, dev_name(dev))
		__field(const char *, reg)
		__field(u32, offs)
		__field(u32, val)
	),
	TP_fast_assign(
		__assign_str(dev, dev_name(dev));
		__entry->reg  = reg;
		__entry->offs = offs;
		__entry->val = val;
	),
	TP_printk("[%s] pci cfg read %s:[%#x] = %#x",
		  __get_str(dev), __entry->reg, __entry->offs, __entry->val)
);

#endif /* _MEI_TRACE_H_ */

/* This part must be outside protection */
#undef TRACE_INCLUDE_PATH
#undef TRACE_INCLUDE_FILE
#define TRACE_INCLUDE_PATH .
#define TRACE_INCLUDE_FILE mei-trace
#include <trace/define_trace.h>
