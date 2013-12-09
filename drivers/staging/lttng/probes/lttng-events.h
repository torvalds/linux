/*
 * lttng-events.h
 *
 * Copyright (C) 2009 Steven Rostedt <rostedt@goodmis.org>
 * Copyright (C) 2009-2012 Mathieu Desnoyers <mathieu.desnoyers@efficios.com>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; only
 * version 2.1 of the License.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */
#include <linux/uaccess.h>
#include <linux/debugfs.h>
#include "lttng.h"
#include "lttng-types.h"
#include "lttng-probe-user.h"
#include "../wrapper/vmalloc.h"	/* for wrapper_vmalloc_sync_all() */
#include "../wrapper/ringbuffer/frontend_types.h"
#include "../lttng-events.h"
#include "../lttng-tracer-core.h"

/*
 * Macro declarations used for all stages.
 */

/*
 * LTTng name mapping macros. LTTng remaps some of the kernel events to
 * enforce name-spacing.
 */
#undef TRACE_EVENT_MAP
#define TRACE_EVENT_MAP(name, map, proto, args, tstruct, assign, print)	\
	DECLARE_EVENT_CLASS(map,					\
			     PARAMS(proto),				\
			     PARAMS(args),				\
			     PARAMS(tstruct),				\
			     PARAMS(assign),				\
			     PARAMS(print))				\
	DEFINE_EVENT_MAP(map, name, map, PARAMS(proto), PARAMS(args))

#undef TRACE_EVENT_MAP_NOARGS
#define TRACE_EVENT_MAP_NOARGS(name, map, tstruct, assign, print)	\
	DECLARE_EVENT_CLASS_NOARGS(map,					\
			     PARAMS(tstruct),				\
			     PARAMS(assign),				\
			     PARAMS(print))				\
	DEFINE_EVENT_MAP_NOARGS(map, name, map)

#undef DEFINE_EVENT_PRINT_MAP
#define DEFINE_EVENT_PRINT_MAP(template, name, map, proto, args, print)	\
	DEFINE_EVENT_MAP(template, name, map, PARAMS(proto), PARAMS(args))

/* Callbacks are meaningless to LTTng. */
#undef TRACE_EVENT_FN_MAP
#define TRACE_EVENT_FN_MAP(name, map, proto, args, tstruct,		\
		assign, print, reg, unreg)				\
	TRACE_EVENT_MAP(name, map, PARAMS(proto), PARAMS(args),		\
		PARAMS(tstruct), PARAMS(assign), PARAMS(print))		\

#undef TRACE_EVENT_CONDITION_MAP
#define TRACE_EVENT_CONDITION_MAP(name, map, proto, args, cond, tstruct, assign, print) \
	TRACE_EVENT_MAP(name, map,					\
		PARAMS(proto),						\
		PARAMS(args),						\
		PARAMS(tstruct),					\
		PARAMS(assign),						\
		PARAMS(print))

/*
 * DECLARE_EVENT_CLASS can be used to add a generic function
 * handlers for events. That is, if all events have the same
 * parameters and just have distinct trace points.
 * Each tracepoint can be defined with DEFINE_EVENT and that
 * will map the DECLARE_EVENT_CLASS to the tracepoint.
 *
 * TRACE_EVENT is a one to one mapping between tracepoint and template.
 */

#undef TRACE_EVENT
#define TRACE_EVENT(name, proto, args, tstruct, assign, print)	\
	TRACE_EVENT_MAP(name, name,				\
			PARAMS(proto),				\
			PARAMS(args),				\
			PARAMS(tstruct),			\
			PARAMS(assign),				\
			PARAMS(print))

#undef TRACE_EVENT_NOARGS
#define TRACE_EVENT_NOARGS(name, tstruct, assign, print)	\
	TRACE_EVENT_MAP_NOARGS(name, name,			\
			PARAMS(tstruct),			\
			PARAMS(assign),				\
			PARAMS(print))

#undef DEFINE_EVENT_PRINT
#define DEFINE_EVENT_PRINT(template, name, proto, args, print)	\
	DEFINE_EVENT_PRINT_MAP(template, name, name,		\
			PARAMS(proto), PARAMS(args), PARAMS(print_))

