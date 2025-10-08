/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2003 Sistina Software.
 * Copyright (C) 2004 Red Hat, Inc. All rights reserved.
 *
 * Module Author: Heinz Mauelshagen
 *
 * This file is released under the GPL.
 *
 * Path-Selector registration.
 */

#ifndef	DM_PATH_SELECTOR_H
#define	DM_PATH_SELECTOR_H

#include <linux/device-mapper.h>

#include "dm-mpath.h"

/*
 * We provide an abstraction for the code that chooses which path
 * to send some io down.
 */
struct path_selector_type;
struct path_selector {
	struct path_selector_type *type;
	void *context;
};

/*
 * If a path selector uses this flag, a high resolution timer is used
 * (via ktime_get_ns) to account for IO start time in BIO-based mpath.
 * This improves performance of some path selectors (i.e. HST), in
 * exchange for slightly higher overhead when submitting the BIO.
 * The extra cost is usually offset by improved path selection for
 * some benchmarks.
 *
 * This has no effect for request-based mpath, since it already uses a
 * higher precision timer by default.
 */
#define DM_PS_USE_HR_TIMER		0x00000001
#define dm_ps_use_hr_timer(type)	((type)->features & DM_PS_USE_HR_TIMER)

/* Information about a path selector type */
struct path_selector_type {
	char *name;
	struct module *module;

	unsigned int features;
	unsigned int table_args;
	unsigned int info_args;

	/*
	 * Constructs a path selector object, takes custom arguments
	 */
	int (*create)(struct path_selector *ps, unsigned int argc, char **argv);
	void (*destroy)(struct path_selector *ps);

	/*
	 * Add an opaque path object, along with some selector specific
	 * path args (eg, path priority).
	 */
	int (*add_path)(struct path_selector *ps, struct dm_path *path,
			int argc, char **argv, char **error);

	/*
	 * Chooses a path for this io, if no paths are available then
	 * NULL will be returned.
	 */
	struct dm_path *(*select_path)(struct path_selector *ps, size_t nr_bytes);

	/*
	 * Notify the selector that a path has failed.
	 */
	void (*fail_path)(struct path_selector *ps, struct dm_path *p);

	/*
	 * Ask selector to reinstate a path.
	 */
	int (*reinstate_path)(struct path_selector *ps, struct dm_path *p);

	/*
	 * Table content based on parameters added in ps_add_path_fn
	 * or path selector status
	 */
	int (*status)(struct path_selector *ps, struct dm_path *path,
		      status_type_t type, char *result, unsigned int maxlen);

	int (*start_io)(struct path_selector *ps, struct dm_path *path,
			size_t nr_bytes);
	int (*end_io)(struct path_selector *ps, struct dm_path *path,
		      size_t nr_bytes, u64 start_time);
};

/* Register a path selector */
int dm_register_path_selector(struct path_selector_type *type);

/* Unregister a path selector */
void dm_unregister_path_selector(struct path_selector_type *type);

/* Returns a registered path selector type */
struct path_selector_type *dm_get_path_selector(const char *name);

/* Releases a path selector  */
void dm_put_path_selector(struct path_selector_type *pst);

#endif
