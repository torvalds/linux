/* SPDX-License-Identifier: GPL-2.0-only */
/******************************************************************************
 *
 * Copyright(c) 2009 - 2014 Intel Corporation. All rights reserved.
 * Copyright(c) 2016-2017 Intel Deutschland GmbH
 *****************************************************************************/

#if !defined(__IWLWIFI_DEVICE_TRACE_IO) || defined(TRACE_HEADER_MULTI_READ)
#define __IWLWIFI_DEVICE_TRACE_IO

#include <linux/tracepoint.h>
#include <linux/pci.h>

#undef TRACE_SYSTEM
#define TRACE_SYSTEM iwlwifi_io

TRACE_EVENT(iwlwifi_dev_ioread32,
	TP_PROTO(const struct device *dev, u32 offs, u32 val),
	TP_ARGS(dev, offs, val),
	TP_STRUCT__entry(
		DEV_ENTRY
		__field(u32, offs)
		__field(u32, val)
	),
	TP_fast_assign(
		DEV_ASSIGN;
		__entry->offs = offs;
		__entry->val = val;
	),
	TP_printk("[%s] read io[%#x] = %#x",
		  __get_str(dev), __entry->offs, __entry->val)
);

TRACE_EVENT(iwlwifi_dev_iowrite8,
	TP_PROTO(const struct device *dev, u32 offs, u8 val),
	TP_ARGS(dev, offs, val),
	TP_STRUCT__entry(
		DEV_ENTRY
		__field(u32, offs)
		__field(u8, val)
	),
	TP_fast_assign(
		DEV_ASSIGN;
		__entry->offs = offs;
		__entry->val = val;
	),
	TP_printk("[%s] write io[%#x] = %#x)",
		  __get_str(dev), __entry->offs, __entry->val)
);

TRACE_EVENT(iwlwifi_dev_iowrite32,
	TP_PROTO(const struct device *dev, u32 offs, u32 val),
	TP_ARGS(dev, offs, val),
	TP_STRUCT__entry(
		DEV_ENTRY
		__field(u32, offs)
		__field(u32, val)
	),
	TP_fast_assign(
		DEV_ASSIGN;
		__entry->offs = offs;
		__entry->val = val;
	),
	TP_printk("[%s] write io[%#x] = %#x)",
		  __get_str(dev), __entry->offs, __entry->val)
);

TRACE_EVENT(iwlwifi_dev_iowrite64,
	TP_PROTO(const struct device *dev, u64 offs, u64 val),
	TP_ARGS(dev, offs, val),
	TP_STRUCT__entry(
		DEV_ENTRY
		__field(u64, offs)
		__field(u64, val)
	),
	TP_fast_assign(
		DEV_ASSIGN;
		__entry->offs = offs;
		__entry->val = val;
	),
	TP_printk("[%s] write io[%llu] = %llu)",
		  __get_str(dev), __entry->offs, __entry->val)
);

TRACE_EVENT(iwlwifi_dev_iowrite_prph32,
	TP_PROTO(const struct device *dev, u32 offs, u32 val),
	TP_ARGS(dev, offs, val),
	TP_STRUCT__entry(
		DEV_ENTRY
		__field(u32, offs)
		__field(u32, val)
	),
	TP_fast_assign(
		DEV_ASSIGN;
		__entry->offs = offs;
		__entry->val = val;
	),
	TP_printk("[%s] write PRPH[%#x] = %#x)",
		  __get_str(dev), __entry->offs, __entry->val)
);

TRACE_EVENT(iwlwifi_dev_iowrite_prph64,
	TP_PROTO(const struct device *dev, u64 offs, u64 val),
	TP_ARGS(dev, offs, val),
	TP_STRUCT__entry(
		DEV_ENTRY
		__field(u64, offs)
		__field(u64, val)
	),
	TP_fast_assign(
		DEV_ASSIGN;
		__entry->offs = offs;
		__entry->val = val;
	),
	TP_printk("[%s] write PRPH[%llu] = %llu)",
		  __get_str(dev), __entry->offs, __entry->val)
);

TRACE_EVENT(iwlwifi_dev_ioread_prph32,
	TP_PROTO(const struct device *dev, u32 offs, u32 val),
	TP_ARGS(dev, offs, val),
	TP_STRUCT__entry(
		DEV_ENTRY
		__field(u32, offs)
		__field(u32, val)
	),
	TP_fast_assign(
		DEV_ASSIGN;
		__entry->offs = offs;
		__entry->val = val;
	),
	TP_printk("[%s] read PRPH[%#x] = %#x",
		  __get_str(dev), __entry->offs, __entry->val)
);

TRACE_EVENT(iwlwifi_dev_irq,
	TP_PROTO(const struct device *dev),
	TP_ARGS(dev),
	TP_STRUCT__entry(
		DEV_ENTRY
	),
	TP_fast_assign(
		DEV_ASSIGN;
	),
	/* TP_printk("") doesn't compile */
	TP_printk("%d", 0)
);

TRACE_EVENT(iwlwifi_dev_irq_msix,
	TP_PROTO(const struct device *dev, struct msix_entry *msix_entry,
		 bool defirq, u32 inta_fh, u32 inta_hw),
	TP_ARGS(dev, msix_entry, defirq, inta_fh, inta_hw),
	TP_STRUCT__entry(
		DEV_ENTRY
		__field(u32, entry)
		__field(u8, defirq)
		__field(u32, inta_fh)
		__field(u32, inta_hw)
	),
	TP_fast_assign(
		DEV_ASSIGN;
		__entry->entry = msix_entry->entry;
		__entry->defirq = defirq;
		__entry->inta_fh = inta_fh;
		__entry->inta_hw = inta_hw;
	),
	TP_printk("entry:%d defirq:%d fh:0x%x, hw:0x%x",
		  __entry->entry, __entry->defirq,
		  __entry->inta_fh, __entry->inta_hw)
);

TRACE_EVENT(iwlwifi_dev_ict_read,
	TP_PROTO(const struct device *dev, u32 index, u32 value),
	TP_ARGS(dev, index, value),
	TP_STRUCT__entry(
		DEV_ENTRY
		__field(u32, index)
		__field(u32, value)
	),
	TP_fast_assign(
		DEV_ASSIGN;
		__entry->index = index;
		__entry->value = value;
	),
	TP_printk("[%s] read ict[%d] = %#.8x",
		  __get_str(dev), __entry->index, __entry->value)
);
#endif /* __IWLWIFI_DEVICE_TRACE_IO */

#undef TRACE_INCLUDE_PATH
#define TRACE_INCLUDE_PATH .
#undef TRACE_INCLUDE_FILE
#define TRACE_INCLUDE_FILE iwl-devtrace-io
#include <trace/define_trace.h>