#undef TRACE_EVENT_FN
#define TRACE_EVENT_FN(name, proto, args, tstruct,			\
		assign, print, reg, unreg)				\
	TRACE_EVENT_FN_MAP(name, name, PARAMS(proto), PARAMS(args),	\
		PARAMS(tstruct), PARAMS(assign), PARAMS(print),		\
		PARAMS(reg), PARAMS(unreg))				\

#undef DEFINE_EVENT
#define DEFINE_EVENT(template, name, proto, args)			\
	DEFINE_EVENT_MAP(template, name, name, PARAMS(proto), PARAMS(args))

#undef DEFINE_EVENT_NOARGS
#define DEFINE_EVENT_NOARGS(template, name)				\
	DEFINE_EVENT_MAP_NOARGS(template, name, name)

#undef TRACE_EVENT_CONDITION
#define TRACE_EVENT_CONDITION(name, proto, args, cond, tstruct, assign, print) \
	TRACE_EVENT_CONDITION_MAP(name, name,				\
		PARAMS(proto),						\
		PARAMS(args),						\
		PARAMS(cond),						\
		PARAMS(tstruct),					\
		PARAMS(assign),						\
		PARAMS(print))

/*
 * Stage 1 of the trace events.
 *
 * Create dummy trace calls for each events, verifying that the LTTng module
 * TRACE_EVENT headers match the kernel arguments. Will be optimized out by the
 * compiler.
 */

#include "lttng-events-reset.h"	/* Reset all macros within TRACE_EVENT */

#undef TP_PROTO
#define TP_PROTO(args...) args

#undef TP_ARGS
#define TP_ARGS(args...) args

#undef DEFINE_EVENT_MAP
#define DEFINE_EVENT_MAP(_template, _name, _map, _proto, _args)		\
void trace_##_name(_proto);

#undef DEFINE_EVENT_MAP_NOARGS
#define DEFINE_EVENT_MAP_NOARGS(_template, _name, _map)			\
void trace_##_name(void *__data);

#include TRACE_INCLUDE(TRACE_INCLUDE_FILE)

/*
 * Stage 2 of the trace events.
 *
 * Create event field type metadata section.
 * Each event produce an array of fields.
 */

#include "lttng-events-reset.h"	/* Reset all macros within TRACE_EVENT */

/* Named field types must be defined in lttng-types.h */

#undef __field_full
#define __field_full(_type, _item, _order, _base)		\
	{							\
	  .name = #_item,					\
	  .type = __type_integer(_type, _order, _base, none),	\
	},

#undef __field
#define __field(_type, _item)					\
	__field_full(_type, _item, __BYTE_ORDER, 10)

#undef __field_ext
#define __field_ext(_type, _item, _filter_type)			\
	__field(_type, _item)

#undef __field_hex
#define __field_hex(_type, _item)				\
	__field_full(_type, _item, __BYTE_ORDER, 16)

#undef __field_network
#define __field_network(_type, _item)				\
	__field_full(_type, _item, __BIG_ENDIAN, 10)

#undef __field_network_hex
#define __field_network_hex(_type, _item)				\
	__field_full(_type, _item, __BIG_ENDIAN, 16)

#undef __array_enc_ext
#define __array_enc_ext(_type, _item, _length, _order, _base, _encoding)\
	{							\
	  .name = #_item,					\
	  .type =						\
		{						\
		  .atype = atype_array,				\
		  .u.array =					\
			{					\
			    .length = _length,			\
			    .elem_type = __type_integer(_type, _order, _base, _encoding), \
			},					\
		},						\
	},

#undef __array
#define __array(_type, _item, _length)				\
	__array_enc_ext(_type, _item, _length, __BYTE_ORDER, 10, none)

#undef __array_text
#define __array_text(_type, _item, _length)			\
	__array_enc_ext(_type, _item, _length, __BYTE_ORDER, 10, UTF8)

#undef __array_hex
#define __array_hex(_type, _item, _length)			\
	__array_enc_ext(_type, _item, _length, __BYTE_ORDER, 16, none)

