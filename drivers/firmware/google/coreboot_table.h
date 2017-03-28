/*
 * coreboot_table.h
 *
 * Internal header for coreboot table access.
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

#ifndef __COREBOOT_TABLE_H
#define __COREBOOT_TABLE_H

#include <linux/io.h>

/* List of coreboot entry structures that is used */
struct lb_cbmem_ref {
	uint32_t tag;
	uint32_t size;

	uint64_t cbmem_addr;
};

/* Coreboot table header structure */
struct coreboot_table_header {
	char signature[4];
	u32 header_bytes;
	u32 header_checksum;
	u32 table_bytes;
	u32 table_checksum;
	u32 table_entries;
};

/* Retrieve coreboot table entry with tag *tag* and copy it to data */
int coreboot_table_find(int tag, void *data, size_t data_size);

/* Initialize coreboot table module given a pointer to iomem */
int coreboot_table_init(void __iomem *ptr);

/* Cleanup coreboot table module */
int coreboot_table_exit(void);

#endif /* __COREBOOT_TABLE_H */
