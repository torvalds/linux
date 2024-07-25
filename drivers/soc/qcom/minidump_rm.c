// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2017-2021, The Linux Foundation. All rights reserved.
 * Copyright (c) 2022-2024 Qualcomm Innovation Center, Inc. All rights reserved.
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
#include <linux/gunyah/gh_rm_drv.h>
#include <soc/qcom/minidump.h>
#include "debug_symbol.h"
#include "minidump_private.h"
#include "elf.h"

/* Protect elfheader and smem table from deferred calls contention */
static DEFINE_RWLOCK(mdt_remove_lock);
struct workqueue_struct *minidump_rm_wq;

static inline int md_elf_entry_number(const struct md_region *entry)
{
	struct elf_shdr *shdr;
	char *hdr_name;
	int i;

	for (i = 0; i < minidump_elfheader.ehdr->e_shnum; i++) {
		shdr = elf_section(minidump_elfheader.ehdr, i);
		hdr_name = elf_lookup_string(minidump_elfheader.ehdr,
					     shdr->sh_name);
		if (hdr_name && !strcmp(hdr_name, entry->name))
			return i;
	}
	return -ENOENT;
}

static void md_rm_update_elf_header(int entryno, const struct md_region *entry)
{
	struct elfhdr *hdr = minidump_elfheader.ehdr;
	struct elf_shdr *shdr;
	struct elf_phdr *phdr;

	shdr = elf_section(hdr, entryno + 3);
	phdr = elf_program(hdr, entryno + 1);
	shdr->sh_addr = (elf_addr_t)entry->virt_addr;
	phdr->p_vaddr = entry->virt_addr;
	phdr->p_paddr = entry->phys_addr;
}

static void md_rm_update_work(struct md_region *entry)
{
	int slot_num, entryno, ret;
	unsigned long flags;

	slot_num = gh_rm_minidump_get_slot_from_name(0, entry->name,
						     strlen(entry->name));
	if (slot_num < 0) {
		printk_deferred(
			"Get slot number:[%s] failed:%d unable to get slot number\n",
			entry->name, slot_num);
		return;
	}

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
	entryno = md_elf_entry_number(entry);
	if (entryno < 0) {
		printk_deferred(
			"Update entry:%s failed, minidump table is corrupt\n",
			entry->name);
		md_rm_update_elf_header(entryno, entry);
	}
	spin_unlock_irqrestore(&mdt_lock, flags);
}

static void md_rm_add_work(struct md_region *entry)
{
	int slot_num;
	unsigned long flags;

	slot_num =
		gh_rm_minidump_register_range(entry->phys_addr, entry->size,
					      entry->name, strlen(entry->name));
	spin_lock_irqsave(&mdt_lock, flags);
	if (slot_num < 0) {
		md_num_regions--;
		pr_err("Failed to register minidump entry:%s ret:%d\n",
		       entry->name, slot_num);
		msm_minidump_clear_headers(entry);
	}

	spin_unlock_irqrestore(&mdt_lock, flags);
}

