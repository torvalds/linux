/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2014 Felix Fietkau <nbd@openwrt.org>
 * Copyright (C) 2015 Jakub Kicinski <kubakici@wp.pl>
 */

#if !defined(__MT7601U_TRACE_H) || defined(TRACE_HEADER_MULTI_READ)
#define __MT7601U_TRACE_H

#include <linux/tracepoint.h>
#include "mt7601u.h"
#include "mac.h"

#undef TRACE_SYSTEM
#define TRACE_SYSTEM mt7601u

#define MAXNAME		32
#define DEV_ENTRY	__array(char, wiphy_name, 32)
#define DEV_ASSIGN	strscpy(__entry->wiphy_name,			\
				wiphy_name(dev->hw->wiphy), MAXNAME)
#define DEV_PR_FMT	"%s "
#define DEV_PR_ARG	__entry->wiphy_name

#define REG_ENTRY	__field(u32, reg) __field(u32, val)
#define REG_ASSIGN	__entry->reg = reg; __entry->val = val
#define REG_PR_FMT	"%04x=%08x"
#define REG_PR_ARG	__entry->reg, __entry->val

DECLARE_EVENT_CLASS(dev_reg_evtu,
	TP_PROTO(struct mt7601u_dev *dev, u32 reg, u32 val),
	TP_ARGS(dev, reg, val),
	TP_STRUCT__entry(
		DEV_ENTRY
		REG_ENTRY
	),
	TP_fast_assign(
		DEV_ASSIGN;
		REG_ASSIGN;
	),
	TP_printk(
		DEV_PR_FMT REG_PR_FMT,
		DEV_PR_ARG, REG_PR_ARG
	)
);

DEFINE_EVENT(dev_reg_evtu, reg_read,
	TP_PROTO(struct mt7601u_dev *dev, u32 reg, u32 val),
	TP_ARGS(dev, reg, val)
);

DEFINE_EVENT(dev_reg_evtu, reg_write,
	TP_PROTO(struct mt7601u_dev *dev, u32 reg, u32 val),
	TP_ARGS(dev, reg, val)
);

TRACE_EVENT(mt_submit_urb,
	TP_PROTO(struct mt7601u_dev *dev, struct urb *u),
	TP_ARGS(dev, u),
	TP_STRUCT__entry(
		DEV_ENTRY __field(unsigned, pipe) __field(u32, len)
	),
	TP_fast_assign(
		DEV_ASSIGN;
		__entry->pipe = u->pipe;
		__entry->len = u->transfer_buffer_length;
	),
	TP_printk(DEV_PR_FMT "p:%08x len:%u",
		  DEV_PR_ARG, __entry->pipe, __entry->len)
);

#define trace_mt_submit_urb_sync(__dev, __pipe, __len) ({	\
	struct urb u;					\
	u.pipe = __pipe;				\
	u.transfer_buffer_length = __len;		\
	trace_mt_submit_urb(__dev, &u);			\
})

TRACE_EVENT(mt_mcu_msg_send,
	TP_PROTO(struct mt7601u_dev *dev,
		 struct sk_buff *skb, u32 csum, bool resp),
	TP_ARGS(dev, skb, csum, resp),
	TP_STRUCT__entry(
		DEV_ENTRY
		__field(u32, info)
		__field(u32, csum)
		__field(bool, resp)
	),
	TP_fast_assign(
		DEV_ASSIGN;
		__entry->info = *(u32 *)skb->data;
		__entry->csum = csum;
		__entry->resp = resp;
	),
	TP_printk(DEV_PR_FMT "i:%08x c:%08x r:%d",
		  DEV_PR_ARG, __entry->info, __entry->csum, __entry->resp)
);

TRACE_EVENT(mt_vend_req,
	TP_PROTO(struct mt7601u_dev *dev, unsigned pipe, u8 req, u8 req_type,
		 u16 val, u16 offset, void *buf, size_t buflen, int ret),
	TP_ARGS(dev, pipe, req, req_type, val, offset, buf, buflen, ret),
	TP_STRUCT__entry(
		DEV_ENTRY
		__field(unsigned, pipe) __field(u8, req) __field(u8, req_type)
		__field(u16, val) __field(u16, offset) __field(void*, buf)
		__field(int, buflen) __field(int, ret)
	),
	TP_fast_assign(
		DEV_ASSIGN;
		__entry->pipe = pipe;
		__entry->req = req;
		__entry->req_type = req_type;
		__entry->val = val;
		__entry->offset = offset;
		__entry->buf = buf;
		__entry->buflen = buflen;
		__entry->ret = ret;
	),
	TP_printk(DEV_PR_FMT
		  "%d p:%08x req:%02hhx %02hhx val:%04hx %04hx buf:%d %d",
		  DEV_PR_ARG, __entry->ret, __entry->pipe, __entry->req,
		  __entry->req_type, __entry->val, __entry->offset,
		  !!__entry->buf, __entry->buflen)
);

