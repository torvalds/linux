// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2017-2021, The Linux Foundation. All rights reserved.
 * Copyright (c) 2022 Qualcomm Innovation Center, Inc. All rights reserved.
 */

#define pr_fmt(fmt) "Minidump: " fmt

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
#include <linux/android_debug_symbols.h>
#include <linux/gunyah/gh_rm_drv.h>
#include <linux/soc/qcom/smem.h>
#include <soc/qcom/minidump.h>
#include "minidump_private.h"
#include "elf.h"

#define MAX_NUM_ENTRIES         (CONFIG_MINIDUMP_MAX_ENTRIES + 1)
#define MAX_STRTBL_SIZE		(MAX_NUM_ENTRIES * MAX_REGION_NAME_LENGTH)

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

struct md_rm_table {
	struct workqueue_struct *minidump_rm_wq;
	struct md_rm_region entry[MAX_NUM_ENTRIES];
};

/**
 * md_elfhdr: Minidump table elf header
 * @ehdr: elf main header
 * @shdr: Section header
 * @phdr: Program header
 * @elf_offset: section offset in elf
 * @strtable_idx: string table current index position
 */
struct md_elfhdr {
	struct elfhdr		*ehdr;
	struct elf_shdr		*shdr;
	struct elf_phdr		*phdr;
	u64			elf_offset;
	u64			strtable_idx;
};

/* Protect elfheader and smem table from deferred calls contention */
static DEFINE_SPINLOCK(mdt_lock);
static DEFINE_RWLOCK(mdt_remove_lock);
static struct md_table		*minidump_table;
static struct md_rm_table	*minidump_rm_table;
static struct md_elfhdr		minidump_elfheader;
static int first_removed_entry = INT_MAX;
static bool md_init_done;
static unsigned int num_regions;
static bool is_rm_minidump;

/* Number of pending entries to be added in ToC regions */
static LIST_HEAD(pending_list);

static inline char *elf_lookup_string(struct elfhdr *hdr, int offset)
{
	char *strtab = elf_str_table(hdr);

	if ((strtab == NULL) || (minidump_elfheader.strtable_idx < offset))
		return NULL;
	return strtab + offset;
}

static inline unsigned int set_section_name(const char *name)
{
	char *strtab = elf_str_table(minidump_elfheader.ehdr);
	int idx = minidump_elfheader.strtable_idx;
	int ret = 0;

	if ((strtab == NULL) || (name == NULL))
		return 0;

	ret = idx;
	idx += strscpy((strtab + idx), name, MAX_REGION_NAME_LENGTH);
	minidump_elfheader.strtable_idx = idx + 1;

	return ret;
}

struct md_region md_get_region(char *name)
{
	struct md_region *mdr = NULL, tmp = {0};
	struct md_rm_region *rm_region;
	int i, regno;
	struct elfhdr *hdr = minidump_elfheader.ehdr;
	struct elf_phdr *phdr;


	if (is_rm_minidump) {
		if (!minidump_rm_table)
			goto out;
		regno = num_regions;
		for (i = 0; i < regno; i++) {
			rm_region = &minidump_rm_table->entry[i];
			if (!strcmp(rm_region->name, name)) {
				strscpy(tmp.name, rm_region->name, sizeof(tmp.name));
				phdr = elf_program(hdr, i + 1);
				tmp.phys_addr = phdr->p_vaddr;
				tmp.virt_addr = phdr->p_paddr;
				tmp.size = phdr->p_filesz;
				goto out;
			}
		}
	} else {
		if (!minidump_table)
			goto out;
		regno = num_regions;
		for (i = 0; i < regno; i++) {
			mdr = &minidump_table->entry[i];
			if (!strcmp(mdr->name, name)) {
				tmp = *mdr;
				goto out;
			}
		}
	}
out:
	return tmp;
}

static inline int md_region_num(const char *name, int *seqno)
{
	struct md_ss_region *mde = minidump_table->md_regions;
	int i, regno = minidump_table->md_ss_toc->ss_region_count;
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
	int i, regno = num_regions;

	for (i = 0; i < regno; i++) {
		mdr = &minidump_table->entry[i];
		if (!strcmp(mdr->name, entry->name))
			return i;
	}
	return -ENOENT;
}

static inline int md_rm_entry_num(const struct md_region *entry)
{
	struct md_rm_region *mdr;
	int i, regno = num_regions;

	for (i = 0; i < regno; i++) {
		mdr = &minidump_rm_table->entry[i];
		if (!strcmp(mdr->name, entry->name))
			return i;
	}
	return -ENOENT;
}

