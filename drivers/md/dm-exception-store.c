/*
 * Copyright (C) 2001-2002 Sistina Software (UK) Limited.
 * Copyright (C) 2006-2008 Red Hat GmbH
 *
 * This file is released under the GPL.
 */

#include "dm-exception-store.h"

#include <linux/mm.h>
#include <linux/pagemap.h>
#include <linux/vmalloc.h>
#include <linux/slab.h>

#define DM_MSG_PREFIX "snapshot exception stores"

int dm_exception_store_init(void)
{
	int r;

	r = dm_transient_snapshot_init();
	if (r) {
		DMERR("Unable to register transient exception store type.");
		goto transient_fail;
	}

	r = dm_persistent_snapshot_init();
	if (r) {
		DMERR("Unable to register persistent exception store type");
		goto persistent_fail;
	}

	return 0;

persistent_fail:
	dm_persistent_snapshot_exit();
transient_fail:
	return r;
}

void dm_exception_store_exit(void)
{
	dm_persistent_snapshot_exit();
	dm_transient_snapshot_exit();
}
