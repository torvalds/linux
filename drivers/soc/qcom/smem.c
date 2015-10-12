/*
 * Copyright (c) 2015, Sony Mobile Communications AB.
 * Copyright (c) 2012-2013, The Linux Foundation. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 and
 * only version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <linux/hwspinlock.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/soc/qcom/smem.h>

/*
 * The Qualcomm shared memory system is a allocate only heap structure that
 * consists of one of more memory areas that can be accessed by the processors
 * in the SoC.
 *
 * All systems contains a global heap, accessible by all processors in the SoC,
 * with a table of contents data structure (@smem_header) at the beginning of
 * the main shared memory block.
 *
 * The global header contains meta data for allocations as well as a fixed list
 * of 512 entries (@smem_global_entry) that can be initialized to reference
 * parts of the shared memory space.
 *
 *
 * In addition to this global heap a set of "private" heaps can be set up at
 * boot time with access restrictions so that only certain processor pairs can
 * access the data.
 *
 * These partitions are referenced from an optional partition table
 * (@smem_ptable), that is found 4kB from the end of the main smem region. The
 * partition table entries (@smem_ptable_entry) lists the involved processors
 * (or hosts) and their location in the main shared memory region.
 *
 * Each partition starts with a header (@smem_partition_header) that identifies
 * the partition and holds properties for the two internal memory regions. The
 * two regions are cached and non-cached memory respectively. Each region
 * contain a link list of allocation headers (@smem_private_entry) followed by
 * their data.
 *
 * Items in the non-cached region are allocated from the start of the partition
 * while items in the cached region are allocated from the end. The free area
 * is hence the region between the cached and non-cached offsets.
 *
 *
 * To synchronize allocations in the shared memory heaps a remote spinlock must
 * be held - currently lock number 3 of the sfpb or tcsr is used for this on all
 * platforms.
 *
 */

/*
 * Item 3 of the global heap contains an array of versions for the various
 * software components in the SoC. We verify that the boot loader version is
 * what the expected version (SMEM_EXPECTED_VERSION) as a sanity check.
 */
#define SMEM_ITEM_VERSION	3
#define  SMEM_MASTER_SBL_VERSION_INDEX	7
#define  SMEM_EXPECTED_VERSION		11

/*
 * The first 8 items are only to be allocated by the boot loader while
 * initializing the heap.
 */
#define SMEM_ITEM_LAST_FIXED	8

/* Highest accepted item number, for both global and private heaps */
#define SMEM_ITEM_COUNT		512

/* Processor/host identifier for the application processor */
#define SMEM_HOST_APPS		0

/* Max number of processors/hosts in a system */
#define SMEM_HOST_COUNT		9

/**
  * struct smem_proc_comm - proc_comm communication struct (legacy)
  * @command:	current command to be executed
  * @status:	status of the currently requested command
  * @params:	parameters to the command
  */
struct smem_proc_comm {
	u32 command;
	u32 status;
	u32 params[2];
};

/**
 * struct smem_global_entry - entry to reference smem items on the heap
 * @allocated:	boolean to indicate if this entry is used
 * @offset:	offset to the allocated space
 * @size:	size of the allocated space, 8 byte aligned
 * @aux_base:	base address for the memory region used by this unit, or 0 for
 *		the default region. bits 0,1 are reserved
 */
struct smem_global_entry {
	u32 allocated;
	u32 offset;
	u32 size;
	u32 aux_base; /* bits 1:0 reserved */
};
#define AUX_BASE_MASK		0xfffffffc

/**
 * struct smem_header - header found in beginning of primary smem region
 * @proc_comm:		proc_comm communication interface (legacy)
 * @version:		array of versions for the various subsystems
 * @initialized:	boolean to indicate that smem is initialized
 * @free_offset:	index of the first unallocated byte in smem
 * @available:		number of bytes available for allocation
 * @reserved:		reserved field, must be 0
 * toc:			array of references to items
 */
struct smem_header {
	struct smem_proc_comm proc_comm[4];
	u32 version[32];
	u32 initialized;
	u32 free_offset;
	u32 available;
	u32 reserved;
	struct smem_global_entry toc[SMEM_ITEM_COUNT];
};

/**
 * struct smem_ptable_entry - one entry in the @smem_ptable list
 * @offset:	offset, within the main shared memory region, of the partition
 * @size:	size of the partition
 * @flags:	flags for the partition (currently unused)
 * @host0:	first processor/host with access to this partition
 * @host1:	second processor/host with access to this partition
 * @reserved:	reserved entries for later use
 */
