/* SPDX-License-Identifier: ISC */
/*
 * Copyright (C) 2023 Lorenzo Bianconi <lorenzo@kernel.org>
 */

#if !defined(__MT792X_TRACE_H) || defined(TRACE_HEADER_MULTI_READ)
#define __MT792X_TRACE_H

#include <linux/tracepoint.h>
#include "mt792x.h"

#undef TRACE_SYSTEM
#define TRACE_SYSTEM mt792x

#define MAXNAME		32
#define DEV_ENTRY	__array(char, wiphy_name, 32)
#define DEV_ASSIGN	strscpy(__entry->wiphy_name,	\
				wiphy_name(mt76_hw(dev)->wiphy), MAXNAME)
#define DEV_PR_FMT	"%s"
#define DEV_PR_ARG	__entry->wiphy_name
#define LP_STATE_PR_ARG	__entry->lp_state ? "lp ready" : "lp not ready"

TRACE_EVENT(lp_event,
	TP_PROTO(struct mt792x_dev *dev, u8 lp_state),

	TP_ARGS(dev, lp_state),

	TP_STRUCT__entry(
		DEV_ENTRY
		__field(u8, lp_state)
	),

	TP_fast_assign(
		DEV_ASSIGN;
		__entry->lp_state = lp_state;
	),

	TP_printk(
		DEV_PR_FMT " %s",
		DEV_PR_ARG, LP_STATE_PR_ARG
	)
);

#endif

#undef TRACE_INCLUDE_PATH
#define TRACE_INCLUDE_PATH .
#undef TRACE_INCLUDE_FILE
#define TRACE_INCLUDE_FILE mt792x_trace

#include <trace/define_trace.h>
