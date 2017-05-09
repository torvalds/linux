/*
 * coreboot_table.c
 *
 * Module providing coreboot table access.
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

#include <linux/err.h>
#include <linux/init.h>
#include <linux/io.h>
#include <linux/kernel.h>
#include <linux/module.h>

#include "coreboot_table.h"

struct coreboot_table_entry {
	u32 tag;
	u32 size;
};

static struct coreboot_table_header __iomem *ptr_header;

/*
 * This function parses the coreboot table for an entry that contains the base
 * address of the given entry tag. The coreboot table consists of a header
 * directly followed by a number of small, variable-sized entries, which each
 * contain an identifying tag and their length as the first two fields.
 */
int coreboot_table_find(int tag, void *data, size_t data_size)
{
	struct coreboot_table_header header;
	struct coreboot_table_entry entry;
	void *ptr_entry;
	int i;

	if (!ptr_header)
		return -EPROBE_DEFER;

	memcpy_fromio(&header, ptr_header, sizeof(header));

	if (strncmp(header.signature, "LBIO", sizeof(header.signature))) {
		pr_warn("coreboot_table: coreboot table missing or corrupt!\n");
		return -ENODEV;
	}

	ptr_entry = (void *)ptr_header + header.header_bytes;

	for (i = 0; i < header.table_entries; i++) {
		memcpy_fromio(&entry, ptr_entry, sizeof(entry));
		if (entry.tag == tag) {
			if (data_size < entry.size)
				return -EINVAL;

			memcpy_fromio(data, ptr_entry, entry.size);

			return 0;
		}

		ptr_entry += entry.size;
	}

	return -ENOENT;
}
EXPORT_SYMBOL(coreboot_table_find);

int coreboot_table_init(void __iomem *ptr)
{
	ptr_header = ptr;

	return 0;
}
EXPORT_SYMBOL(coreboot_table_init);

int coreboot_table_exit(void)
{
	if (ptr_header)
		iounmap(ptr_header);

	return 0;
}
EXPORT_SYMBOL(coreboot_table_exit);

MODULE_AUTHOR("Google, Inc.");
MODULE_LICENSE("GPL");
