/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright(c) 2021        Intel Corporation
 */

#if !defined(CONFIG_IWLWIFI_DEVICE_TRACING)

#define trace_iwlmei_sap_cmd(...)
#define trace_iwlmei_me_msg(...)

#else

#if !defined(__IWLWIFI_DEVICE_TRACE_IWLWIFI_SAP_CMD) || defined(TRACE_HEADER_MULTI_READ)
#define __IWLWIFI_DEVICE_TRACE_IWLWIFI_SAP_CMD

#include <linux/tracepoint.h>

#undef TRACE_SYSTEM
#define TRACE_SYSTEM iwlmei_sap_cmd

#include "mei/sap.h"

TRACE_EVENT(iwlmei_sap_cmd,
	TP_PROTO(const struct iwl_sap_hdr *sap_cmd, bool tx),
	TP_ARGS(sap_cmd, tx),
	TP_STRUCT__entry(
		__dynamic_array(u8, cmd,
				le16_to_cpu(sap_cmd->len) + sizeof(*sap_cmd))
		__field(u8, tx)
		__field(u16, type)
		__field(u16, len)
		__field(u32, seq)
	),
	TP_fast_assign(
		memcpy(__get_dynamic_array(cmd), sap_cmd,
		       le16_to_cpu(sap_cmd->len) + sizeof(*sap_cmd));
		__entry->tx = tx;
		__entry->type = le16_to_cpu(sap_cmd->type);
		__entry->len = le16_to_cpu(sap_cmd->len);
		__entry->seq = le32_to_cpu(sap_cmd->seq_num);
	),
	TP_printk("sap_cmd %s: type %d len %d seq %d", __entry->tx ? "Tx" : "Rx",
		  __entry->type, __entry->len, __entry->seq)
);

TRACE_EVENT(iwlmei_me_msg,
	TP_PROTO(const struct iwl_sap_me_msg_hdr *hdr, bool tx),
	TP_ARGS(hdr, tx),
	TP_STRUCT__entry(
		__field(u8, type)
		__field(u8, tx)
		__field(u32, seq_num)
	),
	TP_fast_assign(
		__entry->type = le32_to_cpu(hdr->type);
		__entry->seq_num = le32_to_cpu(hdr->seq_num);
		__entry->tx = tx;
	),
	TP_printk("ME message: %s: type %d seq %d", __entry->tx ? "Tx" : "Rx",
		  __entry->type, __entry->seq_num)
);

/*
 * If you add something here, add a stub in case
 * !defined(CONFIG_IWLWIFI_DEVICE_TRACING)
 */

#endif /* __IWLWIFI_DEVICE_TRACE_IWLWIFI_SAP_CMD */

#undef TRACE_INCLUDE_PATH
#define TRACE_INCLUDE_PATH .
#undef TRACE_INCLUDE_FILE
#define TRACE_INCLUDE_FILE trace
#include <trace/define_trace.h>

#endif /* CONFIG_IWLWIFI_DEVICE_TRACING */
