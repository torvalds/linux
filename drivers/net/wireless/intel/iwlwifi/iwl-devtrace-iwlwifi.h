/* SPDX-License-Identifier: GPL-2.0-only */
/******************************************************************************
 *
 * Copyright(c) 2009 - 2014 Intel Corporation. All rights reserved.
 * Copyright(c) 2015 Intel Mobile Communications GmbH
 * Copyright(c) 2016 - 2017 Intel Deutschland GmbH
 * Copyright(c) 2018, 2023-2024  Intel Corporation
 *****************************************************************************/

#if !defined(__IWLWIFI_DEVICE_TRACE_IWLWIFI) || defined(TRACE_HEADER_MULTI_READ)
#define __IWLWIFI_DEVICE_TRACE_IWLWIFI

#include <linux/tracepoint.h>

#undef TRACE_SYSTEM
#define TRACE_SYSTEM iwlwifi

TRACE_EVENT(iwlwifi_dev_hcmd,
	TP_PROTO(const struct device *dev,
		 struct iwl_host_cmd *cmd, u16 total_size,
		 struct iwl_cmd_header_wide *hdr),
	TP_ARGS(dev, cmd, total_size, hdr),
	TP_STRUCT__entry(
		DEV_ENTRY
		__dynamic_array(u8, hcmd, total_size)
		__field(u32, flags)
	),
	TP_fast_assign(
		int i, offset = sizeof(struct iwl_cmd_header);

		if (hdr->group_id)
			offset = sizeof(struct iwl_cmd_header_wide);

		DEV_ASSIGN;
		__entry->flags = cmd->flags;
		memcpy(__get_dynamic_array(hcmd), hdr, offset);

		for (i = 0; i < IWL_MAX_CMD_TBS_PER_TFD; i++) {
			if (!cmd->len[i])
				continue;
			memcpy((u8 *)__get_dynamic_array(hcmd) + offset,
			       cmd->data[i], cmd->len[i]);
			offset += cmd->len[i];
		}
	),
	TP_printk("[%s] hcmd %#.2x.%#.2x (%ssync)",
		  __get_str(dev), ((u8 *)__get_dynamic_array(hcmd))[1],
		  ((u8 *)__get_dynamic_array(hcmd))[0],
		  __entry->flags & CMD_ASYNC ? "a" : "")
);

TRACE_EVENT(iwlwifi_dev_rx,
	TP_PROTO(const struct device *dev,
		 struct iwl_rx_packet *pkt, size_t len, size_t trace_len,
		 size_t hdr_offset),
	TP_ARGS(dev, pkt, len, trace_len, hdr_offset),
	TP_STRUCT__entry(
		DEV_ENTRY
		__field(u16, cmd)
		__field(u8, hdr_offset)
		__dynamic_array(u8, rxbuf, trace_len)
	),
	TP_fast_assign(
		DEV_ASSIGN;
		__entry->cmd = WIDE_ID(pkt->hdr.group_id, pkt->hdr.cmd);
		memcpy(__get_dynamic_array(rxbuf), pkt, trace_len);
		__entry->hdr_offset = hdr_offset;
	),
	TP_printk("[%s] RX cmd %#.2x",
		  __get_str(dev), __entry->cmd)
);

TRACE_EVENT(iwlwifi_dev_tx,
	TP_PROTO(const struct device *dev, struct sk_buff *skb,
		 void *tfd, size_t tfdlen,
		 void *buf0, size_t buf0_len,
		 int hdr_len),
	TP_ARGS(dev, skb, tfd, tfdlen, buf0, buf0_len, hdr_len),
	TP_STRUCT__entry(
		DEV_ENTRY
		__field(void *, skbaddr)
		__field(size_t, framelen)
		__dynamic_array(u8, tfd, tfdlen)

		/*
		 * Do not insert between or below these items,
		 * we want to keep the frame together (except
		 * for the possible padding).
		 */
		__dynamic_array(u8, buf0, buf0_len)
		__dynamic_array(u8, buf1, hdr_len > 0 && !iwl_trace_data(skb) ?
						skb->len - hdr_len : 0)
	),
	TP_fast_assign(
		DEV_ASSIGN;
		__entry->skbaddr = skb;
		__entry->framelen = buf0_len;
		if (hdr_len > 0)
			__entry->framelen += skb->len - hdr_len;
		memcpy(__get_dynamic_array(tfd), tfd, tfdlen);
		memcpy(__get_dynamic_array(buf0), buf0, buf0_len);
		if (__get_dynamic_array_len(buf1))
			skb_copy_bits(skb, hdr_len,
				      __get_dynamic_array(buf1),
				      skb->len - hdr_len);
	),
	TP_printk("[%s] TX %.2x (%zu bytes) skbaddr=%p",
		  __get_str(dev), ((u8 *)__get_dynamic_array(buf0))[0],
		  __entry->framelen, __entry->skbaddr)
);

TRACE_EVENT(iwlwifi_dev_ucode_event,
	TP_PROTO(const struct device *dev, u32 time, u32 data, u32 ev),
	TP_ARGS(dev, time, data, ev),
	TP_STRUCT__entry(
		DEV_ENTRY

		__field(u32, time)
		__field(u32, data)
		__field(u32, ev)
	),
	TP_fast_assign(
		DEV_ASSIGN;
		__entry->time = time;
		__entry->data = data;
		__entry->ev = ev;
	),
	TP_printk("[%s] EVT_LOGT:%010u:0x%08x:%04u",
		  __get_str(dev), __entry->time, __entry->data, __entry->ev)
);
#endif /* __IWLWIFI_DEVICE_TRACE_IWLWIFI */

#undef TRACE_INCLUDE_PATH
#define TRACE_INCLUDE_PATH .
#undef TRACE_INCLUDE_FILE
#define TRACE_INCLUDE_FILE iwl-devtrace-iwlwifi
#include <trace/define_trace.h>
