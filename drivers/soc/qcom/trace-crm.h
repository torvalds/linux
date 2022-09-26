/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2022 Qualcomm Innovation Center, Inc. All rights reserved.
 */

#if !defined(_TRACE_CRM_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_CRM_H

#undef TRACE_SYSTEM
#define TRACE_SYSTEM crm

#include <linux/tracepoint.h>
#include <soc/qcom/crm.h>

DECLARE_EVENT_CLASS(crm_vcd_votes,

	TP_PROTO(const char *name, u32 vcd_type, u32 resource_idx, u32 pwr_state, u32 data),

	TP_ARGS(name, vcd_type, resource_idx, pwr_state, data),

	TP_STRUCT__entry(
			 __string(name, name)
			 __field(u32, vcd_type)
			 __field(u32, resource_idx)
			 __field(u32, pwr_state)
			 __field(u32, data)
	),

	TP_fast_assign(
		       __assign_str(name, name);
		       __entry->vcd_type = vcd_type;
		       __entry->resource_idx = resource_idx;
		       __entry->pwr_state = pwr_state;
		       __entry->data = data;
	),

	TP_printk("%s: vcd_type: %u resource_idx: %u pwr_state: %u data: %#x",
		  __get_str(name), __entry->vcd_type, __entry->resource_idx,
		   __entry->pwr_state, __entry->data)
);

DEFINE_EVENT(crm_vcd_votes, crm_cache_vcd_votes,

	TP_PROTO(const char *name, u32 vcd_type, u32 resource_idx, u32 pwr_state, u32 data),

	TP_ARGS(name, vcd_type, resource_idx, pwr_state, data)
);

DEFINE_EVENT(crm_vcd_votes, crm_write_vcd_votes,

	TP_PROTO(const char *name, u32 vcd_type, u32 resource_idx, u32 pwr_state, u32 data),

	TP_ARGS(name, vcd_type, resource_idx, pwr_state, data)
);

TRACE_EVENT(crm_irq,

	TP_PROTO(const char *name, u32 vcd_type, u32 resource_idx, unsigned long irq_status),

	TP_ARGS(name, vcd_type, resource_idx, irq_status),

	TP_STRUCT__entry(
			 __string(name, name)
			 __field(u32, vcd_type)
			 __field(u32, resource_idx)
			 __field(unsigned long, irq_status)
	),

	TP_fast_assign(
		       __assign_str(name, name);
		       __entry->vcd_type = vcd_type;
		       __entry->resource_idx = resource_idx;
		       __entry->irq_status = irq_status;
	),

	TP_printk("%s: IRQ vcd_type: %u resource_idx: %u irq_status: %lu",
		  __get_str(name), __entry->vcd_type, __entry->resource_idx,
		  __entry->irq_status)
);

TRACE_EVENT(crm_switch_channel,

	TP_PROTO(const char *name, int ch, int ret),

	TP_ARGS(name, ch, ret),

	TP_STRUCT__entry(
			 __string(name, name)
			 __field(int, ch)
			 __field(int, ret)
	),

	TP_fast_assign(
		       __assign_str(name, name);
		       __entry->ch = ch;
		       __entry->ret = ret;
	),

	TP_printk("%s: channel switched to: %d ret: %d",
		  __get_str(name), __entry->ch, __entry->ret)
);

#endif /* _TRACE_CRM_H */

#undef TRACE_INCLUDE_PATH
#define TRACE_INCLUDE_PATH .

#undef TRACE_INCLUDE_FILE
#define TRACE_INCLUDE_FILE trace-crm

#include <trace/define_trace.h>
