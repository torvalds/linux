/* SPDX-License-Identifier: GPL-2.0 */
/*
 * MacBook (Pro) SPI keyboard and touchpad driver
 *
 * Copyright (c) 2015-2019 Federico Lorenzi
 * Copyright (c) 2017-2019 Ronald Tschalär
 */

#undef TRACE_SYSTEM
#define TRACE_SYSTEM applespi

#if !defined(_APPLESPI_TRACE_H_) || defined(TRACE_HEADER_MULTI_READ)
#define _APPLESPI_TRACE_H_

#include <linux/types.h>
#include <linux/tracepoint.h>

#include "applespi.h"

DECLARE_EVENT_CLASS(dump_message_template,
	TP_PROTO(enum applespi_evt_type evt_type,
		 enum applespi_pkt_type pkt_type,
		 u8 *buf,
		 size_t len),

	TP_ARGS(evt_type, pkt_type, buf, len),

	TP_STRUCT__entry(
		__field(enum applespi_evt_type, evt_type)
		__field(enum applespi_pkt_type, pkt_type)
		__field(size_t, len)
		__dynamic_array(u8, buf, len)
	),

	TP_fast_assign(
		__entry->evt_type = evt_type;
		__entry->pkt_type = pkt_type;
		__entry->len = len;
		memcpy(__get_dynamic_array(buf), buf, len);
	),

	TP_printk("%-6s: %s",
		  __print_symbolic(__entry->pkt_type,
				   { PT_READ, "read" },
				   { PT_WRITE, "write" },
				   { PT_STATUS, "status" }
		  ),
		  __print_hex(__get_dynamic_array(buf), __entry->len))
);

#define DEFINE_DUMP_MESSAGE_EVENT(name)			\
DEFINE_EVENT(dump_message_template, name,		\
	TP_PROTO(enum applespi_evt_type evt_type,	\
		 enum applespi_pkt_type pkt_type,	\
		 u8 *buf,				\
		 size_t len),				\
	TP_ARGS(evt_type, pkt_type, buf, len)		\
)

DEFINE_DUMP_MESSAGE_EVENT(applespi_tp_ini_cmd);
DEFINE_DUMP_MESSAGE_EVENT(applespi_backlight_cmd);
DEFINE_DUMP_MESSAGE_EVENT(applespi_caps_lock_cmd);
DEFINE_DUMP_MESSAGE_EVENT(applespi_keyboard_data);
DEFINE_DUMP_MESSAGE_EVENT(applespi_touchpad_data);
DEFINE_DUMP_MESSAGE_EVENT(applespi_unknown_data);
DEFINE_DUMP_MESSAGE_EVENT(applespi_bad_crc);

TRACE_EVENT(applespi_irq_received,
	TP_PROTO(enum applespi_evt_type evt_type,
		 enum applespi_pkt_type pkt_type),

	TP_ARGS(evt_type, pkt_type),

	TP_STRUCT__entry(
		__field(enum applespi_evt_type, evt_type)
		__field(enum applespi_pkt_type, pkt_type)
	),

	TP_fast_assign(
		__entry->evt_type = evt_type;
		__entry->pkt_type = pkt_type;
	),

	"\n"
);

#endif /* _APPLESPI_TRACE_H_ */

/* This part must be outside protection */
#undef TRACE_INCLUDE_PATH
#define TRACE_INCLUDE_PATH ../../drivers/input/keyboard
#define TRACE_INCLUDE_FILE applespi_trace
#include <trace/define_trace.h>
