#ifndef _LTT_TRACER_H
#define _LTT_TRACER_H

/*
 * ltt-tracer.h
 *
 * Copyright (C) 2005-2011 Mathieu Desnoyers <mathieu.desnoyers@efficios.com>
 *
 * This contains the definitions for the Linux Trace Toolkit tracer.
 *
 * Dual LGPL v2.1/GPL v2 license.
 */

#include <stdarg.h>
#include <linux/types.h>
#include <linux/limits.h>
#include <linux/list.h>
#include <linux/cache.h>
#include <linux/timex.h>
#include <linux/wait.h>
#include <asm/atomic.h>
#include <asm/local.h>

#include "wrapper/trace-clock.h"
#include "ltt-tracer-core.h"
#include "ltt-events.h"

#define LTTNG_VERSION 0
#define LTTNG_PATCHLEVEL 9
#define LTTNG_SUBLEVEL 1

#ifndef CHAR_BIT
#define CHAR_BIT 8
#endif

/* Number of bytes to log with a read/write event */
#define LTT_LOG_RW_SIZE			32L
#define LTT_MAX_SMALL_SIZE		0xFFFFU

#ifdef RING_BUFFER_ALIGN
#define ltt_alignof(type)	__alignof__(type)
#else
#define ltt_alignof(type)	1
#endif

/* Tracer properties */
#define CTF_MAGIC_NUMBER		0xC1FC1FC1
#define TSDL_MAGIC_NUMBER		0x75D11D57

/* CTF specification version followed */
#define CTF_SPEC_MAJOR			1
#define CTF_SPEC_MINOR			8

/* Tracer major/minor versions */
#define CTF_VERSION_MAJOR		0
#define CTF_VERSION_MINOR		1

/*
 * Number of milliseconds to retry before failing metadata writes on buffer full
 * condition. (10 seconds)
 */
#define LTTNG_METADATA_TIMEOUT_MSEC	10000

#define LTT_RFLAG_EXTENDED		RING_BUFFER_RFLAG_END
#define LTT_RFLAG_END			(LTT_RFLAG_EXTENDED << 1)

#endif /* _LTT_TRACER_H */
