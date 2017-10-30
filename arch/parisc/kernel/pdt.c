/*
 *    Page Deallocation Table (PDT) support
 *
 *    The Page Deallocation Table (PDT) is maintained by firmware and holds a
 *    list of memory addresses in which memory errors were detected.
 *    The list contains both single-bit (correctable) and double-bit
 *    (uncorrectable) errors.
 *
 *    Copyright 2017 by Helge Deller <deller@gmx.de>
 *
 *    possible future enhancements:
 *    - add userspace interface via procfs or sysfs to clear PDT
 */

#include <linux/memblock.h>
#include <linux/seq_file.h>
#include <linux/kthread.h>
#include <linux/initrd.h>

#include <asm/pdc.h>
#include <asm/pdcpat.h>
#include <asm/sections.h>
#include <asm/pgtable.h>

enum pdt_access_type {
	PDT_NONE,
	PDT_PDC,
	PDT_PAT_NEW,
	PDT_PAT_CELL
};

static enum pdt_access_type pdt_type;

/* PDT poll interval: 1 minute if errors, 5 minutes if everything OK. */
#define PDT_POLL_INTERVAL_DEFAULT	(5*60*HZ)
#define PDT_POLL_INTERVAL_SHORT		(1*60*HZ)
static unsigned long pdt_poll_interval = PDT_POLL_INTERVAL_DEFAULT;

/* global PDT status information */
static struct pdc_mem_retinfo pdt_status;

#define MAX_PDT_TABLE_SIZE	PAGE_SIZE
#define MAX_PDT_ENTRIES		(MAX_PDT_TABLE_SIZE / sizeof(unsigned long))
static unsigned long pdt_entry[MAX_PDT_ENTRIES] __page_aligned_bss;

/*
 * Constants for the pdt_entry format:
 * A pdt_entry holds the physical address in bits 0-57, bits 58-61 are
 * reserved, bit 62 is the perm bit and bit 63 is the error_type bit.
 * The perm bit indicates whether the error have been verified as a permanent
 * error (value of 1) or has not been verified, and may be transient (value
 * of 0). The error_type bit indicates whether the error is a single bit error
 * (value of 1) or a multiple bit error.
 * On non-PAT machines phys_addr is encoded in bits 0-59 and error_type in bit
 * 63. Those machines don't provide the perm bit.
 */

#define PDT_ADDR_PHYS_MASK	(pdt_type != PDT_PDC ? ~0x3f : ~0x0f)
#define PDT_ADDR_PERM_ERR	(pdt_type != PDT_PDC ? 2UL : 0UL)
#define PDT_ADDR_SINGLE_ERR	1UL

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

static int get_info_pat_new(void)
{
	struct pdc_pat_mem_retinfo pat_rinfo;
	int ret;

	/* newer PAT machines like C8000 report info for all cells */
	if (is_pdc_pat())
		ret = pdc_pat_mem_pdt_info(&pat_rinfo);
	else
		return PDC_BAD_PROC;

	pdt_status.pdt_size = pat_rinfo.max_pdt_entries;
	pdt_status.pdt_entries = pat_rinfo.current_pdt_entries;
	pdt_status.pdt_status = 0;
	pdt_status.first_dbe_loc = pat_rinfo.first_dbe_loc;
	pdt_status.good_mem = pat_rinfo.good_mem;

	return ret;
}

static int get_info_pat_cell(void)
{
	struct pdc_pat_mem_cell_pdt_retinfo cell_rinfo;
	int ret;

	/* older PAT machines like rp5470 report cell info only */
	if (is_pdc_pat())
		ret = pdc_pat_mem_pdt_cell_info(&cell_rinfo, parisc_cell_num);
	else
		return PDC_BAD_PROC;

	pdt_status.pdt_size = cell_rinfo.max_pdt_entries;
	pdt_status.pdt_entries = cell_rinfo.current_pdt_entries;
	pdt_status.pdt_status = 0;
	pdt_status.first_dbe_loc = cell_rinfo.first_dbe_loc;
	pdt_status.good_mem = cell_rinfo.good_mem;

	return ret;
}