#undef __dynamic_array_enc_ext
#define __dynamic_array_enc_ext(_type, _item, _length, _order, _base, _encoding) \
	{							\
	  .name = #_item,					\
	  .type =						\
		{						\
		  .atype = atype_sequence,			\
		  .u.sequence =					\
			{					\
			    .length_type = __type_integer(u32, __BYTE_ORDER, 10, none), \
			    .elem_type = __type_integer(_type, _order, _base, _encoding), \
			},					\
		},						\
	},

#undef __dynamic_array_enc_ext_2
#define __dynamic_array_enc_ext_2(_type, _item, _length1, _length2, _order, _base, _encoding) \
	__dynamic_array_enc_ext(_type, _item, _length1 + _length2, _order, _base, _encoding)

#undef __dynamic_array
#define __dynamic_array(_type, _item, _length)			\
	__dynamic_array_enc_ext(_type, _item, _length, __BYTE_ORDER, 10, none)

#undef __dynamic_array_text
#define __dynamic_array_text(_type, _item, _length)		\
	__dynamic_array_enc_ext(_type, _item, _length, __BYTE_ORDER, 10, UTF8)

#undef __dynamic_array_hex
#define __dynamic_array_hex(_type, _item, _length)		\
	__dynamic_array_enc_ext(_type, _item, _length, __BYTE_ORDER, 16, none)

#undef __dynamic_array_text_2
#define __dynamic_array_text_2(_type, _item, _length1, _length2)	\
	__dynamic_array_enc_ext_2(_type, _item, _length1, _length2, __BYTE_ORDER, 10, UTF8)

#undef __string
#define __string(_item, _src)					\
	{							\
	  .name = #_item,					\
	  .type =						\
		{						\
		  .atype = atype_string,			\
		  .u.basic.string.encoding = lttng_encode_UTF8,	\
		},						\
	},

#undef __string_from_user
#define __string_from_user(_item, _src)				\
	__string(_item, _src)

#undef TP_STRUCT__entry
#define TP_STRUCT__entry(args...) args	/* Only one used in this phase */

#undef DECLARE_EVENT_CLASS_NOARGS
#define DECLARE_EVENT_CLASS_NOARGS(_name, _tstruct, _assign, _print) \
	static const struct lttng_event_field __event_fields___##_name[] = { \
		_tstruct						     \
	};

#undef DECLARE_EVENT_CLASS
#define DECLARE_EVENT_CLASS(_name, _proto, _args, _tstruct, _assign, _print) \
	DECLARE_EVENT_CLASS_NOARGS(_name, PARAMS(_tstruct), PARAMS(_assign), \
			PARAMS(_print))

#include TRACE_INCLUDE(TRACE_INCLUDE_FILE)

/*
 * Stage 3 of the trace events.
 *
 * Create probe callback prototypes.
 */

#include "lttng-events-reset.h"	/* Reset all macros within TRACE_EVENT */

#undef TP_PROTO
#define TP_PROTO(args...) args

#undef DECLARE_EVENT_CLASS
#define DECLARE_EVENT_CLASS(_name, _proto, _args, _tstruct, _assign, _print)  \
static void __event_probe__##_name(void *__data, _proto);

#undef DECLARE_EVENT_CLASS_NOARGS
#define DECLARE_EVENT_CLASS_NOARGS(_name, _tstruct, _assign, _print)	      \
static void __event_probe__##_name(void *__data);

#include TRACE_INCLUDE(TRACE_INCLUDE_FILE)

/*
 * Stage 3.9 of the trace events.
 *
 * Create event descriptions.
 */

/* Named field types must be defined in lttng-types.h */

#include "lttng-events-reset.h"	/* Reset all macros within TRACE_EVENT */

#ifndef TP_PROBE_CB
#define TP_PROBE_CB(_template)	&__event_probe__##_template
#endif

#undef DEFINE_EVENT_MAP_NOARGS
#define DEFINE_EVENT_MAP_NOARGS(_template, _name, _map)			\
static const struct lttng_event_desc __event_desc___##_map = {		\
	.fields = __event_fields___##_template,		     		\
	.name = #_map,					     		\
	.probe_callback = (void *) TP_PROBE_CB(_template),   		\
	.nr_fields = ARRAY_SIZE(__event_fields___##_template),		\
	.owner = THIS_MODULE,				     		\
};

