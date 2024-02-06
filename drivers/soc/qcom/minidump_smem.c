// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2017-2021, The Linux Foundation. All rights reserved.
 * Copyright (c) 2022-2024 Qualcomm Innovation Center, Inc. All rights reserved.
 */

#include <linux/init.h>
#include <linux/export.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/err.h>
#include <linux/elf.h>
#include <linux/errno.h>
#include <linux/string.h>
#include <linux/slab.h>
#include <linux/soc/qcom/smem.h>
#include <soc/qcom/minidump.h>
#include "debug_symbol.h"
#include "minidump_private.h"
#include "elf.h"

/**
 * md_table : Local Minidump toc holder
 * @num_regions : Number of regions requested
 * @md_ss_toc  : HLOS toc pointer
 * @md_gbl_toc : Global toc pointer
 * @md_regions : HLOS regions base pointer
 * @entry : array of HLOS regions requested
 */
struct md_table {
	u32			revision;
	struct md_ss_toc	*md_ss_toc;
	struct md_global_toc	*md_gbl_toc;
	struct md_ss_region	*md_regions;
	struct md_region	entry[MAX_NUM_ENTRIES];
};

/* Protect elfheader and smem table from deferred calls contention */
static DEFINE_RWLOCK(mdt_remove_lock);
static struct md_table		minidump_table;
static struct md_global_toc *md_global_toc;
static struct md_ss_toc *md_ss_toc;
static int first_removed_entry = INT_MAX;

static inline int md_region_num(const char *name, int *seqno)
{
	struct md_ss_region *mde = minidump_table.md_regions;
	int i, regno = minidump_table.md_ss_toc->ss_region_count;
	int ret = -EINVAL;

	for (i = 0; i < regno; i++, mde++) {
		if (!strcmp(mde->name, name)) {
			ret = i;
			if (mde->seq_num > *seqno)
				*seqno = mde->seq_num;
		}
	}

	return ret;
}

static inline int md_entry_num(const struct md_region *entry)
{
	struct md_region *mdr;
	int i, regno = md_num_regions;

	for (i = 0; i < regno; i++) {
		mdr = &minidump_table.entry[i];
		if (!strcmp(mdr->name, entry->name))
			return i;
	}

	return -ENOENT;
}

/* Update Mini dump table in SMEM */
static void md_add_ss_toc(const struct md_region *entry, bool pending)
{
	struct md_ss_region *ss_mdr;
	struct md_region *mdr;
	int seq = 0, reg_cnt = minidump_table.md_ss_toc->ss_region_count;

	if (!pending) {
		mdr = &minidump_table.entry[md_num_regions];
		strscpy(mdr->name, entry->name, sizeof(mdr->name));
		mdr->virt_addr = entry->virt_addr;
		mdr->phys_addr = entry->phys_addr;
		mdr->size = entry->size;
		mdr->id = entry->id;
	}

	ss_mdr = &minidump_table.md_regions[reg_cnt];
	strscpy(ss_mdr->name, entry->name, sizeof(ss_mdr->name));
	ss_mdr->region_base_address = entry->phys_addr;
	ss_mdr->region_size = entry->size;
	if (md_region_num(entry->name, &seq) >= 0)
		ss_mdr->seq_num = seq + 1;

	ss_mdr->md_valid = MD_REGION_VALID;
	minidump_table.md_ss_toc->ss_region_count++;
}

static int md_update_ss_toc(int regno, const struct md_region *entry)
{
	int ret = 0;
	struct md_region *mdr;
	struct md_ss_region *mdssr;

	if (regno >= first_removed_entry) {
		printk_deferred("Region:[%s] was moved\n", entry->name);
		return -EINVAL;
	}

	ret = md_entry_num(entry);
	if (ret < 0) {
		printk_deferred("Region:[%s] does not exist to update\n",
				entry->name);
		return ret;
	}

	mdr = &minidump_table.entry[regno];
	mdr->virt_addr = entry->virt_addr;
	mdr->phys_addr = entry->phys_addr;

	mdssr = &minidump_table.md_regions[regno + 1];
	mdssr->region_base_address = entry->phys_addr;

	return 0;
}

