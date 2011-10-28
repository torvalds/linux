/******************************************************************************
 *
 * Copyright(c) 2009 - 2011 Intel Corporation. All rights reserved.
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

#if !defined(__IWLWIFI_LEGACY_DEVICE_TRACE) || defined(TRACE_HEADER_MULTI_READ)
#define __IWLWIFI_LEGACY_DEVICE_TRACE

#include <linux/tracepoint.h>

#if !defined(CONFIG_IWLWIFI_LEGACY_DEVICE_TRACING) || defined(__CHECKER__)
#undef TRACE_EVENT
#define TRACE_EVENT(name, proto, ...) \
static inline void trace_ ## name(proto) {}
#endif


#define PRIV_ENTRY	__field(struct iwl_priv *, priv)
#define PRIV_ASSIGN	(__entry->priv = priv)

#undef TRACE_SYSTEM
#define TRACE_SYSTEM iwlwifi_legacy_io

TRACE_EVENT(iwlwifi_legacy_dev_ioread32,
	TP_PROTO(struct iwl_priv *priv, u32 offs, u32 val),
	TP_ARGS(priv, offs, val),
	TP_STRUCT__entry(
		PRIV_ENTRY
		__field(u32, offs)
		__field(u32, val)
	),
	TP_fast_assign(
		PRIV_ASSIGN;
		__entry->offs = offs;
		__entry->val = val;
	),
	TP_printk("[%p] read io[%#x] = %#x", __entry->priv,
					__entry->offs, __entry->val)
);

TRACE_EVENT(iwlwifi_legacy_dev_iowrite8,
	TP_PROTO(struct iwl_priv *priv, u32 offs, u8 val),
	TP_ARGS(priv, offs, val),
	TP_STRUCT__entry(
		PRIV_ENTRY
		__field(u32, offs)
		__field(u8, val)
	),
	TP_fast_assign(
		PRIV_ASSIGN;
		__entry->offs = offs;
		__entry->val = val;
	),
	TP_printk("[%p] write io[%#x] = %#x)", __entry->priv,
					__entry->offs, __entry->val)
);

TRACE_EVENT(iwlwifi_legacy_dev_iowrite32,
	TP_PROTO(struct iwl_priv *priv, u32 offs, u32 val),
	TP_ARGS(priv, offs, val),
	TP_STRUCT__entry(
		PRIV_ENTRY
		__field(u32, offs)
		__field(u32, val)
	),
	TP_fast_assign(
		PRIV_ASSIGN;
		__entry->offs = offs;
		__entry->val = val;
	),
	TP_printk("[%p] write io[%#x] = %#x)", __entry->priv,
					__entry->offs, __entry->val)
);

#undef TRACE_SYSTEM
#define TRACE_SYSTEM iwlwifi_legacy_ucode

TRACE_EVENT(iwlwifi_legacy_dev_ucode_cont_event,
	TP_PROTO(struct iwl_priv *priv, u32 time, u32 data, u32 ev),
	TP_ARGS(priv, time, data, ev),
	TP_STRUCT__entry(
		PRIV_ENTRY

		__field(u32, time)
		__field(u32, data)
		__field(u32, ev)
	),
	TP_fast_assign(
		PRIV_ASSIGN;
		__entry->time = time;
		__entry->data = data;
		__entry->ev = ev;
	),
	TP_printk("[%p] EVT_LOGT:%010u:0x%08x:%04u",
		  __entry->priv, __entry->time, __entry->data, __entry->ev)
);

TRACE_EVENT(iwlwifi_legacy_dev_ucode_wrap_event,
	TP_PROTO(struct iwl_priv *priv, u32 wraps, u32 n_entry, u32 p_entry),
	TP_ARGS(priv, wraps, n_entry, p_entry),
	TP_STRUCT__entry(
		PRIV_ENTRY

		__field(u32, wraps)
		__field(u32, n_entry)
		__field(u32, p_entry)
	),
	TP_fast_assign(
		PRIV_ASSIGN;
		__entry->wraps = wraps;
		__entry->n_entry = n_entry;
		__entry->p_entry = p_entry;
	),
	TP_printk("[%p] wraps=#%02d n=0x%X p=0x%X",
		  __entry->priv, __entry->wraps, __entry->n_entry,
		  __entry->p_entry)
);

#undef TRACE_SYSTEM
#define TRACE_SYSTEM iwlwifi

TRACE_EVENT(iwlwifi_legacy_dev_hcmd,
	TP_PROTO(struct iwl_priv *priv, void *hcmd, size_t len, u32 flags),
	TP_ARGS(priv, hcmd, len, flags),
	TP_STRUCT__entry(
		PRIV_ENTRY
		__dynamic_array(u8, hcmd, len)
		__field(u32, flags)
	),
	TP_fast_assign(
		PRIV_ASSIGN;
		memcpy(__get_dynamic_array(hcmd), hcmd, len);
		__entry->flags = flags;
	),
	TP_printk("[%p] hcmd %#.2x (%ssync)",
		  __entry->priv, ((u8 *)__get_dynamic_array(hcmd))[0],
		  __entry->flags & CMD_ASYNC ? "a" : "")
);

TRACE_EVENT(iwlwifi_legacy_dev_rx,
	TP_PROTO(struct iwl_priv *priv, void *rxbuf, size_t len),
	TP_ARGS(priv, rxbuf, len),
	TP_STRUCT__entry(
		PRIV_ENTRY
		__dynamic_array(u8, rxbuf, len)
	),
	TP_fast_assign(
		PRIV_ASSIGN;
		memcpy(__get_dynamic_array(rxbuf), rxbuf, len);
	),
	TP_printk("[%p] RX cmd %#.2x",
		  __entry->priv, ((u8 *)__get_dynamic_array(rxbuf))[4])
);

TRACE_EVENT(iwlwifi_legacy_dev_tx,
	TP_PROTO(struct iwl_priv *priv, void *tfd, size_t tfdlen,
		 void *buf0, size_t buf0_len,
		 void *buf1, size_t buf1_len),
	TP_ARGS(priv, tfd, tfdlen, buf0, buf0_len, buf1, buf1_len),
	TP_STRUCT__entry(
		PRIV_ENTRY

		__field(size_t, framelen)
		__dynamic_array(u8, tfd, tfdlen)

		/*
		 * Do not insert between or below these items,
		 * we want to keep the frame together (except
		 * for the possible padding).
		 */
		__dynamic_array(u8, buf0, buf0_len)
		__dynamic_array(u8, buf1, buf1_len)
	),
	TP_fast_assign(
		PRIV_ASSIGN;
		__entry->framelen = buf0_len + buf1_len;
		memcpy(__get_dynamic_array(tfd), tfd, tfdlen);
		memcpy(__get_dynamic_array(buf0), buf0, buf0_len);
		memcpy(__get_dynamic_array(buf1), buf1, buf1_len);
	),
	TP_printk("[%p] TX %.2x (%zu bytes)",
		  __entry->priv,
		  ((u8 *)__get_dynamic_array(buf0))[0],
		  __entry->framelen)
);

