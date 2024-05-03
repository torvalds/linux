// SPDX-License-Identifier: GPL-2.0-only
/*
 * Qualcomm Peripheral Image Loader
 *
 * Copyright (C) 2016 Linaro Ltd
 * Copyright (C) 2015 Sony Mobile Communications Inc
 * Copyright (c) 2012-2013, 2021 The Linux Foundation. All rights reserved.
 */

#include <linux/device.h>
#include <linux/elf.h>
#include <linux/firmware.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/qcom_scm.h>
#include <linux/sizes.h>
#include <linux/slab.h>
#include <linux/dma-mapping.h>
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

static bool qcom_mdt_bins_are_split(const struct firmware *fw)
{
	const struct elf32_phdr *phdrs;
	const struct elf32_hdr *ehdr;
	uint64_t seg_start, seg_end;
	int i;

	ehdr = (struct elf32_hdr *)fw->data;
	phdrs = (struct elf32_phdr *)(ehdr + 1);

	for (i = 0; i < ehdr->e_phnum; i++) {
		seg_start = phdrs[i].p_offset;
		seg_end = phdrs[i].p_offset + phdrs[i].p_filesz;
		if (seg_start > fw->size || seg_end > fw->size)
			return true;
	}

	return false;
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
 * @dev:	device handle to associate resources with
 * @fw:		firmware of mdt header or mbn
 * @firmware:	name of the firmware, for construction of segment file names
 * @data_len:	length of the read metadata blob
 * @metadata_phys:	phys address for the assigned metadata buffer
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
void *qcom_mdt_read_metadata(struct device *dev, const struct firmware *fw, const char *firmware,
			     size_t *data_len, bool dma_phys_below_32b, dma_addr_t *metadata_phys)
{
	const struct elf32_phdr *phdrs;
	const struct elf32_hdr *ehdr;
	const struct firmware *seg_fw;
	struct device *scm_dev = NULL;
	size_t hash_index;
	size_t hash_size;
	size_t ehdr_size;
	char *fw_name;
	void *data;
	int ret;

	if (fw->size < sizeof(struct elf32_hdr)) {
		dev_err(dev, "Image is too small\n");
		return ERR_PTR(-EINVAL);
	}

	ehdr = (struct elf32_hdr *)fw->data;
	phdrs = (struct elf32_phdr *)(ehdr + 1);

	if (ehdr->e_phnum < 2 || ehdr->e_phoff > fw->size ||
	    (sizeof(phdrs) * ehdr->e_phnum > fw->size - ehdr->e_phoff))
		return ERR_PTR(-EINVAL);

	if (phdrs[0].p_type == PT_LOAD)
		return ERR_PTR(-EINVAL);

	for (hash_index = 1; hash_index < ehdr->e_phnum; hash_index++) {
		if (phdrs[hash_index].p_type != PT_LOAD &&
		   (phdrs[hash_index].p_flags & QCOM_MDT_TYPE_MASK) == QCOM_MDT_TYPE_HASH)
			break;
	}
	if (hash_index >= ehdr->e_phnum)
		return ERR_PTR(-EINVAL);

	ehdr_size = phdrs[0].p_filesz;
	hash_size = phdrs[hash_index].p_filesz;

	/* Overflow check */
	if (ehdr_size >  SIZE_MAX - hash_size)
		return ERR_PTR(-ENOMEM);

	/*
	 * During the scm call memory protection will be enabled for the metadata
	 * blob, so make sure it's physically contiguous, 4K aligned and
	 * non-cachable to avoid XPU violations.
	 */
	if (metadata_phys) {
		if (!dma_phys_below_32b) {
			scm_dev = qcom_get_scm_device();
			if (!scm_dev)
				return ERR_PTR(-EPROBE_DEFER);
			data = dma_alloc_coherent(scm_dev, ehdr_size + hash_size,
					metadata_phys, GFP_KERNEL);
		} else {
			data = dma_alloc_coherent(dev, ehdr_size + hash_size,
						  metadata_phys, GFP_KERNEL);
		}
	} else {
		data = kmalloc(ehdr_size + hash_size, GFP_KERNEL);
	}

	if (!data)
		return ERR_PTR(-ENOMEM);

	/* copy elf header */
	memcpy(data, fw->data, ehdr_size);

	if (qcom_mdt_bins_are_split(fw)) {
		fw_name = kstrdup(firmware, GFP_KERNEL);
		if (!fw_name) {
			ret = -ENOMEM;
			goto free_metadata;

		}
		snprintf(fw_name + strlen(fw_name) - 3, 4, "b%02d", hash_index);

		ret = request_firmware_into_buf(&seg_fw, fw_name, dev, data + ehdr_size, hash_size);
		kfree(fw_name);

		if (ret)
			goto free_metadata;

		release_firmware(seg_fw);
	} else {
		memcpy(data + ehdr_size, fw->data + phdrs[hash_index].p_offset, hash_size);
	}

	*data_len = ehdr_size + hash_size;

	return data;
free_metadata:
	if (metadata_phys) {
		if (!dma_phys_below_32b)
			dma_free_coherent(scm_dev, ehdr_size + hash_size, data, *metadata_phys);
		else
			dma_free_coherent(dev, ehdr_size + hash_size, data, *metadata_phys);
	} else
		kfree(data);
	return ERR_PTR(ret);
}
EXPORT_SYMBOL_GPL(qcom_mdt_read_metadata);


static int __qcom_mdt_load(struct device *dev, const struct firmware *fw, const char *firmware,
			   int pas_id, void *mem_region, phys_addr_t mem_phys, size_t mem_size,
			   phys_addr_t *reloc_base, bool pas_init, bool dma_phys_below_32b,
			   struct qcom_mdt_metadata *mdata)
{
	const struct elf32_phdr *phdrs;
	const struct elf32_phdr *phdr;
	const struct elf32_hdr *ehdr;
	const struct firmware *seg_fw;
	phys_addr_t mem_reloc;
	phys_addr_t min_addr = PHYS_ADDR_MAX;
	phys_addr_t max_addr = 0;
	dma_addr_t metadata_phys = 0;
	struct device *scm_dev = NULL;
	size_t metadata_len = 0;
	size_t fw_name_len;
	ssize_t offset;
	void *metadata = NULL;
	char *fw_name;
	bool relocate = false;
	bool is_split;
	void *ptr;
	int ret = 0;
	int i;

	if (!fw || !mem_region || !mem_phys || !mem_size)
		return -EINVAL;

	is_split = qcom_mdt_bins_are_split(fw);
	ehdr = (struct elf32_hdr *)fw->data;
	phdrs = (struct elf32_phdr *)(ehdr + 1);

	fw_name_len = strlen(firmware);
	if (fw_name_len <= 4)
		return -EINVAL;

	fw_name = kstrdup(firmware, GFP_KERNEL);
	if (!fw_name)
		return -ENOMEM;

	if (pas_init) {
		metadata = qcom_mdt_read_metadata(dev, fw, firmware, &metadata_len,
						  dma_phys_below_32b, &metadata_phys);
		if (IS_ERR(metadata)) {
			ret = PTR_ERR(metadata);
			dev_err(dev, "error %d reading firmware %s metadata\n",
				ret, fw_name);
			goto out;
		}

		if (mdata) {
			mdata->buf = metadata;
			mdata->buf_phys = metadata_phys;
			mdata->size = metadata_len;
		}

		ret = qcom_scm_pas_init_image(pas_id, metadata_phys);
		if (ret) {
			dev_err(dev, "invalid firmware metadata\n");
			goto deinit;
		}
	}

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

	if (relocate) {
		if (pas_init) {
			ret = qcom_scm_pas_mem_setup(pas_id, mem_phys,
						     max_addr - min_addr);
			if (ret) {
				dev_err(dev, "unable to setup relocation\n");
				goto deinit;
			}
		}

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

		if (phdr->p_filesz) {
			if (!is_split) {
				/* Firmware is large enough to be non-split */
				memcpy(ptr, fw->data + phdr->p_offset, phdr->p_filesz);
			} else {
				/* Firmware not large enough, load split-out segments */
				snprintf(fw_name + fw_name_len - 3, 4, "b%02d", i);
				ret = request_firmware_into_buf(&seg_fw, fw_name, dev,
								ptr, phdr->p_filesz);
				if (ret) {
					dev_err(dev, "failed to load %s\n", fw_name);
					break;
				}

				if (seg_fw->size != phdr->p_filesz) {
					dev_err(dev,
						"failed to load segment %d from truncated file %s\n",
						i, fw_name);
					release_firmware(seg_fw);
					ret = -EINVAL;
					break;
				}

				release_firmware(seg_fw);
			}
		}

		if (phdr->p_memsz > phdr->p_filesz)
			memset(ptr + phdr->p_filesz, 0, phdr->p_memsz - phdr->p_filesz);
	}

	if (reloc_base)
		*reloc_base = mem_reloc;
deinit:
	if (ret)
		qcom_scm_pas_shutdown(pas_id);

	if (!mdata && pas_init) {
		if (dma_phys_below_32b) {
			dma_free_coherent(dev, metadata_len, metadata, metadata_phys);
		} else {
			scm_dev = qcom_get_scm_device();
			if (!scm_dev)
				goto out;

			dma_free_coherent(scm_dev,  metadata_len, metadata, metadata_phys);
		}
	}

out:
	kfree(fw_name);
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
	return __qcom_mdt_load(dev, fw, firmware, pas_id, mem_region, mem_phys,
			       mem_size, reloc_base, true, false, NULL);
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
			       mem_size, reloc_base, false, false, NULL);
}
EXPORT_SYMBOL_GPL(qcom_mdt_load_no_init);

