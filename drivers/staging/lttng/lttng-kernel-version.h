#ifndef _LTTNG_KERNEL_VERSION_H
#define _LTTNG_KERNEL_VERSION_H

/*
 * lttng-kernel-version.h
 *
 * Contains helpers to check more complex kernel version conditions.
 *
 * Copyright (C) 2012 Mathieu Desnoyers <mathieu.desnoyers@efficios.com>
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

/*
 * This macro checks if the kernel version is between the two specified
 * versions (lower limit inclusive, upper limit exclusive).
 */
#define LTTNG_KERNEL_RANGE(a_low, b_low, c_low, a_high, b_high, c_high) \
	(LINUX_VERSION_CODE >= KERNEL_VERSION(a_low, b_low, c_low) && \
	 LINUX_VERSION_CODE < KERNEL_VERSION(a_high, b_high, c_high))

#endif /* _LTTNG_KERNEL_VERSION_H */
