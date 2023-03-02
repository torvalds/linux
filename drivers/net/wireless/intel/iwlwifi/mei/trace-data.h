/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright(c) 2021        Intel Corporation
 */

#if !defined(CONFIG_IWLWIFI_DEVICE_TRACING)

#define trace_iwlmei_sap_data(...)

#else

#if !defined(__IWLWIFI_DEVICE_TRACE_IWLWIFI_SAP_DATA) || defined(TRACE_HEADER_MULTI_READ)

#ifndef __IWLWIFI_DEVICE_TRACE_IWLWIFI_SAP_DATA
enum iwl_sap_data_trace_type {
	IWL_SAP_RX_DATA_TO_AIR,
	IWL_SAP_TX_DATA_FROM_AIR,
	IWL_SAP_RX_DATA_DROPPED_FROM_AIR,
	IWL_SAP_TX_DHCP,
};

static inline size_t
iwlmei_sap_data_offset(enum iwl_sap_data_trace_type trace_type)
{
	switch (trace_type) {
	case IWL_SAP_RX_DATA_TO_AIR:
		return 0;
	case IWL_SAP_TX_DATA_FROM_AIR:
	case IWL_SAP_RX_DATA_DROPPED_FROM_AIR:
		return sizeof(struct iwl_sap_hdr);
	case IWL_SAP_TX_DHCP:
		return sizeof(struct iwl_sap_cb_data);
	default:
		WARN_ON_ONCE(1);
	}

	return 0;
}
#endif

#define __IWLWIFI_DEVICE_TRACE_IWLWIFI_SAP_DATA

#include <linux/tracepoint.h>
#include <linux/skbuff.h>
#include "sap.h"

#undef TRACE_SYSTEM
#define TRACE_SYSTEM iwlmei_sap_data

TRACE_EVENT(iwlmei_sap_data,
	TP_PROTO(const struct sk_buff *skb,
		 enum iwl_sap_data_trace_type trace_type),
	TP_ARGS(skb, trace_type),
	TP_STRUCT__entry(
		__dynamic_array(u8, data,
				skb->len - iwlmei_sap_data_offset(trace_type))
		__field(u32, trace_type)
	),
	TP_fast_assign(
		size_t offset = iwlmei_sap_data_offset(trace_type);
		__entry->trace_type = trace_type;
		skb_copy_bits(skb, offset, __get_dynamic_array(data),
			      skb->len - offset);
	),
	TP_printk("sap_data:trace_type %d len %d",
		  __entry->trace_type, __get_dynamic_array_len(data))
);

/*
 * If you add something here, add a stub in case
 * !defined(CONFIG_IWLWIFI_DEVICE_TRACING)
 */

#endif /* __IWLWIFI_DEVICE_TRACE_IWLWIFI_SAP_DATA */

#undef TRACE_INCLUDE_PATH
#define TRACE_INCLUDE_PATH .
#undef TRACE_INCLUDE_FILE
#define TRACE_INCLUDE_FILE trace-data
#include <trace/define_trace.h>

#endif /* CONFIG_IWLWIFI_DEVICE_TRACING */