struct smem_ptable_entry {
	u32 offset;
	u32 size;
	u32 flags;
	u16 host0;
	u16 host1;
	u32 reserved[8];
};

/**
 * struct smem_ptable - partition table for the private partitions
 * @magic:	magic number, must be SMEM_PTABLE_MAGIC
 * @version:	version of the partition table
 * @num_entries: number of partitions in the table
 * @reserved:	for now reserved entries
 * @entry:	list of @smem_ptable_entry for the @num_entries partitions
 */
struct smem_ptable {
	u32 magic;
	u32 version;
	u32 num_entries;
	u32 reserved[5];
	struct smem_ptable_entry entry[];
};
#define SMEM_PTABLE_MAGIC	0x434f5424 /* "$TOC" */

/**
 * struct smem_partition_header - header of the partitions
 * @magic:	magic number, must be SMEM_PART_MAGIC
 * @host0:	first processor/host with access to this partition
 * @host1:	second processor/host with access to this partition
 * @size:	size of the partition
 * @offset_free_uncached: offset to the first free byte of uncached memory in
 *		this partition
 * @offset_free_cached: offset to the first free byte of cached memory in this
 *		partition
 * @reserved:	for now reserved entries
 */
struct smem_partition_header {
	u32 magic;
	u16 host0;
	u16 host1;
	u32 size;
	u32 offset_free_uncached;
	u32 offset_free_cached;
	u32 reserved[3];
};
#define SMEM_PART_MAGIC		0x54525024 /* "$PRT" */

/**
 * struct smem_private_entry - header of each item in the private partition
 * @canary:	magic number, must be SMEM_PRIVATE_CANARY
 * @item:	identifying number of the smem item
 * @size:	size of the data, including padding bytes
 * @padding_data: number of bytes of padding of data
 * @padding_hdr: number of bytes of padding between the header and the data
 * @reserved:	for now reserved entry
 */
struct smem_private_entry {
	u16 canary;
	u16 item;
	u32 size; /* includes padding bytes */
	u16 padding_data;
	u16 padding_hdr;
	u32 reserved;
};
#define SMEM_PRIVATE_CANARY	0xa5a5

/**
 * struct smem_region - representation of a chunk of memory used for smem
 * @aux_base:	identifier of aux_mem base
 * @virt_base:	virtual base address of memory with this aux_mem identifier
 * @size:	size of the memory region
 */
struct smem_region {
	u32 aux_base;
	void __iomem *virt_base;
	size_t size;
};

/**
 * struct qcom_smem - device data for the smem device
 * @dev:	device pointer
 * @hwlock:	reference to a hwspinlock
 * @partitions:	list of pointers to partitions affecting the current
 *		processor/host
 * @num_regions: number of @regions
 * @regions:	list of the memory regions defining the shared memory
 */
struct qcom_smem {
	struct device *dev;

	struct hwspinlock *hwlock;

	struct smem_partition_header *partitions[SMEM_HOST_COUNT];

	unsigned num_regions;
	struct smem_region regions[0];
};

/* Pointer to the one and only smem handle */
static struct qcom_smem *__smem;

/* Timeout (ms) for the trylock of remote spinlocks */
#define HWSPINLOCK_TIMEOUT	1000

static int qcom_smem_alloc_private(struct qcom_smem *smem,
				   unsigned host,
				   unsigned item,
				   size_t size)
{
	struct smem_partition_header *phdr;
	struct smem_private_entry *hdr;
	size_t alloc_size;
	void *p;

	phdr = smem->partitions[host];

	p = (void *)phdr + sizeof(*phdr);
	while (p < (void *)phdr + phdr->offset_free_uncached) {
		hdr = p;

		if (hdr->canary != SMEM_PRIVATE_CANARY) {
			dev_err(smem->dev,
				"Found invalid canary in host %d partition\n",
				host);
			return -EINVAL;
		}

		if (hdr->item == item)
			return -EEXIST;

		p += sizeof(*hdr) + hdr->padding_hdr + hdr->size;
	}

	/* Check that we don't grow into the cached region */
	alloc_size = sizeof(*hdr) + ALIGN(size, 8);
	if (p + alloc_size >= (void *)phdr + phdr->offset_free_cached) {
		dev_err(smem->dev, "Out of memory\n");
		return -ENOSPC;
	}

	hdr = p;
	hdr->canary = SMEM_PRIVATE_CANARY;
	hdr->item = item;
	hdr->size = ALIGN(size, 8);
	hdr->padding_data = hdr->size - size;
	hdr->padding_hdr = 0;

	/*
	 * Ensure the header is written before we advance the free offset, so
	 * that remote processors that does not take the remote spinlock still
	 * gets a consistent view of the linked list.
	 */
	wmb();
	phdr->offset_free_uncached += alloc_size;

	return 0;
}

