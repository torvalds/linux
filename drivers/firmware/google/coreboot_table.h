/*
 * coreboot_table.h
 *
 * Internal header for coreboot table access.
 *
 * Copyright 2017 Google Inc.
 * Copyright 2017 Samuel Holland <samuel@sholland.org>
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

#ifndef __COREBOOT_TABLE_H
#define __COREBOOT_TABLE_H

#include <linux/io.h>

/* Coreboot table header structure */
struct coreboot_table_header {
	char signature[4];
	u32 header_bytes;
	u32 header_checksum;
	u32 table_bytes;
	u32 table_checksum;
	u32 table_entries;
};

/* List of coreboot entry structures that is used */
/* Generic */
struct coreboot_table_entry {
	u32 tag;
	u32 size;
};

/* Points to a CBMEM entry */
struct lb_cbmem_ref {
	u32 tag;
	u32 size;

	u64 cbmem_addr;
};

/* A device, additionally with information from coreboot. */
struct coreboot_device {
	struct device dev;
	union {
		struct coreboot_table_entry entry;
		struct lb_cbmem_ref cbmem_ref;
	};
};

/* A driver for handling devices described in coreboot tables. */
struct coreboot_driver {
	int (*probe)(struct coreboot_device *);
	int (*remove)(struct coreboot_device *);
	struct device_driver drv;
	u32 tag;
};

/* Register a driver that uses the data from a coreboot table. */
int coreboot_driver_register(struct coreboot_driver *driver);

/* Unregister a driver that uses the data from a coreboot table. */
void coreboot_driver_unregister(struct coreboot_driver *driver);

/* Initialize coreboot table module given a pointer to iomem */
int coreboot_table_init(struct device *dev, void __iomem *ptr);

/* Cleanup coreboot table module */
int coreboot_table_exit(void);

#endif /* __COREBOOT_TABLE_H */
