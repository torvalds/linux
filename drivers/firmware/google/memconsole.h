/*
 * memconsole.h
 *
 * Internal headers of the memory based BIOS console.
 *
 * Copyright 2017 Google Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License v2.0 as published by
 * the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#ifndef __FIRMWARE_GOOGLE_MEMCONSOLE_H
#define __FIRMWARE_GOOGLE_MEMCONSOLE_H

/*
 * memconsole_setup
 *
 * Initialize the memory console from raw (virtual) base
 * address and length.
 */
void memconsole_setup(void *baseaddr, size_t length);

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
