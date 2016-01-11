/******************************************************************************
 *
 * Copyright(c) 2009 - 2014 Intel Corporation. All rights reserved.
 * Copyright(c) 2015 Intel Mobile Communications GmbH
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
	TP_PROTO(const struct device *dev, const struct iwl_trans *trans,
		 struct iwl_rx_packet *pkt, size_t len),
	TP_ARGS(dev, trans, pkt, len),
	TP_STRUCT__entry(
		DEV_ENTRY
		__field(u8, cmd)
		__dynamic_array(u8, rxbuf, iwl_rx_trace_len(trans, pkt, len))
	),
	TP_fast_assign(
		DEV_ASSIGN;
		__entry->cmd = pkt->hdr.cmd;
		memcpy(__get_dynamic_array(rxbuf), pkt,
		       iwl_rx_trace_len(trans, pkt, len));
	),
	TP_printk("[%s] RX cmd %#.2x",
		  __get_str(dev), __entry->cmd)
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
		 u32 gp1, u32 gp2, u32 gp3, u32 major, u32 minor, u32 hw_ver,
		 u32 brd_ver),
	TP_ARGS(dev, desc, tsf_low, data1, data2, line,
		blink1, blink2, ilink1, ilink2, bcon_time, gp1, gp2,
		gp3, major, minor, hw_ver, brd_ver),
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
		__field(u32, major)
		__field(u32, minor)
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
		__entry->major = major;
		__entry->minor = minor;
		__entry->hw_ver = hw_ver;
		__entry->brd_ver = brd_ver;
	),
	TP_printk("[%s] #%02d %010u data 0x%08X 0x%08X line %u, "
		  "blink 0x%05X 0x%05X ilink 0x%05X 0x%05X "
		  "bcon_tm %010u gp 0x%08X 0x%08X 0x%08X major 0x%08X "
		  "minor 0x%08X hw 0x%08X brd 0x%08X",
		  __get_str(dev), __entry->desc, __entry->tsf_low,
		  __entry->data1,
		  __entry->data2, __entry->line, __entry->blink1,
		  __entry->blink2, __entry->ilink1, __entry->ilink2,
		  __entry->bcon_time, __entry->gp1, __entry->gp2,
		  __entry->gp3, __entry->major, __entry->minor,
		  __entry->hw_ver, __entry->brd_ver)
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
