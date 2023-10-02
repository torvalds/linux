// SPDX-License-Identifier: GPL-2.0-only
/*
 * Common helpers for stackable filesystems and backing files.
 *
 * Copyright (C) 2023 CTERA Networks.
 */

#include <linux/fs.h>
#include <linux/backing-file.h>

#include "internal.h"

/**
 * backing_file_open - open a backing file for kernel internal use
 * @user_path:	path that the user reuqested to open
 * @flags:	open flags
 * @real_path:	path of the backing file
 * @cred:	credentials for open
 *
 * Open a backing file for a stackable filesystem (e.g., overlayfs).
 * @user_path may be on the stackable filesystem and @real_path on the
 * underlying filesystem.  In this case, we want to be able to return the
 * @user_path of the stackable filesystem. This is done by embedding the
 * returned file into a container structure that also stores the stacked
 * file's path, which can be retrieved using backing_file_user_path().
 */
struct file *backing_file_open(const struct path *user_path, int flags,
			       const struct path *real_path,
			       const struct cred *cred)
{
	struct file *f;
	int error;

	f = alloc_empty_backing_file(flags, cred);
	if (IS_ERR(f))
		return f;

	path_get(user_path);
	*backing_file_user_path(f) = *user_path;
	error = vfs_open(real_path, f);
	if (error) {
		fput(f);
		f = ERR_PTR(error);
	}

	return f;
}
EXPORT_SYMBOL_GPL(backing_file_open);
