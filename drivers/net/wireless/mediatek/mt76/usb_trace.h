/* SPDX-License-Identifier: ISC */
/*
 * Copyright (C) 2018 Lorenzo Bianconi <lorenzo.bianconi83@gmail.com>
 */

#if !defined(__MT76_USB_TRACE_H) || defined(TRACE_HEADER_MULTI_READ)
#define __MT76_USB_TRACE_H

#include <linux/tracepoint.h>
#include "mt76.h"

#undef TRACE_SYSTEM
#define TRACE_SYSTEM mt76_usb

#define MAXNAME		32
#define DEV_ENTRY	__array(char, wiphy_name, 32)
#define DEV_ASSIGN	strscpy(__entry->wiphy_name,	\
				wiphy_name(dev->hw->wiphy), MAXNAME)
#define DEV_PR_FMT	"%s "
#define DEV_PR_ARG	__entry->wiphy_name

#define REG_ENTRY	__field(u32, reg) __field(u32, val)
#define REG_ASSIGN	__entry->reg = reg; __entry->val = val
#define REG_PR_FMT	"reg:0x%04x=0x%08x"
#define REG_PR_ARG	__entry->reg, __entry->val

DECLARE_EVENT_CLASS(dev_reg_evt,
	TP_PROTO(struct mt76_dev *dev, u32 reg, u32 val),
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

DEFINE_EVENT(dev_reg_evt, usb_reg_rr,
	TP_PROTO(struct mt76_dev *dev, u32 reg, u32 val),
	TP_ARGS(dev, reg, val)
);

DEFINE_EVENT(dev_reg_evt, usb_reg_wr,
	TP_PROTO(struct mt76_dev *dev, u32 reg, u32 val),
	TP_ARGS(dev, reg, val)
);

DECLARE_EVENT_CLASS(urb_transfer,
	TP_PROTO(struct mt76_dev *dev, struct urb *u),
	TP_ARGS(dev, u),
	TP_STRUCT__entry(
		DEV_ENTRY __field(unsigned int, pipe) __field(u32, len)
	),
	TP_fast_assign(
		DEV_ASSIGN;
		__entry->pipe = u->pipe;
		__entry->len = u->transfer_buffer_length;
	),
	TP_printk(DEV_PR_FMT "p:%08x len:%u",
		  DEV_PR_ARG, __entry->pipe, __entry->len)
);

DEFINE_EVENT(urb_transfer, submit_urb,
	TP_PROTO(struct mt76_dev *dev, struct urb *u),
	TP_ARGS(dev, u)
);

DEFINE_EVENT(urb_transfer, rx_urb,
	TP_PROTO(struct mt76_dev *dev, struct urb *u),
	TP_ARGS(dev, u)
);

#endif

#undef TRACE_INCLUDE_PATH
#define TRACE_INCLUDE_PATH .
#undef TRACE_INCLUDE_FILE
#define TRACE_INCLUDE_FILE usb_trace

#include <trace/define_trace.h>
