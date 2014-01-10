/*
 * drivers/gpu/ion/ion_dummy_driver.c
 *
 * Copyright (C) 2013 Linaro, Inc
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */

#include <linux/err.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/bootmem.h>
#include <linux/memblock.h>
#include <linux/sizes.h>
#include "ion.h"
#include "ion_priv.h"

struct ion_device *idev;
struct ion_heap **heaps;

struct ion_platform_heap dummy_heaps[] = {
		{
			.id	= ION_HEAP_TYPE_SYSTEM,
			.type	= ION_HEAP_TYPE_SYSTEM,
			.name	= "system",
		},
		{
			.id	= ION_HEAP_TYPE_SYSTEM_CONTIG,
			.type	= ION_HEAP_TYPE_SYSTEM_CONTIG,
			.name	= "system contig",
		},
};

struct ion_platform_data dummy_ion_pdata = {
	.nr = 2,
	.heaps = dummy_heaps,
};

static int __init ion_dummy_init(void)
{
	int i, err;

	idev = ion_device_create(NULL);
	heaps = kzalloc(sizeof(struct ion_heap *) * dummy_ion_pdata.nr,
			GFP_KERNEL);
	if (!heaps)
		return PTR_ERR(heaps);

	for (i = 0; i < dummy_ion_pdata.nr; i++) {
		struct ion_platform_heap *heap_data = &dummy_ion_pdata.heaps[i];

		heaps[i] = ion_heap_create(heap_data);
		if (IS_ERR_OR_NULL(heaps[i])) {
			err = PTR_ERR(heaps[i]);
			goto err;
		}
		ion_device_add_heap(idev, heaps[i]);
	}
	return 0;
err:
	for (i = 0; i < dummy_ion_pdata.nr; i++) {
		if (heaps[i])
			ion_heap_destroy(heaps[i]);
	}
	kfree(heaps);

	return err;
}

static void __exit ion_dummy_exit(void)
{
	int i;

	ion_device_destroy(idev);

	for (i = 0; i < dummy_ion_pdata.nr; i++)
		ion_heap_destroy(heaps[i]);
	kfree(heaps);

	return;
}

module_init(ion_dummy_init);
module_exit(ion_dummy_exit);

