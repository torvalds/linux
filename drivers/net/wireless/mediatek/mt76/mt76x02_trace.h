/* SPDX-License-Identifier: BSD-3-Clause-Clear */
/*
 * Copyright (C) 2016 Felix Fietkau <nbd@nbd.name>
 */

#if !defined(__MT76x02_TRACE_H) || defined(TRACE_HEADER_MULTI_READ)
#define __MT76x02_TRACE_H

#include <linux/tracepoint.h>
#include "mt76x02.h"

#undef TRACE_SYSTEM
#define TRACE_SYSTEM mt76x02

#define MAXNAME		32
#define DEV_ENTRY	__array(char, wiphy_name, 32)
#define DEV_ASSIGN	strscpy(__entry->wiphy_name,	\
				wiphy_name(mt76_hw(dev)->wiphy), MAXNAME)
#define DEV_PR_FMT	"%s"
#define DEV_PR_ARG	__entry->wiphy_name

#define TXID_ENTRY	__field(u8, wcid) __field(u8, pktid)
#define TXID_PR_FMT	" [%d:%d]"
#define TXID_PR_ARG	__entry->wcid, __entry->pktid

DECLARE_EVENT_CLASS(dev_evt,
	TP_PROTO(struct mt76x02_dev *dev),
	TP_ARGS(dev),
	TP_STRUCT__entry(
		DEV_ENTRY
	),
	TP_fast_assign(
		DEV_ASSIGN;
	),
	TP_printk(DEV_PR_FMT, DEV_PR_ARG)
);

DEFINE_EVENT(dev_evt, mac_txstat_poll,
	TP_PROTO(struct mt76x02_dev *dev),
	TP_ARGS(dev)
);

TRACE_EVENT(mac_txstat_fetch,
	TP_PROTO(struct mt76x02_dev *dev,
		 struct mt76x02_tx_status *stat),

	TP_ARGS(dev, stat),

	TP_STRUCT__entry(
		DEV_ENTRY
		TXID_ENTRY
		__field(bool, success)
		__field(bool, aggr)
		__field(bool, ack_req)
		__field(u16, rate)
		__field(u8, retry)
	),

	TP_fast_assign(
		DEV_ASSIGN;
		__entry->success = stat->success;
		__entry->aggr = stat->aggr;
		__entry->ack_req = stat->ack_req;
		__entry->wcid = stat->wcid;
		__entry->pktid = stat->pktid;
		__entry->rate = stat->rate;
		__entry->retry = stat->retry;
	),

	TP_printk(
		DEV_PR_FMT TXID_PR_FMT
		" success:%d aggr:%d ack_req:%d"
		" rate:%04x retry:%d",
		DEV_PR_ARG, TXID_PR_ARG,
		__entry->success, __entry->aggr, __entry->ack_req,
		__entry->rate, __entry->retry
	)
);

#endif

#undef TRACE_INCLUDE_PATH
#define TRACE_INCLUDE_PATH .
#undef TRACE_INCLUDE_FILE
#define TRACE_INCLUDE_FILE mt76x02_trace

#include <trace/define_trace.h>
