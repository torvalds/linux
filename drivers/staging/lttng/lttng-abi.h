#ifndef _LTTNG_ABI_H
#define _LTTNG_ABI_H

/*
 * lttng-abi.h
 *
 * LTTng ABI header
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

#include <linux/fs.h>

#define LTTNG_KERNEL_SYM_NAME_LEN	256

enum lttng_kernel_instrumentation {
	LTTNG_KERNEL_TRACEPOINT	= 0,
	LTTNG_KERNEL_KPROBE	= 1,
	LTTNG_KERNEL_FUNCTION	= 2,
	LTTNG_KERNEL_KRETPROBE	= 3,
	LTTNG_KERNEL_NOOP	= 4,	/* not hooked */
	LTTNG_KERNEL_SYSCALL	= 5,
};

/*
 * LTTng consumer mode
 */
enum lttng_kernel_output {
	LTTNG_KERNEL_SPLICE	= 0,
	LTTNG_KERNEL_MMAP	= 1,
};

/*
 * LTTng DebugFS ABI structures.
 */
#define LTTNG_KERNEL_CHANNEL_PADDING	LTTNG_KERNEL_SYM_NAME_LEN + 32
struct lttng_kernel_channel {
	uint64_t subbuf_size;			/* in bytes */
	uint64_t num_subbuf;
	unsigned int switch_timer_interval;	/* usecs */
	unsigned int read_timer_interval;	/* usecs */
	enum lttng_kernel_output output;	/* splice, mmap */
	int overwrite;				/* 1: overwrite, 0: discard */
	char padding[LTTNG_KERNEL_CHANNEL_PADDING];
}__attribute__((packed));

struct lttng_kernel_kretprobe {
	uint64_t addr;

	uint64_t offset;
	char symbol_name[LTTNG_KERNEL_SYM_NAME_LEN];
}__attribute__((packed));

/*
 * Either addr is used, or symbol_name and offset.
 */
struct lttng_kernel_kprobe {
	uint64_t addr;

	uint64_t offset;
	char symbol_name[LTTNG_KERNEL_SYM_NAME_LEN];
}__attribute__((packed));

struct lttng_kernel_function_tracer {
	char symbol_name[LTTNG_KERNEL_SYM_NAME_LEN];
}__attribute__((packed));

/*
 * For syscall tracing, name = '\0' means "enable all".
 */
#define LTTNG_KERNEL_EVENT_PADDING1	16
#define LTTNG_KERNEL_EVENT_PADDING2	LTTNG_KERNEL_SYM_NAME_LEN + 32
struct lttng_kernel_event {
	char name[LTTNG_KERNEL_SYM_NAME_LEN];	/* event name */
	enum lttng_kernel_instrumentation instrumentation;
	char padding[LTTNG_KERNEL_EVENT_PADDING1];

	/* Per instrumentation type configuration */
	union {
		struct lttng_kernel_kretprobe kretprobe;
		struct lttng_kernel_kprobe kprobe;
		struct lttng_kernel_function_tracer ftrace;
		char padding[LTTNG_KERNEL_EVENT_PADDING2];
	} u;
}__attribute__((packed));

struct lttng_kernel_tracer_version {
	uint32_t major;
	uint32_t minor;
	uint32_t patchlevel;
}__attribute__((packed));

enum lttng_kernel_calibrate_type {
	LTTNG_KERNEL_CALIBRATE_KRETPROBE,
};

struct lttng_kernel_calibrate {
	enum lttng_kernel_calibrate_type type;	/* type (input) */
}__attribute__((packed));

enum lttng_kernel_context_type {
	LTTNG_KERNEL_CONTEXT_PID		= 0,
	LTTNG_KERNEL_CONTEXT_PERF_COUNTER	= 1,
	LTTNG_KERNEL_CONTEXT_PROCNAME		= 2,
	LTTNG_KERNEL_CONTEXT_PRIO		= 3,
	LTTNG_KERNEL_CONTEXT_NICE		= 4,
	LTTNG_KERNEL_CONTEXT_VPID		= 5,
	LTTNG_KERNEL_CONTEXT_TID		= 6,
	LTTNG_KERNEL_CONTEXT_VTID		= 7,
	LTTNG_KERNEL_CONTEXT_PPID		= 8,
	LTTNG_KERNEL_CONTEXT_VPPID		= 9,
	LTTNG_KERNEL_CONTEXT_HOSTNAME		= 10,
};

struct lttng_kernel_perf_counter_ctx {
	uint32_t type;
	uint64_t config;
	char name[LTTNG_KERNEL_SYM_NAME_LEN];
}__attribute__((packed));

#define LTTNG_KERNEL_CONTEXT_PADDING1	16
#define LTTNG_KERNEL_CONTEXT_PADDING2	LTTNG_KERNEL_SYM_NAME_LEN + 32
struct lttng_kernel_context {
	enum lttng_kernel_context_type ctx;
	char padding[LTTNG_KERNEL_CONTEXT_PADDING1];

	union {
		struct lttng_kernel_perf_counter_ctx perf_counter;
		char padding[LTTNG_KERNEL_CONTEXT_PADDING2];
	} u;
}__attribute__((packed));

/* LTTng file descriptor ioctl */
#define LTTNG_KERNEL_SESSION			_IO(0xF6, 0x45)
#define LTTNG_KERNEL_TRACER_VERSION		\
	_IOR(0xF6, 0x46, struct lttng_kernel_tracer_version)
#define LTTNG_KERNEL_TRACEPOINT_LIST		_IO(0xF6, 0x47)
#define LTTNG_KERNEL_WAIT_QUIESCENT		_IO(0xF6, 0x48)
#define LTTNG_KERNEL_CALIBRATE			\
	_IOWR(0xF6, 0x49, struct lttng_kernel_calibrate)

/* Session FD ioctl */
#define LTTNG_KERNEL_METADATA			\
	_IOW(0xF6, 0x54, struct lttng_kernel_channel)
#define LTTNG_KERNEL_CHANNEL			\
	_IOW(0xF6, 0x55, struct lttng_kernel_channel)
#define LTTNG_KERNEL_SESSION_START		_IO(0xF6, 0x56)
#define LTTNG_KERNEL_SESSION_STOP		_IO(0xF6, 0x57)

/* Channel FD ioctl */
#define LTTNG_KERNEL_STREAM			_IO(0xF6, 0x62)
#define LTTNG_KERNEL_EVENT			\
	_IOW(0xF6, 0x63, struct lttng_kernel_event)

/* Event and Channel FD ioctl */
#define LTTNG_KERNEL_CONTEXT			\
	_IOW(0xF6, 0x71, struct lttng_kernel_context)

/* Event, Channel and Session ioctl */
#define LTTNG_KERNEL_ENABLE			_IO(0xF6, 0x82)
#define LTTNG_KERNEL_DISABLE			_IO(0xF6, 0x83)

#endif /* _LTTNG_ABI_H */
