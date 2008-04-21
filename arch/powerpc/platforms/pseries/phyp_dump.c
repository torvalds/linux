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

static int ibm_configure_kernel_dump;
/* ------------------------------------------------- */
/* RTAS interfaces to declare the dump regions */

struct dump_section {
	u32 dump_flags;
	u16 source_type;
	u16 error_flags;
	u64 source_address;
	u64 source_length;
	u64 length_copied;
	u64 destination_address;
};

struct phyp_dump_header {
	u32 version;
	u16 num_of_sections;
	u16 status;

	u32 first_offset_section;
	u32 dump_disk_section;
	u64 block_num_dd;
	u64 num_of_blocks_dd;
	u32 offset_dd;
	u32 maxtime_to_auto;
	/* No dump disk path string used */

	struct dump_section cpu_data;
	struct dump_section hpte_data;
	struct dump_section kernel_data;
};

/* The dump header *must be* in low memory, so .bss it */
static struct phyp_dump_header phdr;

#define NUM_DUMP_SECTIONS	3
#define DUMP_HEADER_VERSION	0x1
#define DUMP_REQUEST_FLAG	0x1
#define DUMP_SOURCE_CPU		0x0001
#define DUMP_SOURCE_HPTE	0x0002
#define DUMP_SOURCE_RMO		0x0011
#define DUMP_ERROR_FLAG		0x2000
#define DUMP_TRIGGERED		0x4000
#define DUMP_PERFORMED		0x8000


/**
 * init_dump_header() - initialize the header declaring a dump
 * Returns: length of dump save area.
 *
 * When the hypervisor saves crashed state, it needs to put
 * it somewhere. The dump header tells the hypervisor where
 * the data can be saved.
 */
static unsigned long init_dump_header(struct phyp_dump_header *ph)
{
	unsigned long addr_offset = 0;

	/* Set up the dump header */
	ph->version = DUMP_HEADER_VERSION;
	ph->num_of_sections = NUM_DUMP_SECTIONS;
	ph->status = 0;

	ph->first_offset_section =
		(u32)offsetof(struct phyp_dump_header, cpu_data);
	ph->dump_disk_section = 0;
	ph->block_num_dd = 0;
	ph->num_of_blocks_dd = 0;
	ph->offset_dd = 0;

	ph->maxtime_to_auto = 0; /* disabled */

	/* The first two sections are mandatory */
	ph->cpu_data.dump_flags = DUMP_REQUEST_FLAG;
	ph->cpu_data.source_type = DUMP_SOURCE_CPU;
	ph->cpu_data.source_address = 0;
	ph->cpu_data.source_length = phyp_dump_info->cpu_state_size;
	ph->cpu_data.destination_address = addr_offset;
	addr_offset += phyp_dump_info->cpu_state_size;

	ph->hpte_data.dump_flags = DUMP_REQUEST_FLAG;
	ph->hpte_data.source_type = DUMP_SOURCE_HPTE;
	ph->hpte_data.source_address = 0;
	ph->hpte_data.source_length = phyp_dump_info->hpte_region_size;
	ph->hpte_data.destination_address = addr_offset;
	addr_offset += phyp_dump_info->hpte_region_size;

	/* This section describes the low kernel region */
	ph->kernel_data.dump_flags = DUMP_REQUEST_FLAG;
	ph->kernel_data.source_type = DUMP_SOURCE_RMO;
	ph->kernel_data.source_address = PHYP_DUMP_RMR_START;
	ph->kernel_data.source_length = PHYP_DUMP_RMR_END;
	ph->kernel_data.destination_address = addr_offset;
	addr_offset += ph->kernel_data.source_length;

	return addr_offset;
}

