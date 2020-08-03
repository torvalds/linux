/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Tracepoints definitions.
 *
 * Copyright (c) 2018-2019, Silicon Laboratories, Inc.
 */

#undef TRACE_SYSTEM
#define TRACE_SYSTEM wfx

#if !defined(_WFX_TRACE_H) || defined(TRACE_HEADER_MULTI_READ)
#define _WFX_TRACE_H

#include <linux/tracepoint.h>
#include <net/mac80211.h>

#include "bus.h"
#include "hif_api_cmd.h"
#include "hif_api_mib.h"

/* The hell below need some explanations. For each symbolic number, we need to
 * define it with TRACE_DEFINE_ENUM() and in a list for __print_symbolic.
 *
 *   1. Define a new macro that call TRACE_DEFINE_ENUM():
 *
 *          #define xxx_name(sym) TRACE_DEFINE_ENUM(sym);
 *
 *   2. Define list of all symbols:
 *
 *          #define list_names     \
 *             ...                 \
 *             xxx_name(XXX)       \
 *             ...
 *
 *   3. Instantiate that list_names:
 *
 *          list_names
 *
 *   4. Redefine xxx_name() as an entry of array for __print_symbolic()
 *
 *          #undef xxx_name
 *          #define xxx_name(msg) { msg, #msg },
 *
 *   5. list_name can now nearly be used with __print_symbolic() but,
 *      __print_symbolic() dislike last comma of list. So we define a new list
 *      with a dummy element:
 *
 *          #define list_for_print_symbolic list_names { -1, NULL }
 */

#define _hif_msg_list                       \
	hif_cnf_name(ADD_KEY)               \
	hif_cnf_name(BEACON_TRANSMIT)       \
	hif_cnf_name(EDCA_QUEUE_PARAMS)     \
	hif_cnf_name(JOIN)                  \
	hif_cnf_name(MAP_LINK)              \
	hif_cnf_name(READ_MIB)              \
	hif_cnf_name(REMOVE_KEY)            \
	hif_cnf_name(RESET)                 \
	hif_cnf_name(SET_BSS_PARAMS)        \
	hif_cnf_name(SET_PM_MODE)           \
	hif_cnf_name(START)                 \
	hif_cnf_name(START_SCAN)            \
	hif_cnf_name(STOP_SCAN)             \
	hif_cnf_name(TX)                    \
	hif_cnf_name(MULTI_TRANSMIT)        \
	hif_cnf_name(UPDATE_IE)             \
	hif_cnf_name(WRITE_MIB)             \
	hif_cnf_name(CONFIGURATION)         \
	hif_cnf_name(CONTROL_GPIO)          \
	hif_cnf_name(PREVENT_ROLLBACK)      \
	hif_cnf_name(SET_SL_MAC_KEY)        \
	hif_cnf_name(SL_CONFIGURE)          \
	hif_cnf_name(SL_EXCHANGE_PUB_KEYS)  \
	hif_cnf_name(SHUT_DOWN)             \
	hif_ind_name(EVENT)                 \
	hif_ind_name(JOIN_COMPLETE)         \
	hif_ind_name(RX)                    \
	hif_ind_name(SCAN_CMPL)             \
	hif_ind_name(SET_PM_MODE_CMPL)      \
	hif_ind_name(SUSPEND_RESUME_TX)     \
	hif_ind_name(SL_EXCHANGE_PUB_KEYS)  \
	hif_ind_name(ERROR)                 \
	hif_ind_name(EXCEPTION)             \
	hif_ind_name(GENERIC)               \
	hif_ind_name(WAKEUP)                \
	hif_ind_name(STARTUP)

#define hif_msg_list_enum _hif_msg_list

#undef hif_cnf_name
#undef hif_ind_name
#define hif_cnf_name(msg) TRACE_DEFINE_ENUM(HIF_CNF_ID_##msg);
#define hif_ind_name(msg) TRACE_DEFINE_ENUM(HIF_IND_ID_##msg);
hif_msg_list_enum
#undef hif_cnf_name
#undef hif_ind_name
#define hif_cnf_name(msg) { HIF_CNF_ID_##msg, #msg },
#define hif_ind_name(msg) { HIF_IND_ID_##msg, #msg },
#define hif_msg_list hif_msg_list_enum { -1, NULL }