static void md_rm_remove_work(struct md_region *entry)
{
	int ret, slot_num;

	slot_num = gh_rm_minidump_get_slot_from_name(0, entry->name,
						     strlen(entry->name));
	if (slot_num < 0) {
		pr_err("Failed to find slot number of minidump entry:%s ret:%d\n",
		       entry->name, slot_num);
		return;
	}

	ret = gh_rm_minidump_deregister_slot(slot_num);
	if (ret < 0)
		pr_err("Failed to deregister minidump entry:%s ret:%d\n",
		       entry->name, ret);
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

static int md_rm_add_region(const struct md_region *entry)
{
	struct md_rm_request *rm_work;

	/* alloc a RM entry for workqueue, need free in work */
	rm_work = kzalloc(sizeof(*rm_work), GFP_ATOMIC);
	if (!rm_work)
		return -ENOMEM;
	rm_work->entry = *entry;
	rm_work->work_cmd = MINIDUMP_ADD;
	INIT_WORK(&rm_work->work, minidump_rm_work);
	queue_work(minidump_rm_wq, &rm_work->work);

	return 0;
}

static int md_rm_update(int regno, const struct md_region *entry)
{
	int entryno;
	struct md_rm_request *rm_work;

	entryno = md_elf_entry_number(entry);
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
	queue_work(minidump_rm_wq, &rm_work->work);

	return 0;
}

static int md_rm_remove_region(const struct md_region *entry)
{
	int ret;
	struct md_rm_request *rm_work;

	ret = md_elf_entry_number(entry);
	if (ret < 0) {
		printk_deferred("Not able to find the entry %s in table\n",
				entry->name);
		return ret;
	}
	rm_work = kzalloc(sizeof(*rm_work), GFP_ATOMIC);
	if (!rm_work)
		return -ENOMEM;
	ret = msm_minidump_clear_headers(entry);
	if (ret) {
		printk_deferred("Fail to remove entry %s in elf header\n",
				entry->name);
		kfree(rm_work);
		return ret;
	}
	rm_work->work_cmd = MINIDUMP_REMOVE;
	rm_work->entry = *entry;
	INIT_WORK(&rm_work->work, minidump_rm_work);
	queue_work(minidump_rm_wq, &rm_work->work);
	md_num_regions--;

	return 0;
}

static int md_rm_init_md_table(void)
{
	int ret = 0;

	ret = gh_rm_minidump_get_info();
	if (ret < 0) {
		pr_err("Get minidump info failed ret=%d\n", ret);
		return ret;
	}
	pr_debug("Get available slot number:%d\n", ret);

	minidump_rm_wq = alloc_workqueue("minidump_wq",
					 WQ_HIGHPRI | WQ_UNBOUND, 1);
	if (!minidump_rm_wq) {
		pr_err("Unable to initialize workqueue\n");
		return -EINVAL;
	}

	return 0;
}

static int md_rm_add_pending_entry(struct list_head *pending_list)
{
	unsigned int region_number;
	struct md_pending_region *pending_region, *tmp;
	struct md_rm_request *rm_region;
	unsigned long flags;

	/* Add pending entries to HLOS TOC */
	spin_lock_irqsave(&mdt_lock, flags);

	region_number = 0;
	list_for_each_entry_safe(pending_region, tmp, pending_list, list) {
		rm_region = kzalloc(sizeof(*rm_region), GFP_ATOMIC);
		if (!rm_region) {
			spin_unlock_irqrestore(&mdt_lock, flags);
			return -ENOMEM;
		}
		rm_region->entry = pending_region->entry;
		rm_region->work_cmd = MINIDUMP_ADD;
		INIT_WORK(&rm_region->work, minidump_rm_work);
		queue_work(minidump_rm_wq, &rm_region->work);
		md_add_elf_header(&pending_region->entry);
		list_del(&pending_region->list);
		kfree(pending_region);
		region_number++;
	}
	spin_unlock_irqrestore(&mdt_lock, flags);

	return 0;
}

static void md_rm_add_header(struct elf_shdr *shdr, struct elfhdr *ehdr, unsigned int elfh_size)
{
	int slot_num;

	slot_num = gh_rm_minidump_register_range(
		virt_to_phys(minidump_elfheader.ehdr), elfh_size,
		"KELF_HDR", strlen("KELF_HDR"));
	if (slot_num < 0)
		pr_err("Failed to register elf_header minidump entry\n");

	/* Update headers count*/
	ehdr->e_shnum = 3;
}

static int md_rm_remove_md_region(const struct md_region *entry)
{
	int ret;
	unsigned long flags;

	spin_lock_irqsave(&mdt_lock, flags);
	write_lock(&mdt_remove_lock);

	ret = md_rm_remove_region(entry);

	write_unlock(&mdt_remove_lock);
	spin_unlock_irqrestore(&mdt_lock, flags);

	return ret;
}

static int md_rm_add_md_region(const struct md_region *entry)
{
	int ret = 0;
	unsigned long flags;

	spin_lock_irqsave(&mdt_lock, flags);

	if (md_num_regions >= MAX_NUM_ENTRIES) {
		printk_deferred("Maximum entries reached\n");
		ret = -ENOMEM;
		goto out;
	}

	/* Ensure that init completes before register region */
	if (md_elf_entry_number(entry) >= 0) {
		printk_deferred("Entry name already exist\n");
		ret = -EEXIST;
		goto out;
	}
	ret = md_rm_add_region(entry);
	md_add_elf_header(entry);
	if (ret)
		goto out;

	ret = md_num_regions;
	md_num_regions++;

out:
	spin_unlock_irqrestore(&mdt_lock, flags);

	return ret;
}

static int md_rm_update_region(int regno, const struct md_region *entry)
{
	int ret = 0;
	unsigned long flags;

	read_lock_irqsave(&mdt_remove_lock, flags);

	ret = md_rm_update(regno, entry);

	read_unlock_irqrestore(&mdt_remove_lock, flags);

	return ret;
}

static int md_rm_get_available_region(void)
{
	int res;

	res = gh_rm_minidump_get_info();
	if (res < 0)
		pr_err("Fail to get minidump available region ret=%d\n",
			   res);

	return res;
}

static bool md_rm_md_enable(void)
{
	/* If minidump driver init successfully, minidump is enabled */
	return smp_load_acquire(&md_init_done);
}

static struct md_region md_rm_get_region(char *name)
{
	struct md_region tmp = {0};
	int i, j;
	struct elfhdr *hdr = minidump_elfheader.ehdr;
	struct elf_phdr *phdr;
	struct elf_shdr *shdr;
	char *hdr_name;

	for (i = 0; i < hdr->e_shnum; i++) {
		shdr = elf_section(hdr, i);
		hdr_name = elf_lookup_string(hdr, shdr->sh_name);
		if (hdr_name && !strcmp(hdr_name, name)) {
			for (j = 0; j < hdr->e_phnum; j++) {
				phdr = elf_program(hdr, j);
				if (shdr->sh_addr == phdr->p_vaddr) {
					strscpy(tmp.name, hdr_name,
						sizeof(tmp.name));
					tmp.phys_addr = phdr->p_vaddr;
					tmp.virt_addr = phdr->p_paddr;
					tmp.size = phdr->p_filesz;
					goto out;
				}
			}
		}
	}

out:
	return tmp;
}

static const struct md_ops md_rm_ops = {
	.init_md_table			= md_rm_init_md_table,
	.add_pending_entry		= md_rm_add_pending_entry,
	.add_header				= md_rm_add_header,
	.remove_region			= md_rm_remove_md_region,
	.add_region				= md_rm_add_md_region,
	.update_region			= md_rm_update_region,
	.get_available_region	= md_rm_get_available_region,
	.md_enable				= md_rm_md_enable,
	.get_region				= md_rm_get_region,
};

static struct md_init_data md_rm_init_data = {
	.ops = &md_rm_ops,
};

static int minidump_rm_driver_probe(struct platform_device *pdev)
{
	return msm_minidump_driver_probe(&md_rm_init_data);
}

static const struct of_device_id msm_minidump_rm_of_match[] = {
	{ .compatible = "qcom,minidump-rm" },
	{ }
};
MODULE_DEVICE_TABLE(of, msm_minidump_rm_of_match);

static struct platform_driver msm_minidump_rm_driver = {
	.driver = {
		.name = "qcom-minidump",
		.of_match_table = msm_minidump_rm_of_match,
	},
	.probe = minidump_rm_driver_probe,
};

static int __init minidump_rm_init(void)
{
	return platform_driver_register(&msm_minidump_rm_driver);
}

subsys_initcall(minidump_rm_init);

static void __exit minidump_rm_exit(void)
{
	platform_driver_unregister(&msm_minidump_rm_driver);
}
module_exit(minidump_rm_exit);

MODULE_LICENSE("GPL");
