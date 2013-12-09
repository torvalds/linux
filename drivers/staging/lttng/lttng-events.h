#ifndef _LTTNG_EVENTS_H
#define _LTTNG_EVENTS_H

/*
 * lttng-events.h
 *
 * Holds LTTng per-session event registry.
 *
 * Copyright (C) 2010-2012 Mathieu Desnoyers <mathieu.desnoyers@efficios.com>
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

#include <linux/version.h>
#include <linux/list.h>
#include <linux/kprobes.h>
#include <linux/kref.h>
#include "wrapper/uuid.h"
#include "lttng-abi.h"
#include "lttng-abi-old.h"

#define lttng_is_signed_type(type)	(((type)(-1)) < 0)

struct lttng_channel;
struct lttng_session;
struct lttng_metadata_cache;
struct lib_ring_buffer_ctx;
struct perf_event;
struct perf_event_attr;

/* Type description */

/* Update the astract_types name table in lttng-types.c along with this enum */
enum abstract_types {
	atype_integer,
	atype_enum,
	atype_array,
	atype_sequence,
	atype_string,
	NR_ABSTRACT_TYPES,
};

/* Update the string_encodings name table in lttng-types.c along with this enum */
enum lttng_string_encodings {
	lttng_encode_none = 0,
	lttng_encode_UTF8 = 1,
	lttng_encode_ASCII = 2,
	NR_STRING_ENCODINGS,
};

enum channel_type {
	PER_CPU_CHANNEL,
	METADATA_CHANNEL,
};

struct lttng_enum_entry {
	unsigned long long start, end;	/* start and end are inclusive */
	const char *string;
};

#define __type_integer(_type, _byte_order, _base, _encoding)	\
	{							\
	    .atype = atype_integer,				\
	    .u.basic.integer =					\
		{						\
		  .size = sizeof(_type) * CHAR_BIT,		\
		  .alignment = lttng_alignof(_type) * CHAR_BIT,	\
		  .signedness = lttng_is_signed_type(_type),	\
		  .reverse_byte_order = _byte_order != __BYTE_ORDER,	\
		  .base = _base,				\
		  .encoding = lttng_encode_##_encoding,		\
		},						\
	}							\

struct lttng_integer_type {
	unsigned int size;		/* in bits */
	unsigned short alignment;	/* in bits */
	unsigned int signedness:1,
		reverse_byte_order:1;
	unsigned int base;		/* 2, 8, 10, 16, for pretty print */
	enum lttng_string_encodings encoding;
};

union _lttng_basic_type {
	struct lttng_integer_type integer;
	struct {
		const char *name;
	} enumeration;
	struct {
		enum lttng_string_encodings encoding;
	} string;
};

struct lttng_basic_type {
	enum abstract_types atype;
	union {
		union _lttng_basic_type basic;
	} u;
};

struct lttng_type {
	enum abstract_types atype;
	union {
		union _lttng_basic_type basic;
		struct {
			struct lttng_basic_type elem_type;
			unsigned int length;		/* num. elems. */
		} array;
		struct {
			struct lttng_basic_type length_type;
			struct lttng_basic_type elem_type;
		} sequence;
	} u;
};

struct lttng_enum {
	const char *name;
	struct lttng_type container_type;
	const struct lttng_enum_entry *entries;
	unsigned int len;
};

/* Event field description */

struct lttng_event_field {
	const char *name;
	struct lttng_type type;
};

/*
 * We need to keep this perf counter field separately from struct
 * lttng_ctx_field because cpu hotplug needs fixed-location addresses.
 */
struct lttng_perf_counter_field {
	struct notifier_block nb;
	int hp_enable;
	struct perf_event_attr *attr;
	struct perf_event **e;	/* per-cpu array */
};

struct lttng_ctx_field {
	struct lttng_event_field event_field;
	size_t (*get_size)(size_t offset);
	void (*record)(struct lttng_ctx_field *field,
		       struct lib_ring_buffer_ctx *ctx,
		       struct lttng_channel *chan);
	union {
		struct lttng_perf_counter_field *perf_counter;
	} u;
	void (*destroy)(struct lttng_ctx_field *field);
};