#define _hif_mib_list                                \
	hif_mib_name(ARP_IP_ADDRESSES_TABLE)         \
	hif_mib_name(ARP_KEEP_ALIVE_PERIOD)          \
	hif_mib_name(BEACON_FILTER_ENABLE)           \
	hif_mib_name(BEACON_FILTER_TABLE)            \
	hif_mib_name(BEACON_STATS)                   \
	hif_mib_name(BEACON_WAKEUP_PERIOD)           \
	hif_mib_name(BLOCK_ACK_POLICY)               \
	hif_mib_name(CCA_CONFIG)                     \
	hif_mib_name(CONFIG_DATA_FILTER)             \
	hif_mib_name(COUNTERS_TABLE)                 \
	hif_mib_name(CURRENT_TX_POWER_LEVEL)         \
	hif_mib_name(DOT11_MAC_ADDRESS)              \
	hif_mib_name(DOT11_MAX_RECEIVE_LIFETIME)     \
	hif_mib_name(DOT11_MAX_TRANSMIT_MSDU_LIFETIME) \
	hif_mib_name(DOT11_RTS_THRESHOLD)            \
	hif_mib_name(DOT11_WEP_DEFAULT_KEY_ID)       \
	hif_mib_name(ETHERTYPE_DATAFRAME_CONDITION)  \
	hif_mib_name(EXTENDED_COUNTERS_TABLE)        \
	hif_mib_name(GL_BLOCK_ACK_INFO)              \
	hif_mib_name(GL_OPERATIONAL_POWER_MODE)      \
	hif_mib_name(GL_SET_MULTI_MSG)               \
	hif_mib_name(GRP_SEQ_COUNTER)                \
	hif_mib_name(INACTIVITY_TIMER)               \
	hif_mib_name(INTERFACE_PROTECTION)           \
	hif_mib_name(IPV4_ADDR_DATAFRAME_CONDITION)  \
	hif_mib_name(IPV6_ADDR_DATAFRAME_CONDITION)  \
	hif_mib_name(KEEP_ALIVE_PERIOD)              \
	hif_mib_name(MAC_ADDR_DATAFRAME_CONDITION)   \
	hif_mib_name(MAGIC_DATAFRAME_CONDITION)      \
	hif_mib_name(MAX_TX_POWER_LEVEL)             \
	hif_mib_name(NON_ERP_PROTECTION)             \
	hif_mib_name(NS_IP_ADDRESSES_TABLE)          \
	hif_mib_name(OVERRIDE_INTERNAL_TX_RATE)      \
	hif_mib_name(PORT_DATAFRAME_CONDITION)       \
	hif_mib_name(PROTECTED_MGMT_POLICY)          \
	hif_mib_name(RCPI_RSSI_THRESHOLD)            \
	hif_mib_name(RX_FILTER)                      \
	hif_mib_name(SET_ASSOCIATION_MODE)           \
	hif_mib_name(SET_DATA_FILTERING)             \
	hif_mib_name(SET_HT_PROTECTION)              \
	hif_mib_name(SET_TX_RATE_RETRY_POLICY)       \
	hif_mib_name(SET_UAPSD_INFORMATION)          \
	hif_mib_name(SLOT_TIME)                      \
	hif_mib_name(STATISTICS_TABLE)               \
	hif_mib_name(TEMPLATE_FRAME)                 \
	hif_mib_name(TSF_COUNTER)                    \
	hif_mib_name(UC_MC_BC_DATAFRAME_CONDITION)

#define hif_mib_list_enum _hif_mib_list

#undef hif_mib_name
#define hif_mib_name(mib) TRACE_DEFINE_ENUM(HIF_MIB_ID_##mib);
hif_mib_list_enum
#undef hif_mib_name
#define hif_mib_name(mib) { HIF_MIB_ID_##mib, #mib },
#define hif_mib_list hif_mib_list_enum { -1, NULL }