/**
 * qcom_mdt_load_no_free() - load the firmware which header is loaded as fw
 * @dev:	device handle to associate resources with
 * @fw:		firmware object for the mdt file
 * @firmware:	name of the firmware, for construction of segment file names
 * @pas_id:	PAS identifier
 * @mem_region:	allocated memory region to load firmware into
 * @mem_phys:	physical address of allocated memory region
 * @mem_size:	size of the allocated memory region
 * @reloc_base:	adjusted physical address after relocation
 *
 * This function is essentially the same as qcom_mdt_load. The only difference
 * between the two is that the metadata is not freed at the end of this call.
 * The client must call qcom_mdt_free_metadata for cleanup.
 *
 * Returns 0 on success, negative errno otherwise.
 */
int qcom_mdt_load_no_free(struct device *dev, const struct firmware *fw, const char *firmware,
		  int pas_id, void *mem_region, phys_addr_t mem_phys, size_t mem_size,
		  phys_addr_t *reloc_base, bool dma_phys_below_32b,
		  struct qcom_mdt_metadata *metadata)
{
	return __qcom_mdt_load(dev, fw, firmware, pas_id, mem_region, mem_phys,
			       mem_size, reloc_base, true, dma_phys_below_32b, metadata);
}
EXPORT_SYMBOL(qcom_mdt_load_no_free);

