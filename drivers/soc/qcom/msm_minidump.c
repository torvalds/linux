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
#include <linux/soc/qcom/smem.h>
#include <soc/qcom/minidump.h>
#include "debug_symbol.h"
#include "minidump_private.h"
#include "elf.h"

/* minidump core structure */
struct md_core {
	const struct md_ops	*ops;
};

bool md_init_done;
EXPORT_SYMBOL_GPL(md_init_done);

static struct md_core md_core;

DEFINE_SPINLOCK(mdt_lock);

unsigned int md_num_regions;
EXPORT_SYMBOL_GPL(md_num_regions);

struct md_elfhdr minidump_elfheader;
EXPORT_SYMBOL_GPL(minidump_elfheader);

/* Number of pending entries to be added in ToC regions */
static LIST_HEAD(pending_list);

inline char *elf_lookup_string(struct elfhdr *hdr, int offset)
{
	char *strtab = elf_str_table(hdr);

	if ((strtab == NULL) || (minidump_elfheader.strtable_idx < offset))
		return NULL;
	return strtab + offset;
}
EXPORT_SYMBOL_GPL(elf_lookup_string);

inline unsigned int set_section_name(const char *name)
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
EXPORT_SYMBOL_GPL(set_section_name);

struct md_region md_get_region(char *name)
{
	struct md_region tmp = {0};

	tmp = md_core.ops->get_region(name);

	return tmp;
}

/* Update elf header */
void md_add_elf_header(const struct md_region *entry)
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
EXPORT_SYMBOL_GPL(md_add_elf_header);

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
EXPORT_SYMBOL_GPL(msm_minidump_clear_headers);

bool msm_minidump_enabled(void)
{
	bool ret = false;
	if (md_core.ops)
		ret = md_core.ops->md_enable();

	return ret;
}
EXPORT_SYMBOL_GPL(msm_minidump_enabled);

int msm_minidump_get_available_region(void)
{
	int res = -EBUSY;

	if (md_core.ops)
		res = md_core.ops->get_available_region();

	return res;
}
EXPORT_SYMBOL_GPL(msm_minidump_get_available_region);

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


int msm_minidump_update_region(int regno, const struct md_region *entry)
{
	int ret = 0;

	/* Ensure that init completes before we update regions */
	if (!smp_load_acquire(&md_init_done))
		return -EINVAL;

	if (validate_region(entry) || (regno >= MAX_NUM_ENTRIES))
		return -EINVAL;

	if (md_core.ops)
		ret = md_core.ops->update_region(regno, entry);
	else
		return -EINVAL;

	return ret;
}
EXPORT_SYMBOL_GPL(msm_minidump_update_region);

int msm_minidump_add_region(const struct md_region *entry)
{
	struct md_pending_region *pending_region;
	unsigned long flags;
	int ret;

	if (validate_region(entry))
		return -EINVAL;

	/* Region adding should after init completes */
	if (md_core.ops && smp_load_acquire(&md_init_done)) {
		ret = md_core.ops->add_region(entry);
	} else {
		/* Local table not initialized
		 * add to pending list, need free after initialized
		 */
		pr_info("Minidump driver hasn't probe, add region to pending list\n");
		spin_lock_irqsave(&mdt_lock, flags);

		if (md_num_regions >= MAX_NUM_ENTRIES) {
			printk_deferred("Maximum entries reached\n");
			spin_unlock_irqrestore(&mdt_lock, flags);
			return -ENOMEM;
		}

		pending_region = kzalloc(sizeof(*pending_region), GFP_ATOMIC);
		if (!pending_region) {
			spin_unlock_irqrestore(&mdt_lock, flags);
			return -ENOMEM;
		}
		pending_region->entry = *entry;
		list_add_tail(&pending_region->list, &pending_list);
		ret = md_num_regions;
		md_num_regions++;
		spin_unlock_irqrestore(&mdt_lock, flags);
	}

	return ret;
}
EXPORT_SYMBOL_GPL(msm_minidump_add_region);

int msm_minidump_remove_region(const struct md_region *entry)
{
	int ret;

	if (!entry)
		return -EINVAL;

	/* Ensure that init completes before we remove regions */
	if (!smp_load_acquire(&md_init_done))
		return -EINVAL;

	if (md_core.ops)
		ret = md_core.ops->remove_region(entry);
	else
		ret = -EINVAL;

	if (ret)
		printk_deferred("Failed to remove region:%s\n", entry->name);

	return ret;
}
EXPORT_SYMBOL_GPL(msm_minidump_remove_region);

static int msm_minidump_add_header(void)
{
	struct elfhdr *ehdr;
	struct elf_shdr *shdr;
	struct elf_phdr *phdr;
	unsigned int strtbl_off, elfh_size, phdr_off;
	char *banner, *linux_banner;

	linux_banner = DEBUG_SYMBOL_LOOKUP(linux_banner);
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

	/* KELF header and 3th section will be added in platform specific driver */
	if (md_core.ops)
		md_core.ops->add_header(shdr, ehdr, elfh_size);
	else
		return -EINVAL;

	return 0;
}

int msm_minidump_driver_probe(const struct md_init_data *data)
{
	int ret = 0;

	if (!debug_symbol_available())
		return -EPROBE_DEFER;

	md_core.ops = data->ops;

	ret = md_core.ops->init_md_table();
	if (ret) {
		pr_err("Minidump table initialize failed\n");
		goto out;
	}

	/* First entry would be ELF header */
	ret = msm_minidump_add_header();
	if (ret < 0) {
		pr_err("failed to init minidump header\n");
		return ret;
	}

	/* All updates above should be visible, before init completes */
	smp_store_release(&md_init_done, true);

	ret = md_core.ops->add_pending_entry(&pending_list);
	if (ret) {
		pr_err("Add pending entry failed\n");
		goto out;
	}

	msm_minidump_log_init();
	pr_info("Enabled with max number of regions %d\n",
		CONFIG_MINIDUMP_MAX_ENTRIES);

out:
	return ret;
}
EXPORT_SYMBOL_GPL(msm_minidump_driver_probe);

MODULE_DESCRIPTION("MSM Mini Dump Driver");
MODULE_IMPORT_NS(MINIDUMP);
MODULE_LICENSE("GPL v2");
