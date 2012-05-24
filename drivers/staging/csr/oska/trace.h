/*
 * OSKA Linux implementation -- tracing messages.
 *
 * Copyright (C) 2009 Cambridge Silicon Radio Ltd.
 *
 * Refer to LICENSE.txt included with this source code for details on
 * the license terms.
 */
#ifndef __OSKA_LINUX_TRACE_H
#define __OSKA_LINUX_TRACE_H

#include <linux/kernel.h>

#ifndef OS_TRACE_PREFIX
#  define OS_TRACE_PREFIX ""
#endif

#define os_trace_err(format, ...)  printk(KERN_ERR OS_TRACE_PREFIX format "\n", ## __VA_ARGS__)
#define os_trace_warn(format, ...) printk(KERN_WARNING OS_TRACE_PREFIX format "\n", ##  __VA_ARGS__)
#define os_trace_info(format, ...) printk(KERN_INFO OS_TRACE_PREFIX format "\n", ## __VA_ARGS__)
#define os_trace_dbg(format, ...)  printk(KERN_DEBUG OS_TRACE_PREFIX format "\n", ## __VA_ARGS__)

#endif /* #ifndef __OSKA_LINUX_TRACE_H */