#undef DEFINE_EVENT_MAP
#define DEFINE_EVENT_MAP(_template, _name, _map, _proto, _args)		\
	DEFINE_EVENT_MAP_NOARGS(_template, _name, _map)

#include TRACE_INCLUDE(TRACE_INCLUDE_FILE)


/*
 * Stage 4 of the trace events.
 *
 * Create an array of event description pointers.
 */

/* Named field types must be defined in lttng-types.h */

#include "lttng-events-reset.h"	/* Reset all macros within TRACE_EVENT */

#undef DEFINE_EVENT_MAP_NOARGS
#define DEFINE_EVENT_MAP_NOARGS(_template, _name, _map)			       \
		&__event_desc___##_map,

#undef DEFINE_EVENT_MAP
#define DEFINE_EVENT_MAP(_template, _name, _map, _proto, _args)		       \
	DEFINE_EVENT_MAP_NOARGS(_template, _name, _map)

#define TP_ID1(_token, _system)	_token##_system
#define TP_ID(_token, _system)	TP_ID1(_token, _system)

static const struct lttng_event_desc *TP_ID(__event_desc___, TRACE_SYSTEM)[] = {
#include TRACE_INCLUDE(TRACE_INCLUDE_FILE)
};

#undef TP_ID1
#undef TP_ID


/*
 * Stage 5 of the trace events.
 *
 * Create a toplevel descriptor for the whole probe.
 */

#define TP_ID1(_token, _system)	_token##_system
#define TP_ID(_token, _system)	TP_ID1(_token, _system)

/* non-const because list head will be modified when registered. */
static __used struct lttng_probe_desc TP_ID(__probe_desc___, TRACE_SYSTEM) = {
	.event_desc = TP_ID(__event_desc___, TRACE_SYSTEM),
	.nr_events = ARRAY_SIZE(TP_ID(__event_desc___, TRACE_SYSTEM)),
};

#undef TP_ID1
#undef TP_ID

/*
 * Stage 6 of the trace events.
 *
 * Create static inline function that calculates event size.
 */

#include "lttng-events-reset.h"	/* Reset all macros within TRACE_EVENT */

/* Named field types must be defined in lttng-types.h */

#undef __field_full
#define __field_full(_type, _item, _order, _base)			       \
	__event_len += lib_ring_buffer_align(__event_len, lttng_alignof(_type)); \
	__event_len += sizeof(_type);

#undef __array_enc_ext
#define __array_enc_ext(_type, _item, _length, _order, _base, _encoding)       \
	__event_len += lib_ring_buffer_align(__event_len, lttng_alignof(_type)); \
	__event_len += sizeof(_type) * (_length);

#undef __dynamic_array_enc_ext
#define __dynamic_array_enc_ext(_type, _item, _length, _order, _base, _encoding)\
	__event_len += lib_ring_buffer_align(__event_len, lttng_alignof(u32));   \
	__event_len += sizeof(u32);					       \
	__event_len += lib_ring_buffer_align(__event_len, lttng_alignof(_type)); \
	__dynamic_len[__dynamic_len_idx] = (_length);			       \
	__event_len += sizeof(_type) * __dynamic_len[__dynamic_len_idx];       \
	__dynamic_len_idx++;

#undef __dynamic_array_enc_ext_2
#define __dynamic_array_enc_ext_2(_type, _item, _length1, _length2, _order, _base, _encoding)\
	__event_len += lib_ring_buffer_align(__event_len, lttng_alignof(u32));   \
	__event_len += sizeof(u32);					       \
	__event_len += lib_ring_buffer_align(__event_len, lttng_alignof(_type)); \
	__dynamic_len[__dynamic_len_idx] = (_length1);			       \
	__event_len += sizeof(_type) * __dynamic_len[__dynamic_len_idx];       \
	__dynamic_len_idx++;						       \
	__dynamic_len[__dynamic_len_idx] = (_length2);			       \
	__event_len += sizeof(_type) * __dynamic_len[__dynamic_len_idx];       \
	__dynamic_len_idx++;

#undef __string
#define __string(_item, _src)						       \
	__event_len += __dynamic_len[__dynamic_len_idx++] = strlen(_src) + 1;

/*
 * strlen_user includes \0. If returns 0, it faulted, so we set size to
 * 1 (\0 only).
 */