struct lttng_ctx {
	struct lttng_ctx_field *fields;
	unsigned int nr_fields;
	unsigned int allocated_fields;
};

struct lttng_event_desc {
	const char *name;
	void *probe_callback;
	const struct lttng_event_ctx *ctx;	/* context */
	const struct lttng_event_field *fields;	/* event payload */
	unsigned int nr_fields;
	struct module *owner;
};

struct lttng_probe_desc {
	const struct lttng_event_desc **event_desc;
	unsigned int nr_events;
	struct list_head head;			/* chain registered probes */
};

struct lttng_krp;				/* Kretprobe handling */

/*
 * lttng_event structure is referred to by the tracing fast path. It must be
 * kept small.
 */
struct lttng_event {
	unsigned int id;
	struct lttng_channel *chan;
	int enabled;
	const struct lttng_event_desc *desc;
	void *filter;
	struct lttng_ctx *ctx;
	enum lttng_kernel_instrumentation instrumentation;
	union {
		struct {
			struct kprobe kp;
			char *symbol_name;
		} kprobe;
		struct {
			struct lttng_krp *lttng_krp;
			char *symbol_name;
		} kretprobe;
		struct {
			char *symbol_name;
		} ftrace;
	} u;
	struct list_head list;		/* Event list */
	unsigned int metadata_dumped:1;
};

struct lttng_channel_ops {
	struct channel *(*channel_create)(const char *name,
				struct lttng_channel *lttng_chan,
				void *buf_addr,
				size_t subbuf_size, size_t num_subbuf,
				unsigned int switch_timer_interval,
				unsigned int read_timer_interval);
	void (*channel_destroy)(struct channel *chan);
	struct lib_ring_buffer *(*buffer_read_open)(struct channel *chan);
	int (*buffer_has_read_closed_stream)(struct channel *chan);
	void (*buffer_read_close)(struct lib_ring_buffer *buf);
	int (*event_reserve)(struct lib_ring_buffer_ctx *ctx,
			     uint32_t event_id);
	void (*event_commit)(struct lib_ring_buffer_ctx *ctx);
	void (*event_write)(struct lib_ring_buffer_ctx *ctx, const void *src,
			    size_t len);
	void (*event_write_from_user)(struct lib_ring_buffer_ctx *ctx,
				      const void *src, size_t len);
	void (*event_memset)(struct lib_ring_buffer_ctx *ctx,
			     int c, size_t len);
	/*
	 * packet_avail_size returns the available size in the current
	 * packet. Note that the size returned is only a hint, since it
	 * may change due to concurrent writes.
	 */
	size_t (*packet_avail_size)(struct channel *chan);
	wait_queue_head_t *(*get_writer_buf_wait_queue)(struct channel *chan, int cpu);
	wait_queue_head_t *(*get_hp_wait_queue)(struct channel *chan);
	int (*is_finalized)(struct channel *chan);
	int (*is_disabled)(struct channel *chan);
};

struct lttng_transport {
	char *name;
	struct module *owner;
	struct list_head node;
	struct lttng_channel_ops ops;
};

struct lttng_channel {
	unsigned int id;
	struct channel *chan;		/* Channel buffers */
	int enabled;
	struct lttng_ctx *ctx;
	/* Event ID management */
	struct lttng_session *session;
	struct file *file;		/* File associated to channel */
	unsigned int free_event_id;	/* Next event ID to allocate */
	struct list_head list;		/* Channel list */
	struct lttng_channel_ops *ops;
	struct lttng_transport *transport;
	struct lttng_event **sc_table;	/* for syscall tracing */
	struct lttng_event **compat_sc_table;
	struct lttng_event *sc_unknown;	/* for unknown syscalls */
	struct lttng_event *sc_compat_unknown;
	struct lttng_event *sc_exit;	/* for syscall exit */
	int header_type;		/* 0: unset, 1: compact, 2: large */
	enum channel_type channel_type;
	unsigned int metadata_dumped:1;
};