/* Update elf header */
static void md_add_elf_header(const struct md_region *entry)
{
	struct elfhdr *hdr = minidump_elfheader.ehdr;
	struct elf_shdr *shdr = elf_section(hdr, hdr->e_shnum++);
	struct elf_phdr *phdr = elf_program(hdr, hdr->e_phnum++);

	shdr->sh_type = SHT_PROGBITS;
	shdr->sh_name = set_section_name(entry->name);
	shdr->sh_addr = (elf_addr_t)entry->virt_addr;
	shdr->sh_size = entry->size;
	shdr->sh_flags = SHF_WRITE;
	shdr->sh_offset = minidump_elfheader.elf_offset;
	shdr->sh_entsize = 0;

	phdr->p_type = PT_LOAD;
	phdr->p_offset = minidump_elfheader.elf_offset;
	phdr->p_vaddr = entry->virt_addr;
	phdr->p_paddr = entry->phys_addr;
	phdr->p_filesz = phdr->p_memsz = entry->size;
	phdr->p_flags = PF_R | PF_W;
	minidump_elfheader.elf_offset += shdr->sh_size;
}

static void md_update_elf_header(int entryno, const struct md_region *entry)
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

int msm_minidump_clear_headers(const struct md_region *entry)
{
	struct elfhdr *hdr = minidump_elfheader.ehdr;
	struct elf_shdr *shdr = NULL, *tshdr = NULL;
	struct elf_phdr *phdr = NULL, *tphdr = NULL;
	int pidx, shidx, strln, i;
	char *shname;
	u64 esize;

	esize = entry->size;
	for (i = 0; i < hdr->e_phnum; i++) {
		phdr = elf_program(hdr, i);
		if ((phdr->p_paddr == entry->phys_addr) &&
			(phdr->p_memsz == entry->size))
			break;

	}
	if (i == hdr->e_phnum) {
		printk_deferred("Cannot find entry in elf\n");
		return -EINVAL;
	}
	pidx = i;

	for (i = 0; i < hdr->e_shnum; i++) {
		shdr = elf_section(hdr, i);
		shname = elf_lookup_string(hdr, shdr->sh_name);
		if (shname && !strcmp(shname, entry->name))
			if ((shdr->sh_addr == entry->virt_addr) &&
				(shdr->sh_size == entry->size))
				break;

	}
	if (i == hdr->e_shnum) {
		printk_deferred("Cannot find entry in elf\n");
		return -EINVAL;
	}
	shidx = i;

	if (shdr->sh_offset != phdr->p_offset) {
		printk_deferred("Invalid entry details in elf, Minidump broken..\n");
		return -EINVAL;
	}

	/* Clear name in string table */
	strln = strlen(shname) + 1;
	memmove(shname, shname + strln,
		(minidump_elfheader.strtable_idx - shdr->sh_name));
	minidump_elfheader.strtable_idx -= strln;

	/* Clear program header */
	tphdr = elf_program(hdr, pidx);
	for (i = pidx; i < hdr->e_phnum - 1; i++) {
		tphdr = elf_program(hdr, i + 1);
		phdr = elf_program(hdr, i);
		memcpy(phdr, tphdr, sizeof(struct elf_phdr));
		phdr->p_offset = phdr->p_offset - esize;
	}
	memset(tphdr, 0, sizeof(struct elf_phdr));
	hdr->e_phnum--;

	/* Clear section header */
	tshdr = elf_section(hdr, shidx);
	for (i = shidx; i < hdr->e_shnum - 1; i++) {
		tshdr = elf_section(hdr, i + 1);
		shdr = elf_section(hdr, i);
		memcpy(shdr, tshdr, sizeof(struct elf_shdr));
		shdr->sh_offset -= esize;
		shdr->sh_name -= strln;
	}
	memset(tshdr, 0, sizeof(struct elf_shdr));
	hdr->e_shnum--;

	minidump_elfheader.elf_offset -= esize;
	return 0;
}

