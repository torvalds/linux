/*
 * lttng-events.h
 *
 * Copyright (C) 2009 Steven Rostedt <rostedt@goodmis.org>
 * Copyright (C) 2010-2011 Mathieu Desnoyers <mathieu.desnoyers@efficios.com>
 *
 * Dual LGPL v2.1/GPL v2 license.
 */

#include <linux/debugfs.h>
#include "lttng.h"
#include "lttng-types.h"
#include "../wrapper/vmalloc.h"	/* for wrapper_vmalloc_sync_all() */
#include "../wrapper/ringbuffer/frontend_types.h"
#include "../ltt-events.h"
#include "../ltt-tracer-core.h"

/*
 * Macro declarations used for all stages.
 */

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
#define TRACE_EVENT(name, proto, args, tstruct, assign, print) \
	DECLARE_EVENT_CLASS(name,			       \
			     PARAMS(proto),		       \
			     PARAMS(args),		       \
			     PARAMS(tstruct),		       \
			     PARAMS(assign),		       \
			     PARAMS(print))		       \
	DEFINE_EVENT(name, name, PARAMS(proto), PARAMS(args))

#undef TRACE_EVENT_NOARGS
#define TRACE_EVENT_NOARGS(name, tstruct, assign, print)       \
	DECLARE_EVENT_CLASS_NOARGS(name,		       \
			     PARAMS(tstruct),		       \
			     PARAMS(assign),		       \
			     PARAMS(print))		       \
	DEFINE_EVENT_NOARGS(name, name)


#undef DEFINE_EVENT_PRINT
#define DEFINE_EVENT_PRINT(template, name, proto, args, print)	\
	DEFINE_EVENT(template, name, PARAMS(proto), PARAMS(args))

/* Callbacks are meaningless to LTTng. */
#undef TRACE_EVENT_FN
#define TRACE_EVENT_FN(name, proto, args, tstruct,			\
		assign, print, reg, unreg)				\
	TRACE_EVENT(name, PARAMS(proto), PARAMS(args),			\
		PARAMS(tstruct), PARAMS(assign), PARAMS(print))		\

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

#undef DEFINE_EVENT
#define DEFINE_EVENT(_template, _name, _proto, _args)			\
void trace_##_name(_proto);

#undef DEFINE_EVENT_NOARGS
#define DEFINE_EVENT_NOARGS(_template, _name)				\
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

#undef __dynamic_array
#define __dynamic_array(_type, _item, _length)			\
	__dynamic_array_enc_ext(_type, _item, _length, __BYTE_ORDER, 10, none)

#undef __dynamic_array_text
#define __dynamic_array_text(_type, _item, _length)		\
	__dynamic_array_enc_ext(_type, _item, _length, __BYTE_ORDER, 10, UTF8)

#undef __dynamic_array_hex
#define __dynamic_array_hex(_type, _item, _length)		\
	__dynamic_array_enc_ext(_type, _item, _length, __BYTE_ORDER, 16, none)

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

#undef DEFINE_EVENT_NOARGS
#define DEFINE_EVENT_NOARGS(_template, _name)				\
static const struct lttng_event_desc __event_desc___##_name = {		\
	.fields = __event_fields___##_template,		     		\
	.name = #_name,					     		\
	.probe_callback = (void *) TP_PROBE_CB(_template),   		\
	.nr_fields = ARRAY_SIZE(__event_fields___##_template),		\
	.owner = THIS_MODULE,				     		\
};

#undef DEFINE_EVENT
#define DEFINE_EVENT(_template, _name, _proto, _args)			\
	DEFINE_EVENT_NOARGS(_template, _name)

#include TRACE_INCLUDE(TRACE_INCLUDE_FILE)


/*
 * Stage 4 of the trace events.
 *
 * Create an array of event description pointers.
 */

/* Named field types must be defined in lttng-types.h */

#include "lttng-events-reset.h"	/* Reset all macros within TRACE_EVENT */

#undef DEFINE_EVENT_NOARGS
#define DEFINE_EVENT_NOARGS(_template, _name)				       \
		&__event_desc___##_name,

#undef DEFINE_EVENT
#define DEFINE_EVENT(_template, _name, _proto, _args)			       \
	DEFINE_EVENT_NOARGS(_template, _name)

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
	__event_len += lib_ring_buffer_align(__event_len, ltt_alignof(_type)); \
	__event_len += sizeof(_type);

#undef __array_enc_ext
#define __array_enc_ext(_type, _item, _length, _order, _base, _encoding)       \
	__event_len += lib_ring_buffer_align(__event_len, ltt_alignof(_type)); \
	__event_len += sizeof(_type) * (_length);

