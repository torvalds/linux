// SPDX-License-Identifier: GPL-2.0-only
/*
 * Qualcomm Peripheral Image Loader
 *
 * Copyright (C) 2016 Linaro Ltd
 * Copyright (C) 2015 Sony Mobile Communications Inc
 * Copyright (c) 2012-2013, The Linux Foundation. All rights reserved.
 */

#include <linux/cleanup.h>
#include <linux/device.h>
#include <linux/elf.h>
#include <linux/firmware.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/firmware/qcom/qcom_scm.h>
#include <linux/sizes.h>
#include <linux/slab.h>
#include <linux/soc/qcom/mdt_loader.h>

static bool mdt_phdr_valid(const struct elf32_phdr *phdr)
{
	if (phdr->p_type != PT_LOAD)
		return false;

	if ((phdr->p_flags & QCOM_MDT_TYPE_MASK) == QCOM_MDT_TYPE_HASH)
		return false;

	if (!phdr->p_memsz)
		return false;

	return true;
}

static ssize_t mdt_load_split_segment(void *ptr, const struct elf32_phdr *phdrs,
				      unsigned int segment, const char *fw_name,
				      struct device *dev)
{
	const struct elf32_phdr *phdr = &phdrs[segment];
	const struct firmware *seg_fw;
	ssize_t ret;

	if (strlen(fw_name) < 4)
		return -EINVAL;

	char *seg_name __free(kfree) = kstrdup(fw_name, GFP_KERNEL);
	if (!seg_name)
		return -ENOMEM;

	sprintf(seg_name + strlen(fw_name) - 3, "b%02d", segment);
	ret = request_firmware_into_buf(&seg_fw, seg_name, dev,
					ptr, phdr->p_filesz);
	if (ret) {
		dev_err(dev, "error %zd loading %s\n", ret, seg_name);
		return ret;
	}

	if (seg_fw->size != phdr->p_filesz) {
		dev_err(dev,
			"failed to load segment %d from truncated file %s\n",
			segment, seg_name);
		ret = -EINVAL;
	}

	release_firmware(seg_fw);

	return ret;
}

/**
 * qcom_mdt_get_size() - acquire size of the memory region needed to load mdt
 * @fw:		firmware object for the mdt file
 *
 * Returns size of the loaded firmware blob, or -EINVAL on failure.
 */
ssize_t qcom_mdt_get_size(const struct firmware *fw)
{
	const struct elf32_phdr *phdrs;
	const struct elf32_phdr *phdr;
	const struct elf32_hdr *ehdr;
	phys_addr_t min_addr = PHYS_ADDR_MAX;
	phys_addr_t max_addr = 0;
	int i;

	ehdr = (struct elf32_hdr *)fw->data;
	phdrs = (struct elf32_phdr *)(ehdr + 1);

	for (i = 0; i < ehdr->e_phnum; i++) {
		phdr = &phdrs[i];

		if (!mdt_phdr_valid(phdr))
			continue;

		if (phdr->p_paddr < min_addr)
			min_addr = phdr->p_paddr;

		if (phdr->p_paddr + phdr->p_memsz > max_addr)
			max_addr = ALIGN(phdr->p_paddr + phdr->p_memsz, SZ_4K);
	}

	return min_addr < max_addr ? max_addr - min_addr : -EINVAL;
}
EXPORT_SYMBOL_GPL(qcom_mdt_get_size);

/**
 * qcom_mdt_read_metadata() - read header and metadata from mdt or mbn
 * @fw:		firmware of mdt header or mbn
 * @data_len:	length of the read metadata blob
 * @fw_name:	name of the firmware, for construction of segment file names
 * @dev:	device handle to associate resources with
 *
 * The mechanism that performs the authentication of the loading firmware
 * expects an ELF header directly followed by the segment of hashes, with no
 * padding inbetween. This function allocates a chunk of memory for this pair
 * and copy the two pieces into the buffer.
 *
 * In the case of split firmware the hash is found directly following the ELF
 * header, rather than at p_offset described by the second program header.
 *
 * The caller is responsible to free (kfree()) the returned pointer.
 *
 * Return: pointer to data, or ERR_PTR()
 */