static void md_rm_update_work(struct md_region *entry)
{
	int slot_num, entryno, ret;
	struct md_rm_region *mdr;
	unsigned long flags;

	spin_lock_irqsave(&mdt_lock, flags);
	/* clear slot number to avoid remove during update */
	entryno = md_rm_entry_num(entry);
	if (entryno < 0) {
		printk_deferred("Not able to find the entry %s in table\n",
				entry->name);
		spin_unlock_irqrestore(&mdt_lock, flags);
		return;
	}
	mdr = &minidump_rm_table->entry[entryno];
	slot_num = mdr->slot_num;
	mdr->slot_num = 0;
	spin_unlock_irqrestore(&mdt_lock, flags);

	ret = gh_rm_minidump_deregister_slot(slot_num);
	if (ret < 0) {
		printk_deferred(
			"Update region:[%s] failed:%d unable to deregister\n",
			entry->name, ret);
		return;
	}
	slot_num =
		gh_rm_minidump_register_range(entry->phys_addr, entry->size,
					      entry->name, strlen(entry->name));
	if (slot_num < 0) {
		printk_deferred(
			"Update region:[%s] failed:%d unable to register\n",
			entry->name, ret);
		return;
	}

	spin_lock_irqsave(&mdt_lock, flags);
	if (strcmp(entry->name, mdr->name)) {
		printk_deferred(
			"Update entry:%s failed, minidump table is corrupt\n",
			entry->name);
	} else {
		mdr->slot_num = slot_num;
		md_update_elf_header(entryno, entry);
	}
	spin_unlock_irqrestore(&mdt_lock, flags);
}

static void md_rm_add_work(struct md_region *entry)
{
	int slot_num, entry_num;
	unsigned long flags;

	spin_lock_irqsave(&mdt_lock, flags);
	entry_num = md_rm_entry_num(entry);
	if (entry_num < 0) {
		printk_deferred(
			"Failed to find entry %s in table, table is broken\n",
			entry->name);
		goto out;
	}
	spin_unlock_irqrestore(&mdt_lock, flags);

	slot_num =
		gh_rm_minidump_register_range(entry->phys_addr, entry->size,
					      entry->name, strlen(entry->name));
	spin_lock_irqsave(&mdt_lock, flags);
	if (slot_num < 0) {
		memmove(&minidump_rm_table->entry[entry_num],
			&minidump_rm_table->entry[entry_num + 1],
			((num_regions - entry_num - 1) *
			 sizeof(struct md_rm_region)));
		memset(&minidump_rm_table->entry[num_regions - 1], 0,
		       sizeof(struct md_rm_region));
		num_regions--;
		pr_err("Failed to register minidump entry:%s ret:%d\n",
		       entry->name, slot_num);
		goto out;
	}
	if (strcmp(entry->name, minidump_rm_table->entry[entry_num].name)) {
		printk_deferred(
			"Add entry:%s failed, minidump table is corrupt\n",
			entry->name);
		spin_unlock_irqrestore(&mdt_lock, flags);
		gh_rm_minidump_deregister_slot(slot_num);
	} else {
		minidump_rm_table->entry[entry_num].slot_num = slot_num;
	}
	md_add_elf_header(entry);

out:
	spin_unlock_irqrestore(&mdt_lock, flags);
}

static void md_rm_remove_work(struct md_region *entry)
{
	int entryno, ret, slot_num;
	struct md_rm_region *mdr;
	unsigned long flags;

	spin_lock_irqsave(&mdt_lock, flags);
	ret = md_rm_entry_num(entry);
	if (ret < 0) {
		printk_deferred("Not able to find the entry %s in table\n",
				entry->name);
		goto out;
	}
	entryno = ret;
	mdr = &minidump_rm_table->entry[entryno];
	/* to avoid double remove */
	slot_num = mdr->slot_num;
	if (slot_num == 0) {
		printk_deferred("Minidump entry:%s not registered\n",
				entry->name);
		goto out;
	}
	mdr->slot_num = 0;
	spin_unlock_irqrestore(&mdt_lock, flags);

	ret = gh_rm_minidump_deregister_slot(slot_num);
	if (ret < 0) {
		pr_err("Failed to deregister minidump entry:%s ret:%d\n",
		       entry->name, ret);
		return;
	}

	spin_lock_irqsave(&mdt_lock, flags);
	if (strcmp(entry->name, mdr->name)) {
		printk_deferred(
			"Remove entry:%s failed, minidump table is corrupt\n",
			entry->name);
	} else {
		ret = msm_minidump_clear_headers(entry);
		if (ret)
			goto out;
		memmove(&minidump_rm_table->entry[entryno],
			&minidump_rm_table->entry[entryno + 1],
			((num_regions - entryno - 1) *
			 sizeof(struct md_rm_region)));
		memset(&minidump_rm_table->entry[num_regions - 1], 0,
		       sizeof(struct md_rm_region));
		if (ret)
			goto out;
		num_regions--;
	}

out:
	spin_unlock_irqrestore(&mdt_lock, flags);
}