#undef __string_from_user
#define __string_from_user(_item, _src)					       \
	__event_len += __dynamic_len[__dynamic_len_idx++] =		       \
		max_t(size_t, lttng_strlen_user_inatomic(_src), 1);

#undef TP_PROTO
#define TP_PROTO(args...) args

#undef TP_STRUCT__entry
#define TP_STRUCT__entry(args...) args

#undef DECLARE_EVENT_CLASS
#define DECLARE_EVENT_CLASS(_name, _proto, _args, _tstruct, _assign, _print)  \
static inline size_t __event_get_size__##_name(size_t *__dynamic_len, _proto) \
{									      \
	size_t __event_len = 0;						      \
	unsigned int __dynamic_len_idx = 0;				      \
									      \
	if (0)								      \
		(void) __dynamic_len_idx;	/* don't warn if unused */    \
	_tstruct							      \
	return __event_len;						      \
}

#include TRACE_INCLUDE(TRACE_INCLUDE_FILE)

/*
 * Stage 7 of the trace events.
 *
 * Create static inline function that calculates event payload alignment.
 */

#include "lttng-events-reset.h"	/* Reset all macros within TRACE_EVENT */

/* Named field types must be defined in lttng-types.h */

#undef __field_full
#define __field_full(_type, _item, _order, _base)			  \
	__event_align = max_t(size_t, __event_align, lttng_alignof(_type));

#undef __array_enc_ext
#define __array_enc_ext(_type, _item, _length, _order, _base, _encoding)  \
	__event_align = max_t(size_t, __event_align, lttng_alignof(_type));

#undef __dynamic_array_enc_ext
#define __dynamic_array_enc_ext(_type, _item, _length, _order, _base, _encoding)\
	__event_align = max_t(size_t, __event_align, lttng_alignof(u32));	  \
	__event_align = max_t(size_t, __event_align, lttng_alignof(_type));

#undef __dynamic_array_enc_ext_2
#define __dynamic_array_enc_ext_2(_type, _item, _length1, _length2, _order, _base, _encoding)\
	__dynamic_array_enc_ext(_type, _item, _length1 + _length2, _order, _base, _encoding)

#undef __string
#define __string(_item, _src)

#undef __string_from_user
#define __string_from_user(_item, _src)

#undef TP_PROTO
#define TP_PROTO(args...) args

#undef TP_STRUCT__entry
#define TP_STRUCT__entry(args...) args

#undef DECLARE_EVENT_CLASS
#define DECLARE_EVENT_CLASS(_name, _proto, _args, _tstruct, _assign, _print)  \
static inline size_t __event_get_align__##_name(_proto)			      \
{									      \
	size_t __event_align = 1;					      \
	_tstruct							      \
	return __event_align;						      \
}

#include TRACE_INCLUDE(TRACE_INCLUDE_FILE)


/*
 * Stage 8 of the trace events.
 *
 * Create structure declaration that allows the "assign" macros to access the
 * field types.
 */

#include "lttng-events-reset.h"	/* Reset all macros within TRACE_EVENT */

/* Named field types must be defined in lttng-types.h */

#undef __field_full
#define __field_full(_type, _item, _order, _base)	_type	_item;

#undef __array_enc_ext
#define __array_enc_ext(_type, _item, _length, _order, _base, _encoding)  \
	_type	_item;

#undef __dynamic_array_enc_ext
#define __dynamic_array_enc_ext(_type, _item, _length, _order, _base, _encoding)\
	_type	_item;

#undef __dynamic_array_enc_ext_2
#define __dynamic_array_enc_ext_2(_type, _item, _length1, _length2, _order, _base, _encoding)\
	__dynamic_array_enc_ext(_type, _item, _length1 + _length2, _order, _base, _encoding)

#undef __string
#define __string(_item, _src)			char _item;

#undef __string_from_user
#define __string_from_user(_item, _src)		\
	__string(_item, _src)

#undef TP_STRUCT__entry
#define TP_STRUCT__entry(args...) args

#undef DECLARE_EVENT_CLASS
#define DECLARE_EVENT_CLASS(_name, _proto, _args, _tstruct, _assign, _print)  \
struct __event_typemap__##_name {					      \
	_tstruct							      \
};

