/******************************************************************************
 *
 * Copyright(c) 2009 - 2014 Intel Corporation. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of version 2 of the GNU General Public License as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110, USA
 *
 * The full GNU General Public License is included in this distribution in the
 * file called LICENSE.
 *
 * Contact Information:
 *  Intel Linux Wireless <ilw@linux.intel.com>
 * Intel Corporation, 5200 N.E. Elam Young Parkway, Hillsboro, OR 97124-6497
 *
 *****************************************************************************/

#if !defined(__IWLWIFI_DEVICE_TRACE_IO) || defined(TRACE_HEADER_MULTI_READ)
#define __IWLWIFI_DEVICE_TRACE_IO

#include <linux/tracepoint.h>

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
