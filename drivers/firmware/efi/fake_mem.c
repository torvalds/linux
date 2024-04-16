// SPDX-License-Identifier: GPL-2.0
/*
 * fake_mem.c
 *
 * Copyright (C) 2015 FUJITSU LIMITED
 * Author: Taku Izumi <izumi.taku@jp.fujitsu.com>
 *
 * This code introduces new boot option named "efi_fake_mem"
 * By specifying this parameter, you can add arbitrary attribute to
 * specific memory range by updating original (firmware provided) EFI
 * memmap.
 */

#include <linux/kernel.h>
#include <linux/efi.h>
#include <linux/init.h>
#include <linux/memblock.h>
#include <linux/types.h>
#include <linux/sort.h>
#include "fake_mem.h"

struct efi_mem_range efi_fake_mems[EFI_MAX_FAKEMEM];
int nr_fake_mem;

static int __init cmp_fake_mem(const void *x1, const void *x2)
{
	const struct efi_mem_range *m1 = x1;
	const struct efi_mem_range *m2 = x2;

	if (m1->range.start < m2->range.start)
		return -1;
	if (m1->range.start > m2->range.start)
		return 1;
	return 0;
}

static void __init efi_fake_range(struct efi_mem_range *efi_range)
{
	struct efi_memory_map_data data = { 0 };
	int new_nr_map = efi.memmap.nr_map;
	efi_memory_desc_t *md;
	void *new_memmap;

	/* count up the number of EFI memory descriptor */
	for_each_efi_memory_desc(md)
		new_nr_map += efi_memmap_split_count(md, &efi_range->range);

	/* allocate memory for new EFI memmap */
	if (efi_memmap_alloc(new_nr_map, &data) != 0)
		return;

	/* create new EFI memmap */
	new_memmap = early_memremap(data.phys_map, data.size);
	if (!new_memmap) {
		__efi_memmap_free(data.phys_map, data.size, data.flags);
		return;
	}

	efi_memmap_insert(&efi.memmap, new_memmap, efi_range);

	/* swap into new EFI memmap */
	early_memunmap(new_memmap, data.size);

	efi_memmap_install(&data);
}

void __init efi_fake_memmap(void)
{
	int i;

	if (!efi_enabled(EFI_MEMMAP) || !nr_fake_mem)
		return;

	for (i = 0; i < nr_fake_mem; i++)
		efi_fake_range(&efi_fake_mems[i]);

	/* print new EFI memmap */
	efi_print_memmap();
}

static int __init setup_fake_mem(char *p)
{
	u64 start = 0, mem_size = 0, attribute = 0;
	int i;

	if (!p)
		return -EINVAL;

	while (*p != '\0') {
		mem_size = memparse(p, &p);
		if (*p == '@')
			start = memparse(p+1, &p);
		else
			break;

		if (*p == ':')
			attribute = simple_strtoull(p+1, &p, 0);
		else
			break;

		if (nr_fake_mem >= EFI_MAX_FAKEMEM)
			break;

		efi_fake_mems[nr_fake_mem].range.start = start;
		efi_fake_mems[nr_fake_mem].range.end = start + mem_size - 1;
		efi_fake_mems[nr_fake_mem].attribute = attribute;
		nr_fake_mem++;

		if (*p == ',')
			p++;
	}

	sort(efi_fake_mems, nr_fake_mem, sizeof(struct efi_mem_range),
	     cmp_fake_mem, NULL);

	for (i = 0; i < nr_fake_mem; i++)
		pr_info("efi_fake_mem: add attr=0x%016llx to [mem 0x%016llx-0x%016llx]",
			efi_fake_mems[i].attribute, efi_fake_mems[i].range.start,
			efi_fake_mems[i].range.end);

	return *p == '\0' ? 0 : -EINVAL;
}

early_param("efi_fake_mem", setup_fake_mem);
