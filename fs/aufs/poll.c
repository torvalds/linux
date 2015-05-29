/*
 * Copyright (C) 2005-2014 Junjiro R. Okajima
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
	struct dentry *dentry;
	struct super_block *sb;

	/* We should pretend an error happened. */
	mask = POLLERR /* | POLLIN | POLLOUT */;
	dentry = file->f_dentry;
	sb = dentry->d_sb;
	si_read_lock(sb, AuLock_FLUSH | AuLock_NOPLMW);
	err = au_reval_and_lock_fdi(file, au_reopen_nondir, /*wlock*/0);
	if (unlikely(err))
		goto out;

	/* it is not an error if h_file has no operation */
	mask = DEFAULT_POLLMASK;
	h_file = au_hf_top(file);
	if (h_file->f_op && h_file->f_op->poll)
		mask = h_file->f_op->poll(h_file, wait);

	di_read_unlock(dentry, AuLock_IR);
	fi_read_unlock(file);

out:
	si_read_unlock(sb);
	AuTraceErr((int)mask);
	return mask;
}