static int qcom_smem_alloc_global(struct qcom_smem *smem,
				  unsigned item,
				  size_t size)
{
	struct smem_header *header;
	struct smem_global_entry *entry;

	if (WARN_ON(item >= SMEM_ITEM_COUNT))
		return -EINVAL;

	header = smem->regions[0].virt_base;
	entry = &header->toc[item];
	if (entry->allocated)
		return -EEXIST;

	size = ALIGN(size, 8);
	if (WARN_ON(size > header->available))
		return -ENOMEM;

	entry->offset = header->free_offset;
	entry->size = size;

	/*
	 * Ensure the header is consistent before we mark the item allocated,
	 * so that remote processors will get a consistent view of the item
	 * even though they do not take the spinlock on read.
	 */
	wmb();
	entry->allocated = 1;

	header->free_offset += size;
	header->available -= size;

	return 0;
}

/**
 * qcom_smem_alloc() - allocate space for a smem item
 * @host:	remote processor id, or -1
 * @item:	smem item handle
 * @size:	number of bytes to be allocated
 *
 * Allocate space for a given smem item of size @size, given that the item is
 * not yet allocated.
 */
int qcom_smem_alloc(unsigned host, unsigned item, size_t size)
{
	unsigned long flags;
	int ret;

	if (!__smem)
		return -EPROBE_DEFER;

	if (item < SMEM_ITEM_LAST_FIXED) {
		dev_err(__smem->dev,
			"Rejecting allocation of static entry %d\n", item);
		return -EINVAL;
	}

	ret = hwspin_lock_timeout_irqsave(__smem->hwlock,
					  HWSPINLOCK_TIMEOUT,
					  &flags);
	if (ret)
		return ret;

	if (host < SMEM_HOST_COUNT && __smem->partitions[host])
		ret = qcom_smem_alloc_private(__smem, host, item, size);
	else
		ret = qcom_smem_alloc_global(__smem, item, size);

	hwspin_unlock_irqrestore(__smem->hwlock, &flags);

	return ret;
}
EXPORT_SYMBOL(qcom_smem_alloc);

static int qcom_smem_get_global(struct qcom_smem *smem,
				unsigned item,
				void **ptr,
				size_t *size)
{
	struct smem_header *header;
	struct smem_region *area;
	struct smem_global_entry *entry;
	u32 aux_base;
	unsigned i;

	if (WARN_ON(item >= SMEM_ITEM_COUNT))
		return -EINVAL;

	header = smem->regions[0].virt_base;
	entry = &header->toc[item];
	if (!entry->allocated)
		return -ENXIO;

	if (ptr != NULL) {
		aux_base = entry->aux_base & AUX_BASE_MASK;

		for (i = 0; i < smem->num_regions; i++) {
			area = &smem->regions[i];

			if (area->aux_base == aux_base || !aux_base) {
				*ptr = area->virt_base + entry->offset;
				break;
			}
		}
	}
	if (size != NULL)
		*size = entry->size;

	return 0;
}

static int qcom_smem_get_private(struct qcom_smem *smem,
				 unsigned host,
				 unsigned item,
				 void **ptr,
				 size_t *size)
{
	struct smem_partition_header *phdr;
	struct smem_private_entry *hdr;
	void *p;

	phdr = smem->partitions[host];

	p = (void *)phdr + sizeof(*phdr);
	while (p < (void *)phdr + phdr->offset_free_uncached) {
		hdr = p;

		if (hdr->canary != SMEM_PRIVATE_CANARY) {
			dev_err(smem->dev,
				"Found invalid canary in host %d partition\n",
				host);
			return -EINVAL;
		}

		if (hdr->item == item) {
			if (ptr != NULL)
				*ptr = p + sizeof(*hdr) + hdr->padding_hdr;

			if (size != NULL)
				*size = hdr->size - hdr->padding_data;

			return 0;
		}

		p += sizeof(*hdr) + hdr->padding_hdr + hdr->size;
	}

	return -ENOENT;
}

/**
 * qcom_smem_get() - resolve ptr of size of a smem item
 * @host:	the remote processor, or -1
 * @item:	smem item handle
 * @ptr:	pointer to be filled out with address of the item
 * @size:	pointer to be filled out with size of the item
 *
 * Looks up pointer and size of a smem item.
 */