#undef __dynamic_array_enc_ext
#define __dynamic_array_enc_ext(_type, _item, _length, _order, _base, _encoding)\
	__event_len += lib_ring_buffer_align(__event_len, ltt_alignof(u32));   \
	__event_len += sizeof(u32);					       \
	__event_len += lib_ring_buffer_align(__event_len, ltt_alignof(_type)); \
	__dynamic_len[__dynamic_len_idx] = (_length);			       \
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
		min_t(size_t, strlen_user(_src), 1);

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
	__event_align = max_t(size_t, __event_align, ltt_alignof(_type));

#undef __array_enc_ext
#define __array_enc_ext(_type, _item, _length, _order, _base, _encoding)  \
	__event_align = max_t(size_t, __event_align, ltt_alignof(_type));

#undef __dynamic_array_enc_ext
#define __dynamic_array_enc_ext(_type, _item, _length, _order, _base, _encoding)\
	__event_align = max_t(size_t, __event_align, ltt_alignof(u32));	  \
	__event_align = max_t(size_t, __event_align, ltt_alignof(_type));

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
		lib_ring_buffer_align_ctx(&__ctx, ltt_alignof(__tmp));	\
		__chan->ops->event_write(&__ctx, &__tmp, sizeof(__tmp));\
	}								\
	goto __end_field_##dest;

#undef tp_memcpy
#define tp_memcpy(dest, src, len)					\
__assign_##dest:							\
	if (0)								\
		(void) __typemap.dest;					\
	lib_ring_buffer_align_ctx(&__ctx, ltt_alignof(__typemap.dest));	\
	__chan->ops->event_write(&__ctx, src, len);			\
	goto __end_field_##dest;

#undef tp_memcpy_dyn
#define tp_memcpy_dyn(dest, src)					\
__assign_##dest##_1:							\
	{								\
		u32 __tmpl = __dynamic_len[__dynamic_len_idx];		\
		lib_ring_buffer_align_ctx(&__ctx, ltt_alignof(u32));	\
		__chan->ops->event_write(&__ctx, &__tmpl, sizeof(u32));	\
	}								\
	goto __end_field_##dest##_1;					\
__assign_##dest##_2:							\
	lib_ring_buffer_align_ctx(&__ctx, ltt_alignof(__typemap.dest));	\
	__chan->ops->event_write(&__ctx, src,				\
		sizeof(__typemap.dest) * __get_dynamic_array_len(dest));\
	goto __end_field_##dest##_2;

#undef tp_memcpy_from_user
#define tp_memcpy_from_user(dest, src, len)				\
	__assign_##dest:						\
	if (0)								\
		(void) __typemap.dest;					\
	lib_ring_buffer_align_ctx(&__ctx, ltt_alignof(__typemap.dest));	\
	__chan->ops->event_write_from_user(&__ctx, src, len);		\
	goto __end_field_##dest;

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
		lib_ring_buffer_align_ctx(&__ctx, ltt_alignof(__typemap.dest));\
		__ustrlen = __get_dynamic_array_len(dest);		\
		if (likely(__ustrlen) > 1) {				\
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

#undef DECLARE_EVENT_CLASS
#define DECLARE_EVENT_CLASS(_name, _proto, _args, _tstruct, _assign, _print)  \
static void __event_probe__##_name(void *__data, _proto)		      \
{									      \
	struct ltt_event *__event = __data;				      \
	struct ltt_channel *__chan = __event->chan;			      \
	struct lib_ring_buffer_ctx __ctx;				      \
	size_t __event_len, __event_align;				      \
	size_t __dynamic_len_idx = 0;					      \
	size_t __dynamic_len[ARRAY_SIZE(__event_fields___##_name)];	      \
	struct __event_typemap__##_name __typemap;			      \
	int __ret;							      \
									      \
	if (0)								      \
		(void) __dynamic_len_idx;	/* don't warn if unused */    \
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
	struct ltt_event *__event = __data;				      \
	struct ltt_channel *__chan = __event->chan;			      \
	struct lib_ring_buffer_ctx __ctx;				      \
	size_t __event_len, __event_align;				      \
	int __ret;							      \
									      \
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

#ifndef TP_MODULE_OVERRIDE
static int TP_ID(__lttng_events_init__, TRACE_SYSTEM)(void)
{
	wrapper_vmalloc_sync_all();
	return ltt_probe_register(&TP_ID(__probe_desc___, TRACE_SYSTEM));
}

module_init_eval(__lttng_events_init__, TRACE_SYSTEM);

static void TP_ID(__lttng_events_exit__, TRACE_SYSTEM)(void)
{
	ltt_probe_unregister(&TP_ID(__probe_desc___, TRACE_SYSTEM));
}

module_exit_eval(__lttng_events_exit__, TRACE_SYSTEM);
#endif

#undef module_init_eval
#undef module_exit_eval
#undef TP_ID1
#undef TP_ID

#undef TP_PROTO
#undef TP_ARGS
#undef TRACE_EVENT_FLAGS