TRACE_EVENT(ee_read,
	TP_PROTO(struct mt7601u_dev *dev, int offset, u16 val),
	TP_ARGS(dev, offset, val),
	TP_STRUCT__entry(
		DEV_ENTRY
		__field(int, o) __field(u16, v)
	),
	TP_fast_assign(
		DEV_ASSIGN;
		__entry->o = offset;
		__entry->v = val;
	),
	TP_printk(DEV_PR_FMT "%04x=%04x", DEV_PR_ARG, __entry->o, __entry->v)
);

DECLARE_EVENT_CLASS(dev_rf_reg_evt,
	TP_PROTO(struct mt7601u_dev *dev, u8 bank, u8 reg, u8 val),
	TP_ARGS(dev, bank, reg, val),
	TP_STRUCT__entry(
		DEV_ENTRY
		__field(u8, bank)
		__field(u8, reg)
		__field(u8, val)
	),
	TP_fast_assign(
		DEV_ASSIGN;
		REG_ASSIGN;
		__entry->bank = bank;
	),
	TP_printk(
		DEV_PR_FMT "%02hhx:%02hhx=%02hhx",
		DEV_PR_ARG, __entry->bank, __entry->reg, __entry->val
	)
);

DEFINE_EVENT(dev_rf_reg_evt, rf_read,
	TP_PROTO(struct mt7601u_dev *dev, u8 bank, u8 reg, u8 val),
	TP_ARGS(dev, bank, reg, val)
);

DEFINE_EVENT(dev_rf_reg_evt, rf_write,
	TP_PROTO(struct mt7601u_dev *dev, u8 bank, u8 reg, u8 val),
	TP_ARGS(dev, bank, reg, val)
);

DECLARE_EVENT_CLASS(dev_bbp_reg_evt,
	TP_PROTO(struct mt7601u_dev *dev, u8 reg, u8 val),
	TP_ARGS(dev, reg, val),
	TP_STRUCT__entry(
		DEV_ENTRY
		__field(u8, reg)
		__field(u8, val)
	),
	TP_fast_assign(
		DEV_ASSIGN;
		REG_ASSIGN;
	),
	TP_printk(
		DEV_PR_FMT "%02hhx=%02hhx",
		DEV_PR_ARG, __entry->reg, __entry->val
	)
);

DEFINE_EVENT(dev_bbp_reg_evt, bbp_read,
	TP_PROTO(struct mt7601u_dev *dev, u8 reg, u8 val),
	TP_ARGS(dev, reg, val)
);

DEFINE_EVENT(dev_bbp_reg_evt, bbp_write,
	TP_PROTO(struct mt7601u_dev *dev, u8 reg, u8 val),
	TP_ARGS(dev, reg, val)
);

DECLARE_EVENT_CLASS(dev_simple_evt,
	TP_PROTO(struct mt7601u_dev *dev, u8 val),
	TP_ARGS(dev, val),
	TP_STRUCT__entry(
		DEV_ENTRY
		__field(u8, val)
	),
	TP_fast_assign(
		DEV_ASSIGN;
		__entry->val = val;
	),
	TP_printk(
		DEV_PR_FMT "%02hhx", DEV_PR_ARG, __entry->val
	)
);

DEFINE_EVENT(dev_simple_evt, temp_mode,
	TP_PROTO(struct mt7601u_dev *dev, u8 val),
	TP_ARGS(dev, val)
);

DEFINE_EVENT(dev_simple_evt, read_temp,
	TP_PROTO(struct mt7601u_dev *dev, u8 val),
	TP_ARGS(dev, val)
);

DEFINE_EVENT(dev_simple_evt, freq_cal_adjust,
	TP_PROTO(struct mt7601u_dev *dev, u8 val),
	TP_ARGS(dev, val)
);

TRACE_EVENT(freq_cal_offset,
	TP_PROTO(struct mt7601u_dev *dev, u8 phy_mode, s8 freq_off),
	TP_ARGS(dev, phy_mode, freq_off),
	TP_STRUCT__entry(
		DEV_ENTRY
		__field(u8, phy_mode)
		__field(s8, freq_off)
	),
	TP_fast_assign(
		DEV_ASSIGN;
		__entry->phy_mode = phy_mode;
		__entry->freq_off = freq_off;
	),
	TP_printk(DEV_PR_FMT "phy:%02hhx off:%02hhx",
		  DEV_PR_ARG, __entry->phy_mode, __entry->freq_off)
);

TRACE_EVENT(mt_rx,
	TP_PROTO(struct mt7601u_dev *dev, struct mt7601u_rxwi *rxwi, u32 f),
	TP_ARGS(dev, rxwi, f),
	TP_STRUCT__entry(
		DEV_ENTRY
		__field_struct(struct mt7601u_rxwi, rxwi)
		__field(u32, fce_info)
	),
	TP_fast_assign(
		DEV_ASSIGN;
		__entry->rxwi = *rxwi;
		__entry->fce_info = f;
	),
	TP_printk(DEV_PR_FMT "rxi:%08x ctl:%08x frag_sn:%04hx rate:%04hx "
		  "uknw:%02hhx z:%02hhx%02hhx%02hhx snr:%02hhx "
		  "ant:%02hhx gain:%02hhx freq_o:%02hhx "
		  "r:%08x ea:%08x fce:%08x", DEV_PR_ARG,
		  le32_to_cpu(__entry->rxwi.rxinfo),
		  le32_to_cpu(__entry->rxwi.ctl),
		  le16_to_cpu(__entry->rxwi.frag_sn),
		  le16_to_cpu(__entry->rxwi.rate),
		  __entry->rxwi.unknown,
		  __entry->rxwi.zero[0], __entry->rxwi.zero[1],
		  __entry->rxwi.zero[2],
		  __entry->rxwi.snr, __entry->rxwi.ant,
		  __entry->rxwi.gain, __entry->rxwi.freq_off,
		  __entry->rxwi.resv2, __entry->rxwi.expert_ant,
		  __entry->fce_info)
);

