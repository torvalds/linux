/*
 * wrapper/random.c
 *
 * wrapper around bootid read. Using KALLSYMS to get its address when
 * available, else we need to have a kernel that exports this function to GPL
 * modules.
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

/* boot_id depends on sysctl */
#if defined(CONFIG_SYSCTL)

#include <linux/fs.h>
#include <linux/file.h>
#include <linux/sched.h>
#include <linux/uaccess.h>
#include "random.h"

/*
 * Returns string boot id.
 */
int wrapper_get_bootid(char *bootid)
{
	struct file *file;
	int ret;
	ssize_t len;
	mm_segment_t old_fs;

	file = filp_open("/proc/sys/kernel/random/boot_id", O_RDONLY, 0);
	if (IS_ERR(file))
		return PTR_ERR(file);

	old_fs = get_fs();
	set_fs(KERNEL_DS);

	if (!file->f_op || !file->f_op->read) {
		ret = -EINVAL;
		goto end;
	}

	len = file->f_op->read(file, bootid, BOOT_ID_LEN - 1, &file->f_pos);
	if (len != BOOT_ID_LEN - 1) {
		ret = -EINVAL;
		goto end;
	}

	bootid[BOOT_ID_LEN - 1] = '\0';
	ret = 0;
end:
	set_fs(old_fs);
	filp_close(file, current->files);
	return ret;
}

#else

int wrapper_get_bootid(char *bootid)
{
	return -ENOSYS;
}

#endif
