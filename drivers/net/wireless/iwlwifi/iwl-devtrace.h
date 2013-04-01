/******************************************************************************
 *
 * Copyright(c) 2009 - 2013 Intel Corporation. All rights reserved.
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

#if !defined(__IWLWIFI_DEVICE_TRACE) || defined(TRACE_HEADER_MULTI_READ)
#include <linux/skbuff.h>
#include <linux/ieee80211.h>
#include <net/cfg80211.h>
#include "iwl-trans.h"
#if !defined(__IWLWIFI_DEVICE_TRACE)
static inline bool iwl_trace_data(struct sk_buff *skb)
{
	struct ieee80211_hdr *hdr = (void *)skb->data;

	if (ieee80211_is_data(hdr->frame_control))
		return skb->protocol != cpu_to_be16(ETH_P_PAE);
	return false;
}

static inline size_t iwl_rx_trace_len(const struct iwl_trans *trans,
				      void *rxbuf, size_t len)
{
	struct iwl_cmd_header *cmd = (void *)((u8 *)rxbuf + sizeof(__le32));
	struct ieee80211_hdr *hdr;

	if (cmd->cmd != trans->rx_mpdu_cmd)
		return len;

	hdr = (void *)((u8 *)cmd + sizeof(struct iwl_cmd_header) +
			trans->rx_mpdu_cmd_hdr_size);
	if (!ieee80211_is_data(hdr->frame_control))
		return len;
	/* maybe try to identify EAPOL frames? */
	return sizeof(__le32) + sizeof(*cmd) + trans->rx_mpdu_cmd_hdr_size +
		ieee80211_hdrlen(hdr->frame_control);
}
#endif

#define __IWLWIFI_DEVICE_TRACE

#include <linux/tracepoint.h>
#include <linux/device.h>
#include "iwl-trans.h"


#if !defined(CONFIG_IWLWIFI_DEVICE_TRACING) || defined(__CHECKER__)
#undef TRACE_EVENT
#define TRACE_EVENT(name, proto, ...) \
static inline void trace_ ## name(proto) {}
#undef DECLARE_EVENT_CLASS
#define DECLARE_EVENT_CLASS(...)
#undef DEFINE_EVENT
#define DEFINE_EVENT(evt_class, name, proto, ...) \
static inline void trace_ ## name(proto) {}
#endif

#define DEV_ENTRY	__string(dev, dev_name(dev))
#define DEV_ASSIGN	__assign_str(dev, dev_name(dev))

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

#undef TRACE_SYSTEM
#define TRACE_SYSTEM iwlwifi_ucode

