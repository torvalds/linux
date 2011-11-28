#ifndef LTT_TRACER_CORE_H
#define LTT_TRACER_CORE_H

/*
 * ltt-tracer-core.h
 *
 * Copyright (C) 2005-2011 Mathieu Desnoyers <mathieu.desnoyers@efficios.com>
 *
 * This contains the core definitions for the Linux Trace Toolkit.
 *
 * Dual LGPL v2.1/GPL v2 license.
 */

#include <linux/list.h>
#include <linux/percpu.h>

#ifndef CONFIG_HAVE_EFFICIENT_UNALIGNED_ACCESS
/* Align data on its natural alignment */
#define RING_BUFFER_ALIGN
#endif

#include "wrapper/ringbuffer/config.h"

struct ltt_session;
struct ltt_channel;
struct ltt_event;

#endif /* LTT_TRACER_CORE_H */
