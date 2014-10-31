/**
 * Copyright (C) ARM Limited 2012-2014. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 */

#if !defined(GATOR_EVENTS_MALI_COMMON_H)
#define GATOR_EVENTS_MALI_COMMON_H

#include "gator.h"

#include <linux/module.h>
#include <linux/time.h>
#include <linux/math64.h>
#include <linux/slab.h>
#include <linux/io.h>

/* Ensure that MALI_SUPPORT has been defined to something. */
#ifndef MALI_SUPPORT
#error MALI_SUPPORT not defined!
#endif

/* Values for the supported activity event types */
#define ACTIVITY_START  (1)
#define ACTIVITY_STOP   (2)

/*
 * Runtime state information for a counter.
 */
struct mali_counter {
	/* 'key' (a unique id set by gatord and returned by gator.ko) */
	unsigned long key;
	/* counter enable state */
	unsigned long enabled;
	/* for activity counters, the number of cores, otherwise -1 */
	unsigned long cores;
};

/*
 * Mali-4xx
 */
typedef int mali_profiling_set_event_type(unsigned int, int);
typedef void mali_profiling_control_type(unsigned int, unsigned int);

/*
 * Driver entry points for functions called directly by gator.
 */
extern int _mali_profiling_set_event(unsigned int, int);
extern void _mali_profiling_control(unsigned int, unsigned int);
extern void _mali_profiling_get_counters(unsigned int *, unsigned int *, unsigned int *, unsigned int *);

/**
 * Creates a filesystem entry under /dev/gator relating to the specified event name and key, and
 * associate the key/enable values with this entry point.
 *
 * @param event_name The name of the event.
 * @param sb Linux super block
 * @param root Directory under which the entry will be created.
 * @param counter_key Ptr to location which will be associated with the counter key.
 * @param counter_enabled Ptr to location which will be associated with the counter enable state.
 *
 * @return 0 if entry point was created, non-zero if not.
 */
extern int gator_mali_create_file_system(const char *mali_name, const char *event_name, struct super_block *sb, struct dentry *root, struct mali_counter *counter, unsigned long *event);

/**
 * Initializes the counter array.
 *
 * @param keys The array of counters
 * @param n_counters The number of entries in each of the arrays.
 */
extern void gator_mali_initialise_counters(struct mali_counter counters[], unsigned int n_counters);

#endif /* GATOR_EVENTS_MALI_COMMON_H  */
