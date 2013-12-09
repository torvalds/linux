/*
 * wrapper/fdtable.c
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
#include <linux/spinlock.h>
#include "fdtable.h"

#if (LINUX_VERSION_CODE < KERNEL_VERSION(3,7,0))

/*
 * Reimplementation of iterate_fd() for kernels between 2.6.32 and 3.6
 * (inclusive).
 */
int lttng_iterate_fd(struct files_struct *files,
		unsigned int first,
		int (*cb)(const void *, struct file *, unsigned int),
		const void *ctx)
{
	struct fdtable *fdt;
	struct file *filp;
	unsigned int i;
	int res = 0;

	if (!files)
		return 0;
	spin_lock(&files->file_lock);
	fdt = files_fdtable(files);
	for (i = 0; i < fdt->max_fds; i++) {
		filp = fcheck_files(files, i);
		if (!filp)
			continue;
		res = cb(ctx, filp, i);
		if (res)
			break;
	}
	spin_unlock(&files->file_lock);
	return res;
}

#endif