static void report_mem_err(unsigned long pde)
{
	struct pdc_pat_mem_phys_mem_location loc;
	unsigned long addr;
	char dimm_txt[32];

	addr = pde & PDT_ADDR_PHYS_MASK;

	/* show DIMM slot description on PAT machines */
	if (is_pdc_pat()) {
		pdc_pat_mem_get_dimm_phys_location(&loc, addr);
		sprintf(dimm_txt, "DIMM slot %02x, ", loc.dimm_slot);
	} else
		dimm_txt[0] = 0;

	pr_warn("PDT: BAD MEMORY at 0x%08lx, %s%s%s-bit error.\n",
		addr, dimm_txt,
		pde & PDT_ADDR_PERM_ERR ? "permanent ":"",
		pde & PDT_ADDR_SINGLE_ERR ? "single":"multi");
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

	pdt_type = PDT_PAT_NEW;
	ret = get_info_pat_new();

	if (ret != PDC_OK) {
		pdt_type = PDT_PAT_CELL;
		ret = get_info_pat_cell();
	}

	if (ret != PDC_OK) {
		pdt_type = PDT_PDC;
		/* non-PAT machines provide the standard PDC call */
		ret = pdc_mem_pdt_info(&pdt_status);
	}

	if (ret != PDC_OK) {
		pdt_type = PDT_NONE;
		pr_info("PDT: Firmware does not provide any page deallocation"
			" information.\n");
		return;
	}

	entries = pdt_status.pdt_entries;
	if (WARN_ON(entries > MAX_PDT_ENTRIES))
		entries = pdt_status.pdt_entries = MAX_PDT_ENTRIES;

	pr_info("PDT: type %s, size %lu, entries %lu, status %lu, dbe_loc 0x%lx,"
		" good_mem %lu MB\n",
			pdt_type == PDT_PDC ? __stringify(PDT_PDC) :
			pdt_type == PDT_PAT_CELL ? __stringify(PDT_PAT_CELL)
						 : __stringify(PDT_PAT_NEW),
			pdt_status.pdt_size, pdt_status.pdt_entries,
			pdt_status.pdt_status, pdt_status.first_dbe_loc,
			pdt_status.good_mem / 1024 / 1024);

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

		if (pdt_type == PDT_PAT_CELL)
			ret = pdc_pat_mem_read_cell_pdt(&pat_pret, pdt_entry,
				MAX_PDT_ENTRIES);
		else
			ret = pdc_pat_mem_read_pd_pdt(&pat_pret, pdt_entry,
				MAX_PDT_TABLE_SIZE, 0);
#else
		ret = PDC_BAD_PROC;
#endif
	}

	if (ret != PDC_OK) {
		pdt_type = PDT_NONE;
		pr_warn("PDT: Get PDT entries failed with %d\n", ret);
		return;
	}

	for (i = 0; i < pdt_status.pdt_entries; i++) {
		unsigned long addr;

		report_mem_err(pdt_entry[i]);

		addr = pdt_entry[i] & PDT_ADDR_PHYS_MASK;
		if (IS_ENABLED(CONFIG_BLK_DEV_INITRD) &&
			addr >= initrd_start && addr < initrd_end)
			pr_crit("CRITICAL: initrd possibly broken "
				"due to bad memory!\n");

		/* mark memory page bad */
		memblock_reserve(pdt_entry[i] & PAGE_MASK, PAGE_SIZE);
	}
}


/*
 * This is the PDT kernel thread main loop.
 */

static int pdt_mainloop(void *unused)
{
	struct pdc_mem_read_pdt pdt_read_ret;
	struct pdc_pat_mem_read_pd_retinfo pat_pret __maybe_unused;
	unsigned long old_num_entries;
	unsigned long *bad_mem_ptr;
	int num, ret;

	for (;;) {
		set_current_state(TASK_INTERRUPTIBLE);

		old_num_entries = pdt_status.pdt_entries;

		schedule_timeout(pdt_poll_interval);
		if (kthread_should_stop())
			break;

		/* Do we have new PDT entries? */
		switch (pdt_type) {
		case PDT_PAT_NEW:
			ret = get_info_pat_new();
			break;
		case PDT_PAT_CELL:
			ret = get_info_pat_cell();
			break;
		default:
			ret = pdc_mem_pdt_info(&pdt_status);
			break;
		}

		if (ret != PDC_OK) {
			pr_warn("PDT: unexpected failure %d\n", ret);
			return -EINVAL;
		}

		/* if no new PDT entries, just wait again */
		num = pdt_status.pdt_entries - old_num_entries;
		if (num <= 0)
			continue;

		/* decrease poll interval in case we found memory errors */
		if (pdt_status.pdt_entries &&
			pdt_poll_interval == PDT_POLL_INTERVAL_DEFAULT)
			pdt_poll_interval = PDT_POLL_INTERVAL_SHORT;

		/* limit entries to get */
		if (num > MAX_PDT_ENTRIES) {
			num = MAX_PDT_ENTRIES;
			pdt_status.pdt_entries = old_num_entries + num;
		}

		/* get new entries */
		switch (pdt_type) {
#ifdef CONFIG_64BIT
		case PDT_PAT_CELL:
			if (pdt_status.pdt_entries > MAX_PDT_ENTRIES) {
				pr_crit("PDT: too many entries.\n");
				return -ENOMEM;
			}
			ret = pdc_pat_mem_read_cell_pdt(&pat_pret, pdt_entry,
				MAX_PDT_ENTRIES);
			bad_mem_ptr = &pdt_entry[old_num_entries];
			break;
		case PDT_PAT_NEW:
			ret = pdc_pat_mem_read_pd_pdt(&pat_pret,
				pdt_entry,
				num * sizeof(unsigned long),
				old_num_entries * sizeof(unsigned long));
			bad_mem_ptr = &pdt_entry[0];
			break;
#endif
		default:
			ret = pdc_mem_pdt_read_entries(&pdt_read_ret,
				pdt_entry);
			bad_mem_ptr = &pdt_entry[old_num_entries];
			break;
		}

		/* report and mark memory broken */
		while (num--) {
			unsigned long pde = *bad_mem_ptr++;

			report_mem_err(pde);

#ifdef CONFIG_MEMORY_FAILURE
			if ((pde & PDT_ADDR_PERM_ERR) ||
			    ((pde & PDT_ADDR_SINGLE_ERR) == 0))
				memory_failure(pde >> PAGE_SHIFT, 0, 0);
			else
				soft_offline_page(
					pfn_to_page(pde >> PAGE_SHIFT), 0);
#else
			pr_crit("PDT: memory error at 0x%lx ignored.\n"
				"Rebuild kernel with CONFIG_MEMORY_FAILURE=y "
				"for real handling.\n",
				pde & PDT_ADDR_PHYS_MASK);
#endif

		}
	}

	return 0;
}


static int __init pdt_initcall(void)
{
	struct task_struct *kpdtd_task;

	if (pdt_type == PDT_NONE)
		return -ENODEV;

	kpdtd_task = kthread_create(pdt_mainloop, NULL, "kpdtd");
	if (IS_ERR(kpdtd_task))
		return PTR_ERR(kpdtd_task);

	wake_up_process(kpdtd_task);

	return 0;
}

late_initcall(pdt_initcall);
