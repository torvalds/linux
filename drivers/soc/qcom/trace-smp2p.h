/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (c) 2024 Qualcomm Innovation Center, Inc. All rights reserved.
 */

#undef TRACE_SYSTEM
#define TRACE_SYSTEM qcom_smp2p

#if !defined(__QCOM_SMP2P_TRACE_H__) || defined(TRACE_HEADER_MULTI_READ)
#define __QCOM_SMP2P_TRACE_H__

#include <linux/device.h>
#include <linux/tracepoint.h>

TRACE_EVENT(smp2p_ssr_ack,
	TP_PROTO(const struct device *dev),
	TP_ARGS(dev),
	TP_STRUCT__entry(
		__string(dev_name, dev_name(dev))
	),
	TP_fast_assign(
		__assign_str(dev_name);
	),
	TP_printk("%s: SSR detected", __get_str(dev_name))
);

TRACE_EVENT(smp2p_negotiate,
	TP_PROTO(const struct device *dev, unsigned int features),
	TP_ARGS(dev, features),
	TP_STRUCT__entry(
		__string(dev_name, dev_name(dev))
		__field(u32, out_features)
	),
	TP_fast_assign(
		__assign_str(dev_name);
		__entry->out_features = features;
	),
	TP_printk("%s: state=open out_features=%s", __get_str(dev_name),
		__print_flags(__entry->out_features, "|",
			{SMP2P_FEATURE_SSR_ACK, "SMP2P_FEATURE_SSR_ACK"})
	)
);

TRACE_EVENT(smp2p_notify_in,
	TP_PROTO(struct smp2p_entry *smp2p_entry, unsigned long status, u32 val),
	TP_ARGS(smp2p_entry, status, val),
	TP_STRUCT__entry(
		__string(dev_name, dev_name(smp2p_entry->smp2p->dev))
		__string(client_name, smp2p_entry->name)
		__field(unsigned long, status)
		__field(u32, val)
	),
	TP_fast_assign(
		__assign_str(dev_name);
		__assign_str(client_name);
		__entry->status = status;
		__entry->val = val;
	),
	TP_printk("%s: %s: status:0x%0lx val:0x%0x",
		__get_str(dev_name),
		__get_str(client_name),
		__entry->status,
		__entry->val
	)
);

TRACE_EVENT(smp2p_update_bits,
	TP_PROTO(struct smp2p_entry *smp2p_entry, u32 orig, u32 val),
	TP_ARGS(smp2p_entry, orig, val),
	TP_STRUCT__entry(
		__string(dev_name, dev_name(smp2p_entry->smp2p->dev))
		__string(client_name, smp2p_entry->name)
		__field(u32, orig)
		__field(u32, val)
	),
	TP_fast_assign(
		__assign_str(dev_name);
		__assign_str(client_name);
		__entry->orig = orig;
		__entry->val = val;
	),
	TP_printk("%s: %s: orig:0x%0x new:0x%0x",
		__get_str(dev_name),
		__get_str(client_name),
		__entry->orig,
		__entry->val
	)
);

#endif /* __QCOM_SMP2P_TRACE_H__ */

#undef TRACE_INCLUDE_PATH
#define TRACE_INCLUDE_PATH .

#undef TRACE_INCLUDE_FILE
#define TRACE_INCLUDE_FILE trace-smp2p

#include <trace/define_trace.h>