TRACE_EVENT(iwlwifi_legacy_dev_ucode_error,
	TP_PROTO(struct iwl_priv *priv, u32 desc, u32 time,
		 u32 data1, u32 data2, u32 line, u32 blink1,
		 u32 blink2, u32 ilink1, u32 ilink2),
	TP_ARGS(priv, desc, time, data1, data2, line,
		blink1, blink2, ilink1, ilink2),
	TP_STRUCT__entry(
		PRIV_ENTRY
		__field(u32, desc)
		__field(u32, time)
		__field(u32, data1)
		__field(u32, data2)
		__field(u32, line)
		__field(u32, blink1)
		__field(u32, blink2)
		__field(u32, ilink1)
		__field(u32, ilink2)
	),
	TP_fast_assign(
		PRIV_ASSIGN;
		__entry->desc = desc;
		__entry->time = time;
		__entry->data1 = data1;
		__entry->data2 = data2;
		__entry->line = line;
		__entry->blink1 = blink1;
		__entry->blink2 = blink2;
		__entry->ilink1 = ilink1;
		__entry->ilink2 = ilink2;
	),
	TP_printk("[%p] #%02d %010u data 0x%08X 0x%08X line %u, "
		  "blink 0x%05X 0x%05X ilink 0x%05X 0x%05X",
		  __entry->priv, __entry->desc, __entry->time, __entry->data1,
		  __entry->data2, __entry->line, __entry->blink1,
		  __entry->blink2, __entry->ilink1, __entry->ilink2)
);

TRACE_EVENT(iwlwifi_legacy_dev_ucode_event,
	TP_PROTO(struct iwl_priv *priv, u32 time, u32 data, u32 ev),
	TP_ARGS(priv, time, data, ev),
	TP_STRUCT__entry(
		PRIV_ENTRY

		__field(u32, time)
		__field(u32, data)
		__field(u32, ev)
	),
	TP_fast_assign(
		PRIV_ASSIGN;
		__entry->time = time;
		__entry->data = data;
		__entry->ev = ev;
	),
	TP_printk("[%p] EVT_LOGT:%010u:0x%08x:%04u",
		  __entry->priv, __entry->time, __entry->data, __entry->ev)
);
#endif /* __IWLWIFI_DEVICE_TRACE */

#undef TRACE_INCLUDE_PATH
#define TRACE_INCLUDE_PATH .
#undef TRACE_INCLUDE_FILE
#define TRACE_INCLUDE_FILE iwl-devtrace
#include <trace/define_trace.h>
