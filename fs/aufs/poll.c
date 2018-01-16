/*
 * Copyright (C) 2005-2017 Junjiro R. Okajima
 *
 * This program, aufs is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

/*
 * poll operation
 * There is only one filesystem which implements ->poll operation, currently.
 */

#include "aufs.h"

unsigned int aufs_poll(struct file *file, poll_table *wait)
{
	unsigned int mask;
	int err;
	struct file *h_file;
	struct super_block *sb;

	/* We should pretend an error happened. */
	mask = POLLERR /* | POLLIN | POLLOUT */;
	sb = file->f_path.dentry->d_sb;
	si_read_lock(sb, AuLock_FLUSH | AuLock_NOPLMW);

	h_file = au_read_pre(file, /*keep_fi*/0, /*lsc*/0);
	err = PTR_ERR(h_file);
	if (IS_ERR(h_file))
		goto out;

	/* it is not an error if h_file has no operation */
	mask = DEFAULT_POLLMASK;
	if (h_file->f_op->poll)
		mask = h_file->f_op->poll(h_file, wait);
	fput(h_file); /* instead of au_read_post() */

out:
	si_read_unlock(sb);
	AuTraceErr((int)mask);
	return mask;
}
