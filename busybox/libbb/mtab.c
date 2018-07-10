/* vi: set sw=4 ts=4: */
/*
 * Utility routines.
 *
 * Copyright (C) 1999-2004 by Erik Andersen <andersen@codepoet.org>
 *
 * Licensed under GPLv2 or later, see file LICENSE in this source tree.
 */
#include <mntent.h>
#include "libbb.h"

#if ENABLE_FEATURE_MTAB_SUPPORT
void FAST_FUNC erase_mtab(const char *name)
{
	struct mntent *entries;
	int i, count;
	FILE *mountTable;
	struct mntent *m;

	mountTable = setmntent(bb_path_mtab_file, "r");
	/* Bummer. Fall back on trying the /proc filesystem */
	if (!mountTable) mountTable = setmntent("/proc/mounts", "r");
	if (!mountTable) {
		bb_perror_msg(bb_path_mtab_file);
		return;
	}

	entries = NULL;
	count = 0;
	while ((m = getmntent(mountTable)) != 0) {
		entries = xrealloc_vector(entries, 3, count);
		entries[count].mnt_fsname = xstrdup(m->mnt_fsname);
		entries[count].mnt_dir = xstrdup(m->mnt_dir);
		entries[count].mnt_type = xstrdup(m->mnt_type);
		entries[count].mnt_opts = xstrdup(m->mnt_opts);
		entries[count].mnt_freq = m->mnt_freq;
		entries[count].mnt_passno = m->mnt_passno;
		count++;
	}
	endmntent(mountTable);

//TODO: make update atomic
	mountTable = setmntent(bb_path_mtab_file, "w");
	if (mountTable) {
		for (i = 0; i < count; i++) {
			if (strcmp(entries[i].mnt_fsname, name) != 0
			 && strcmp(entries[i].mnt_dir, name) != 0)
				addmntent(mountTable, &entries[i]);
		}
		endmntent(mountTable);
	} else if (errno != EROFS)
		bb_perror_msg(bb_path_mtab_file);
}
#endif