void *qcom_mdt_read_metadata(const struct firmware *fw, size_t *data_len,
			     const char *fw_name, struct device *dev)
{
	const struct elf32_phdr *phdrs;
	const struct elf32_hdr *ehdr;
	unsigned int hash_segment = 0;
	size_t hash_offset;
	size_t hash_size;
	size_t ehdr_size;
	unsigned int i;
	ssize_t ret;
	void *data;

	ehdr = (struct elf32_hdr *)fw->data;
	phdrs = (struct elf32_phdr *)(ehdr + 1);

	if (ehdr->e_phnum < 2)
		return ERR_PTR(-EINVAL);

	if (phdrs[0].p_type == PT_LOAD)
		return ERR_PTR(-EINVAL);

	for (i = 1; i < ehdr->e_phnum; i++) {
		if ((phdrs[i].p_flags & QCOM_MDT_TYPE_MASK) == QCOM_MDT_TYPE_HASH) {
			hash_segment = i;
			break;
		}
	}

	if (!hash_segment) {
		dev_err(dev, "no hash segment found in %s\n", fw_name);
		return ERR_PTR(-EINVAL);
	}

	ehdr_size = phdrs[0].p_filesz;
	hash_size = phdrs[hash_segment].p_filesz;

	data = kmalloc(ehdr_size + hash_size, GFP_KERNEL);
	if (!data)
		return ERR_PTR(-ENOMEM);

	/* Copy ELF header */
	memcpy(data, fw->data, ehdr_size);

	if (ehdr_size + hash_size == fw->size) {
		/* Firmware is split and hash is packed following the ELF header */
		hash_offset = phdrs[0].p_filesz;
		memcpy(data + ehdr_size, fw->data + hash_offset, hash_size);
	} else if (phdrs[hash_segment].p_offset + hash_size <= fw->size) {
		/* Hash is in its own segment, but within the loaded file */
		hash_offset = phdrs[hash_segment].p_offset;
		memcpy(data + ehdr_size, fw->data + hash_offset, hash_size);
	} else {
		/* Hash is in its own segment, beyond the loaded file */
		ret = mdt_load_split_segment(data + ehdr_size, phdrs, hash_segment, fw_name, dev);
		if (ret) {
			kfree(data);
			return ERR_PTR(ret);
		}
	}

	*data_len = ehdr_size + hash_size;

	return data;
}
EXPORT_SYMBOL_GPL(qcom_mdt_read_metadata);

/**
 * qcom_mdt_pas_init() - initialize PAS region for firmware loading
 * @dev:	device handle to associate resources with
 * @fw:		firmware object for the mdt file
 * @fw_name:	name of the firmware, for construction of segment file names
 * @pas_id:	PAS identifier
 * @mem_phys:	physical address of allocated memory region
 * @ctx:	PAS metadata context, to be released by caller
 *
 * Returns 0 on success, negative errno otherwise.
 */
int qcom_mdt_pas_init(struct device *dev, const struct firmware *fw,
		      const char *fw_name, int pas_id, phys_addr_t mem_phys,
		      struct qcom_scm_pas_metadata *ctx)
{
	const struct elf32_phdr *phdrs;
	const struct elf32_phdr *phdr;
	const struct elf32_hdr *ehdr;
	phys_addr_t min_addr = PHYS_ADDR_MAX;
	phys_addr_t max_addr = 0;
	bool relocate = false;
	size_t metadata_len;
	void *metadata;
	int ret;
	int i;

	ehdr = (struct elf32_hdr *)fw->data;
	phdrs = (struct elf32_phdr *)(ehdr + 1);