DECLARE_EVENT_CLASS(hif_data,
	TP_PROTO(const struct hif_msg *hif, int tx_fill_level, bool is_recv),
	TP_ARGS(hif, tx_fill_level, is_recv),
	TP_STRUCT__entry(
		__field(int, tx_fill_level)
		__field(int, msg_id)
		__field(const char *, msg_type)
		__field(int, msg_len)
		__field(int, buf_len)
		__field(int, if_id)
		__field(int, mib)
		__array(u8, buf, 128)
	),
	TP_fast_assign(
		int header_len;

		__entry->tx_fill_level = tx_fill_level;
		__entry->msg_len = le16_to_cpu(hif->len);
		__entry->msg_id = hif->id;
		__entry->if_id = hif->interface;
		if (is_recv)
			__entry->msg_type = __entry->msg_id & 0x80 ? "IND" : "CNF";
		else
			__entry->msg_type = "REQ";
		if (!is_recv &&
		    (__entry->msg_id == HIF_REQ_ID_READ_MIB ||
		     __entry->msg_id == HIF_REQ_ID_WRITE_MIB)) {
			__entry->mib = le16_to_cpup((__le16 *)hif->body);
			header_len = 4;
		} else {
			__entry->mib = -1;
			header_len = 0;
		}
		__entry->buf_len = min_t(int, __entry->msg_len,
					 sizeof(__entry->buf))
				   - sizeof(struct hif_msg) - header_len;
		memcpy(__entry->buf, hif->body + header_len, __entry->buf_len);
	),
	TP_printk("%d:%d:%s_%s%s%s: %s%s (%d bytes)",
		__entry->tx_fill_level,
		__entry->if_id,
		__entry->msg_type,
		__print_symbolic(__entry->msg_id, hif_msg_list),
		__entry->mib != -1 ? "/" : "",
		__entry->mib != -1 ? __print_symbolic(__entry->mib, hif_mib_list) : "",
		__print_hex(__entry->buf, __entry->buf_len),
		__entry->msg_len > sizeof(__entry->buf) ? " ..." : "",
		__entry->msg_len
	)
);
DEFINE_EVENT(hif_data, hif_send,
	TP_PROTO(const struct hif_msg *hif, int tx_fill_level, bool is_recv),
	TP_ARGS(hif, tx_fill_level, is_recv));
#define _trace_hif_send(hif, tx_fill_level)\
	trace_hif_send(hif, tx_fill_level, false)
DEFINE_EVENT(hif_data, hif_recv,
	TP_PROTO(const struct hif_msg *hif, int tx_fill_level, bool is_recv),
	TP_ARGS(hif, tx_fill_level, is_recv));
#define _trace_hif_recv(hif, tx_fill_level)\
	trace_hif_recv(hif, tx_fill_level, true)

#define wfx_reg_list_enum                                 \
	wfx_reg_name(WFX_REG_CONFIG,       "CONFIG")      \
	wfx_reg_name(WFX_REG_CONTROL,      "CONTROL")     \
	wfx_reg_name(WFX_REG_IN_OUT_QUEUE, "QUEUE")       \
	wfx_reg_name(WFX_REG_AHB_DPORT,    "AHB")         \
	wfx_reg_name(WFX_REG_BASE_ADDR,    "BASE_ADDR")   \
	wfx_reg_name(WFX_REG_SRAM_DPORT,   "SRAM")        \
	wfx_reg_name(WFX_REG_SET_GEN_R_W,  "SET_GEN_R_W") \
	wfx_reg_name(WFX_REG_FRAME_OUT,    "FRAME_OUT")

#undef wfx_reg_name
#define wfx_reg_name(sym, name) TRACE_DEFINE_ENUM(sym);
wfx_reg_list_enum
#undef wfx_reg_name
#define wfx_reg_name(sym, name) { sym, name },
#define wfx_reg_list wfx_reg_list_enum { -1, NULL }