/**
 * qcom_mdt_free_metadata() - free the firmware metadata
 * @dev:	device handle to associate resources with
 * @pas_id:	PAS identifier
 * @mdata:	reference to metadata region to be freed
 * @err:	whether this call was made after an error occurred
 *
 * Free the metadata that was allocated by mdt loader.
 *
 */
void qcom_mdt_free_metadata(struct device *dev, int pas_id, struct qcom_mdt_metadata *mdata,
			    bool dma_phys_below_32b, int err)
{
	struct device *scm_dev;

	if (err && qcom_scm_pas_shutdown_retry(pas_id))
		panic("Panicking, failed to shutdown peripheral %d\n", pas_id);
	if (mdata) {
		if (!dma_phys_below_32b) {
			scm_dev = qcom_get_scm_device();
			if (!scm_dev) {
				pr_err("%s: scm_dev has not been created!\n", __func__);
				return;
			}
			dma_free_coherent(scm_dev, mdata->size, mdata->buf, mdata->buf_phys);
		} else {
			dma_free_coherent(dev, mdata->size, mdata->buf, mdata->buf_phys);
		}
	}
}
EXPORT_SYMBOL(qcom_mdt_free_metadata);

MODULE_DESCRIPTION("Firmware parser for Qualcomm MDT format");
MODULE_LICENSE("GPL v2");