	for (i = 0; i < ehdr->e_phnum; i++) {
		phdr = &phdrs[i];

		if (!mdt_phdr_valid(phdr))
			continue;

		if (phdr->p_flags & QCOM_MDT_RELOCATABLE)
			relocate = true;

		if (phdr->p_paddr < min_addr)
			min_addr = phdr->p_paddr;

		if (phdr->p_paddr + phdr->p_memsz > max_addr)
			max_addr = ALIGN(phdr->p_paddr + phdr->p_memsz, SZ_4K);
	}

	metadata = qcom_mdt_read_metadata(fw, &metadata_len, fw_name, dev);
	if (IS_ERR(metadata)) {
		ret = PTR_ERR(metadata);
		dev_err(dev, "error %d reading firmware %s metadata\n", ret, fw_name);
		goto out;
	}

	ret = qcom_scm_pas_init_image(pas_id, metadata, metadata_len, ctx);
	kfree(metadata);
	if (ret) {
		/* Invalid firmware metadata */
		dev_err(dev, "error %d initializing firmware %s\n", ret, fw_name);
		goto out;
	}

	if (relocate) {
		ret = qcom_scm_pas_mem_setup(pas_id, mem_phys, max_addr - min_addr);
		if (ret) {
			/* Unable to set up relocation */
			dev_err(dev, "error %d setting up firmware %s\n", ret, fw_name);
			goto out;
		}
	}

out:
	return ret;
}
EXPORT_SYMBOL_GPL(qcom_mdt_pas_init);

static bool qcom_mdt_bins_are_split(const struct firmware *fw, const char *fw_name)
{
	const struct elf32_phdr *phdrs;
	const struct elf32_hdr *ehdr;
	uint64_t seg_start, seg_end;
	int i;

	ehdr = (struct elf32_hdr *)fw->data;
	phdrs = (struct elf32_phdr *)(ehdr + 1);

	for (i = 0; i < ehdr->e_phnum; i++) {
		/*
		 * The size of the MDT file is not padded to include any
		 * zero-sized segments at the end. Ignore these, as they should
		 * not affect the decision about image being split or not.
		 */
		if (!phdrs[i].p_filesz)
			continue;

		seg_start = phdrs[i].p_offset;
		seg_end = phdrs[i].p_offset + phdrs[i].p_filesz;
		if (seg_start > fw->size || seg_end > fw->size)
			return true;
	}

	return false;
}