DECLARE_EVENT_CLASS(io_data,
	TP_PROTO(int reg, int addr, const void *io_buf, size_t len),
	TP_ARGS(reg, addr, io_buf, len),
	TP_STRUCT__entry(
		__field(int, reg)
		__field(int, addr)
		__field(int, msg_len)
		__field(int, buf_len)
		__array(u8, buf, 32)
		__array(u8, addr_str, 10)
	),
	TP_fast_assign(
		__entry->reg = reg;
		__entry->addr = addr;
		__entry->msg_len = len;
		__entry->buf_len = min_t(int, sizeof(__entry->buf),
					 __entry->msg_len);
		memcpy(__entry->buf, io_buf, __entry->buf_len);
		if (addr >= 0)
			snprintf(__entry->addr_str, 10, "/%08x", addr);
		else
			__entry->addr_str[0] = 0;
	),
	TP_printk("%s%s: %s%s (%d bytes)",
		__print_symbolic(__entry->reg, wfx_reg_list),
		__entry->addr_str,
		__print_hex(__entry->buf, __entry->buf_len),
		__entry->msg_len > sizeof(__entry->buf) ? " ..." : "",
		__entry->msg_len
	)
);
DEFINE_EVENT(io_data, io_write,
	TP_PROTO(int reg, int addr, const void *io_buf, size_t len),
	TP_ARGS(reg, addr, io_buf, len));
#define _trace_io_ind_write(reg, addr, io_buf, len)\
	trace_io_write(reg, addr, io_buf, len)
#define _trace_io_write(reg, io_buf, len) trace_io_write(reg, -1, io_buf, len)
DEFINE_EVENT(io_data, io_read,
	TP_PROTO(int reg, int addr, const void *io_buf, size_t len),
	TP_ARGS(reg, addr, io_buf, len));
#define _trace_io_ind_read(reg, addr, io_buf, len)\
	trace_io_read(reg, addr, io_buf, len)
#define _trace_io_read(reg, io_buf, len) trace_io_read(reg, -1, io_buf, len)

DECLARE_EVENT_CLASS(io_data32,
	TP_PROTO(int reg, int addr, u32 val),
	TP_ARGS(reg, addr, val),
	TP_STRUCT__entry(
		__field(int, reg)
		__field(int, addr)
		__field(int, val)
		__array(u8, addr_str, 10)
	),
	TP_fast_assign(
		__entry->reg = reg;
		__entry->addr = addr;
		__entry->val = val;
		if (addr >= 0)
			snprintf(__entry->addr_str, 10, "/%08x", addr);
		else
			__entry->addr_str[0] = 0;
	),
	TP_printk("%s%s: %08x",
		__print_symbolic(__entry->reg, wfx_reg_list),
		__entry->addr_str,
		__entry->val
	)
);
DEFINE_EVENT(io_data32, io_write32,
	TP_PROTO(int reg, int addr, u32 val),
	TP_ARGS(reg, addr, val));
#define _trace_io_ind_write32(reg, addr, val) trace_io_write32(reg, addr, val)
#define _trace_io_write32(reg, val) trace_io_write32(reg, -1, val)
DEFINE_EVENT(io_data32, io_read32,
	TP_PROTO(int reg, int addr, u32 val),
	TP_ARGS(reg, addr, val));
#define _trace_io_ind_read32(reg, addr, val) trace_io_read32(reg, addr, val)
#define _trace_io_read32(reg, val) trace_io_read32(reg, -1, val)

DECLARE_EVENT_CLASS(piggyback,
	TP_PROTO(u32 val, bool ignored),
	TP_ARGS(val, ignored),
	TP_STRUCT__entry(
		__field(int, val)
		__field(bool, ignored)
	),
	TP_fast_assign(
		__entry->val = val;
		__entry->ignored = ignored;
	),
	TP_printk("CONTROL: %08x%s",
		__entry->val,
		__entry->ignored ? " (ignored)" : ""
	)
);
DEFINE_EVENT(piggyback, piggyback,
	TP_PROTO(u32 val, bool ignored),
	TP_ARGS(val, ignored));
#define _trace_piggyback(val, ignored) trace_piggyback(val, ignored)

TRACE_EVENT(bh_stats,
	TP_PROTO(int ind, int req, int cnf, int busy, bool release),
	TP_ARGS(ind, req, cnf, busy, release),
	TP_STRUCT__entry(
		__field(int, ind)
		__field(int, req)
		__field(int, cnf)
		__field(int, busy)
		__field(bool, release)
	),
	TP_fast_assign(
		__entry->ind = ind;
		__entry->req = req;
		__entry->cnf = cnf;
		__entry->busy = busy;
		__entry->release = release;
	),
	TP_printk("IND/REQ/CNF:%3d/%3d/%3d, REQ in progress:%3d, WUP: %s",
		__entry->ind,
		__entry->req,
		__entry->cnf,
		__entry->busy,
		__entry->release ? "release" : "keep"
	)
);
#define _trace_bh_stats(ind, req, cnf, busy, release)\
	trace_bh_stats(ind, req, cnf, busy, release)

