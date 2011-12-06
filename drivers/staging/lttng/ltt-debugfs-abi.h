#ifndef _LTT_DEBUGFS_ABI_H
#define _LTT_DEBUGFS_ABI_H

/*
 * ltt-debugfs-abi.h
 *
 * Copyright 2010 (c) - Mathieu Desnoyers <mathieu.desnoyers@efficios.com>
 *
 * LTTng debugfs ABI header
 *
 * Dual LGPL v2.1/GPL v2 license.
 */

#include <linux/fs.h>

#define LTTNG_SYM_NAME_LEN	256

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

struct lttng_kernel_channel {
	int overwrite;				/* 1: overwrite, 0: discard */
	uint64_t subbuf_size;			/* in bytes */
	uint64_t num_subbuf;
	unsigned int switch_timer_interval;	/* usecs */
	unsigned int read_timer_interval;	/* usecs */
	enum lttng_kernel_output output;	/* splice, mmap */
};

struct lttng_kernel_kretprobe {
	uint64_t addr;

	uint64_t offset;
	char symbol_name[LTTNG_SYM_NAME_LEN];
};

/*
 * Either addr is used, or symbol_name and offset.
 */
struct lttng_kernel_kprobe {
	uint64_t addr;

	uint64_t offset;
	char symbol_name[LTTNG_SYM_NAME_LEN];
};

struct lttng_kernel_function_tracer {
	char symbol_name[LTTNG_SYM_NAME_LEN];
};

/*
 * For syscall tracing, name = '\0' means "enable all".
 */
struct lttng_kernel_event {
	char name[LTTNG_SYM_NAME_LEN];	/* event name */
	enum lttng_kernel_instrumentation instrumentation;
	/* Per instrumentation type configuration */
	union {
		struct lttng_kernel_kretprobe kretprobe;
		struct lttng_kernel_kprobe kprobe;
		struct lttng_kernel_function_tracer ftrace;
	} u;
};

struct lttng_kernel_tracer_version {
	uint32_t version;
	uint32_t patchlevel;
	uint32_t sublevel;
};

enum lttng_kernel_calibrate_type {
	LTTNG_KERNEL_CALIBRATE_KRETPROBE,
};

struct lttng_kernel_calibrate {
	enum lttng_kernel_calibrate_type type;	/* type (input) */
};

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
};

struct lttng_kernel_perf_counter_ctx {
	uint32_t type;
	uint64_t config;
	char name[LTTNG_SYM_NAME_LEN];
};

struct lttng_kernel_context {
	enum lttng_kernel_context_type ctx;
	union {
		struct lttng_kernel_perf_counter_ctx perf_counter;
	} u;
};

/* LTTng file descriptor ioctl */
#define LTTNG_KERNEL_SESSION			_IO(0xF6, 0x40)
#define LTTNG_KERNEL_TRACER_VERSION		\
	_IOR(0xF6, 0x41, struct lttng_kernel_tracer_version)
#define LTTNG_KERNEL_TRACEPOINT_LIST		_IO(0xF6, 0x42)
#define LTTNG_KERNEL_WAIT_QUIESCENT		_IO(0xF6, 0x43)
#define LTTNG_KERNEL_CALIBRATE			\
	_IOWR(0xF6, 0x44, struct lttng_kernel_calibrate)

/* Session FD ioctl */
#define LTTNG_KERNEL_METADATA			\
	_IOW(0xF6, 0x50, struct lttng_kernel_channel)
#define LTTNG_KERNEL_CHANNEL			\
	_IOW(0xF6, 0x51, struct lttng_kernel_channel)
#define LTTNG_KERNEL_SESSION_START		_IO(0xF6, 0x52)
#define LTTNG_KERNEL_SESSION_STOP		_IO(0xF6, 0x53)

/* Channel FD ioctl */
#define LTTNG_KERNEL_STREAM			_IO(0xF6, 0x60)
#define LTTNG_KERNEL_EVENT			\
	_IOW(0xF6, 0x61, struct lttng_kernel_event)

/* Event and Channel FD ioctl */
#define LTTNG_KERNEL_CONTEXT			\
	_IOW(0xF6, 0x70, struct lttng_kernel_context)

/* Event, Channel and Session ioctl */
#define LTTNG_KERNEL_ENABLE			_IO(0xF6, 0x80)
#define LTTNG_KERNEL_DISABLE			_IO(0xF6, 0x81)

#endif /* _LTT_DEBUGFS_ABI_H */
