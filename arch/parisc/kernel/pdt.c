/*
 *    Page Deallocation Table (PDT) support
 *
 *    The Page Deallocation Table (PDT) holds a table with pointers to bad
 *    memory (broken RAM modules) which is maintained by firmware.
 *
 *    Copyright 2017 by Helge Deller <deller@gmx.de>
 *
 *    TODO:
 *    - check regularily for new bad memory
 *    - add userspace interface with procfs or sysfs
 *    - increase number of PDT entries dynamically
 */

#include <linux/memblock.h>
#include <linux/seq_file.h>

#include <asm/pdc.h>
#include <asm/pdcpat.h>
#include <asm/sections.h>
#include <asm/pgtable.h>

enum pdt_access_type {
	PDT_NONE,
	PDT_PDC,
	PDT_PAT_NEW,
	PDT_PAT_OLD
};

static enum pdt_access_type pdt_type;

/* global PDT status information */
static struct pdc_mem_retinfo pdt_status;

#define MAX_PDT_TABLE_SIZE	PAGE_SIZE
#define MAX_PDT_ENTRIES		(MAX_PDT_TABLE_SIZE / sizeof(unsigned long))
static unsigned long pdt_entry[MAX_PDT_ENTRIES] __page_aligned_bss;


/* report PDT entries via /proc/meminfo */
void arch_report_meminfo(struct seq_file *m)
{
	if (pdt_type == PDT_NONE)
		return;

	seq_printf(m, "PDT_max_entries: %7lu\n",
			pdt_status.pdt_size);
	seq_printf(m, "PDT_cur_entries: %7lu\n",
			pdt_status.pdt_entries);
}

/*
 * pdc_pdt_init()
 *
 * Initialize kernel PDT structures, read initial PDT table from firmware,
 * report all current PDT entries and mark bad memory with memblock_reserve()
 * to avoid that the kernel will use broken memory areas.
 *
 */
void __init pdc_pdt_init(void)
{
	int ret, i;
	unsigned long entries;
	struct pdc_mem_read_pdt pdt_read_ret;

	if (is_pdc_pat()) {
		struct pdc_pat_mem_retinfo pat_rinfo;

		pdt_type = PDT_PAT_NEW;
		ret = pdc_pat_mem_pdt_info(&pat_rinfo);
		pdt_status.pdt_size = pat_rinfo.max_pdt_entries;
		pdt_status.pdt_entries = pat_rinfo.current_pdt_entries;
		pdt_status.pdt_status = 0;
		pdt_status.first_dbe_loc = pat_rinfo.first_dbe_loc;
		pdt_status.good_mem = pat_rinfo.good_mem;
	} else {
		pdt_type = PDT_PDC;
		ret = pdc_mem_pdt_info(&pdt_status);
	}

	if (ret != PDC_OK) {
		pdt_type = PDT_NONE;
		pr_info("PDT: Firmware does not provide any page deallocation"
			" information.\n");
		return;
	}

	entries = pdt_status.pdt_entries;
	WARN_ON(entries > MAX_PDT_ENTRIES);

	pr_info("PDT: size %lu, entries %lu, status %lu, dbe_loc 0x%lx,"
		" good_mem %lu\n",
			pdt_status.pdt_size, pdt_status.pdt_entries,
			pdt_status.pdt_status, pdt_status.first_dbe_loc,
			pdt_status.good_mem);

	if (entries == 0) {
		pr_info("PDT: Firmware reports all memory OK.\n");
		return;
	}

	if (pdt_status.first_dbe_loc &&
		pdt_status.first_dbe_loc <= __pa((unsigned long)&_end))
		pr_crit("CRITICAL: Bad memory inside kernel image memory area!\n");

	pr_warn("PDT: Firmware reports %lu entries of faulty memory:\n",
		entries);

	if (pdt_type == PDT_PDC)
		ret = pdc_mem_pdt_read_entries(&pdt_read_ret, pdt_entry);
	else {
#ifdef CONFIG_64BIT
		struct pdc_pat_mem_read_pd_retinfo pat_pret;

		/* try old obsolete PAT firmware function first */
		pdt_type = PDT_PAT_OLD;
		ret = pdc_pat_mem_read_cell_pdt(&pat_pret, pdt_entry,
			MAX_PDT_ENTRIES);
		if (ret != PDC_OK) {
			pdt_type = PDT_PAT_NEW;
			ret = pdc_pat_mem_read_pd_pdt(&pat_pret, pdt_entry,
				MAX_PDT_TABLE_SIZE, 0);
		}
#else
		ret = PDC_BAD_PROC;
#endif
	}

	if (ret != PDC_OK) {
		pdt_type = PDT_NONE;
		pr_debug("PDT type %d, retval = %d\n", pdt_type, ret);
		return;
	}

	for (i = 0; i < pdt_status.pdt_entries; i++) {
		struct pdc_pat_mem_phys_mem_location loc;

		/* get DIMM slot number */
		loc.dimm_slot = 0xff;
#ifdef CONFIG_64BIT
		pdc_pat_mem_get_dimm_phys_location(&loc, pdt_entry[i]);
#endif

		pr_warn("PDT: BAD PAGE #%d at 0x%08lx, "
			"DIMM slot %02x (error_type = %lu)\n",
			i,
			pdt_entry[i] & PAGE_MASK,
			loc.dimm_slot,
			pdt_entry[i] & 1);

		/* mark memory page bad */
		memblock_reserve(pdt_entry[i] & PAGE_MASK, PAGE_SIZE);
	}
}
