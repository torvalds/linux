#ifndef _LTTNG_WRAPPER_FDTABLE_H
#define _LTTNG_WRAPPER_FDTABLE_H

/*
 * wrapper/fdtable.h
 *
 * Copyright (C) 2013 Mathieu Desnoyers <mathieu.desnoyers@efficios.com>
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
#include <linux/fdtable.h>

#if (LINUX_VERSION_CODE < KERNEL_VERSION(3,7,0))

int lttng_iterate_fd(struct files_struct *files,
		unsigned int first,
		int (*cb)(const void *, struct file *, unsigned int),
		const void *ctx);

#else

/*
 * iterate_fd() appeared at commit
 * c3c073f808b22dfae15ef8412b6f7b998644139a in the Linux kernel (first
 * released kernel: v3.7).
 */
#define lttng_iterate_fd	iterate_fd

#endif
#endif /* _LTTNG_WRAPPER_FDTABLE_H */