int qcom_smem_get(unsigned host, unsigned item, void **ptr, size_t *size)
{
	unsigned long flags;
	int ret;

	if (!__smem)
		return -EPROBE_DEFER;

	ret = hwspin_lock_timeout_irqsave(__smem->hwlock,
					  HWSPINLOCK_TIMEOUT,
					  &flags);
	if (ret)
		return ret;

	if (host < SMEM_HOST_COUNT && __smem->partitions[host])
		ret = qcom_smem_get_private(__smem, host, item, ptr, size);
	else
		ret = qcom_smem_get_global(__smem, item, ptr, size);

	hwspin_unlock_irqrestore(__smem->hwlock, &flags);
	return ret;

}
EXPORT_SYMBOL(qcom_smem_get);

/**
 * qcom_smem_get_free_space() - retrieve amount of free space in a partition
 * @host:	the remote processor identifying a partition, or -1
 *
 * To be used by smem clients as a quick way to determine if any new
 * allocations has been made.
 */
int qcom_smem_get_free_space(unsigned host)
{
	struct smem_partition_header *phdr;
	struct smem_header *header;
	unsigned ret;

	if (!__smem)
		return -EPROBE_DEFER;

	if (host < SMEM_HOST_COUNT && __smem->partitions[host]) {
		phdr = __smem->partitions[host];
		ret = phdr->offset_free_cached - phdr->offset_free_uncached;
	} else {
		header = __smem->regions[0].virt_base;
		ret = header->available;
	}

	return ret;
}
EXPORT_SYMBOL(qcom_smem_get_free_space);

static int qcom_smem_get_sbl_version(struct qcom_smem *smem)
{
	unsigned *versions;
	size_t size;
	int ret;

	ret = qcom_smem_get_global(smem, SMEM_ITEM_VERSION,
				   (void **)&versions, &size);
	if (ret < 0) {
		dev_err(smem->dev, "Unable to read the version item\n");
		return -ENOENT;
	}

	if (size < sizeof(unsigned) * SMEM_MASTER_SBL_VERSION_INDEX) {
		dev_err(smem->dev, "Version item is too small\n");
		return -EINVAL;
	}

	return versions[SMEM_MASTER_SBL_VERSION_INDEX];
}

static int qcom_smem_enumerate_partitions(struct qcom_smem *smem,
					  unsigned local_host)
{
	struct smem_partition_header *header;
	struct smem_ptable_entry *entry;
	struct smem_ptable *ptable;
	unsigned remote_host;
	int i;

	ptable = smem->regions[0].virt_base + smem->regions[0].size - SZ_4K;
	if (ptable->magic != SMEM_PTABLE_MAGIC)
		return 0;

	if (ptable->version != 1) {
		dev_err(smem->dev,
			"Unsupported partition header version %d\n",
			ptable->version);
		return -EINVAL;
	}

	for (i = 0; i < ptable->num_entries; i++) {
		entry = &ptable->entry[i];

		if (entry->host0 != local_host && entry->host1 != local_host)
			continue;

		if (!entry->offset)
			continue;

		if (!entry->size)
			continue;

		if (entry->host0 == local_host)
			remote_host = entry->host1;
		else
			remote_host = entry->host0;

		if (remote_host >= SMEM_HOST_COUNT) {
			dev_err(smem->dev,
				"Invalid remote host %d\n",
				remote_host);
			return -EINVAL;
		}

		if (smem->partitions[remote_host]) {
			dev_err(smem->dev,
				"Already found a partition for host %d\n",
				remote_host);
			return -EINVAL;
		}

		header = smem->regions[0].virt_base + entry->offset;

		if (header->magic != SMEM_PART_MAGIC) {
			dev_err(smem->dev,
				"Partition %d has invalid magic\n", i);
			return -EINVAL;
		}

		if (header->host0 != local_host && header->host1 != local_host) {
			dev_err(smem->dev,
				"Partition %d hosts are invalid\n", i);
			return -EINVAL;
		}

		if (header->host0 != remote_host && header->host1 != remote_host) {
			dev_err(smem->dev,
				"Partition %d hosts are invalid\n", i);
			return -EINVAL;
		}

		if (header->size != entry->size) {
			dev_err(smem->dev,
				"Partition %d has invalid size\n", i);
			return -EINVAL;
		}

		if (header->offset_free_uncached > header->size) {
			dev_err(smem->dev,
				"Partition %d has invalid free pointer\n", i);
			return -EINVAL;
		}

		smem->partitions[remote_host] = header;
	}

	return 0;
}

