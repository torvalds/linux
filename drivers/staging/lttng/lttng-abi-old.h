#ifndef _LTTNG_ABI_OLD_H
#define _LTTNG_ABI_OLD_H

/*
 * lttng-abi-old.h
 *
 * LTTng old ABI header (without support for compat 32/64 bits)
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
#include "lttng-abi.h"

/*
 * LTTng DebugFS ABI structures.
 */
#define LTTNG_KERNEL_OLD_CHANNEL_PADDING	LTTNG_KERNEL_SYM_NAME_LEN + 32
struct lttng_kernel_old_channel {
	int overwrite;				/* 1: overwrite, 0: discard */
	uint64_t subbuf_size;			/* in bytes */
	uint64_t num_subbuf;
	unsigned int switch_timer_interval;	/* usecs */
	unsigned int read_timer_interval;	/* usecs */
	enum lttng_kernel_output output;	/* splice, mmap */
	char padding[LTTNG_KERNEL_OLD_CHANNEL_PADDING];
};

struct lttng_kernel_old_kretprobe {
	uint64_t addr;

	uint64_t offset;
	char symbol_name[LTTNG_KERNEL_SYM_NAME_LEN];
};

/*
 * Either addr is used, or symbol_name and offset.
 */
struct lttng_kernel_old_kprobe {
	uint64_t addr;

	uint64_t offset;
	char symbol_name[LTTNG_KERNEL_SYM_NAME_LEN];
};

struct lttng_kernel_old_function_tracer {
	char symbol_name[LTTNG_KERNEL_SYM_NAME_LEN];
};

/*
 * For syscall tracing, name = '\0' means "enable all".
 */
#define LTTNG_KERNEL_OLD_EVENT_PADDING1	16
#define LTTNG_KERNEL_OLD_EVENT_PADDING2	LTTNG_KERNEL_SYM_NAME_LEN + 32
struct lttng_kernel_old_event {
	char name[LTTNG_KERNEL_SYM_NAME_LEN];	/* event name */
	enum lttng_kernel_instrumentation instrumentation;
	char padding[LTTNG_KERNEL_OLD_EVENT_PADDING1];

	/* Per instrumentation type configuration */
	union {
		struct lttng_kernel_old_kretprobe kretprobe;
		struct lttng_kernel_old_kprobe kprobe;
		struct lttng_kernel_old_function_tracer ftrace;
		char padding[LTTNG_KERNEL_OLD_EVENT_PADDING2];
	} u;
};

struct lttng_kernel_old_tracer_version {
	uint32_t major;
	uint32_t minor;
	uint32_t patchlevel;
};

struct lttng_kernel_old_calibrate {
	enum lttng_kernel_calibrate_type type;	/* type (input) */
};

struct lttng_kernel_old_perf_counter_ctx {
	uint32_t type;
	uint64_t config;
	char name[LTTNG_KERNEL_SYM_NAME_LEN];
};

#define LTTNG_KERNEL_OLD_CONTEXT_PADDING1	16
#define LTTNG_KERNEL_OLD_CONTEXT_PADDING2	LTTNG_KERNEL_SYM_NAME_LEN + 32
struct lttng_kernel_old_context {
	enum lttng_kernel_context_type ctx;
	char padding[LTTNG_KERNEL_OLD_CONTEXT_PADDING1];

	union {
		struct lttng_kernel_old_perf_counter_ctx perf_counter;
		char padding[LTTNG_KERNEL_OLD_CONTEXT_PADDING2];
	} u;
};

/* LTTng file descriptor ioctl */
#define LTTNG_KERNEL_OLD_SESSION		_IO(0xF6, 0x40)
#define LTTNG_KERNEL_OLD_TRACER_VERSION		\
	_IOR(0xF6, 0x41, struct lttng_kernel_old_tracer_version)
#define LTTNG_KERNEL_OLD_TRACEPOINT_LIST	_IO(0xF6, 0x42)
#define LTTNG_KERNEL_OLD_WAIT_QUIESCENT		_IO(0xF6, 0x43)
#define LTTNG_KERNEL_OLD_CALIBRATE		\
	_IOWR(0xF6, 0x44, struct lttng_kernel_old_calibrate)

/* Session FD ioctl */
#define LTTNG_KERNEL_OLD_METADATA		\
	_IOW(0xF6, 0x50, struct lttng_kernel_old_channel)
#define LTTNG_KERNEL_OLD_CHANNEL		\
	_IOW(0xF6, 0x51, struct lttng_kernel_old_channel)
#define LTTNG_KERNEL_OLD_SESSION_START		_IO(0xF6, 0x52)
#define LTTNG_KERNEL_OLD_SESSION_STOP		_IO(0xF6, 0x53)

/* Channel FD ioctl */
#define LTTNG_KERNEL_OLD_STREAM			_IO(0xF6, 0x60)
#define LTTNG_KERNEL_OLD_EVENT			\
	_IOW(0xF6, 0x61, struct lttng_kernel_old_event)

/* Event and Channel FD ioctl */
#define LTTNG_KERNEL_OLD_CONTEXT		\
	_IOW(0xF6, 0x70, struct lttng_kernel_old_context)

/* Event, Channel and Session ioctl */
#define LTTNG_KERNEL_OLD_ENABLE			_IO(0xF6, 0x80)
#define LTTNG_KERNEL_OLD_DISABLE		_IO(0xF6, 0x81)

#endif /* _LTTNG_ABI_OLD_H */