static int __qcom_mdt_load(struct device *dev, const struct firmware *fw,
			   const char *fw_name, int pas_id, void *mem_region,
			   phys_addr_t mem_phys, size_t mem_size,
			   phys_addr_t *reloc_base, bool pas_init)
{
	const struct elf32_phdr *phdrs;
	const struct elf32_phdr *phdr;
	const struct elf32_hdr *ehdr;
	phys_addr_t mem_reloc;
	phys_addr_t min_addr = PHYS_ADDR_MAX;
	ssize_t offset;
	bool relocate = false;
	bool is_split;
	void *ptr;
	int ret = 0;
	int i;

	if (!fw || !mem_region || !mem_phys || !mem_size)
		return -EINVAL;

	is_split = qcom_mdt_bins_are_split(fw, fw_name);
	ehdr = (struct elf32_hdr *)fw->data;
	phdrs = (struct elf32_phdr *)(ehdr + 1);

	for (i = 0; i < ehdr->e_phnum; i++) {
		phdr = &phdrs[i];

		if (!mdt_phdr_valid(phdr))
			continue;

		if (phdr->p_flags & QCOM_MDT_RELOCATABLE)
			relocate = true;

		if (phdr->p_paddr < min_addr)
			min_addr = phdr->p_paddr;
	}

	if (relocate) {
		/*
		 * The image is relocatable, so offset each segment based on
		 * the lowest segment address.
		 */
		mem_reloc = min_addr;
	} else {
		/*
		 * Image is not relocatable, so offset each segment based on
		 * the allocated physical chunk of memory.
		 */
		mem_reloc = mem_phys;
	}

	for (i = 0; i < ehdr->e_phnum; i++) {
		phdr = &phdrs[i];

		if (!mdt_phdr_valid(phdr))
			continue;

		offset = phdr->p_paddr - mem_reloc;
		if (offset < 0 || offset + phdr->p_memsz > mem_size) {
			dev_err(dev, "segment outside memory range\n");
			ret = -EINVAL;
			break;
		}

		if (phdr->p_filesz > phdr->p_memsz) {
			dev_err(dev,
				"refusing to load segment %d with p_filesz > p_memsz\n",
				i);
			ret = -EINVAL;
			break;
		}

		ptr = mem_region + offset;

		if (phdr->p_filesz && !is_split) {
			/* Firmware is large enough to be non-split */
			if (phdr->p_offset + phdr->p_filesz > fw->size) {
				dev_err(dev, "file %s segment %d would be truncated\n",
					fw_name, i);
				ret = -EINVAL;
				break;
			}

			memcpy(ptr, fw->data + phdr->p_offset, phdr->p_filesz);
		} else if (phdr->p_filesz) {
			/* Firmware not large enough, load split-out segments */
			ret = mdt_load_split_segment(ptr, phdrs, i, fw_name, dev);
			if (ret)
				break;
		}

		if (phdr->p_memsz > phdr->p_filesz)
			memset(ptr + phdr->p_filesz, 0, phdr->p_memsz - phdr->p_filesz);
	}

	if (reloc_base)
		*reloc_base = mem_reloc;

	return ret;
}

/**
 * qcom_mdt_load() - load the firmware which header is loaded as fw
 * @dev:	device handle to associate resources with
 * @fw:		firmware object for the mdt file
 * @firmware:	name of the firmware, for construction of segment file names
 * @pas_id:	PAS identifier
 * @mem_region:	allocated memory region to load firmware into
 * @mem_phys:	physical address of allocated memory region
 * @mem_size:	size of the allocated memory region
 * @reloc_base:	adjusted physical address after relocation
 *
 * Returns 0 on success, negative errno otherwise.
 */
int qcom_mdt_load(struct device *dev, const struct firmware *fw,
		  const char *firmware, int pas_id, void *mem_region,
		  phys_addr_t mem_phys, size_t mem_size,
		  phys_addr_t *reloc_base)
{
	int ret;

	ret = qcom_mdt_pas_init(dev, fw, firmware, pas_id, mem_phys, NULL);
	if (ret)
		return ret;

	return __qcom_mdt_load(dev, fw, firmware, pas_id, mem_region, mem_phys,
			       mem_size, reloc_base, true);
}
EXPORT_SYMBOL_GPL(qcom_mdt_load);

/**
 * qcom_mdt_load_no_init() - load the firmware which header is loaded as fw
 * @dev:	device handle to associate resources with
 * @fw:		firmware object for the mdt file
 * @firmware:	name of the firmware, for construction of segment file names
 * @pas_id:	PAS identifier
 * @mem_region:	allocated memory region to load firmware into
 * @mem_phys:	physical address of allocated memory region
 * @mem_size:	size of the allocated memory region
 * @reloc_base:	adjusted physical address after relocation
 *
 * Returns 0 on success, negative errno otherwise.
 */
int qcom_mdt_load_no_init(struct device *dev, const struct firmware *fw,
			  const char *firmware, int pas_id,
			  void *mem_region, phys_addr_t mem_phys,
			  size_t mem_size, phys_addr_t *reloc_base)
{
	return __qcom_mdt_load(dev, fw, firmware, pas_id, mem_region, mem_phys,
			       mem_size, reloc_base, false);
}
EXPORT_SYMBOL_GPL(qcom_mdt_load_no_init);

MODULE_DESCRIPTION("Firmware parser for Qualcomm MDT format");
MODULE_LICENSE("GPL v2");