static void print_dump_header(const struct phyp_dump_header *ph)
{
#ifdef DEBUG
	printk(KERN_INFO "dump header:\n");
	/* setup some ph->sections required */
	printk(KERN_INFO "version = %d\n", ph->version);
	printk(KERN_INFO "Sections = %d\n", ph->num_of_sections);
	printk(KERN_INFO "Status = 0x%x\n", ph->status);

	/* No ph->disk, so all should be set to 0 */
	printk(KERN_INFO "Offset to first section 0x%x\n",
		ph->first_offset_section);
	printk(KERN_INFO "dump disk sections should be zero\n");
	printk(KERN_INFO "dump disk section = %d\n", ph->dump_disk_section);
	printk(KERN_INFO "block num = %ld\n", ph->block_num_dd);
	printk(KERN_INFO "number of blocks = %ld\n", ph->num_of_blocks_dd);
	printk(KERN_INFO "dump disk offset = %d\n", ph->offset_dd);
	printk(KERN_INFO "Max auto time= %d\n", ph->maxtime_to_auto);

	/*set cpu state and hpte states as well scratch pad area */
	printk(KERN_INFO " CPU AREA \n");
	printk(KERN_INFO "cpu dump_flags =%d\n", ph->cpu_data.dump_flags);
	printk(KERN_INFO "cpu source_type =%d\n", ph->cpu_data.source_type);
	printk(KERN_INFO "cpu error_flags =%d\n", ph->cpu_data.error_flags);
	printk(KERN_INFO "cpu source_address =%lx\n",
		ph->cpu_data.source_address);
	printk(KERN_INFO "cpu source_length =%lx\n",
		ph->cpu_data.source_length);
	printk(KERN_INFO "cpu length_copied =%lx\n",
		ph->cpu_data.length_copied);

	printk(KERN_INFO " HPTE AREA \n");
	printk(KERN_INFO "HPTE dump_flags =%d\n", ph->hpte_data.dump_flags);
	printk(KERN_INFO "HPTE source_type =%d\n", ph->hpte_data.source_type);
	printk(KERN_INFO "HPTE error_flags =%d\n", ph->hpte_data.error_flags);
	printk(KERN_INFO "HPTE source_address =%lx\n",
		ph->hpte_data.source_address);
	printk(KERN_INFO "HPTE source_length =%lx\n",
		ph->hpte_data.source_length);
	printk(KERN_INFO "HPTE length_copied =%lx\n",
		ph->hpte_data.length_copied);

	printk(KERN_INFO " SRSD AREA \n");
	printk(KERN_INFO "SRSD dump_flags =%d\n", ph->kernel_data.dump_flags);
	printk(KERN_INFO "SRSD source_type =%d\n", ph->kernel_data.source_type);
	printk(KERN_INFO "SRSD error_flags =%d\n", ph->kernel_data.error_flags);
	printk(KERN_INFO "SRSD source_address =%lx\n",
		ph->kernel_data.source_address);
	printk(KERN_INFO "SRSD source_length =%lx\n",
		ph->kernel_data.source_length);
	printk(KERN_INFO "SRSD length_copied =%lx\n",
		ph->kernel_data.length_copied);
#endif
}

static ssize_t show_phyp_dump_active(struct kobject *kobj,
			struct kobj_attribute *attr, char *buf)
{

	/* create filesystem entry so kdump is phyp-dump aware */
	return sprintf(buf, "%lx\n", phyp_dump_info->phyp_dump_at_boot);
}

static struct kobj_attribute pdl = __ATTR(phyp_dump_active, 0600,
					show_phyp_dump_active,
					NULL);

static void register_dump_area(struct phyp_dump_header *ph, unsigned long addr)
{
	int rc;

	/* Add addr value if not initialized before */
	if (ph->cpu_data.destination_address == 0) {
		ph->cpu_data.destination_address += addr;
		ph->hpte_data.destination_address += addr;
		ph->kernel_data.destination_address += addr;
	}

	/* ToDo Invalidate kdump and free memory range. */

	do {
		rc = rtas_call(ibm_configure_kernel_dump, 3, 1, NULL,
				1, ph, sizeof(struct phyp_dump_header));
	} while (rtas_busy_delay(rc));

	if (rc) {
		printk(KERN_ERR "phyp-dump: unexpected error (%d) on "
						"register\n", rc);
		print_dump_header(ph);
		return;
	}

	rc = sysfs_create_file(kernel_kobj, &pdl.attr);
	if (rc)
		printk(KERN_ERR "phyp-dump: unable to create sysfs"
				" file (%d)\n", rc);
}

static
void invalidate_last_dump(struct phyp_dump_header *ph, unsigned long addr)
{
	int rc;

	/* Add addr value if not initialized before */
	if (ph->cpu_data.destination_address == 0) {
		ph->cpu_data.destination_address += addr;
		ph->hpte_data.destination_address += addr;
		ph->kernel_data.destination_address += addr;
	}

	do {
		rc = rtas_call(ibm_configure_kernel_dump, 3, 1, NULL,
				2, ph, sizeof(struct phyp_dump_header));
	} while (rtas_busy_delay(rc));

	if (rc) {
		printk(KERN_ERR "phyp-dump: unexpected error (%d) "
						"on invalidate\n", rc);
		print_dump_header(ph);
	}
}

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
static void release_memory_range(unsigned long start_pfn,
			unsigned long nr_pages)
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

/**
 * track_freed_range -- Counts the range being freed.
 * Once the counter goes to zero, it re-registers dump for
 * future use.
 */
