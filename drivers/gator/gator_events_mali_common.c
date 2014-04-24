/**
 * Copyright (C) ARM Limited 2012-2014. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 */
#include "gator_events_mali_common.h"

static u32 gator_mali_get_id(void)
{
	return MALI_SUPPORT;
}

extern const char *gator_mali_get_mali_name(void)
{
	u32 id = gator_mali_get_id();

	switch (id) {
	case MALI_T6xx:
		return "Mali-T6xx";
	case MALI_4xx:
		return "Mali-4xx";
	default:
		pr_debug("gator: Mali-T6xx: unknown Mali ID (%d)\n", id);
		return "Mali-Unknown";
	}
}

extern int gator_mali_create_file_system(const char *mali_name, const char *event_name, struct super_block *sb, struct dentry *root, mali_counter *counter, unsigned long *event)
{
	int err;
	char buf[255];
	struct dentry *dir;

	/* If the counter name is empty ignore it */
	if (strlen(event_name) != 0) {
		/* Set up the filesystem entry for this event. */
		snprintf(buf, sizeof(buf), "ARM_%s_%s", mali_name, event_name);

		dir = gatorfs_mkdir(sb, root, buf);

		if (dir == NULL) {
			pr_debug("gator: Mali-T6xx: error creating file system for: %s (%s)", event_name, buf);
			return -1;
		}

		err = gatorfs_create_ulong(sb, dir, "enabled", &counter->enabled);
		if (err != 0) {
			pr_debug("gator: Mali-T6xx: error calling gatorfs_create_ulong for: %s (%s)", event_name, buf);
			return -1;
		}
		err = gatorfs_create_ro_ulong(sb, dir, "key", &counter->key);
		if (err != 0) {
			pr_debug("gator: Mali-T6xx: error calling gatorfs_create_ro_ulong for: %s (%s)", event_name, buf);
			return -1;
		}
		if (event != NULL) {
			err = gatorfs_create_ulong(sb, dir, "event", event);
			if (err != 0) {
				pr_debug("gator: Mali-T6xx: error calling gatorfs_create_ro_ulong for: %s (%s)", event_name, buf);
				return -1;
			}
		}
	}

	return 0;
}

extern void gator_mali_initialise_counters(mali_counter counters[], unsigned int n_counters)
{
	unsigned int cnt;

	for (cnt = 0; cnt < n_counters; cnt++) {
		mali_counter *counter = &counters[cnt];

		counter->key = gator_events_get_key();
		counter->enabled = 0;
	}
}
