#ifndef _LTTNG_WRAPPER_TRACEPOINT_H
#define _LTTNG_WRAPPER_TRACEPOINT_H

/*
 * wrapper/tracepoint.h
 *
 * wrapper around DECLARE_EVENT_CLASS.
 *
 * Copyright (C) 2011-2012 Mathieu Desnoyers <mathieu.desnoyers@efficios.com>
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
#include <linux/tracepoint.h>

#if (LINUX_VERSION_CODE < KERNEL_VERSION(2,6,35))

#define DECLARE_EVENT_CLASS(name, proto, args, tstruct, assign, print)

#endif

#ifndef HAVE_KABI_2635_TRACEPOINT

#define kabi_2635_tracepoint_probe_register tracepoint_probe_register
#define kabi_2635_tracepoint_probe_unregister tracepoint_probe_unregister
#define kabi_2635_tracepoint_probe_register_noupdate tracepoint_probe_register_noupdate
#define kabi_2635_tracepoint_probe_unregister_noupdate tracepoint_probe_unregister_noupdate

#endif /* HAVE_KABI_2635_TRACEPOINT */

#endif /* _LTTNG_WRAPPER_TRACEPOINT_H */