TRACE_EVENT(iwlwifi_dev_ucode_cont_event,
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

TRACE_EVENT(iwlwifi_dev_ucode_wrap_event,
	TP_PROTO(const struct device *dev, u32 wraps, u32 n_entry, u32 p_entry),
	TP_ARGS(dev, wraps, n_entry, p_entry),
	TP_STRUCT__entry(
		DEV_ENTRY

		__field(u32, wraps)
		__field(u32, n_entry)
		__field(u32, p_entry)
	),
	TP_fast_assign(
		DEV_ASSIGN;
		__entry->wraps = wraps;
		__entry->n_entry = n_entry;
		__entry->p_entry = p_entry;
	),
	TP_printk("[%s] wraps=#%02d n=0x%X p=0x%X",
		  __get_str(dev), __entry->wraps, __entry->n_entry,
		  __entry->p_entry)
);

#undef TRACE_SYSTEM
#define TRACE_SYSTEM iwlwifi_msg

#define MAX_MSG_LEN	110

DECLARE_EVENT_CLASS(iwlwifi_msg_event,
	TP_PROTO(struct va_format *vaf),
	TP_ARGS(vaf),
	TP_STRUCT__entry(
		__dynamic_array(char, msg, MAX_MSG_LEN)
	),
	TP_fast_assign(
		WARN_ON_ONCE(vsnprintf(__get_dynamic_array(msg),
				       MAX_MSG_LEN, vaf->fmt,
				       *vaf->va) >= MAX_MSG_LEN);
	),
	TP_printk("%s", __get_str(msg))
);

DEFINE_EVENT(iwlwifi_msg_event, iwlwifi_err,
	TP_PROTO(struct va_format *vaf),
	TP_ARGS(vaf)
);

DEFINE_EVENT(iwlwifi_msg_event, iwlwifi_warn,
	TP_PROTO(struct va_format *vaf),
	TP_ARGS(vaf)
);

DEFINE_EVENT(iwlwifi_msg_event, iwlwifi_info,
	TP_PROTO(struct va_format *vaf),
	TP_ARGS(vaf)
);

DEFINE_EVENT(iwlwifi_msg_event, iwlwifi_crit,
	TP_PROTO(struct va_format *vaf),
	TP_ARGS(vaf)
);

TRACE_EVENT(iwlwifi_dbg,
	TP_PROTO(u32 level, bool in_interrupt, const char *function,
		 struct va_format *vaf),
	TP_ARGS(level, in_interrupt, function, vaf),
	TP_STRUCT__entry(
		__field(u32, level)
		__field(u8, in_interrupt)
		__string(function, function)
		__dynamic_array(char, msg, MAX_MSG_LEN)
	),
	TP_fast_assign(
		__entry->level = level;
		__entry->in_interrupt = in_interrupt;
		__assign_str(function, function);
		WARN_ON_ONCE(vsnprintf(__get_dynamic_array(msg),
				       MAX_MSG_LEN, vaf->fmt,
				       *vaf->va) >= MAX_MSG_LEN);
	),
	TP_printk("%s", (char *)__get_dynamic_array(msg))
);

#undef TRACE_SYSTEM
#define TRACE_SYSTEM iwlwifi_data

TRACE_EVENT(iwlwifi_dev_tx_data,
	TP_PROTO(const struct device *dev,
		 struct sk_buff *skb,
		 void *data, size_t data_len),
	TP_ARGS(dev, skb, data, data_len),
	TP_STRUCT__entry(
		DEV_ENTRY

		__dynamic_array(u8, data, iwl_trace_data(skb) ? data_len : 0)
	),
	TP_fast_assign(
		DEV_ASSIGN;
		if (iwl_trace_data(skb))
			memcpy(__get_dynamic_array(data), data, data_len);
	),
	TP_printk("[%s] TX frame data", __get_str(dev))
);

TRACE_EVENT(iwlwifi_dev_rx_data,
	TP_PROTO(const struct device *dev,
		 const struct iwl_trans *trans,
		 void *rxbuf, size_t len),
	TP_ARGS(dev, trans, rxbuf, len),
	TP_STRUCT__entry(
		DEV_ENTRY

		__dynamic_array(u8, data,
				len - iwl_rx_trace_len(trans, rxbuf, len))
	),
	TP_fast_assign(
		size_t offs = iwl_rx_trace_len(trans, rxbuf, len);
		DEV_ASSIGN;
		if (offs < len)
			memcpy(__get_dynamic_array(data),
			       ((u8 *)rxbuf) + offs, len - offs);
	),
	TP_printk("[%s] RX frame data", __get_str(dev))
);

#undef TRACE_SYSTEM
#define TRACE_SYSTEM iwlwifi

TRACE_EVENT(iwlwifi_dev_hcmd,
	TP_PROTO(const struct device *dev,
		 struct iwl_host_cmd *cmd, u16 total_size,
		 const void *hdr, size_t hdr_len),
	TP_ARGS(dev, cmd, total_size, hdr, hdr_len),
	TP_STRUCT__entry(
		DEV_ENTRY
		__dynamic_array(u8, hcmd, total_size)
		__field(u32, flags)
	),
	TP_fast_assign(
		int i, offset = hdr_len;

		DEV_ASSIGN;
		__entry->flags = cmd->flags;
		memcpy(__get_dynamic_array(hcmd), hdr, hdr_len);

		for (i = 0; i < IWL_MAX_CMD_TFDS; i++) {
			if (!cmd->len[i])
				continue;
			if (!(cmd->dataflags[i] & IWL_HCMD_DFL_NOCOPY))
				continue;
			memcpy((u8 *)__get_dynamic_array(hcmd) + offset,
			       cmd->data[i], cmd->len[i]);
			offset += cmd->len[i];
		}
	),
	TP_printk("[%s] hcmd %#.2x (%ssync)",
		  __get_str(dev), ((u8 *)__get_dynamic_array(hcmd))[0],
		  __entry->flags & CMD_ASYNC ? "a" : "")
);

TRACE_EVENT(iwlwifi_dev_rx,
	TP_PROTO(const struct device *dev, const struct iwl_trans *trans,
		 void *rxbuf, size_t len),
	TP_ARGS(dev, trans, rxbuf, len),
	TP_STRUCT__entry(
		DEV_ENTRY
		__dynamic_array(u8, rxbuf, iwl_rx_trace_len(trans, rxbuf, len))
	),
	TP_fast_assign(
		DEV_ASSIGN;
		memcpy(__get_dynamic_array(rxbuf), rxbuf,
		       iwl_rx_trace_len(trans, rxbuf, len));
	),
	TP_printk("[%s] RX cmd %#.2x",
		  __get_str(dev), ((u8 *)__get_dynamic_array(rxbuf))[4])
);

TRACE_EVENT(iwlwifi_dev_tx,
	TP_PROTO(const struct device *dev, struct sk_buff *skb,
		 void *tfd, size_t tfdlen,
		 void *buf0, size_t buf0_len,
		 void *buf1, size_t buf1_len),
	TP_ARGS(dev, skb, tfd, tfdlen, buf0, buf0_len, buf1, buf1_len),
	TP_STRUCT__entry(
		DEV_ENTRY

		__field(size_t, framelen)
		__dynamic_array(u8, tfd, tfdlen)

		/*
		 * Do not insert between or below these items,
		 * we want to keep the frame together (except
		 * for the possible padding).
		 */
		__dynamic_array(u8, buf0, buf0_len)
		__dynamic_array(u8, buf1, iwl_trace_data(skb) ? 0 : buf1_len)
	),
	TP_fast_assign(
		DEV_ASSIGN;
		__entry->framelen = buf0_len + buf1_len;
		memcpy(__get_dynamic_array(tfd), tfd, tfdlen);
		memcpy(__get_dynamic_array(buf0), buf0, buf0_len);
		if (!iwl_trace_data(skb))
			memcpy(__get_dynamic_array(buf1), buf1, buf1_len);
	),
	TP_printk("[%s] TX %.2x (%zu bytes)",
		  __get_str(dev), ((u8 *)__get_dynamic_array(buf0))[0],
		  __entry->framelen)
);

TRACE_EVENT(iwlwifi_dev_ucode_error,
	TP_PROTO(const struct device *dev, u32 desc, u32 tsf_low,
		 u32 data1, u32 data2, u32 line, u32 blink1,
		 u32 blink2, u32 ilink1, u32 ilink2, u32 bcon_time,
		 u32 gp1, u32 gp2, u32 gp3, u32 ucode_ver, u32 hw_ver,
		 u32 brd_ver),
	TP_ARGS(dev, desc, tsf_low, data1, data2, line,
		blink1, blink2, ilink1, ilink2, bcon_time, gp1, gp2,
		gp3, ucode_ver, hw_ver, brd_ver),
	TP_STRUCT__entry(
		DEV_ENTRY
		__field(u32, desc)
		__field(u32, tsf_low)
		__field(u32, data1)
		__field(u32, data2)
		__field(u32, line)
		__field(u32, blink1)
		__field(u32, blink2)
		__field(u32, ilink1)
		__field(u32, ilink2)
		__field(u32, bcon_time)
		__field(u32, gp1)
		__field(u32, gp2)
		__field(u32, gp3)
		__field(u32, ucode_ver)
		__field(u32, hw_ver)
		__field(u32, brd_ver)
	),
	TP_fast_assign(
		DEV_ASSIGN;
		__entry->desc = desc;
		__entry->tsf_low = tsf_low;
		__entry->data1 = data1;
		__entry->data2 = data2;
		__entry->line = line;
		__entry->blink1 = blink1;
		__entry->blink2 = blink2;
		__entry->ilink1 = ilink1;
		__entry->ilink2 = ilink2;
		__entry->bcon_time = bcon_time;
		__entry->gp1 = gp1;
		__entry->gp2 = gp2;
		__entry->gp3 = gp3;
		__entry->ucode_ver = ucode_ver;
		__entry->hw_ver = hw_ver;
		__entry->brd_ver = brd_ver;
	),
	TP_printk("[%s] #%02d %010u data 0x%08X 0x%08X line %u, "
		  "blink 0x%05X 0x%05X ilink 0x%05X 0x%05X "
		  "bcon_tm %010u gp 0x%08X 0x%08X 0x%08X uCode 0x%08X "
		  "hw 0x%08X brd 0x%08X",
		  __get_str(dev), __entry->desc, __entry->tsf_low,
		  __entry->data1,
		  __entry->data2, __entry->line, __entry->blink1,
		  __entry->blink2, __entry->ilink1, __entry->ilink2,
		  __entry->bcon_time, __entry->gp1, __entry->gp2,
		  __entry->gp3, __entry->ucode_ver, __entry->hw_ver,
		  __entry->brd_ver)
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
#endif /* __IWLWIFI_DEVICE_TRACE */

#undef TRACE_INCLUDE_PATH
#define TRACE_INCLUDE_PATH .
#undef TRACE_INCLUDE_FILE
#define TRACE_INCLUDE_FILE iwl-devtrace
#include <trace/define_trace.h>