#include TRACE_INCLUDE(TRACE_INCLUDE_FILE)


/*
 * Stage 9 of the trace events.
 *
 * Create the probe function : call even size calculation and write event data
 * into the buffer.
 *
 * We use both the field and assignment macros to write the fields in the order
 * defined in the field declaration. The field declarations control the
 * execution order, jumping to the appropriate assignment block.
 */

#include "lttng-events-reset.h"	/* Reset all macros within TRACE_EVENT */

#undef __field_full
#define __field_full(_type, _item, _order, _base)			\
	goto __assign_##_item;						\
__end_field_##_item:

#undef __array_enc_ext
#define __array_enc_ext(_type, _item, _length, _order, _base, _encoding)\
	goto __assign_##_item;						\
__end_field_##_item:

#undef __dynamic_array_enc_ext
#define __dynamic_array_enc_ext(_type, _item, _length, _order, _base, _encoding)\
	goto __assign_##_item##_1;					\
__end_field_##_item##_1:						\
	goto __assign_##_item##_2;					\
__end_field_##_item##_2:

#undef __dynamic_array_enc_ext_2
#define __dynamic_array_enc_ext_2(_type, _item, _length1, _length2, _order, _base, _encoding)\
	goto __assign_##_item##_1;					\
__end_field_##_item##_1:						\
	goto __assign_##_item##_2;					\
__end_field_##_item##_2:						\
	goto __assign_##_item##_3;					\
__end_field_##_item##_3:

#undef __string
#define __string(_item, _src)						\
	goto __assign_##_item;						\
__end_field_##_item:

#undef __string_from_user
#define __string_from_user(_item, _src)					\
	__string(_item, _src)

/*
 * Macros mapping tp_assign() to "=", tp_memcpy() to memcpy() and tp_strcpy() to
 * strcpy().
 */
#undef tp_assign
#define tp_assign(dest, src)						\
__assign_##dest:							\
	{								\
		__typeof__(__typemap.dest) __tmp = (src);		\
		lib_ring_buffer_align_ctx(&__ctx, lttng_alignof(__tmp));	\
		__chan->ops->event_write(&__ctx, &__tmp, sizeof(__tmp));\
	}								\
	goto __end_field_##dest;

/* fixed length array memcpy */
#undef tp_memcpy_gen
#define tp_memcpy_gen(write_ops, dest, src, len)			\
__assign_##dest:							\
	if (0)								\
		(void) __typemap.dest;					\
	lib_ring_buffer_align_ctx(&__ctx, lttng_alignof(__typemap.dest));	\
	__chan->ops->write_ops(&__ctx, src, len);			\
	goto __end_field_##dest;

#undef tp_memcpy
#define tp_memcpy(dest, src, len)					\
	tp_memcpy_gen(event_write, dest, src, len)

#undef tp_memcpy_from_user
#define tp_memcpy_from_user(dest, src, len)				\
	tp_memcpy_gen(event_write_from_user, dest, src, len)

/* variable length sequence memcpy */
#undef tp_memcpy_dyn_gen
#define tp_memcpy_dyn_gen(write_ops, dest, src)				\
__assign_##dest##_1:							\
	{								\
		u32 __tmpl = __dynamic_len[__dynamic_len_idx];		\
		lib_ring_buffer_align_ctx(&__ctx, lttng_alignof(u32));	\
		__chan->ops->event_write(&__ctx, &__tmpl, sizeof(u32));	\
	}								\
	goto __end_field_##dest##_1;					\
__assign_##dest##_2:							\
	lib_ring_buffer_align_ctx(&__ctx, lttng_alignof(__typemap.dest));	\
	__chan->ops->write_ops(&__ctx, src,				\
		sizeof(__typemap.dest) * __get_dynamic_array_len(dest));\
	goto __end_field_##dest##_2;

#undef tp_memcpy_dyn_gen_2
#define tp_memcpy_dyn_gen_2(write_ops, dest, src1, src2)		\
__assign_##dest##_1:							\
	{								\
		u32 __tmpl = __dynamic_len[__dynamic_len_idx]		\
			+ __dynamic_len[__dynamic_len_idx + 1];		\
		lib_ring_buffer_align_ctx(&__ctx, lttng_alignof(u32));	\
		__chan->ops->event_write(&__ctx, &__tmpl, sizeof(u32));	\
	}								\
	goto __end_field_##dest##_1;					\