static void minidump_rm_work(struct work_struct *work)
{
	struct md_rm_request *rm_work =
		container_of(work, struct md_rm_request, work);
	enum minidump_entry_cmd cmd;

	cmd = rm_work->work_cmd;

	switch (cmd) {
	case MINIDUMP_ADD:
		md_rm_add_work(&rm_work->entry);
		break;
	case MINIDUMP_REMOVE:
		md_rm_remove_work(&rm_work->entry);
		break;
	case MINIDUMP_UPDATE:
		md_rm_update_work(&rm_work->entry);
		break;
	default:
		printk_deferred("No command for minidump rm work\n");
		break;
	}
	kfree(rm_work);
}

/* Update Mini dump table in SMEM */
static void md_add_ss_toc(const struct md_region *entry)
{
	struct md_ss_region *ss_mdr;
	struct md_region *mdr;
	int seq = 0, reg_cnt = minidump_table->md_ss_toc->ss_region_count;

	mdr = &minidump_table->entry[num_regions];
	strscpy(mdr->name, entry->name, sizeof(mdr->name));
	mdr->virt_addr = entry->virt_addr;
	mdr->phys_addr = entry->phys_addr;
	mdr->size = entry->size;
	mdr->id = entry->id;

	ss_mdr = &minidump_table->md_regions[reg_cnt];
	strscpy(ss_mdr->name, entry->name, sizeof(ss_mdr->name));
	ss_mdr->region_base_address = entry->phys_addr;
	ss_mdr->region_size = entry->size;
	if (md_region_num(entry->name, &seq) >= 0)
		ss_mdr->seq_num = seq + 1;

	ss_mdr->md_valid = MD_REGION_VALID;
	minidump_table->md_ss_toc->ss_region_count++;
}

static int md_rm_add_region(const struct md_region *entry)
{
	struct md_rm_request *rm_work;
	struct md_rm_region *rm_region;

	/* alloc a RM entry for workqueue, need free in work */
	rm_work = kzalloc(sizeof(*rm_work), GFP_ATOMIC);
	if (!rm_work)
		return -ENOMEM;
	rm_region = &minidump_rm_table->entry[num_regions];
	strscpy(rm_region->name, entry->name, sizeof(rm_region->name));
	rm_region->slot_num = 0;
	rm_work->entry = *entry;
	rm_work->work_cmd = MINIDUMP_ADD;
	INIT_WORK(&rm_work->work, minidump_rm_work);
	queue_work(minidump_rm_table->minidump_rm_wq, &rm_work->work);

	return 0;
}

bool msm_minidump_enabled(void)
{
	bool ret = false;
	unsigned long flags;

	if (is_rm_minidump) {
		/* If minidump driver init successfully, minidump is enabled */
		if (smp_load_acquire(&md_init_done))
			ret = true;
	} else {
		spin_lock_irqsave(&mdt_lock, flags);
		if (minidump_table && minidump_table->md_ss_toc &&
		    (minidump_table->md_ss_toc->md_ss_enable_status ==
		     MD_SS_ENABLED))
			ret = true;
		spin_unlock_irqrestore(&mdt_lock, flags);
	}
	return ret;
}
EXPORT_SYMBOL(msm_minidump_enabled);

int msm_minidump_get_available_region(void)
{
	int res = -EBUSY;
	unsigned long flags;

	if (is_rm_minidump) {
		res = gh_rm_minidump_get_info();
		if (res < 0)
			pr_err("Fail to get minidump available region ret=%d\n",
			       res);
	} else {
		spin_lock_irqsave(&mdt_lock, flags);
		res = MAX_NUM_ENTRIES - num_regions;
		spin_unlock_irqrestore(&mdt_lock, flags);
	}

	return res;
}
EXPORT_SYMBOL(msm_minidump_get_available_region);

static inline int validate_region(const struct md_region *entry)
{
	if (!entry)
		return -EINVAL;

	if ((strlen(entry->name) > MAX_NAME_LENGTH) || !entry->virt_addr ||
		(!IS_ALIGNED(entry->size, 4))) {
		printk_deferred("Invalid entry details\n");
		return -EINVAL;
	}

	return 0;
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

	mdr = &minidump_table->entry[regno];
	mdr->virt_addr = entry->virt_addr;
	mdr->phys_addr = entry->phys_addr;

	mdssr = &minidump_table->md_regions[regno + 1];
	mdssr->region_base_address = entry->phys_addr;
	return 0;
}

