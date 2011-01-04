/*
 * arch/sh/mm/tlb-debugfs.c
 *
 * debugfs ops for SH-4 ITLB/UTLBs.
 *
 * Copyright (C) 2010  Matt Fleming
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 */
#include <linux/init.h>
#include <linux/module.h>
#include <linux/debugfs.h>
#include <linux/seq_file.h>
#include <asm/processor.h>
#include <asm/mmu_context.h>
#include <asm/tlbflush.h>

enum tlb_type {
	TLB_TYPE_ITLB,
	TLB_TYPE_UTLB,
};

static struct {
	int bits;
	const char *size;
} tlb_sizes[] = {
	{ 0x0, "  1KB" },
	{ 0x1, "  4KB" },
	{ 0x2, "  8KB" },
	{ 0x4, " 64KB" },
	{ 0x5, "256KB" },
	{ 0x7, "  1MB" },
	{ 0x8, "  4MB" },
	{ 0xc, " 64MB" },
};

static int tlb_seq_show(struct seq_file *file, void *iter)
{
	unsigned int tlb_type = (unsigned int)file->private;
	unsigned long addr1, addr2, data1, data2;
	unsigned long flags;
	unsigned long mmucr;
	unsigned int nentries, entry;
	unsigned int urb;

	mmucr = __raw_readl(MMUCR);
	if ((mmucr & 0x1) == 0) {
		seq_printf(file, "address translation disabled\n");
		return 0;
	}

	if (tlb_type == TLB_TYPE_ITLB) {
		addr1 = MMU_ITLB_ADDRESS_ARRAY;
		addr2 = MMU_ITLB_ADDRESS_ARRAY2;
		data1 = MMU_ITLB_DATA_ARRAY;
		data2 = MMU_ITLB_DATA_ARRAY2;
		nentries = 4;
	} else {
		addr1 = MMU_UTLB_ADDRESS_ARRAY;
		addr2 = MMU_UTLB_ADDRESS_ARRAY2;
		data1 = MMU_UTLB_DATA_ARRAY;
		data2 = MMU_UTLB_DATA_ARRAY2;
		nentries = 64;
	}

	local_irq_save(flags);
	jump_to_uncached();

	urb = (mmucr & MMUCR_URB) >> MMUCR_URB_SHIFT;

	/* Make the "entry >= urb" test fail. */
	if (urb == 0)
		urb = MMUCR_URB_NENTRIES + 1;

	if (tlb_type == TLB_TYPE_ITLB) {
		addr1 = MMU_ITLB_ADDRESS_ARRAY;
		addr2 = MMU_ITLB_ADDRESS_ARRAY2;
		data1 = MMU_ITLB_DATA_ARRAY;
		data2 = MMU_ITLB_DATA_ARRAY2;
		nentries = 4;
	} else {
		addr1 = MMU_UTLB_ADDRESS_ARRAY;
		addr2 = MMU_UTLB_ADDRESS_ARRAY2;
		data1 = MMU_UTLB_DATA_ARRAY;
		data2 = MMU_UTLB_DATA_ARRAY2;
		nentries = 64;
	}

	seq_printf(file, "entry:     vpn        ppn     asid  size valid wired\n");

	for (entry = 0; entry < nentries; entry++) {
		unsigned long vpn, ppn, asid, size;
		unsigned long valid;
		unsigned long val;
		const char *sz = "    ?";
		int i;

		val = __raw_readl(addr1 | (entry << MMU_TLB_ENTRY_SHIFT));
		ctrl_barrier();
		vpn = val & 0xfffffc00;
		valid = val & 0x100;

		val = __raw_readl(addr2 | (entry << MMU_TLB_ENTRY_SHIFT));
		ctrl_barrier();
		asid = val & MMU_CONTEXT_ASID_MASK;

		val = __raw_readl(data1 | (entry << MMU_TLB_ENTRY_SHIFT));
		ctrl_barrier();
		ppn = (val & 0x0ffffc00) << 4;

		val = __raw_readl(data2 | (entry << MMU_TLB_ENTRY_SHIFT));
		ctrl_barrier();
		size = (val & 0xf0) >> 4;

		for (i = 0; i < ARRAY_SIZE(tlb_sizes); i++) {
			if (tlb_sizes[i].bits == size)
				break;
		}

		if (i != ARRAY_SIZE(tlb_sizes))
			sz = tlb_sizes[i].size;

		seq_printf(file, "%2d:    0x%08lx 0x%08lx %5lu %s   %s     %s\n",
			   entry, vpn, ppn, asid,
			   sz, valid ? "V" : "-",
			   (urb <= entry) ? "W" : "-");
	}

	back_to_cached();
	local_irq_restore(flags);

	return 0;
}

static int tlb_debugfs_open(struct inode *inode, struct file *file)
{
	return single_open(file, tlb_seq_show, inode->i_private);
}

static const struct file_operations tlb_debugfs_fops = {
	.owner		= THIS_MODULE,
	.open		= tlb_debugfs_open,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= single_release,
};

static int __init tlb_debugfs_init(void)
{
	struct dentry *itlb, *utlb;

	itlb = debugfs_create_file("itlb", S_IRUSR, arch_debugfs_dir,
				   (unsigned int *)TLB_TYPE_ITLB,
				   &tlb_debugfs_fops);
	if (unlikely(!itlb))
		return -ENOMEM;

	utlb = debugfs_create_file("utlb", S_IRUSR, arch_debugfs_dir,
				   (unsigned int *)TLB_TYPE_UTLB,
				   &tlb_debugfs_fops);
	if (unlikely(!utlb)) {
		debugfs_remove(itlb);
		return -ENOMEM;
	}

	return 0;
}
module_init(tlb_debugfs_init);

MODULE_LICENSE("GPL v2");
