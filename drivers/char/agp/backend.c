/*
 * AGPGART driver backend routines.
 * Copyright (C) 2004 Silicon Graphics, Inc.
 * Copyright (C) 2002-2003 Dave Jones.
 * Copyright (C) 1999 Jeff Hartmann.
 * Copyright (C) 1999 Precision Insight, Inc.
 * Copyright (C) 1999 Xi Graphics, Inc.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included
 * in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
 * OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * JEFF HARTMANN, DAVE JONES, OR ANY OTHER CONTRIBUTORS BE LIABLE FOR ANY CLAIM,
 * DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR
 * OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE
 * OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 *
 * TODO:
 * - Allocate more than order 0 pages to avoid too much linear map splitting.
 */
#include <linux/module.h>
#include <linux/pci.h>
#include <linux/init.h>
#include <linux/pagemap.h>
#include <linux/miscdevice.h>
#include <linux/pm.h>
#include <linux/agp_backend.h>
#include <linux/agpgart.h>
#include <linux/vmalloc.h>
#include <asm/io.h>
#include "agp.h"

/* Due to XFree86 brain-damage, we can't go to 1.0 until they
 * fix some real stupidity. It's only by chance we can bump
 * past 0.99 at all due to some boolean logic error. */
#define AGPGART_VERSION_MAJOR 0
#define AGPGART_VERSION_MINOR 101
static struct agp_version agp_current_version =
{
	.major = AGPGART_VERSION_MAJOR,
	.minor = AGPGART_VERSION_MINOR,
};

struct agp_bridge_data *(*agp_find_bridge)(struct pci_dev *) =
	&agp_generic_find_bridge;

struct agp_bridge_data *agp_bridge;
LIST_HEAD(agp_bridges);
EXPORT_SYMBOL(agp_bridge);
EXPORT_SYMBOL(agp_bridges);
EXPORT_SYMBOL(agp_find_bridge);

/**
 *	agp_backend_acquire  -  attempt to acquire an agp backend.
 *
 */
struct agp_bridge_data *agp_backend_acquire(struct pci_dev *pdev)
{
	struct agp_bridge_data *bridge;

	bridge = agp_find_bridge(pdev);

	if (!bridge)
		return NULL;

	if (atomic_read(&bridge->agp_in_use))
		return NULL;
	atomic_inc(&bridge->agp_in_use);
	return bridge;
}
EXPORT_SYMBOL(agp_backend_acquire);


/**
 *	agp_backend_release  -  release the lock on the agp backend.
 *
 *	The caller must insure that the graphics aperture translation table
 *	is read for use by another entity.
 *
 *	(Ensure that all memory it bound is unbound.)
 */
void agp_backend_release(struct agp_bridge_data *bridge)
{

	if (bridge)
		atomic_dec(&bridge->agp_in_use);
}
EXPORT_SYMBOL(agp_backend_release);


static struct { int mem, agp; } maxes_table[] = {
	{0, 0},
	{32, 4},
	{64, 28},
	{128, 96},
	{256, 204},
	{512, 440},
	{1024, 942},
	{2048, 1920},
	{4096, 3932}
};

static int agp_find_max(void)
{
	long memory, index, result;

#if PAGE_SHIFT < 20
	memory = num_physpages >> (20 - PAGE_SHIFT);
#else
	memory = num_physpages << (PAGE_SHIFT - 20);
#endif
	index = 1;

	while ((memory > maxes_table[index].mem) && (index < 8))
		index++;

	result = maxes_table[index - 1].agp +
	   ( (memory - maxes_table[index - 1].mem)  *
	     (maxes_table[index].agp - maxes_table[index - 1].agp)) /
	   (maxes_table[index].mem - maxes_table[index - 1].mem);

	result = result << (20 - PAGE_SHIFT);
	return result;
}


static int agp_backend_initialize(struct agp_bridge_data *bridge)
{
	int size_value, rc, got_gatt=0, got_keylist=0;

	bridge->max_memory_agp = agp_find_max();
	bridge->version = &agp_current_version;

	if (bridge->driver->needs_scratch_page) {
		void *addr = bridge->driver->agp_alloc_page(bridge);

		if (!addr) {
			printk(KERN_ERR PFX "unable to get memory for scratch page.\n");
			return -ENOMEM;
		}

		bridge->scratch_page_real = virt_to_gart(addr);
		bridge->scratch_page =
		    bridge->driver->mask_memory(bridge, bridge->scratch_page_real, 0);
	}

	size_value = bridge->driver->fetch_size();
	if (size_value == 0) {
		printk(KERN_ERR PFX "unable to determine aperture size.\n");
		rc = -EINVAL;
		goto err_out;
	}
	if (bridge->driver->create_gatt_table(bridge)) {
		printk(KERN_ERR PFX
		    "unable to get memory for graphics translation table.\n");
		rc = -ENOMEM;
		goto err_out;
	}
	got_gatt = 1;

	bridge->key_list = vmalloc(PAGE_SIZE * 4);
	if (bridge->key_list == NULL) {
		printk(KERN_ERR PFX "error allocating memory for key lists.\n");
		rc = -ENOMEM;
		goto err_out;
	}
	got_keylist = 1;

	/* FIXME vmalloc'd memory not guaranteed contiguous */
	memset(bridge->key_list, 0, PAGE_SIZE * 4);

	if (bridge->driver->configure()) {
		printk(KERN_ERR PFX "error configuring host chipset.\n");
		rc = -EINVAL;
		goto err_out;
	}

	return 0;

err_out:
	if (bridge->driver->needs_scratch_page)
		bridge->driver->agp_destroy_page(
				gart_to_virt(bridge->scratch_page_real));
	if (got_gatt)
		bridge->driver->free_gatt_table(bridge);
	if (got_keylist) {
		vfree(bridge->key_list);
		bridge->key_list = NULL;
	}
	return rc;
}

