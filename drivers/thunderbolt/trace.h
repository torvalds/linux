/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Thunderbolt tracing support
 *
 * Copyright (C) 2024, Intel Corporation
 * Author: Mika Westerberg <mika.westerberg@linux.intel.com>
 *	   Gil Fine <gil.fine@intel.com>
 */

#undef TRACE_SYSTEM
#define TRACE_SYSTEM thunderbolt

#if !defined(TB_TRACE_H_) || defined(TRACE_HEADER_MULTI_READ)
#define TB_TRACE_H_

#include <linux/trace_seq.h>
#include <linux/tracepoint.h>

#include "tb_msgs.h"

#define tb_cfg_type_name(type)		{ type, #type }
#define show_type_name(val)					\
	__print_symbolic(val,					\
		tb_cfg_type_name(TB_CFG_PKG_READ),		\
		tb_cfg_type_name(TB_CFG_PKG_WRITE),		\
		tb_cfg_type_name(TB_CFG_PKG_ERROR),		\
		tb_cfg_type_name(TB_CFG_PKG_NOTIFY_ACK),	\
		tb_cfg_type_name(TB_CFG_PKG_EVENT),		\
		tb_cfg_type_name(TB_CFG_PKG_XDOMAIN_REQ),	\
		tb_cfg_type_name(TB_CFG_PKG_XDOMAIN_RESP),	\
		tb_cfg_type_name(TB_CFG_PKG_OVERRIDE),		\
		tb_cfg_type_name(TB_CFG_PKG_RESET),		\
		tb_cfg_type_name(TB_CFG_PKG_ICM_EVENT),		\
		tb_cfg_type_name(TB_CFG_PKG_ICM_CMD),		\
		tb_cfg_type_name(TB_CFG_PKG_ICM_RESP))

#ifndef TB_TRACE_HELPERS
#define TB_TRACE_HELPERS
static inline const char *show_data_read_write(struct trace_seq *p,
					       const u32 *data)
{
	const struct cfg_read_pkg *msg = (const struct cfg_read_pkg *)data;
	const char *ret = trace_seq_buffer_ptr(p);

	trace_seq_printf(p, "offset=%#x, len=%u, port=%d, config=%#x, seq=%d, ",
			 msg->addr.offset, msg->addr.length, msg->addr.port,
			 msg->addr.space, msg->addr.seq);

	return ret;
}

static inline const char *show_data_error(struct trace_seq *p, const u32 *data)
{
	const struct cfg_error_pkg *msg = (const struct cfg_error_pkg *)data;
	const char *ret = trace_seq_buffer_ptr(p);

	trace_seq_printf(p, "error=%#x, port=%d, plug=%#x, ", msg->error,
			 msg->port, msg->pg);

	return ret;
}

static inline const char *show_data_event(struct trace_seq *p, const u32 *data)
{
	const struct cfg_event_pkg *msg = (const struct cfg_event_pkg *)data;
	const char *ret = trace_seq_buffer_ptr(p);

	trace_seq_printf(p, "port=%d, unplug=%#x, ", msg->port, msg->unplug);

	return ret;
}

static inline const char *show_route(struct trace_seq *p, const u32 *data)
{
	const struct tb_cfg_header *header = (const struct tb_cfg_header *)data;
	const char *ret = trace_seq_buffer_ptr(p);

	trace_seq_printf(p, "route=%llx, ", tb_cfg_get_route(header));

	return ret;
}

static inline const char *show_data(struct trace_seq *p, u8 type,
				    const u32 *data, u32 length)
{
	const char *ret = trace_seq_buffer_ptr(p);
	const char *prefix = "";
	int i;

	switch (type) {
	case TB_CFG_PKG_READ:
	case TB_CFG_PKG_WRITE:
		show_route(p, data);
		show_data_read_write(p, data);
		break;

	case TB_CFG_PKG_ERROR:
		show_route(p, data);
		show_data_error(p, data);
		break;

	case TB_CFG_PKG_EVENT:
		show_route(p, data);
		show_data_event(p, data);
		break;

	case TB_CFG_PKG_ICM_EVENT:
	case TB_CFG_PKG_ICM_CMD:
	case TB_CFG_PKG_ICM_RESP:
		/* ICM messages always target the host router */
		trace_seq_puts(p, "route=0, ");
		break;

	default:
		show_route(p, data);
		break;
	}

	trace_seq_printf(p, "data=[");
	for (i = 0; i < length; i++) {
		trace_seq_printf(p, "%s0x%08x", prefix, data[i]);
		prefix = ", ";
	}
	trace_seq_printf(p, "]");
	trace_seq_putc(p, 0);

	return ret;
}
#endif

DECLARE_EVENT_CLASS(tb_raw,
	TP_PROTO(int index, u8 type, const void *data, size_t size),
	TP_ARGS(index, type, data, size),
	TP_STRUCT__entry(
		__field(int, index)
		__field(u8, type)
		__field(size_t, size)
		__dynamic_array(u32, data, size / 4)
	),
	TP_fast_assign(
		__entry->index = index;
		__entry->type = type;
		__entry->size = size / 4;
		memcpy(__get_dynamic_array(data), data, size);
	),
	TP_printk("type=%s, size=%zd, domain=%d, %s",
		  show_type_name(__entry->type), __entry->size, __entry->index,
		  show_data(p, __entry->type, __get_dynamic_array(data),
			    __entry->size)
	)
);

DEFINE_EVENT(tb_raw, tb_tx,
	TP_PROTO(int index, u8 type, const void *data, size_t size),
	TP_ARGS(index, type, data, size)
);

DEFINE_EVENT(tb_raw, tb_event,
	TP_PROTO(int index, u8 type, const void *data, size_t size),
	TP_ARGS(index, type, data, size)
);

TRACE_EVENT(tb_rx,
	TP_PROTO(int index, u8 type, const void *data, size_t size, bool dropped),
	TP_ARGS(index, type, data, size, dropped),
	TP_STRUCT__entry(
		__field(int, index)
		__field(u8, type)
		__field(size_t, size)
		__dynamic_array(u32, data, size / 4)
		__field(bool, dropped)
	),
	TP_fast_assign(
		__entry->index = index;
		__entry->type = type;
		__entry->size = size / 4;
		memcpy(__get_dynamic_array(data), data, size);
		__entry->dropped = dropped;
	),
	TP_printk("type=%s, dropped=%u, size=%zd, domain=%d, %s",
		  show_type_name(__entry->type), __entry->dropped,
		  __entry->size, __entry->index,
		  show_data(p, __entry->type, __get_dynamic_array(data),
			    __entry->size)
	)
);

#endif /* TB_TRACE_H_ */

#undef TRACE_INCLUDE_PATH
#define TRACE_INCLUDE_PATH .

#undef TRACE_INCLUDE_FILE
#define TRACE_INCLUDE_FILE trace

/* This part must be outside protection */
#include <trace/define_trace.h>