struct lttng_metadata_stream {
	void *priv;			/* Ring buffer private data */
	struct lttng_metadata_cache *metadata_cache;
	unsigned int metadata_in;	/* Bytes read from the cache */
	unsigned int metadata_out;	/* Bytes consumed from stream */
	int finalized;			/* Has channel been finalized */
	wait_queue_head_t read_wait;	/* Reader buffer-level wait queue */
	struct list_head list;		/* Stream list */
	struct lttng_transport *transport;
};

struct lttng_session {
	int active;			/* Is trace session active ? */
	int been_active;		/* Has trace session been active ? */
	struct file *file;		/* File associated to session */
	struct list_head chan;		/* Channel list head */
	struct list_head events;	/* Event list head */
	struct list_head list;		/* Session list */
	unsigned int free_chan_id;	/* Next chan ID to allocate */
	uuid_le uuid;			/* Trace session unique ID */
	struct lttng_metadata_cache *metadata_cache;
	unsigned int metadata_dumped:1;
};

struct lttng_metadata_cache {
	char *data;			/* Metadata cache */
	unsigned int cache_alloc;	/* Metadata allocated size (bytes) */
	unsigned int metadata_written;	/* Number of bytes written in metadata cache */
	struct kref refcount;		/* Metadata cache usage */
	struct list_head metadata_stream;	/* Metadata stream list */
};

struct lttng_session *lttng_session_create(void);
int lttng_session_enable(struct lttng_session *session);
int lttng_session_disable(struct lttng_session *session);
void lttng_session_destroy(struct lttng_session *session);
void metadata_cache_destroy(struct kref *kref);

struct lttng_channel *lttng_channel_create(struct lttng_session *session,
				       const char *transport_name,
				       void *buf_addr,
				       size_t subbuf_size, size_t num_subbuf,
				       unsigned int switch_timer_interval,
				       unsigned int read_timer_interval,
				       enum channel_type channel_type);
struct lttng_channel *lttng_global_channel_create(struct lttng_session *session,
				       int overwrite, void *buf_addr,
				       size_t subbuf_size, size_t num_subbuf,
				       unsigned int switch_timer_interval,
				       unsigned int read_timer_interval);

void lttng_metadata_channel_destroy(struct lttng_channel *chan);
struct lttng_event *lttng_event_create(struct lttng_channel *chan,
				   struct lttng_kernel_event *event_param,
				   void *filter,
				   const struct lttng_event_desc *internal_desc);
struct lttng_event *lttng_event_compat_old_create(struct lttng_channel *chan,
		struct lttng_kernel_old_event *old_event_param,
		void *filter,
		const struct lttng_event_desc *internal_desc);

int lttng_channel_enable(struct lttng_channel *channel);
int lttng_channel_disable(struct lttng_channel *channel);
int lttng_event_enable(struct lttng_event *event);
int lttng_event_disable(struct lttng_event *event);

void lttng_transport_register(struct lttng_transport *transport);
void lttng_transport_unregister(struct lttng_transport *transport);

void synchronize_trace(void);
int lttng_abi_init(void);
int lttng_abi_compat_old_init(void);
void lttng_abi_exit(void);
void lttng_abi_compat_old_exit(void);

int lttng_probe_register(struct lttng_probe_desc *desc);
void lttng_probe_unregister(struct lttng_probe_desc *desc);
const struct lttng_event_desc *lttng_event_get(const char *name);
void lttng_event_put(const struct lttng_event_desc *desc);
int lttng_probes_init(void);
void lttng_probes_exit(void);

int lttng_metadata_output_channel(struct lttng_metadata_stream *stream,
		struct channel *chan);

#if defined(CONFIG_HAVE_SYSCALL_TRACEPOINTS)
int lttng_syscalls_register(struct lttng_channel *chan, void *filter);
int lttng_syscalls_unregister(struct lttng_channel *chan);
#else
static inline int lttng_syscalls_register(struct lttng_channel *chan, void *filter)
{
	return -ENOSYS;
}

static inline int lttng_syscalls_unregister(struct lttng_channel *chan)
{
	return 0;
}
#endif

