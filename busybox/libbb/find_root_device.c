/* vi: set sw=4 ts=4: */
/*
 * Utility routines.
 *
 * Copyright (C) 1999-2004 by Erik Andersen <andersen@codepoet.org>
 *
 * Licensed under GPLv2 or later, see file LICENSE in this source tree.
 */
#include "libbb.h"

/* Find block device /dev/XXX which contains specified file
 * We handle /dev/dir/dir/dir too, at a cost of ~80 more bytes code */

/* Do not reallocate all this stuff on each recursion */
enum { DEVNAME_MAX = 256 };
struct arena {
	struct stat st;
	dev_t dev;
	/* Was PATH_MAX, but we recurse _/dev_. We can assume
	 * people are not crazy enough to have mega-deep tree there */
	char devpath[DEVNAME_MAX];
};

static char *find_block_device_in_dir(struct arena *ap)
{
	DIR *dir;
	struct dirent *entry;
	char *retpath = NULL;
	int len, rem;

	len = strlen(ap->devpath);
	rem = DEVNAME_MAX-2 - len;
	if (rem <= 0)
		return NULL;

	dir = opendir(ap->devpath);
	if (!dir)
		return NULL;

	ap->devpath[len++] = '/';

	while ((entry = readdir(dir)) != NULL) {
		safe_strncpy(ap->devpath + len, entry->d_name, rem);
		/* lstat: do not follow links */
		if (lstat(ap->devpath, &ap->st) != 0)
			continue;
		if (S_ISBLK(ap->st.st_mode) && ap->st.st_rdev == ap->dev) {
			retpath = xstrdup(ap->devpath);
			break;
		}
		if (S_ISDIR(ap->st.st_mode)) {
			/* Do not recurse for '.' and '..' */
			if (DOT_OR_DOTDOT(entry->d_name))
				continue;
			retpath = find_block_device_in_dir(ap);
			if (retpath)
				break;
		}
	}
	closedir(dir);

	return retpath;
}

char* FAST_FUNC find_block_device(const char *path)
{
	struct arena a;

	if (stat(path, &a.st) != 0)
		return NULL;
	a.dev = S_ISBLK(a.st.st_mode) ? a.st.st_rdev : a.st.st_dev;
	strcpy(a.devpath, "/dev");
	return find_block_device_in_dir(&a);
}