__assign_##dest##_2:							\
	lib_ring_buffer_align_ctx(&__ctx, lttng_alignof(__typemap.dest));	\
	__chan->ops->write_ops(&__ctx, src1,				\
		sizeof(__typemap.dest) * __get_dynamic_array_len(dest));\
	goto __end_field_##dest##_2;					\
__assign_##dest##_3:							\
	__chan->ops->write_ops(&__ctx, src2,				\
		sizeof(__typemap.dest) * __get_dynamic_array_len(dest));\
	goto __end_field_##dest##_3;

#undef tp_memcpy_dyn
#define tp_memcpy_dyn(dest, src)					\
	tp_memcpy_dyn_gen(event_write, dest, src)

#undef tp_memcpy_dyn_2
#define tp_memcpy_dyn_2(dest, src1, src2)				\
	tp_memcpy_dyn_gen_2(event_write, dest, src1, src2)

#undef tp_memcpy_dyn_from_user
#define tp_memcpy_dyn_from_user(dest, src)				\
	tp_memcpy_dyn_gen(event_write_from_user, dest, src)

/*
 * The string length including the final \0.
 */
#undef tp_copy_string_from_user
#define tp_copy_string_from_user(dest, src)				\
	__assign_##dest:						\
	{								\
		size_t __ustrlen;					\
									\
		if (0)							\
			(void) __typemap.dest;				\
		lib_ring_buffer_align_ctx(&__ctx, lttng_alignof(__typemap.dest));\
		__ustrlen = __get_dynamic_array_len(dest);		\
		if (likely(__ustrlen > 1)) {				\
			__chan->ops->event_write_from_user(&__ctx, src,	\
				__ustrlen - 1);				\
		}							\
		__chan->ops->event_memset(&__ctx, 0, 1);		\
	}								\
	goto __end_field_##dest;
#undef tp_strcpy
#define tp_strcpy(dest, src)						\
	tp_memcpy(dest, src, __get_dynamic_array_len(dest))

/* Named field types must be defined in lttng-types.h */

#undef __get_str
#define __get_str(field)		field

#undef __get_dynamic_array
#define __get_dynamic_array(field)	field

/* Beware: this get len actually consumes the len value */
#undef __get_dynamic_array_len
#define __get_dynamic_array_len(field)	__dynamic_len[__dynamic_len_idx++]

#undef TP_PROTO
#define TP_PROTO(args...) args

#undef TP_ARGS
#define TP_ARGS(args...) args

#undef TP_STRUCT__entry
#define TP_STRUCT__entry(args...) args

#undef TP_fast_assign
#define TP_fast_assign(args...) args

/*
 * For state dump, check that "session" argument (mandatory) matches the
 * session this event belongs to. Ensures that we write state dump data only
 * into the started session, not into all sessions.
 */
#ifdef TP_SESSION_CHECK
#define _TP_SESSION_CHECK(session, csession)	(session == csession)
#else /* TP_SESSION_CHECK */
#define _TP_SESSION_CHECK(session, csession)	1
#endif /* TP_SESSION_CHECK */

/*
 * __dynamic_len array length is twice the number of fields due to
 * __dynamic_array_enc_ext_2() and tp_memcpy_dyn_2(), which are the
 * worse case, needing 2 entries per field.
 */
