/* SPDX-License-Identifier: BSD-3-Clause-Clear */
/*
 * Copyright (C) 2019 Lorenzo Bianconi <lorenzo@kernel.org>
 */

#if !defined(__MT7615_TRACE_H) || defined(TRACE_HEADER_MULTI_READ)
#define __MT7615_TRACE_H

#include <linux/tracepoint.h>
#include "mt7615.h"

#undef TRACE_SYSTEM
#define TRACE_SYSTEM mt7615

#define MAXNAME		32
#define DEV_ENTRY	__array(char, wiphy_name, 32)
#define DEV_ASSIGN	strscpy(__entry->wiphy_name,	\
				wiphy_name(mt76_hw(dev)->wiphy), MAXNAME)
#define DEV_PR_FMT	"%s"
#define DEV_PR_ARG	__entry->wiphy_name

#define TOKEN_ENTRY	__field(u16, token)
#define TOKEN_ASSIGN	__entry->token = token
#define TOKEN_PR_FMT	" %d"
#define TOKEN_PR_ARG	__entry->token

DECLARE_EVENT_CLASS(dev_token,
	TP_PROTO(struct mt7615_dev *dev, u16 token),
	TP_ARGS(dev, token),
	TP_STRUCT__entry(
		DEV_ENTRY
		TOKEN_ENTRY
	),
	TP_fast_assign(
		DEV_ASSIGN;
		TOKEN_ASSIGN;
	),
	TP_printk(
		DEV_PR_FMT TOKEN_PR_FMT,
		DEV_PR_ARG, TOKEN_PR_ARG
	)
);

DEFINE_EVENT(dev_token, mac_tx_free,
	TP_PROTO(struct mt7615_dev *dev, u16 token),
	TP_ARGS(dev, token)
);

#endif

#undef TRACE_INCLUDE_PATH
#define TRACE_INCLUDE_PATH .
#undef TRACE_INCLUDE_FILE
#define TRACE_INCLUDE_FILE mt7615_trace

#include <trace/define_trace.h>