struct lttng_ctx_field *lttng_append_context(struct lttng_ctx **ctx);
int lttng_find_context(struct lttng_ctx *ctx, const char *name);
void lttng_remove_context_field(struct lttng_ctx **ctx,
				struct lttng_ctx_field *field);
void lttng_destroy_context(struct lttng_ctx *ctx);
int lttng_add_pid_to_ctx(struct lttng_ctx **ctx);
int lttng_add_procname_to_ctx(struct lttng_ctx **ctx);
int lttng_add_prio_to_ctx(struct lttng_ctx **ctx);
int lttng_add_nice_to_ctx(struct lttng_ctx **ctx);
int lttng_add_vpid_to_ctx(struct lttng_ctx **ctx);
int lttng_add_tid_to_ctx(struct lttng_ctx **ctx);
int lttng_add_vtid_to_ctx(struct lttng_ctx **ctx);
int lttng_add_ppid_to_ctx(struct lttng_ctx **ctx);
int lttng_add_vppid_to_ctx(struct lttng_ctx **ctx);
int lttng_add_hostname_to_ctx(struct lttng_ctx **ctx);
#if defined(CONFIG_PERF_EVENTS) && (LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,33))
int lttng_add_perf_counter_to_ctx(uint32_t type,
				  uint64_t config,
				  const char *name,
				  struct lttng_ctx **ctx);
#else
static inline
int lttng_add_perf_counter_to_ctx(uint32_t type,
				  uint64_t config,
				  const char *name,
				  struct lttng_ctx **ctx)
{
	return -ENOSYS;
}
#endif

extern int lttng_statedump_start(struct lttng_session *session);

#ifdef CONFIG_KPROBES
int lttng_kprobes_register(const char *name,
		const char *symbol_name,
		uint64_t offset,
		uint64_t addr,
		struct lttng_event *event);
void lttng_kprobes_unregister(struct lttng_event *event);
void lttng_kprobes_destroy_private(struct lttng_event *event);
#else
static inline
int lttng_kprobes_register(const char *name,
		const char *symbol_name,
		uint64_t offset,
		uint64_t addr,
		struct lttng_event *event)
{
	return -ENOSYS;
}

static inline
void lttng_kprobes_unregister(struct lttng_event *event)
{
}

static inline
void lttng_kprobes_destroy_private(struct lttng_event *event)
{
}
#endif

#ifdef CONFIG_KRETPROBES
int lttng_kretprobes_register(const char *name,
		const char *symbol_name,
		uint64_t offset,
		uint64_t addr,
		struct lttng_event *event_entry,
		struct lttng_event *event_exit);
void lttng_kretprobes_unregister(struct lttng_event *event);
void lttng_kretprobes_destroy_private(struct lttng_event *event);
#else
static inline
int lttng_kretprobes_register(const char *name,
		const char *symbol_name,
		uint64_t offset,
		uint64_t addr,
		struct lttng_event *event_entry,
		struct lttng_event *event_exit)
{
	return -ENOSYS;
}

static inline
void lttng_kretprobes_unregister(struct lttng_event *event)
{
}

static inline
void lttng_kretprobes_destroy_private(struct lttng_event *event)
{
}
#endif

#ifdef CONFIG_DYNAMIC_FTRACE
int lttng_ftrace_register(const char *name,
			  const char *symbol_name,
			  struct lttng_event *event);
void lttng_ftrace_unregister(struct lttng_event *event);
void lttng_ftrace_destroy_private(struct lttng_event *event);
#else
static inline
int lttng_ftrace_register(const char *name,
			  const char *symbol_name,
			  struct lttng_event *event)
{
	return -ENOSYS;
}

static inline
void lttng_ftrace_unregister(struct lttng_event *event)
{
}

static inline
void lttng_ftrace_destroy_private(struct lttng_event *event)
{
}
#endif

int lttng_calibrate(struct lttng_kernel_calibrate *calibrate);

extern const struct file_operations lttng_tracepoint_list_fops;

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,35))
#define TRACEPOINT_HAS_DATA_ARG
#endif

#endif /* _LTTNG_EVENTS_H */