/* cannot be __exit b/c as it could be called from __init code */
static void agp_backend_cleanup(struct agp_bridge_data *bridge)
{
	if (bridge->driver->cleanup)
		bridge->driver->cleanup();
	if (bridge->driver->free_gatt_table)
		bridge->driver->free_gatt_table(bridge);

	vfree(bridge->key_list);
	bridge->key_list = NULL;

	if (bridge->driver->agp_destroy_page &&
	    bridge->driver->needs_scratch_page)
		bridge->driver->agp_destroy_page(
				gart_to_virt(bridge->scratch_page_real));
}

/* When we remove the global variable agp_bridge from all drivers
 * then agp_alloc_bridge and agp_generic_find_bridge need to be updated
 */

struct agp_bridge_data *agp_alloc_bridge(void)
{
	struct agp_bridge_data *bridge = kmalloc(sizeof(*bridge), GFP_KERNEL);

	if (!bridge)
		return NULL;

	memset(bridge, 0, sizeof(*bridge));
	atomic_set(&bridge->agp_in_use, 0);
	atomic_set(&bridge->current_memory_agp, 0);

	if (list_empty(&agp_bridges))
		agp_bridge = bridge;

	return bridge;
}
EXPORT_SYMBOL(agp_alloc_bridge);


void agp_put_bridge(struct agp_bridge_data *bridge)
{
        kfree(bridge);

        if (list_empty(&agp_bridges))
                agp_bridge = NULL;
}
EXPORT_SYMBOL(agp_put_bridge);


int agp_add_bridge(struct agp_bridge_data *bridge)
{
	int error;

	if (agp_off)
		return -ENODEV;

	if (!bridge->dev) {
		printk (KERN_DEBUG PFX "Erk, registering with no pci_dev!\n");
		return -EINVAL;
	}

	/* Grab reference on the chipset driver. */
	if (!try_module_get(bridge->driver->owner)) {
		printk (KERN_INFO PFX "Couldn't lock chipset driver.\n");
		return -EINVAL;
	}

	error = agp_backend_initialize(bridge);
	if (error) {
		printk (KERN_INFO PFX "agp_backend_initialize() failed.\n");
		goto err_out;
	}

	if (list_empty(&agp_bridges)) {
		error = agp_frontend_initialize();
		if (error) {
			printk (KERN_INFO PFX "agp_frontend_initialize() failed.\n");
			goto frontend_err;
		}

		printk(KERN_INFO PFX "AGP aperture is %dM @ 0x%lx\n",
			bridge->driver->fetch_size(), bridge->gart_bus_addr);

	}

	list_add(&bridge->list, &agp_bridges);
	return 0;

frontend_err:
	agp_backend_cleanup(bridge);
err_out:
	module_put(bridge->driver->owner);
	agp_put_bridge(bridge);
	return error;
}
EXPORT_SYMBOL_GPL(agp_add_bridge);


void agp_remove_bridge(struct agp_bridge_data *bridge)
{
	agp_backend_cleanup(bridge);
	list_del(&bridge->list);
	if (list_empty(&agp_bridges))
		agp_frontend_cleanup();
	module_put(bridge->driver->owner);
}
EXPORT_SYMBOL_GPL(agp_remove_bridge);

int agp_off;
int agp_try_unsupported_boot;
EXPORT_SYMBOL(agp_off);
EXPORT_SYMBOL(agp_try_unsupported_boot);

static int __init agp_init(void)
{
	if (!agp_off)
		printk(KERN_INFO "Linux agpgart interface v%d.%d (c) Dave Jones\n",
			AGPGART_VERSION_MAJOR, AGPGART_VERSION_MINOR);
	return 0;
}

static void __exit agp_exit(void)
{
}

#ifndef MODULE
static __init int agp_setup(char *s)
{
	if (!strcmp(s,"off"))
		agp_off = 1;
	if (!strcmp(s,"try_unsupported"))
		agp_try_unsupported_boot = 1;
	return 1;
}
__setup("agp=", agp_setup);
#endif

MODULE_AUTHOR("Dave Jones <davej@codemonkey.org.uk>");
MODULE_DESCRIPTION("AGP GART driver");
MODULE_LICENSE("GPL and additional rights");
MODULE_ALIAS_MISCDEV(AGPGART_MINOR);

module_init(agp_init);
module_exit(agp_exit);