static void
track_freed_range(unsigned long addr, unsigned long length)
{
	static unsigned long scratch_area_size, reserved_area_size;

	if (addr < phyp_dump_info->init_reserve_start)
		return;

	if ((addr >= phyp_dump_info->init_reserve_start) &&
	    (addr <= phyp_dump_info->init_reserve_start +
	     phyp_dump_info->init_reserve_size))
		reserved_area_size += length;

	if ((addr >= phyp_dump_info->reserved_scratch_addr) &&
	    (addr <= phyp_dump_info->reserved_scratch_addr +
	     phyp_dump_info->reserved_scratch_size))
		scratch_area_size += length;

	if ((reserved_area_size == phyp_dump_info->init_reserve_size) &&
	    (scratch_area_size == phyp_dump_info->reserved_scratch_size)) {

		invalidate_last_dump(&phdr,
				phyp_dump_info->reserved_scratch_addr);
		register_dump_area(&phdr,
				phyp_dump_info->reserved_scratch_addr);
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

	track_freed_range(start_addr, length);

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

static ssize_t show_release_region(struct kobject *kobj,
			struct kobj_attribute *attr, char *buf)
{
	u64 second_addr_range;

	/* total reserved size - start of scratch area */
	second_addr_range = phyp_dump_info->init_reserve_size -
				phyp_dump_info->reserved_scratch_size;
	return sprintf(buf, "CPU:0x%lx-0x%lx: HPTE:0x%lx-0x%lx:"
			    " DUMP:0x%lx-0x%lx, 0x%lx-0x%lx:\n",
		phdr.cpu_data.destination_address,
		phdr.cpu_data.length_copied,
		phdr.hpte_data.destination_address,
		phdr.hpte_data.length_copied,
		phdr.kernel_data.destination_address,
		phdr.kernel_data.length_copied,
		phyp_dump_info->init_reserve_start,
		second_addr_range);
}

static struct kobj_attribute rr = __ATTR(release_region, 0600,
					show_release_region,
					store_release_region);

static int __init phyp_dump_setup(void)
{
	struct device_node *rtas;
	const struct phyp_dump_header *dump_header = NULL;
	unsigned long dump_area_start;
	unsigned long dump_area_length;
	int header_len = 0;
	int rc;

	/* If no memory was reserved in early boot, there is nothing to do */
	if (phyp_dump_info->init_reserve_size == 0)
		return 0;

	/* Return if phyp dump not supported */
	if (!phyp_dump_info->phyp_dump_configured)
		return -ENOSYS;

	/* Is there dump data waiting for us? If there isn't,
	 * then register a new dump area, and release all of
	 * the rest of the reserved ram.
	 *
	 * The /rtas/ibm,kernel-dump rtas node is present only
	 * if there is dump data waiting for us.
	 */
	rtas = of_find_node_by_path("/rtas");
	if (rtas) {
		dump_header = of_get_property(rtas, "ibm,kernel-dump",
						&header_len);
		of_node_put(rtas);
	}

	print_dump_header(dump_header);
	dump_area_length = init_dump_header(&phdr);
	/* align down */
	dump_area_start = phyp_dump_info->init_reserve_start & PAGE_MASK;

	if (dump_header == NULL) {
		register_dump_area(&phdr, dump_area_start);
		return 0;
	}

	/* re-register the dump area, if old dump was invalid */
	if ((dump_header) && (dump_header->status & DUMP_ERROR_FLAG)) {
		invalidate_last_dump(&phdr, dump_area_start);
		register_dump_area(&phdr, dump_area_start);
		return 0;
	}

	if (dump_header) {
		phyp_dump_info->reserved_scratch_addr =
				dump_header->cpu_data.destination_address;
		phyp_dump_info->reserved_scratch_size =
				dump_header->cpu_data.source_length +
				dump_header->hpte_data.source_length +
				dump_header->kernel_data.source_length;
	}

	/* Should we create a dump_subsys, analogous to s390/ipl.c ? */
	rc = sysfs_create_file(kernel_kobj, &rr.attr);
	if (rc)
		printk(KERN_ERR "phyp-dump: unable to create sysfs file (%d)\n",
									rc);

	/* ToDo: re-register the dump area, for next time. */
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

/* Look for phyp_dump= cmdline option */
static int __init early_phyp_dump_enabled(char *p)
{
	phyp_dump_info->phyp_dump_at_boot = 1;

        if (!p)
                return 0;

        if (strncmp(p, "1", 1) == 0)
		phyp_dump_info->phyp_dump_at_boot = 1;
        else if (strncmp(p, "0", 1) == 0)
		phyp_dump_info->phyp_dump_at_boot = 0;

        return 0;
}
early_param("phyp_dump", early_phyp_dump_enabled);

/* Look for phyp_dump_reserve_size= cmdline option */
static int __init early_phyp_dump_reserve_size(char *p)
{
        if (p)
		phyp_dump_info->reserve_bootvar = memparse(p, &p);

        return 0;
}
early_param("phyp_dump_reserve_size", early_phyp_dump_reserve_size);
