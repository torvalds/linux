/* SPDX-License-Identifier: GPL-2.0 */
#undef TRACE_SYSTEM
#define TRACE_SYSTEM regmap

#if !defined(_TRACE_REGMAP_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_REGMAP_H

#include <linux/ktime.h>
#include <linux/tracepoint.h>

#include "internal.h"

/*
 * Log register events
 */
DECLARE_EVENT_CLASS(regmap_reg,

	TP_PROTO(struct regmap *map, unsigned int reg,
		 unsigned int val),

	TP_ARGS(map, reg, val),

	TP_STRUCT__entry(
		__string(	name,		regmap_name(map)	)
		__field(	unsigned int,	reg			)
		__field(	unsigned int,	val			)
	),

	TP_fast_assign(
		__assign_str(name);
		__entry->reg = reg;
		__entry->val = val;
	),

	TP_printk("%s reg=%x val=%x", __get_str(name), __entry->reg, __entry->val)
);

DEFINE_EVENT(regmap_reg, regmap_reg_write,

	TP_PROTO(struct regmap *map, unsigned int reg,
		 unsigned int val),

	TP_ARGS(map, reg, val)
);

DEFINE_EVENT(regmap_reg, regmap_reg_read,

	TP_PROTO(struct regmap *map, unsigned int reg,
		 unsigned int val),

	TP_ARGS(map, reg, val)
);

DEFINE_EVENT(regmap_reg, regmap_reg_read_cache,

	TP_PROTO(struct regmap *map, unsigned int reg,
		 unsigned int val),

	TP_ARGS(map, reg, val)
);

DECLARE_EVENT_CLASS(regmap_bulk,

	TP_PROTO(struct regmap *map, unsigned int reg,
		 const void *val, int val_len),

	TP_ARGS(map, reg, val, val_len),

	TP_STRUCT__entry(
		__string(name, regmap_name(map))
		__field(unsigned int, reg)
		__dynamic_array(char, buf, val_len)
		__field(int, val_len)
	),

	TP_fast_assign(
		__assign_str(name);
		__entry->reg = reg;
		__entry->val_len = val_len;
		memcpy(__get_dynamic_array(buf), val, val_len);
	),

	TP_printk("%s reg=%x val=%s", __get_str(name), __entry->reg,
		  __print_hex(__get_dynamic_array(buf), __entry->val_len))
);

DEFINE_EVENT(regmap_bulk, regmap_bulk_write,

	TP_PROTO(struct regmap *map, unsigned int reg,
		 const void *val, int val_len),

	TP_ARGS(map, reg, val, val_len)
);

DEFINE_EVENT(regmap_bulk, regmap_bulk_read,

	TP_PROTO(struct regmap *map, unsigned int reg,
		 const void *val, int val_len),

	TP_ARGS(map, reg, val, val_len)
);

DECLARE_EVENT_CLASS(regmap_block,

	TP_PROTO(struct regmap *map, unsigned int reg, int count),

	TP_ARGS(map, reg, count),

	TP_STRUCT__entry(
		__string(	name,		regmap_name(map)	)
		__field(	unsigned int,	reg			)
		__field(	int,		count			)
	),

	TP_fast_assign(
		__assign_str(name);
		__entry->reg = reg;
		__entry->count = count;
	),

	TP_printk("%s reg=%x count=%d", __get_str(name), __entry->reg, __entry->count)
);

DEFINE_EVENT(regmap_block, regmap_hw_read_start,

	TP_PROTO(struct regmap *map, unsigned int reg, int count),

	TP_ARGS(map, reg, count)
);

DEFINE_EVENT(regmap_block, regmap_hw_read_done,

	TP_PROTO(struct regmap *map, unsigned int reg, int count),

	TP_ARGS(map, reg, count)
);

DEFINE_EVENT(regmap_block, regmap_hw_write_start,

	TP_PROTO(struct regmap *map, unsigned int reg, int count),

	TP_ARGS(map, reg, count)
);

DEFINE_EVENT(regmap_block, regmap_hw_write_done,

	TP_PROTO(struct regmap *map, unsigned int reg, int count),

	TP_ARGS(map, reg, count)
);

TRACE_EVENT(regcache_sync,

	TP_PROTO(struct regmap *map, const char *type,
		 const char *status),

	TP_ARGS(map, type, status),

	TP_STRUCT__entry(
		__string(       name,           regmap_name(map)	)
		__string(	status,		status			)
		__string(	type,		type			)
	),

	TP_fast_assign(
		__assign_str(name);
		__assign_str(status);
		__assign_str(type);
	),

	TP_printk("%s type=%s status=%s", __get_str(name),
		  __get_str(type), __get_str(status))
);

DECLARE_EVENT_CLASS(regmap_bool,

	TP_PROTO(struct regmap *map, bool flag),

	TP_ARGS(map, flag),

	TP_STRUCT__entry(
		__string(	name,		regmap_name(map)	)
		__field(	int,		flag			)
	),

	TP_fast_assign(
		__assign_str(name);
		__entry->flag = flag;
	),

	TP_printk("%s flag=%d", __get_str(name), __entry->flag)
);

DEFINE_EVENT(regmap_bool, regmap_cache_only,

	TP_PROTO(struct regmap *map, bool flag),

	TP_ARGS(map, flag)
);

DEFINE_EVENT(regmap_bool, regmap_cache_bypass,

	TP_PROTO(struct regmap *map, bool flag),

	TP_ARGS(map, flag)
);

DECLARE_EVENT_CLASS(regmap_async,

	TP_PROTO(struct regmap *map),

	TP_ARGS(map),

	TP_STRUCT__entry(
		__string(	name,		regmap_name(map)	)
	),

	TP_fast_assign(
		__assign_str(name);
	),

	TP_printk("%s", __get_str(name))
);

DEFINE_EVENT(regmap_block, regmap_async_write_start,

	TP_PROTO(struct regmap *map, unsigned int reg, int count),

	TP_ARGS(map, reg, count)
);

DEFINE_EVENT(regmap_async, regmap_async_io_complete,

	TP_PROTO(struct regmap *map),

	TP_ARGS(map)
);

DEFINE_EVENT(regmap_async, regmap_async_complete_start,

	TP_PROTO(struct regmap *map),

	TP_ARGS(map)
);

DEFINE_EVENT(regmap_async, regmap_async_complete_done,

	TP_PROTO(struct regmap *map),

	TP_ARGS(map)
);

TRACE_EVENT(regcache_drop_region,

	TP_PROTO(struct regmap *map, unsigned int from,
		 unsigned int to),

	TP_ARGS(map, from, to),

	TP_STRUCT__entry(
		__string(       name,           regmap_name(map)	)
		__field(	unsigned int,	from			)
		__field(	unsigned int,	to			)
	),

	TP_fast_assign(
		__assign_str(name);
		__entry->from = from;
		__entry->to = to;
	),

	TP_printk("%s %u-%u", __get_str(name), __entry->from, __entry->to)
);

#endif /* _TRACE_REGMAP_H */

#undef TRACE_INCLUDE_PATH
#define TRACE_INCLUDE_PATH .

#undef TRACE_INCLUDE_FILE
#define TRACE_INCLUDE_FILE trace

/* This part must be outside protection */
#include <trace/define_trace.h>
