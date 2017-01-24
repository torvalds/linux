/*
 * Qualcomm Peripheral Image Loader
 *
 * Copyright (C) 2016 Linaro Ltd
 * Copyright (C) 2015 Sony Mobile Communications Inc
 * Copyright (c) 2012-2013, The Linux Foundation. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <linux/elf.h>
#include <linux/firmware.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/remoteproc.h>
#include <linux/sizes.h>
#include <linux/slab.h>

#include "remoteproc_internal.h"
#include "qcom_mdt_loader.h"

/**
 * qcom_mdt_find_rsc_table() - provide dummy resource table for remoteproc
 * @rproc:	remoteproc handle
 * @fw:		firmware header
 * @tablesz:	outgoing size of the table
 *
 * Returns a dummy table.
 */
struct resource_table *qcom_mdt_find_rsc_table(struct rproc *rproc,
					       const struct firmware *fw,
					       int *tablesz)
{
	static struct resource_table table = { .ver = 1, };

	*tablesz = sizeof(table);
	return &table;
}
EXPORT_SYMBOL_GPL(qcom_mdt_find_rsc_table);

/**
 * qcom_mdt_parse() - extract useful parameters from the mdt header
 * @fw:		firmware handle
 * @fw_addr:	optional reference for base of the firmware's memory region
 * @fw_size:	optional reference for size of the firmware's memory region
 * @fw_relocate: optional reference for flagging if the firmware is relocatable
 *
 * Returns 0 on success, negative errno otherwise.
 */
int qcom_mdt_parse(const struct firmware *fw, phys_addr_t *fw_addr,
		   size_t *fw_size, bool *fw_relocate)
{
	const struct elf32_phdr *phdrs;
	const struct elf32_phdr *phdr;
	const struct elf32_hdr *ehdr;
	phys_addr_t min_addr = (phys_addr_t)ULLONG_MAX;
	phys_addr_t max_addr = 0;
	bool relocate = false;
	int i;

	ehdr = (struct elf32_hdr *)fw->data;
	phdrs = (struct elf32_phdr *)(ehdr + 1);

	for (i = 0; i < ehdr->e_phnum; i++) {
		phdr = &phdrs[i];

		if (phdr->p_type != PT_LOAD)
			continue;

		if ((phdr->p_flags & QCOM_MDT_TYPE_MASK) == QCOM_MDT_TYPE_HASH)
			continue;

		if (!phdr->p_memsz)
			continue;

		if (phdr->p_flags & QCOM_MDT_RELOCATABLE)
			relocate = true;

		if (phdr->p_paddr < min_addr)
			min_addr = phdr->p_paddr;

		if (phdr->p_paddr + phdr->p_memsz > max_addr)
			max_addr = ALIGN(phdr->p_paddr + phdr->p_memsz, SZ_4K);
	}

	if (fw_addr)
		*fw_addr = min_addr;
	if (fw_size)
		*fw_size = max_addr - min_addr;
	if (fw_relocate)
		*fw_relocate = relocate;

	return 0;
}
EXPORT_SYMBOL_GPL(qcom_mdt_parse);

/**
 * qcom_mdt_load() - load the firmware which header is defined in fw
 * @rproc:	rproc handle
 * @fw:		frimware object for the header
 * @firmware:	filename of the firmware, for building .bXX names
 *
 * Returns 0 on success, negative errno otherwise.
 */
int qcom_mdt_load(struct rproc *rproc,
		  const struct firmware *fw,
		  const char *firmware)
{
	const struct elf32_phdr *phdrs;
	const struct elf32_phdr *phdr;
	const struct elf32_hdr *ehdr;
	size_t fw_name_len;
	char *fw_name;
	void *ptr;
	int ret;
	int i;

	ehdr = (struct elf32_hdr *)fw->data;
	phdrs = (struct elf32_phdr *)(ehdr + 1);

	fw_name_len = strlen(firmware);
	if (fw_name_len <= 4)
		return -EINVAL;

	fw_name = kstrdup(firmware, GFP_KERNEL);
	if (!fw_name)
		return -ENOMEM;

	for (i = 0; i < ehdr->e_phnum; i++) {
		phdr = &phdrs[i];

		if (phdr->p_type != PT_LOAD)
			continue;

		if ((phdr->p_flags & QCOM_MDT_TYPE_MASK) == QCOM_MDT_TYPE_HASH)
			continue;

		if (!phdr->p_memsz)
			continue;

		ptr = rproc_da_to_va(rproc, phdr->p_paddr, phdr->p_memsz);
		if (!ptr) {
			dev_err(&rproc->dev, "segment outside memory range\n");
			ret = -EINVAL;
			break;
		}

		if (phdr->p_filesz) {
			sprintf(fw_name + fw_name_len - 3, "b%02d", i);
			ret = request_firmware(&fw, fw_name, &rproc->dev);
			if (ret) {
				dev_err(&rproc->dev, "failed to load %s\n",
					fw_name);
				break;
			}

			memcpy(ptr, fw->data, fw->size);

			release_firmware(fw);
		}

		if (phdr->p_memsz > phdr->p_filesz)
			memset(ptr + phdr->p_filesz, 0, phdr->p_memsz - phdr->p_filesz);
	}

	kfree(fw_name);

	return ret;
}
EXPORT_SYMBOL_GPL(qcom_mdt_load);

MODULE_DESCRIPTION("Firmware parser for Qualcomm MDT format");
MODULE_LICENSE("GPL v2");