static int md_rm_update(int regno, const struct md_region *entry)
{
	int entryno;
	struct md_rm_request *rm_work;

	entryno = md_rm_entry_num(entry);
	if (entryno < 0) {
		printk_deferred("Not able to find the entry %s in table\n",
				entry->name);
		return entryno;
	}
	rm_work = kzalloc(sizeof(*rm_work), GFP_ATOMIC);
	if (!rm_work)
		return -ENOMEM;
	rm_work->work_cmd = MINIDUMP_UPDATE;
	rm_work->entry = *entry;
	INIT_WORK(&rm_work->work, minidump_rm_work);
	queue_work(minidump_rm_table->minidump_rm_wq, &rm_work->work);

	return 0;
}

int msm_minidump_update_region(int regno, const struct md_region *entry)
{
	int ret = 0;
	unsigned long flags;

	/* Ensure that init completes before we update regions */
	if (!smp_load_acquire(&md_init_done))
		return -EINVAL;

	if (validate_region(entry) || (regno >= MAX_NUM_ENTRIES))
		return -EINVAL;

	read_lock_irqsave(&mdt_remove_lock, flags);
	if (is_rm_minidump)
		ret = md_rm_update(regno, entry);
	else {
		ret = md_update_ss_toc(regno, entry);
		md_update_elf_header(regno, entry);
	}
	read_unlock_irqrestore(&mdt_remove_lock, flags);

	return ret;
}
EXPORT_SYMBOL(msm_minidump_update_region);

int msm_minidump_add_region(const struct md_region *entry)
{
	u32 toc_init;
	struct md_pending_region *pending_region;
	unsigned long flags;
	int ret;

	if (validate_region(entry))
		return -EINVAL;

	spin_lock_irqsave(&mdt_lock, flags);

	if (num_regions >= MAX_NUM_ENTRIES) {
		printk_deferred("Maximum entries reached\n");
		ret = -ENOMEM;
		goto out;
	}

	toc_init = 0;
	if (!is_rm_minidump && minidump_table && minidump_table->md_ss_toc &&
	    (minidump_table->md_ss_toc->md_ss_enable_status == MD_SS_ENABLED)) {
		toc_init = 1;
		if (minidump_table->md_ss_toc->ss_region_count >= MAX_NUM_ENTRIES) {
			printk_deferred("Maximum regions in minidump table reached\n");
			ret = -ENOMEM;
			goto out;
		}
	}

	if (!is_rm_minidump && toc_init) {
		if (md_entry_num(entry) >= 0) {
			printk_deferred("Entry name already exist\n");
			ret = -EEXIST;
			goto out;
		}
		md_add_ss_toc(entry);
		md_add_elf_header(entry);
		/* Ensure that init completes before register region */
	} else if (is_rm_minidump && smp_load_acquire(&md_init_done)) {
		if (md_rm_entry_num(entry) >= 0) {
			printk_deferred("Entry name already exist\n");
			ret = -EEXIST;
			goto out;
		}
		ret = md_rm_add_region(entry);
		if (ret)
			goto out;
	} else {
		/* Local table not initialized
		 * add to pending list, need free after initialized
		 */
		pending_region = kzalloc(sizeof(*pending_region), GFP_ATOMIC);
		if (!pending_region) {
			ret = -ENOMEM;
			goto out;
		}
		pending_region->entry = *entry;
		list_add_tail(&pending_region->list, &pending_list);
	}
	ret = num_regions;
	num_regions++;

out:
	spin_unlock_irqrestore(&mdt_lock, flags);

	return ret;
}
EXPORT_SYMBOL(msm_minidump_add_region);

static int md_rm_remove_region(const struct md_region *entry)
{
	int entryno;
	struct md_rm_request *rm_work;

	entryno = md_rm_entry_num(entry);
	if (entryno < 0) {
		printk_deferred("Not able to find the entry %s in table\n",
				entry->name);
		return entryno;
	}
	rm_work = kzalloc(sizeof(*rm_work), GFP_ATOMIC);
	if (!rm_work)
		return -ENOMEM;
	rm_work->work_cmd = MINIDUMP_REMOVE;
	rm_work->entry = *entry;
	INIT_WORK(&rm_work->work, minidump_rm_work);
	queue_work(minidump_rm_table->minidump_rm_wq, &rm_work->work);

	return 0;
}