#undef DECLARE_EVENT_CLASS
#define DECLARE_EVENT_CLASS(_name, _proto, _args, _tstruct, _assign, _print)  \
static void __event_probe__##_name(void *__data, _proto)		      \
{									      \
	struct lttng_event *__event = __data;				      \
	struct lttng_channel *__chan = __event->chan;			      \
	struct lib_ring_buffer_ctx __ctx;				      \
	size_t __event_len, __event_align;				      \
	size_t __dynamic_len_idx = 0;					      \
	size_t __dynamic_len[2 * ARRAY_SIZE(__event_fields___##_name)];	      \
	struct __event_typemap__##_name __typemap;			      \
	int __ret;							      \
									      \
	if (0) {							      \
		(void) __dynamic_len_idx;	/* don't warn if unused */    \
		(void) __typemap;		/* don't warn if unused */    \
	}								      \
	if (!_TP_SESSION_CHECK(session, __chan->session))		      \
		return;							      \
	if (unlikely(!ACCESS_ONCE(__chan->session->active)))		      \
		return;							      \
	if (unlikely(!ACCESS_ONCE(__chan->enabled)))			      \
		return;							      \
	if (unlikely(!ACCESS_ONCE(__event->enabled)))			      \
		return;							      \
	__event_len = __event_get_size__##_name(__dynamic_len, _args);	      \
	__event_align = __event_get_align__##_name(_args);		      \
	lib_ring_buffer_ctx_init(&__ctx, __chan->chan, __event, __event_len,  \
				 __event_align, -1);			      \
	__ret = __chan->ops->event_reserve(&__ctx, __event->id);	      \
	if (__ret < 0)							      \
		return;							      \
	/* Control code (field ordering) */				      \
	_tstruct							      \
	__chan->ops->event_commit(&__ctx);				      \
	return;								      \
	/* Copy code, steered by control code */			      \
	_assign								      \
}

#undef DECLARE_EVENT_CLASS_NOARGS
#define DECLARE_EVENT_CLASS_NOARGS(_name, _tstruct, _assign, _print)	      \
static void __event_probe__##_name(void *__data)			      \
{									      \
	struct lttng_event *__event = __data;				      \
	struct lttng_channel *__chan = __event->chan;			      \
	struct lib_ring_buffer_ctx __ctx;				      \
	size_t __event_len, __event_align;				      \
	int __ret;							      \
									      \
	if (!_TP_SESSION_CHECK(session, __chan->session))		      \
		return;							      \
	if (unlikely(!ACCESS_ONCE(__chan->session->active)))		      \
		return;							      \
	if (unlikely(!ACCESS_ONCE(__chan->enabled)))			      \
		return;							      \
	if (unlikely(!ACCESS_ONCE(__event->enabled)))			      \
		return;							      \
	__event_len = 0;						      \
	__event_align = 1;						      \
	lib_ring_buffer_ctx_init(&__ctx, __chan->chan, __event, __event_len,  \
				 __event_align, -1);			      \
	__ret = __chan->ops->event_reserve(&__ctx, __event->id);	      \
	if (__ret < 0)							      \
		return;							      \
	/* Control code (field ordering) */				      \
	_tstruct							      \
	__chan->ops->event_commit(&__ctx);				      \
	return;								      \
	/* Copy code, steered by control code */			      \
	_assign								      \
}

#include TRACE_INCLUDE(TRACE_INCLUDE_FILE)

/*
 * Stage 10 of the trace events.
 *
 * Register/unregister probes at module load/unload.
 */

#include "lttng-events-reset.h"	/* Reset all macros within TRACE_EVENT */

#define TP_ID1(_token, _system)	_token##_system
#define TP_ID(_token, _system)	TP_ID1(_token, _system)
#define module_init_eval1(_token, _system)	module_init(_token##_system)
#define module_init_eval(_token, _system)	module_init_eval1(_token, _system)
#define module_exit_eval1(_token, _system)	module_exit(_token##_system)
#define module_exit_eval(_token, _system)	module_exit_eval1(_token, _system)

#ifndef TP_MODULE_NOINIT
static int TP_ID(__lttng_events_init__, TRACE_SYSTEM)(void)
{
	wrapper_vmalloc_sync_all();
	return lttng_probe_register(&TP_ID(__probe_desc___, TRACE_SYSTEM));
}

static void TP_ID(__lttng_events_exit__, TRACE_SYSTEM)(void)
{
	lttng_probe_unregister(&TP_ID(__probe_desc___, TRACE_SYSTEM));
}

#ifndef TP_MODULE_NOAUTOLOAD
module_init_eval(__lttng_events_init__, TRACE_SYSTEM);
module_exit_eval(__lttng_events_exit__, TRACE_SYSTEM);
#endif

#endif

#undef module_init_eval
#undef module_exit_eval
#undef TP_ID1
#undef TP_ID

#undef TP_PROTO
#undef TP_ARGS
#undef TRACE_EVENT_FLAGS
