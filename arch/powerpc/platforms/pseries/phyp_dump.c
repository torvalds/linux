/*
 * Hypervisor-assisted dump
 *
 * Linas Vepstas, Manish Ahuja 2008
 * Copyright 2008 IBM Corp.
 *
 *      This program is free software; you can redistribute it and/or
 *      modify it under the terms of the GNU General Public License
 *      as published by the Free Software Foundation; either version
 *      2 of the License, or (at your option) any later version.
 *
 */

#include <linux/init.h>
#include <linux/kobject.h>
#include <linux/mm.h>
#include <linux/of.h>
#include <linux/pfn.h>
#include <linux/swap.h>
#include <linux/sysfs.h>

#include <asm/page.h>
#include <asm/phyp_dump.h>
#include <asm/machdep.h>
#include <asm/prom.h>
#include <asm/rtas.h>

/* Variables, used to communicate data between early boot and late boot */
static struct phyp_dump phyp_dump_vars;
struct phyp_dump *phyp_dump_info = &phyp_dump_vars;

/* ------------------------------------------------- */
/**
 * release_memory_range -- release memory previously lmb_reserved
 * @start_pfn: starting physical frame number
 * @nr_pages: number of pages to free.
 *
 * This routine will release memory that had been previously
 * lmb_reserved in early boot. The released memory becomes
 * available for genreal use.
 */
static void
release_memory_range(unsigned long start_pfn, unsigned long nr_pages)
{
	struct page *rpage;
	unsigned long end_pfn;
	long i;

	end_pfn = start_pfn + nr_pages;

	for (i = start_pfn; i <= end_pfn; i++) {
		rpage = pfn_to_page(i);
		if (PageReserved(rpage)) {
			ClearPageReserved(rpage);
			init_page_count(rpage);
			__free_page(rpage);
			totalram_pages++;
		}
	}
}

/* ------------------------------------------------- */
/**
 * sysfs_release_region -- sysfs interface to release memory range.
 *
 * Usage:
 *   "echo <start addr> <length> > /sys/kernel/release_region"
 *
 * Example:
 *   "echo 0x40000000 0x10000000 > /sys/kernel/release_region"
 *
 * will release 256MB starting at 1GB.
 */
static ssize_t store_release_region(struct kobject *kobj,
				struct kobj_attribute *attr,
				const char *buf, size_t count)
{
	unsigned long start_addr, length, end_addr;
	unsigned long start_pfn, nr_pages;
	ssize_t ret;

	ret = sscanf(buf, "%lx %lx", &start_addr, &length);
	if (ret != 2)
		return -EINVAL;

	/* Range-check - don't free any reserved memory that
	 * wasn't reserved for phyp-dump */
	if (start_addr < phyp_dump_info->init_reserve_start)
		start_addr = phyp_dump_info->init_reserve_start;

	end_addr = phyp_dump_info->init_reserve_start +
			phyp_dump_info->init_reserve_size;
	if (start_addr+length > end_addr)
		length = end_addr - start_addr;

	/* Release the region of memory assed in by user */
	start_pfn = PFN_DOWN(start_addr);
	nr_pages = PFN_DOWN(length);
	release_memory_range(start_pfn, nr_pages);

	return count;
}

static struct kobj_attribute rr = __ATTR(release_region, 0600,
					 NULL, store_release_region);

static int __init phyp_dump_setup(void)
{
	struct device_node *rtas;
	const int *dump_header = NULL;
	int header_len = 0;
	int rc;

	/* If no memory was reserved in early boot, there is nothing to do */
	if (phyp_dump_info->init_reserve_size == 0)
		return 0;

	/* Return if phyp dump not supported */
	if (!phyp_dump_info->phyp_dump_configured)
		return -ENOSYS;

	/* Is there dump data waiting for us? */
	rtas = of_find_node_by_path("/rtas");
	if (rtas) {
		dump_header = of_get_property(rtas, "ibm,kernel-dump",
						&header_len);
		of_node_put(rtas);
	}

	if (dump_header == NULL)
		return 0;

	/* Should we create a dump_subsys, analogous to s390/ipl.c ? */
	rc = sysfs_create_file(kernel_kobj, &rr.attr);
	if (rc) {
		printk(KERN_ERR "phyp-dump: unable to create sysfs file (%d)\n",
									rc);
		return 0;
	}

	return 0;
}
machine_subsys_initcall(pseries, phyp_dump_setup);

int __init early_init_dt_scan_phyp_dump(unsigned long node,
		const char *uname, int depth, void *data)
{
	const unsigned int *sizes;

	phyp_dump_info->phyp_dump_configured = 0;
	phyp_dump_info->phyp_dump_is_active = 0;

	if (depth != 1 || strcmp(uname, "rtas") != 0)
		return 0;

	if (of_get_flat_dt_prop(node, "ibm,configure-kernel-dump", NULL))
		phyp_dump_info->phyp_dump_configured++;

	if (of_get_flat_dt_prop(node, "ibm,dump-kernel", NULL))
		phyp_dump_info->phyp_dump_is_active++;

	sizes = of_get_flat_dt_prop(node, "ibm,configure-kernel-dump-sizes",
				    NULL);
	if (!sizes)
		return 0;

	if (sizes[0] == 1)
		phyp_dump_info->cpu_state_size = *((unsigned long *)&sizes[1]);

	if (sizes[3] == 2)
		phyp_dump_info->hpte_region_size =
						*((unsigned long *)&sizes[4]);
	return 1;
}