TRACE_EVENT(tx_stats,
	TP_PROTO(const struct hif_cnf_tx *tx_cnf, const struct sk_buff *skb,
		 int delay),
	TP_ARGS(tx_cnf, skb, delay),
	TP_STRUCT__entry(
		__field(int, pkt_id)
		__field(int, delay_media)
		__field(int, delay_queue)
		__field(int, delay_fw)
		__field(int, ack_failures)
		__field(int, flags)
		__array(int, rate, 4)
		__array(int, tx_count, 4)
	),
	TP_fast_assign(
		// Keep sync with wfx_rates definition in main.c
		static const int hw_rate[] = { 0, 1, 2, 3, 6, 7, 8, 9,
					       10, 11, 12, 13 };
		const struct ieee80211_tx_info *tx_info =
			(const struct ieee80211_tx_info *)skb->cb;
		const struct ieee80211_tx_rate *rates = tx_info->driver_rates;
		int i;

		__entry->pkt_id = tx_cnf->packet_id;
		__entry->delay_media = le32_to_cpu(tx_cnf->media_delay);
		__entry->delay_queue = le32_to_cpu(tx_cnf->tx_queue_delay);
		__entry->delay_fw = delay;
		__entry->ack_failures = tx_cnf->ack_failures;
		if (!tx_cnf->status || __entry->ack_failures)
			__entry->ack_failures += 1;

		for (i = 0; i < IEEE80211_NUM_ACS; i++) {
			if (rates[0].flags & IEEE80211_TX_RC_MCS)
				__entry->rate[i] = rates[i].idx;
			else
				__entry->rate[i] = hw_rate[rates[i].idx];
			__entry->tx_count[i] = rates[i].count;
		}
		__entry->flags = 0;
		if (rates[0].flags & IEEE80211_TX_RC_MCS)
			__entry->flags |= 0x01;
		if (rates[0].flags & IEEE80211_TX_RC_SHORT_GI)
			__entry->flags |= 0x02;
		if (rates[0].flags & IEEE80211_TX_RC_GREEN_FIELD)
			__entry->flags |= 0x04;
		if (rates[0].flags & IEEE80211_TX_RC_USE_RTS_CTS)
			__entry->flags |= 0x08;
		if (tx_info->flags & IEEE80211_TX_CTL_SEND_AFTER_DTIM)
			__entry->flags |= 0x10;
		if (tx_cnf->status)
			__entry->flags |= 0x20;
		if (tx_cnf->status == HIF_STATUS_TX_FAIL_REQUEUE)
			__entry->flags |= 0x40;
	),
	TP_printk("packet ID: %08x, rate policy: %s %d|%d %d|%d %d|%d %d|%d -> %d attempt, Delays media/queue/total: %4dus/%4dus/%4dus",
		__entry->pkt_id,
		__print_flags(__entry->flags, NULL,
			{ 0x01, "M" }, { 0x02, "S" }, { 0x04, "G" },
			{ 0x08, "R" }, { 0x10, "D" }, { 0x20, "F" },
			{ 0x40, "Q" }),
		__entry->rate[0],
		__entry->tx_count[0],
		__entry->rate[1],
		__entry->tx_count[1],
		__entry->rate[2],
		__entry->tx_count[2],
		__entry->rate[3],
		__entry->tx_count[3],
		__entry->ack_failures,
		__entry->delay_media,
		__entry->delay_queue,
		__entry->delay_fw
	)
);
#define _trace_tx_stats(tx_cnf, skb, delay) trace_tx_stats(tx_cnf, skb, delay)

#endif

/* This part must be outside protection */
#undef TRACE_INCLUDE_PATH
#define TRACE_INCLUDE_PATH .
#undef TRACE_INCLUDE_FILE
#define TRACE_INCLUDE_FILE traces

#include <trace/define_trace.h>