static int md_remove_ss_toc(const struct md_region *entry)
{
	int ecount, rcount, entryno, rgno, seq = 0, ret;

	if (!minidump_table.md_ss_toc ||
	    (minidump_table.md_ss_toc->md_ss_enable_status != MD_SS_ENABLED))
		return -EINVAL;

	entryno = md_entry_num(entry);
	if (entryno < 0) {
		printk_deferred("Not able to find the entry %s in table\n",
				entry->name);
		return entryno;
	}
	ecount = md_num_regions;
	rgno = md_region_num(entry->name, &seq);
	if (rgno < 0) {
		printk_deferred(
			"Not able to find the region %s (%d,%d) in table\n",
			entry->name, entryno, rgno);
		return -EINVAL;
	}
	rcount = minidump_table.md_ss_toc->ss_region_count;
	if (first_removed_entry > entryno)
		first_removed_entry = entryno;
	minidump_table.md_ss_toc->md_ss_toc_init = 0;
	/* Remove entry from: entry list, ss region list and elf header */
	memmove(&minidump_table.entry[entryno],
		&minidump_table.entry[entryno + 1],
		((ecount - entryno - 1) * sizeof(struct md_region)));
	memset(&minidump_table.entry[ecount - 1], 0, sizeof(struct md_region));

	memmove(&minidump_table.md_regions[rgno],
		&minidump_table.md_regions[rgno + 1],
		((rcount - rgno - 1) * sizeof(struct md_ss_region)));
	memset(&minidump_table.md_regions[rcount - 1], 0,
	       sizeof(struct md_ss_region));

	ret = msm_minidump_clear_headers(entry);
	if (ret)
		return ret;

	minidump_table.md_ss_toc->ss_region_count--;
	minidump_table.md_ss_toc->md_ss_toc_init = 1;
	md_num_regions--;

	return 0;
}

static int md_smem_init_md_table(void)
{
	size_t size;
	int ret = 0;

	/* Get Minidump table */
	md_global_toc = qcom_smem_get(QCOM_SMEM_HOST_ANY,
					  SBL_MINIDUMP_SMEM_ID, &size);
	if (IS_ERR_OR_NULL(md_global_toc)) {
		pr_err("SMEM is not initialized\n");
		return PTR_ERR(md_global_toc);
	}

	/*Check global minidump support initialization */
	if (size < sizeof(*md_global_toc) || !md_global_toc->md_toc_init) {
		pr_err("System Minidump TOC not initialized\n");
		return -ENODEV;
	}

	minidump_table.md_gbl_toc = md_global_toc;
	minidump_table.revision = md_global_toc->md_revision;
	md_ss_toc = &md_global_toc->md_ss_toc[MD_SS_HLOS_ID];

	md_ss_toc->encryption_status = MD_SS_ENCR_DONE;
	md_ss_toc->encryption_required = MD_SS_ENCR_NOTREQ;

	minidump_table.md_ss_toc = md_ss_toc;
	minidump_table.md_regions = kzalloc((MAX_NUM_ENTRIES *
				sizeof(struct md_ss_region)), GFP_KERNEL);
	if (!minidump_table.md_regions)
		return -ENOMEM;

	md_ss_toc->md_ss_smem_regions_baseptr =
		virt_to_phys(minidump_table.md_regions);

	md_ss_toc->ss_region_count = 1;

	return ret;
}

static int md_smem_add_pending_entry(struct list_head *pending_list)
{
	unsigned int region_number;
	struct md_pending_region *pending_region, *tmp;
	unsigned long flags;

	/* Add pending entries to HLOS TOC */
	spin_lock_irqsave(&mdt_lock, flags);

	md_ss_toc->md_ss_toc_init = 1;
	md_ss_toc->md_ss_enable_status = MD_SS_ENABLED;

	region_number = 0;
	list_for_each_entry_safe(pending_region, tmp, pending_list, list) {
		/* Add pending entry to minidump table and ss toc */
		minidump_table.entry[region_number] =
			pending_region->entry;
		md_add_ss_toc(&minidump_table.entry[region_number], true);
		md_add_elf_header(&minidump_table.entry[region_number]);
		list_del(&pending_region->list);
		kfree(pending_region);
		region_number++;
	}
	spin_unlock_irqrestore(&mdt_lock, flags);

	return 0;
}

static void md_smem_add_header(struct elf_shdr *shdr, struct elfhdr *ehdr, unsigned int elfh_size)
{
	struct md_ss_region *mdreg;

	mdreg = &minidump_table.md_regions[0];
	strscpy(mdreg->name, "KELF_HDR", sizeof(mdreg->name));
	mdreg->region_base_address = virt_to_phys(minidump_elfheader.ehdr);
	mdreg->region_size = elfh_size;

	/* 3rd section is for minidump_table VA, used by parsers */
	shdr->sh_type = SHT_PROGBITS;
	shdr->sh_entsize = 0;
	shdr->sh_flags = 0;
	shdr->sh_addr = (elf_addr_t)(&minidump_table);
	shdr->sh_name = set_section_name("minidump_table");
	shdr++;

	/* Update headers count*/
	ehdr->e_shnum = 4;

	mdreg->md_valid = MD_REGION_VALID;
}

static int md_smem_remove_region(const struct md_region *entry)
{
	int ret;
	unsigned long flags;

	if (!minidump_table.md_ss_toc ||
	     (minidump_table.md_ss_toc->md_ss_enable_status != MD_SS_ENABLED))
		return -EINVAL;

	spin_lock_irqsave(&mdt_lock, flags);
	write_lock(&mdt_remove_lock);

	ret = md_remove_ss_toc(entry);

	write_unlock(&mdt_remove_lock);
	spin_unlock_irqrestore(&mdt_lock, flags);

	return ret;
}