TRACE_EVENT(mt_tx,
	TP_PROTO(struct mt7601u_dev *dev, struct sk_buff *skb,
		 struct mt76_sta *sta, struct mt76_txwi *h),
	TP_ARGS(dev, skb, sta, h),
	TP_STRUCT__entry(
		DEV_ENTRY
		__field_struct(struct mt76_txwi, h)
		__field(struct sk_buff *, skb)
		__field(struct mt76_sta *, sta)
	),
	TP_fast_assign(
		DEV_ASSIGN;
		__entry->h = *h;
		__entry->skb = skb;
		__entry->sta = sta;
	),
	TP_printk(DEV_PR_FMT "skb:%p sta:%p  flg:%04hx rate_ctl:%04hx "
		  "ack:%02hhx wcid:%02hhx len_ctl:%05hx", DEV_PR_ARG,
		  __entry->skb, __entry->sta,
		  le16_to_cpu(__entry->h.flags),
		  le16_to_cpu(__entry->h.rate_ctl),
		  __entry->h.ack_ctl, __entry->h.wcid,
		  le16_to_cpu(__entry->h.len_ctl))
);

TRACE_EVENT(mt_tx_dma_done,
	TP_PROTO(struct mt7601u_dev *dev, struct sk_buff *skb),
	TP_ARGS(dev, skb),
	TP_STRUCT__entry(
		DEV_ENTRY
		__field(struct sk_buff *, skb)
	),
	TP_fast_assign(
		DEV_ASSIGN;
		__entry->skb = skb;
	),
	TP_printk(DEV_PR_FMT "%p", DEV_PR_ARG, __entry->skb)
);

TRACE_EVENT(mt_tx_status_cleaned,
	TP_PROTO(struct mt7601u_dev *dev, int cleaned),
	TP_ARGS(dev, cleaned),
	TP_STRUCT__entry(
		DEV_ENTRY
		__field(int, cleaned)
	),
	TP_fast_assign(
		DEV_ASSIGN;
		__entry->cleaned = cleaned;
	),
	TP_printk(DEV_PR_FMT "%d", DEV_PR_ARG, __entry->cleaned)
);

TRACE_EVENT(mt_tx_status,
	TP_PROTO(struct mt7601u_dev *dev, u32 stat1, u32 stat2),
	TP_ARGS(dev, stat1, stat2),
	TP_STRUCT__entry(
		DEV_ENTRY
		__field(u32, stat1)	__field(u32, stat2)
	),
	TP_fast_assign(
		DEV_ASSIGN;
		__entry->stat1 = stat1;
		__entry->stat2 = stat2;
	),
	TP_printk(DEV_PR_FMT "%08x %08x",
		  DEV_PR_ARG, __entry->stat1, __entry->stat2)
);

TRACE_EVENT(mt_rx_dma_aggr,
	TP_PROTO(struct mt7601u_dev *dev, int cnt, bool paged),
	TP_ARGS(dev, cnt, paged),
	TP_STRUCT__entry(
		DEV_ENTRY
		__field(u8, cnt)
		__field(bool, paged)
	),
	TP_fast_assign(
		DEV_ASSIGN;
		__entry->cnt = cnt;
		__entry->paged = paged;
	),
	TP_printk(DEV_PR_FMT "cnt:%d paged:%d",
		  DEV_PR_ARG, __entry->cnt, __entry->paged)
);

DEFINE_EVENT(dev_simple_evt, set_key,
	TP_PROTO(struct mt7601u_dev *dev, u8 val),
	TP_ARGS(dev, val)
);

TRACE_EVENT(set_shared_key,
	TP_PROTO(struct mt7601u_dev *dev, u8 vid, u8 key),
	TP_ARGS(dev, vid, key),
	TP_STRUCT__entry(
		DEV_ENTRY
		__field(u8, vid)
		__field(u8, key)
	),
	TP_fast_assign(
		DEV_ASSIGN;
		__entry->vid = vid;
		__entry->key = key;
	),
	TP_printk(DEV_PR_FMT "phy:%02hhx off:%02hhx",
		  DEV_PR_ARG, __entry->vid, __entry->key)
);

#endif

#undef TRACE_INCLUDE_PATH
#define TRACE_INCLUDE_PATH .
#undef TRACE_INCLUDE_FILE
#define TRACE_INCLUDE_FILE trace

#include <trace/define_trace.h>