static int md_remove_ss_toc(const struct md_region *entry)
{
	int ecount, rcount, entryno, rgno, seq = 0, ret;

	if (!minidump_table->md_ss_toc ||
	    (minidump_table->md_ss_toc->md_ss_enable_status != MD_SS_ENABLED))
		return -EINVAL;

	entryno = md_entry_num(entry);
	if (entryno < 0) {
		printk_deferred("Not able to find the entry %s in table\n",
				entry->name);
		return entryno;
	}
	ecount = num_regions;
	rgno = md_region_num(entry->name, &seq);
	if (rgno < 0) {
		printk_deferred(
			"Not able to find the region %s (%d,%d) in table\n",
			entry->name, entryno, rgno);
		return -EINVAL;
	}
	rcount = minidump_table->md_ss_toc->ss_region_count;
	if (first_removed_entry > entryno)
		first_removed_entry = entryno;
	minidump_table->md_ss_toc->md_ss_toc_init = 0;
	/* Remove entry from: entry list, ss region list and elf header */
	memmove(&minidump_table->entry[entryno],
		&minidump_table->entry[entryno + 1],
		((ecount - entryno - 1) * sizeof(struct md_region)));
	memset(&minidump_table->entry[ecount - 1], 0, sizeof(struct md_region));

	memmove(&minidump_table->md_regions[rgno],
		&minidump_table->md_regions[rgno + 1],
		((rcount - rgno - 1) * sizeof(struct md_ss_region)));
	memset(&minidump_table->md_regions[rcount - 1], 0,
	       sizeof(struct md_ss_region));

	ret = msm_minidump_clear_headers(entry);
	if (ret)
		return ret;

	minidump_table->md_ss_toc->ss_region_count--;
	minidump_table->md_ss_toc->md_ss_toc_init = 1;
	num_regions--;

	return 0;
}

int msm_minidump_remove_region(const struct md_region *entry)
{
	int ret;
	unsigned long flags;

	if (!entry)
		return -EINVAL;

	/* Ensure that init completes before we remove regions */
	if (!smp_load_acquire(&md_init_done))
		return -EINVAL;

	if (!is_rm_minidump &&
	    (!minidump_table->md_ss_toc ||
	     (minidump_table->md_ss_toc->md_ss_enable_status != MD_SS_ENABLED)))
		return -EINVAL;

	spin_lock_irqsave(&mdt_lock, flags);
	write_lock(&mdt_remove_lock);

	if (is_rm_minidump)
		ret = md_rm_remove_region(entry);
	else
		ret = md_remove_ss_toc(entry);

	write_unlock(&mdt_remove_lock);
	spin_unlock_irqrestore(&mdt_lock, flags);

	if (ret)
		printk_deferred("Minidump is broken..disable Minidump collection\n");
	return ret;
}
EXPORT_SYMBOL(msm_minidump_remove_region);

