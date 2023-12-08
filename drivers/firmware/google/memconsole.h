/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * memconsole.h
 *
 * Internal headers of the memory based BIOS console.
 *
 * Copyright 2017 Google Inc.
 */

#ifndef __FIRMWARE_GOOGLE_MEMCONSOLE_H
#define __FIRMWARE_GOOGLE_MEMCONSOLE_H

#include <linux/types.h>

/*
 * memconsole_setup
 *
 * Initialize the memory console, passing the function to handle read accesses.
 */
void memconsole_setup(ssize_t (*read_func)(char *, loff_t, size_t));

/*
 * memconsole_sysfs_init
 *
 * Update memory console length and create binary file
 * for firmware object.
 */
int memconsole_sysfs_init(void);

/* memconsole_exit
 *
 * Unmap the console buffer.
 */
void memconsole_exit(void);

#endif /* __FIRMWARE_GOOGLE_MEMCONSOLE_H */
