/* SPDX-License-Identifier: BSD-3-Clause-Clear */
/*
 * Copyright (c) 2019 The Linux Foundation. All rights reserved.
 */

#if !defined(_TRACE_H_) || defined(TRACE_HEADER_MULTI_READ)

#include <linux/tracepoint.h>
#include "core.h"

#define _TRACE_H_

/* create empty functions when tracing is disabled */
#if !defined(CONFIG_ATH11K_TRACING)
#undef TRACE_EVENT
#define TRACE_EVENT(name, proto, ...) \
static inline void trace_ ## name(proto) {}
#endif /* !CONFIG_ATH11K_TRACING || __CHECKER__ */

#undef TRACE_SYSTEM
#define TRACE_SYSTEM ath11k

TRACE_EVENT(ath11k_htt_pktlog,
	    TP_PROTO(struct ath11k *ar, const void *buf, u16 buf_len),

	TP_ARGS(ar, buf, buf_len),

	TP_STRUCT__entry(
		__string(device, dev_name(ar->ab->dev))
		__string(driver, dev_driver_string(ar->ab->dev))
		__field(u16, buf_len)
		__dynamic_array(u8, pktlog, buf_len)
	),

	TP_fast_assign(
		__assign_str(device, dev_name(ar->ab->dev));
		__assign_str(driver, dev_driver_string(ar->ab->dev));
		__entry->buf_len = buf_len;
		memcpy(__get_dynamic_array(pktlog), buf, buf_len);
	),

	TP_printk(
		"%s %s size %hu",
		__get_str(driver),
		__get_str(device),
		__entry->buf_len
	 )
);

TRACE_EVENT(ath11k_htt_ppdu_stats,
	    TP_PROTO(struct ath11k *ar, const void *data, size_t len),

	TP_ARGS(ar, data, len),

	TP_STRUCT__entry(
		__string(device, dev_name(ar->ab->dev))
		__string(driver, dev_driver_string(ar->ab->dev))
		__field(u16, len)
		__dynamic_array(u8, ppdu, len)
	),

	TP_fast_assign(
		__assign_str(device, dev_name(ar->ab->dev));
		__assign_str(driver, dev_driver_string(ar->ab->dev));
		__entry->len = len;
		memcpy(__get_dynamic_array(ppdu), data, len);
	),

	TP_printk(
		"%s %s ppdu len %d",
		__get_str(driver),
		__get_str(device),
		__entry->len
	 )
);

TRACE_EVENT(ath11k_htt_rxdesc,
	    TP_PROTO(struct ath11k *ar, const void *data, size_t len),

	TP_ARGS(ar, data, len),

	TP_STRUCT__entry(
		__string(device, dev_name(ar->ab->dev))
		__string(driver, dev_driver_string(ar->ab->dev))
		__field(u16, len)
		__dynamic_array(u8, rxdesc, len)
	),

	TP_fast_assign(
		__assign_str(device, dev_name(ar->ab->dev));
		__assign_str(driver, dev_driver_string(ar->ab->dev));
		__entry->len = len;
		memcpy(__get_dynamic_array(rxdesc), data, len);
	),

	TP_printk(
		"%s %s rxdesc len %d",
		__get_str(driver),
		__get_str(device),
		__entry->len
	 )
);

#endif /* _TRACE_H_ || TRACE_HEADER_MULTI_READ*/

/* we don't want to use include/trace/events */
#undef TRACE_INCLUDE_PATH
#define TRACE_INCLUDE_PATH .
#undef TRACE_INCLUDE_FILE
#define TRACE_INCLUDE_FILE trace

/* This part must be outside protection */
#include <trace/define_trace.h>
