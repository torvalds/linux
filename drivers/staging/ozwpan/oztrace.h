/* -----------------------------------------------------------------------------
 * Copyright (c) 2011 Ozmo Inc
 * Released under the GNU General Public License Version 2 (GPLv2).
 * -----------------------------------------------------------------------------
 */
#ifndef _OZTRACE_H_
#define _OZTRACE_H_
#include "ozconfig.h"

#define TRACE_PREFIX	KERN_ALERT "OZWPAN: "

#ifdef WANT_TRACE
#define oz_trace(...) printk(TRACE_PREFIX __VA_ARGS__)
#ifdef WANT_VERBOSE_TRACE
extern unsigned long trace_flags;
#define oz_trace2(_flag, ...) \
	do { if (trace_flags & _flag) printk(TRACE_PREFIX __VA_ARGS__); \
	} while (0)
#else
#define oz_trace2(...)
#endif /* #ifdef WANT_VERBOSE_TRACE */
#else
#define oz_trace(...)
#define oz_trace2(...)
#endif /* #ifdef WANT_TRACE */

#define OZ_TRACE_STREAM		0x1
#define OZ_TRACE_URB		0x2
#define OZ_TRACE_CTRL_DETAIL	0x4
#define OZ_TRACE_HUB		0x8
#define OZ_TRACE_RX_FRAMES	0x10
#define OZ_TRACE_TX_FRAMES	0x20

#endif /* Sentry */

