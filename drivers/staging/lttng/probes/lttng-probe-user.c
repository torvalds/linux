/*
 * lttng-probe-user.c
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

#include <linux/uaccess.h>
#include "lttng-probe-user.h"

/*
 * Calculate string length. Include final null terminating character if there is
 * one, or ends at first fault. Disabling page faults ensures that we can safely
 * call this from pretty much any context, including those where the caller
 * holds mmap_sem, or any lock which nests in mmap_sem.
 */
long lttng_strlen_user_inatomic(const char *addr)
{
	long count = 0;
	mm_segment_t old_fs = get_fs();

	set_fs(KERNEL_DS);
	pagefault_disable();
	for (;;) {
		char v;
		unsigned long ret;

		ret = __copy_from_user_inatomic(&v,
			(__force const char __user *)(addr),
			sizeof(v));
		if (unlikely(ret > 0))
			break;
		count++;
		if (unlikely(!v))
			break;
		addr++;
	}
	pagefault_enable();
	set_fs(old_fs);
	return count;
}