static int md_smem_add_region(const struct md_region *entry)
{
	u32 toc_init;
	int ret = 0;
	unsigned long flags;

	spin_lock_irqsave(&mdt_lock, flags);

	if (md_num_regions >= MAX_NUM_ENTRIES) {
		printk_deferred("Maximum entries reached\n");
		ret = -ENOMEM;
		goto out;
	}

	toc_init = 0;
	if (minidump_table.md_ss_toc &&
	    (minidump_table.md_ss_toc->md_ss_enable_status == MD_SS_ENABLED)) {
		toc_init = 1;
		if (minidump_table.md_ss_toc->ss_region_count >= MAX_NUM_ENTRIES) {
			printk_deferred("Maximum regions in minidump table reached\n");
			ret = -ENOMEM;
			goto out;
		}
	}

	if (toc_init) {
		if (md_entry_num(entry) >= 0) {
			printk_deferred("Entry name already exist\n");
			ret = -EEXIST;
			goto out;
		}
		md_add_ss_toc(entry, false);
		md_add_elf_header(entry);
	}
	ret = md_num_regions;
	md_num_regions++;

out:
	spin_unlock_irqrestore(&mdt_lock, flags);

	return ret;
}

static void md_smem_update_elf_header(int entryno, const struct md_region *entry)
{
	struct elfhdr *hdr = minidump_elfheader.ehdr;
	struct elf_shdr *shdr;
	struct elf_phdr *phdr;

	shdr = elf_section(hdr, entryno + 4);
	phdr = elf_program(hdr, entryno + 1);
	shdr->sh_addr = (elf_addr_t)entry->virt_addr;
	phdr->p_vaddr = entry->virt_addr;
	phdr->p_paddr = entry->phys_addr;
}

static int md_smem_update_region(int regno, const struct md_region *entry)
{
	int ret = 0;
	unsigned long flags;

	read_lock_irqsave(&mdt_remove_lock, flags);

	ret = md_update_ss_toc(regno, entry);
	md_smem_update_elf_header(regno, entry);

	read_unlock_irqrestore(&mdt_remove_lock, flags);

	return ret;
}

static int md_smem_get_available_region(void)
{
	int res = -EBUSY;
	unsigned long flags;

	spin_lock_irqsave(&mdt_lock, flags);
	res = MAX_NUM_ENTRIES - md_num_regions;
	spin_unlock_irqrestore(&mdt_lock, flags);

	return res;
}

static bool md_smem_md_enable(void)
{
	bool ret = false;
	unsigned long flags;

	spin_lock_irqsave(&mdt_lock, flags);
	if (minidump_table.md_ss_toc &&
		(minidump_table.md_ss_toc->md_ss_enable_status ==
		 MD_SS_ENABLED))
		ret = true;
	spin_unlock_irqrestore(&mdt_lock, flags);

	return ret;
}

static struct md_region md_smem_get_region(char *name)
{
	struct md_region *mdr = NULL, tmp = {0};
	int i, regno;

	regno = md_num_regions;
	for (i = 0; i < regno; i++) {
		mdr = &minidump_table.entry[i];
		if (!strcmp(mdr->name, name)) {
			tmp = *mdr;
			goto out;
		}
	}

out:
	return tmp;
}

static const struct md_ops md_smem_ops = {
	.init_md_table			= md_smem_init_md_table,
	.add_pending_entry		= md_smem_add_pending_entry,
	.add_header				= md_smem_add_header,
	.remove_region			= md_smem_remove_region,
	.add_region				= md_smem_add_region,
	.update_region			= md_smem_update_region,
	.get_available_region	= md_smem_get_available_region,
	.md_enable				= md_smem_md_enable,
	.get_region				= md_smem_get_region,
};

static struct md_init_data md_smem_init_data = {
	.ops = &md_smem_ops,
};

static int minidump_smem_driver_probe(struct platform_device *pdev)
{
	return msm_minidump_driver_probe(&md_smem_init_data);
}

static const struct of_device_id msm_minidump_smem_of_match[] = {
	{ .compatible = "qcom,minidump" },
	{ }
};
MODULE_DEVICE_TABLE(of, msm_minidump_smem_of_match);

static struct platform_driver msm_minidump_smem_driver = {
	.driver = {
		.name = "qcom-minidump",
		.of_match_table = msm_minidump_smem_of_match,
	},
	.probe = minidump_smem_driver_probe,
};

static int __init minidump_smem_init(void)
{
	return platform_driver_register(&msm_minidump_smem_driver);
}

subsys_initcall(minidump_smem_init);

static void __exit minidump_smem_exit(void)
{
	platform_driver_unregister(&msm_minidump_smem_driver);
}
module_exit(minidump_smem_exit);

MODULE_LICENSE("GPL");