static int msm_minidump_add_header(void)
{
	struct md_ss_region *mdreg;
	struct elfhdr *ehdr;
	struct elf_shdr *shdr;
	struct elf_phdr *phdr;
	unsigned int strtbl_off, elfh_size, phdr_off;
	char *banner, *linux_banner;
	int slot_num;

	linux_banner = android_debug_symbol(ADS_LINUX_BANNER);
	/* Header buffer contains:
	 * elf header, MAX_NUM_ENTRIES+4 of section and program elf headers,
	 * string table section and linux banner.
	 */
	elfh_size = sizeof(*ehdr) + MAX_STRTBL_SIZE +
			(strlen(linux_banner) + 1) +
			((sizeof(*shdr) + sizeof(*phdr))
			 * (MAX_NUM_ENTRIES + 4));

	elfh_size = ALIGN(elfh_size, 4);

	minidump_elfheader.ehdr = kzalloc(elfh_size, GFP_KERNEL);
	if (!minidump_elfheader.ehdr)
		return -ENOMEM;

	if (is_rm_minidump) {
		slot_num = gh_rm_minidump_register_range(
			virt_to_phys(minidump_elfheader.ehdr), elfh_size,
			"KELF_HDR", strlen("KELF_HDR"));
		if (slot_num < 0) {
			pr_err("Failed to register elf_header minidump entry\n");
			return -EBUSY;
		}
	} else {
		mdreg = &minidump_table->md_regions[0];
		strscpy(mdreg->name, "KELF_HDR", sizeof(mdreg->name));
		mdreg->region_base_address = virt_to_phys(minidump_elfheader.ehdr);
		mdreg->region_size = elfh_size;
	}

	ehdr = minidump_elfheader.ehdr;
	/* Assign section/program headers offset */
	minidump_elfheader.shdr = shdr = (struct elf_shdr *)(ehdr + 1);
	minidump_elfheader.phdr = phdr =
				 (struct elf_phdr *)(shdr + MAX_NUM_ENTRIES);
	phdr_off = sizeof(*ehdr) + (sizeof(*shdr) * MAX_NUM_ENTRIES);

	memcpy(ehdr->e_ident, ELFMAG, SELFMAG);
	ehdr->e_ident[EI_CLASS] = ELF_CLASS;
	ehdr->e_ident[EI_DATA] = ELF_DATA;
	ehdr->e_ident[EI_VERSION] = EV_CURRENT;
	ehdr->e_ident[EI_OSABI] = ELF_OSABI;
	ehdr->e_type = ET_CORE;
	ehdr->e_machine  = ELF_ARCH;
	ehdr->e_version = EV_CURRENT;
	ehdr->e_ehsize = sizeof(*ehdr);
	ehdr->e_phoff = phdr_off;
	ehdr->e_phentsize = sizeof(*phdr);
	ehdr->e_shoff = sizeof(*ehdr);
	ehdr->e_shentsize = sizeof(*shdr);
	ehdr->e_shstrndx = 1;

	minidump_elfheader.elf_offset = elfh_size;

	/*
	 * First section header should be NULL,
	 * 2nd section is string table.
	 */
	minidump_elfheader.strtable_idx = 1;
	strtbl_off = sizeof(*ehdr) +
			((sizeof(*phdr) + sizeof(*shdr)) * MAX_NUM_ENTRIES);
	shdr++;
	shdr->sh_type = SHT_STRTAB;
	shdr->sh_offset = (elf_addr_t)strtbl_off;
	shdr->sh_size = MAX_STRTBL_SIZE;
	shdr->sh_entsize = 0;
	shdr->sh_flags = 0;
	shdr->sh_name = set_section_name("STR_TBL");
	shdr++;

	/* 3rd section is for minidump_table VA, used by parsers */
	shdr->sh_type = SHT_PROGBITS;
	shdr->sh_entsize = 0;
	shdr->sh_flags = 0;
	if (is_rm_minidump)
		shdr->sh_addr = (elf_addr_t)minidump_rm_table;
	else
		shdr->sh_addr = (elf_addr_t)minidump_table;
	shdr->sh_name = set_section_name("minidump_table");
	shdr++;

	/* 4th section is linux banner */
	banner = (char *)ehdr + strtbl_off + MAX_STRTBL_SIZE;
	strscpy(banner, linux_banner, MAX_STRTBL_SIZE);

	shdr->sh_type = SHT_PROGBITS;
	shdr->sh_offset = (elf_addr_t)(strtbl_off + MAX_STRTBL_SIZE);
	shdr->sh_size = strlen(linux_banner) + 1;
	shdr->sh_addr = (elf_addr_t)linux_banner;
	shdr->sh_entsize = 0;
	shdr->sh_flags = SHF_WRITE;
	shdr->sh_name = set_section_name("linux_banner");

	phdr->p_type = PT_LOAD;
	phdr->p_offset = (elf_addr_t)(strtbl_off + MAX_STRTBL_SIZE);
	phdr->p_vaddr = (elf_addr_t)linux_banner;
	phdr->p_paddr = virt_to_phys(linux_banner);
	phdr->p_filesz = phdr->p_memsz = strlen(linux_banner) + 1;
	phdr->p_flags = PF_R | PF_W;

	/* Update headers count*/
	ehdr->e_phnum = 1;
	ehdr->e_shnum = 4;

	if (!is_rm_minidump)
		mdreg->md_valid = MD_REGION_VALID;
	return 0;
}

static int msm_minidump_driver_remove(struct platform_device *pdev)
{
	/* TO-DO.
	 *Free the required resources and set the global
	 * variables as minidump is not initialized.
	 */
	return 0;
}