static int qcom_smem_count_mem_regions(struct platform_device *pdev)
{
	struct resource *res;
	int num_regions = 0;
	int i;

	for (i = 0; i < pdev->num_resources; i++) {
		res = &pdev->resource[i];

		if (resource_type(res) == IORESOURCE_MEM)
			num_regions++;
	}

	return num_regions;
}

static int qcom_smem_probe(struct platform_device *pdev)
{
	struct smem_header *header;
	struct device_node *np;
	struct qcom_smem *smem;
	struct resource *res;
	struct resource r;
	size_t array_size;
	int num_regions = 0;
	int hwlock_id;
	u32 version;
	int ret;
	int i;

	num_regions = qcom_smem_count_mem_regions(pdev) + 1;

	array_size = num_regions * sizeof(struct smem_region);
	smem = devm_kzalloc(&pdev->dev, sizeof(*smem) + array_size, GFP_KERNEL);
	if (!smem)
		return -ENOMEM;

	smem->dev = &pdev->dev;
	smem->num_regions = num_regions;

	np = of_parse_phandle(pdev->dev.of_node, "memory-region", 0);
	if (!np) {
		dev_err(&pdev->dev, "No memory-region specified\n");
		return -EINVAL;
	}

	ret = of_address_to_resource(np, 0, &r);
	of_node_put(np);
	if (ret)
		return ret;

	smem->regions[0].aux_base = (u32)r.start;
	smem->regions[0].size = resource_size(&r);
	smem->regions[0].virt_base = devm_ioremap_nocache(&pdev->dev,
							  r.start,
							  resource_size(&r));
	if (!smem->regions[0].virt_base)
		return -ENOMEM;

	for (i = 1; i < num_regions; i++) {
		res = platform_get_resource(pdev, IORESOURCE_MEM, i - 1);

		smem->regions[i].aux_base = (u32)res->start;
		smem->regions[i].size = resource_size(res);
		smem->regions[i].virt_base = devm_ioremap_nocache(&pdev->dev,
								  res->start,
								  resource_size(res));
		if (!smem->regions[i].virt_base)
			return -ENOMEM;
	}

	header = smem->regions[0].virt_base;
	if (header->initialized != 1 || header->reserved) {
		dev_err(&pdev->dev, "SMEM is not initialized by SBL\n");
		return -EINVAL;
	}

	version = qcom_smem_get_sbl_version(smem);
	if (version >> 16 != SMEM_EXPECTED_VERSION) {
		dev_err(&pdev->dev, "Unsupported SMEM version 0x%x\n", version);
		return -EINVAL;
	}

	ret = qcom_smem_enumerate_partitions(smem, SMEM_HOST_APPS);
	if (ret < 0)
		return ret;

	hwlock_id = of_hwspin_lock_get_id(pdev->dev.of_node, 0);
	if (hwlock_id < 0) {
		dev_err(&pdev->dev, "failed to retrieve hwlock\n");
		return hwlock_id;
	}

	smem->hwlock = hwspin_lock_request_specific(hwlock_id);
	if (!smem->hwlock)
		return -ENXIO;

	__smem = smem;

	return 0;
}

static int qcom_smem_remove(struct platform_device *pdev)
{
	__smem = NULL;
	hwspin_lock_free(__smem->hwlock);

	return 0;
}

static const struct of_device_id qcom_smem_of_match[] = {
	{ .compatible = "qcom,smem" },
	{}
};
MODULE_DEVICE_TABLE(of, qcom_smem_of_match);

static struct platform_driver qcom_smem_driver = {
	.probe = qcom_smem_probe,
	.remove = qcom_smem_remove,
	.driver  = {
		.name = "qcom-smem",
		.of_match_table = qcom_smem_of_match,
		.suppress_bind_attrs = true,
	},
};

static int __init qcom_smem_init(void)
{
	return platform_driver_register(&qcom_smem_driver);
}
arch_initcall(qcom_smem_init);

static void __exit qcom_smem_exit(void)
{
	platform_driver_unregister(&qcom_smem_driver);
}
module_exit(qcom_smem_exit)

MODULE_AUTHOR("Bjorn Andersson <bjorn.andersson@sonymobile.com>");
MODULE_DESCRIPTION("Qualcomm Shared Memory Manager");
MODULE_LICENSE("GPL v2");
