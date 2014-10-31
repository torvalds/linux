/**
 * Copyright (C) ARM Limited 2012-2014. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 */
#include "gator_events_mali_common.h"

extern int gator_mali_create_file_system(const char *mali_name, const char *event_name, struct super_block *sb, struct dentry *root, struct mali_counter *counter, unsigned long *event)
{
	int err;
	char buf[255];
	struct dentry *dir;

	/* If the counter name is empty ignore it */
	if (strlen(event_name) != 0) {
		/* Set up the filesystem entry for this event. */
		if (mali_name == NULL)
			snprintf(buf, sizeof(buf), "ARM_Mali-%s", event_name);
		else
			snprintf(buf, sizeof(buf), "ARM_Mali-%s_%s", mali_name, event_name);

		dir = gatorfs_mkdir(sb, root, buf);

		if (dir == NULL) {
			pr_debug("gator: %s: error creating file system for: %s (%s)\n", mali_name, event_name, buf);
			return -1;
		}

		err = gatorfs_create_ulong(sb, dir, "enabled", &counter->enabled);
		if (err != 0) {
			pr_debug("gator: %s: error calling gatorfs_create_ulong for: %s (%s)\n", mali_name, event_name, buf);
			return -1;
		}
		err = gatorfs_create_ro_ulong(sb, dir, "key", &counter->key);
		if (err != 0) {
			pr_debug("gator: %s: error calling gatorfs_create_ro_ulong for: %s (%s)\n", mali_name, event_name, buf);
			return -1;
		}
		if (counter->cores != -1) {
			err = gatorfs_create_ro_ulong(sb, dir, "cores", &counter->cores);
			if (err != 0) {
				pr_debug("gator: %s: error calling gatorfs_create_ro_ulong for: %s (%s)\n", mali_name, event_name, buf);
				return -1;
			}
		}
		if (event != NULL) {
			err = gatorfs_create_ulong(sb, dir, "event", event);
			if (err != 0) {
				pr_debug("gator: %s: error calling gatorfs_create_ro_ulong for: %s (%s)\n", mali_name, event_name, buf);
				return -1;
			}
		}
	}

	return 0;
}

extern void gator_mali_initialise_counters(struct mali_counter counters[], unsigned int n_counters)
{
	unsigned int cnt;

	for (cnt = 0; cnt < n_counters; cnt++) {
		struct mali_counter *counter = &counters[cnt];

		counter->key = gator_events_get_key();
		counter->enabled = 0;
		counter->cores = -1;
	}
}
