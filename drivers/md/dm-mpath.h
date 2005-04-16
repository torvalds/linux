/*
 * Copyright (C) 2004 Red Hat, Inc. All rights reserved.
 *
 * This file is released under the GPL.
 *
 * Multipath.
 */

#ifndef	DM_MPATH_H
#define	DM_MPATH_H

struct dm_dev;

struct path {
	struct dm_dev *dev;	/* Read-only */
	unsigned is_active;	/* Read-only */

	void *pscontext;	/* For path-selector use */
	void *hwhcontext;	/* For hw-handler use */
};

/* Callback for hwh_pg_init_fn to use when complete */
void dm_pg_init_complete(struct path *path, unsigned err_flags);

#endif