static int msm_minidump_driver_probe(struct platform_device *pdev)
{
	unsigned int region_number;
	size_t size;
	struct md_pending_region *pending_region, *tmp;
	struct md_rm_request *rm_region;
	struct md_global_toc *md_global_toc;
	struct md_ss_toc *md_ss_toc;
	unsigned long flags;
	int ret;

	is_rm_minidump =
		of_device_is_compatible(pdev->dev.of_node, "qcom,minidump-rm");

	if (is_rm_minidump) {
		ret = gh_rm_minidump_get_info();
		if (ret < 0) {
			pr_err("Get minidump info failed ret=%d\n", ret);
			return ret;
		}
		pr_debug("Get available slot number:%d\n", ret);
		minidump_rm_table = devm_kzalloc(
			&pdev->dev, sizeof(*minidump_rm_table), GFP_KERNEL);
		if (!minidump_rm_table)
			return -ENOMEM;

		minidump_rm_table->minidump_rm_wq = alloc_workqueue(
			"minidump_wq", WQ_HIGHPRI | WQ_UNBOUND, 1);
		if (!minidump_rm_table->minidump_rm_wq) {
			pr_err("Unable to initialize workqueue\n");
			return -EINVAL;
		}
	} else {
		/* Get Minidump table */
		md_global_toc = qcom_smem_get(QCOM_SMEM_HOST_ANY,
					      SBL_MINIDUMP_SMEM_ID, &size);
		if (IS_ERR_OR_NULL(md_global_toc)) {
			pr_err("SMEM is not initialized\n");
			return PTR_ERR(md_global_toc);
		}

		/*Check global minidump support initialization */
		if (!md_global_toc->md_toc_init) {
			pr_err("System Minidump TOC not initialized\n");
			return -ENODEV;
		}

		minidump_table = devm_kzalloc(
			&pdev->dev, sizeof(*minidump_table), GFP_KERNEL);
		if (!minidump_table)
			return -ENOMEM;

		minidump_table->md_gbl_toc = md_global_toc;
		minidump_table->revision = md_global_toc->md_revision;
		md_ss_toc = &md_global_toc->md_ss_toc[MD_SS_HLOS_ID];

		md_ss_toc->encryption_status = MD_SS_ENCR_DONE;
		md_ss_toc->encryption_required = MD_SS_ENCR_NOTREQ;

		minidump_table->md_ss_toc = md_ss_toc;
		minidump_table->md_regions = devm_kzalloc(
			&pdev->dev,
			(MAX_NUM_ENTRIES * sizeof(struct md_ss_region)),
			GFP_KERNEL);
		if (!minidump_table->md_regions)
			return -ENOMEM;

		md_ss_toc->md_ss_smem_regions_baseptr =
			virt_to_phys(minidump_table->md_regions);

		md_ss_toc->ss_region_count = 1;
	}

	/* First entry would be ELF header */
	msm_minidump_add_header();

	/* Add pending entries to HLOS TOC */
	spin_lock_irqsave(&mdt_lock, flags);
	/* only need initialize when use smem */
	if (!is_rm_minidump) {
		md_ss_toc->md_ss_toc_init = 1;
		md_ss_toc->md_ss_enable_status = MD_SS_ENABLED;
	}
	region_number = 0;
	list_for_each_entry_safe(pending_region, tmp, &pending_list, list) {
		if (is_rm_minidump) {
			rm_region = kzalloc(sizeof(*rm_region), GFP_ATOMIC);
			if (!rm_region) {
				spin_unlock_irqrestore(&mdt_lock, flags);
				return -ENOMEM;
			}
			strscpy(minidump_rm_table->entry[region_number].name,
				pending_region->entry.name,
				sizeof(minidump_rm_table->entry[region_number]
					       .name));
			rm_region->entry = pending_region->entry;
			rm_region->work_cmd = MINIDUMP_ADD;
			INIT_WORK(&rm_region->work, minidump_rm_work);
			queue_work(minidump_rm_table->minidump_rm_wq,
				   &rm_region->work);
		} else {
			/* Add pending entry to minidump table and ss toc */
			minidump_table->entry[region_number] =
				pending_region->entry;
			md_add_ss_toc(&minidump_table->entry[region_number]);
		}
		list_del(&pending_region->list);
		kfree(pending_region);
		region_number++;
	}
	spin_unlock_irqrestore(&mdt_lock, flags);

	/* All updates above should be visible, before init completes */
	smp_store_release(&md_init_done, true);
	msm_minidump_log_init();
	pr_info("Enabled with max number of regions %d\n",
		CONFIG_MINIDUMP_MAX_ENTRIES);

	return 0;
}

static const struct of_device_id msm_minidump_of_match[] = {
	{ .compatible = "qcom,minidump" },
	{ .compatible = "qcom,minidump-rm"},
	{ }
};
MODULE_DEVICE_TABLE(of, msm_minidump_of_match);

static struct platform_driver msm_minidump_driver = {
	.driver = {
		.name = "qcom-minidump",
		.of_match_table = msm_minidump_of_match,
	},
	.probe = msm_minidump_driver_probe,
	.remove = msm_minidump_driver_remove,
};
module_platform_driver(msm_minidump_driver);

MODULE_DESCRIPTION("MSM Mini Dump Driver");
MODULE_IMPORT_NS(MINIDUMP);
MODULE_LICENSE("GPL v2");
