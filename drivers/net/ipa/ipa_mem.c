// SPDX-License-Identifier: GPL-2.0

/* Copyright (c) 2012-2018, The Linux Foundation. All rights reserved.
 * Copyright (C) 2019-2020 Linaro Ltd.
 */

#include <linux/types.h>
#include <linux/bitfield.h>
#include <linux/bug.h>
#include <linux/dma-mapping.h>
#include <linux/iommu.h>
#include <linux/io.h>
#include <linux/soc/qcom/smem.h>

#include "ipa.h"
#include "ipa_reg.h"
#include "ipa_data.h"
#include "ipa_cmd.h"
#include "ipa_mem.h"
#include "ipa_table.h"
#include "gsi_trans.h"

/* "Canary" value placed between memory regions to detect overflow */
#define IPA_MEM_CANARY_VAL		cpu_to_le32(0xdeadbeef)

/* SMEM host id representing the modem. */
#define QCOM_SMEM_HOST_MODEM	1

/* Add an immediate command to a transaction that zeroes a memory region */
static void
ipa_mem_zero_region_add(struct gsi_trans *trans, const struct ipa_mem *mem)
{
	struct ipa *ipa = container_of(trans->gsi, struct ipa, gsi);
	dma_addr_t addr = ipa->zero_addr;

	if (!mem->size)
		return;

	ipa_cmd_dma_shared_mem_add(trans, mem->offset, mem->size, addr, true);
}

/**
 * ipa_mem_setup() - Set up IPA AP and modem shared memory areas
 *
 * Set up the shared memory regions in IPA local memory.  This involves
 * zero-filling memory regions, and in the case of header memory, telling
 * the IPA where it's located.
 *
 * This function performs the initial setup of this memory.  If the modem
 * crashes, its regions are re-zeroed in ipa_mem_zero_modem().
 *
 * The AP informs the modem where its portions of memory are located
 * in a QMI exchange that occurs at modem startup.
 *
 * @Return:	0 if successful, or a negative error code
 */
int ipa_mem_setup(struct ipa *ipa)
{
	dma_addr_t addr = ipa->zero_addr;
	struct gsi_trans *trans;
	u32 offset;
	u16 size;

	/* Get a transaction to define the header memory region and to zero
	 * the processing context and modem memory regions.
	 */
	trans = ipa_cmd_trans_alloc(ipa, 4);
	if (!trans) {
		dev_err(&ipa->pdev->dev, "no transaction for memory setup\n");
		return -EBUSY;
	}

	/* Initialize IPA-local header memory.  The modem and AP header
	 * regions are contiguous, and initialized together.
	 */
	offset = ipa->mem[IPA_MEM_MODEM_HEADER].offset;
	size = ipa->mem[IPA_MEM_MODEM_HEADER].size;
	size += ipa->mem[IPA_MEM_AP_HEADER].size;

	ipa_cmd_hdr_init_local_add(trans, offset, size, addr);

	ipa_mem_zero_region_add(trans, &ipa->mem[IPA_MEM_MODEM_PROC_CTX]);

	ipa_mem_zero_region_add(trans, &ipa->mem[IPA_MEM_AP_PROC_CTX]);

	ipa_mem_zero_region_add(trans, &ipa->mem[IPA_MEM_MODEM]);

	gsi_trans_commit_wait(trans);

	/* Tell the hardware where the processing context area is located */
	iowrite32(ipa->mem_offset + offset,
		  ipa->reg_virt + IPA_REG_LOCAL_PKT_PROC_CNTXT_BASE_OFFSET);

	return 0;
}

void ipa_mem_teardown(struct ipa *ipa)
{
	/* Nothing to do */
}

#ifdef IPA_VALIDATE

static bool ipa_mem_valid(struct ipa *ipa, enum ipa_mem_id mem_id)
{
	const struct ipa_mem *mem = &ipa->mem[mem_id];
	struct device *dev = &ipa->pdev->dev;
	u16 size_multiple;

	/* Other than modem memory, sizes must be a multiple of 8 */
	size_multiple = mem_id == IPA_MEM_MODEM ? 4 : 8;
	if (mem->size % size_multiple)
		dev_err(dev, "region %u size not a multiple of %u bytes\n",
			mem_id, size_multiple);
	else if (mem->offset % 8)
		dev_err(dev, "region %u offset not 8-byte aligned\n", mem_id);
	else if (mem->offset < mem->canary_count * sizeof(__le32))
		dev_err(dev, "region %u offset too small for %hu canaries\n",
			mem_id, mem->canary_count);
	else if (mem->offset + mem->size > ipa->mem_size)
		dev_err(dev, "region %u ends beyond memory limit (0x%08x)\n",
			mem_id, ipa->mem_size);
	else
		return true;

	return false;
}

#else /* !IPA_VALIDATE */

static bool ipa_mem_valid(struct ipa *ipa, enum ipa_mem_id mem_id)
{
	return true;
}

#endif /*! IPA_VALIDATE */

/**
 * ipa_mem_config() - Configure IPA shared memory
 *
 * @Return:	0 if successful, or a negative error code
 */
int ipa_mem_config(struct ipa *ipa)
{
	struct device *dev = &ipa->pdev->dev;
	enum ipa_mem_id mem_id;
	dma_addr_t addr;
	u32 mem_size;
	void *virt;
	u32 val;

	/* Check the advertised location and size of the shared memory area */
	val = ioread32(ipa->reg_virt + IPA_REG_SHARED_MEM_SIZE_OFFSET);

	/* The fields in the register are in 8 byte units */
	ipa->mem_offset = 8 * u32_get_bits(val, SHARED_MEM_BADDR_FMASK);
	/* Make sure the end is within the region's mapped space */
	mem_size = 8 * u32_get_bits(val, SHARED_MEM_SIZE_FMASK);

	/* If the sizes don't match, issue a warning */
	if (ipa->mem_offset + mem_size > ipa->mem_size) {
		dev_warn(dev, "ignoring larger reported memory size: 0x%08x\n",
			mem_size);
	} else if (ipa->mem_offset + mem_size < ipa->mem_size) {
		dev_warn(dev, "limiting IPA memory size to 0x%08x\n",
			 mem_size);
		ipa->mem_size = mem_size;
	}

	/* Prealloc DMA memory for zeroing regions */
	virt = dma_alloc_coherent(dev, IPA_MEM_MAX, &addr, GFP_KERNEL);
	if (!virt)
		return -ENOMEM;
	ipa->zero_addr = addr;
	ipa->zero_virt = virt;
	ipa->zero_size = IPA_MEM_MAX;

	/* Verify each defined memory region is valid, and if indicated
	 * for the region, write "canary" values in the space prior to
	 * the region's base address.
	 */
	for (mem_id = 0; mem_id < IPA_MEM_COUNT; mem_id++) {
		const struct ipa_mem *mem = &ipa->mem[mem_id];
		u16 canary_count;
		__le32 *canary;

		/* Validate all regions (even undefined ones) */
		if (!ipa_mem_valid(ipa, mem_id))
			goto err_dma_free;

		/* Skip over undefined regions */
		if (!mem->offset && !mem->size)
			continue;

		canary_count = mem->canary_count;
		if (!canary_count)
			continue;

		/* Write canary values in the space before the region */
		canary = ipa->mem_virt + ipa->mem_offset + mem->offset;
		do
			*--canary = IPA_MEM_CANARY_VAL;
		while (--canary_count);
	}

	/* Make sure filter and route table memory regions are valid */
	if (!ipa_table_valid(ipa))
		goto err_dma_free;

	/* Validate memory-related properties relevant to immediate commands */
	if (!ipa_cmd_data_valid(ipa))
		goto err_dma_free;

	/* Verify the microcontroller ring alignment (0 is OK too) */
	if (ipa->mem[IPA_MEM_UC_EVENT_RING].offset % 1024) {
		dev_err(dev, "microcontroller ring not 1024-byte aligned\n");
		goto err_dma_free;
	}

	return 0;

err_dma_free:
	dma_free_coherent(dev, IPA_MEM_MAX, ipa->zero_virt, ipa->zero_addr);

	return -EINVAL;
}

/* Inverse of ipa_mem_config() */
void ipa_mem_deconfig(struct ipa *ipa)
{
	struct device *dev = &ipa->pdev->dev;

	dma_free_coherent(dev, ipa->zero_size, ipa->zero_virt, ipa->zero_addr);
	ipa->zero_size = 0;
	ipa->zero_virt = NULL;
	ipa->zero_addr = 0;
}

/**
 * ipa_mem_zero_modem() - Zero IPA-local memory regions owned by the modem
 *
 * Zero regions of IPA-local memory used by the modem.  These are configured
 * (and initially zeroed) by ipa_mem_setup(), but if the modem crashes and
 * restarts via SSR we need to re-initialize them.  A QMI message tells the
 * modem where to find regions of IPA local memory it needs to know about
 * (these included).
 */
int ipa_mem_zero_modem(struct ipa *ipa)
{
	struct gsi_trans *trans;

	/* Get a transaction to zero the modem memory, modem header,
	 * and modem processing context regions.
	 */
	trans = ipa_cmd_trans_alloc(ipa, 3);
	if (!trans) {
		dev_err(&ipa->pdev->dev,
			"no transaction to zero modem memory\n");
		return -EBUSY;
	}

	ipa_mem_zero_region_add(trans, &ipa->mem[IPA_MEM_MODEM_HEADER]);

	ipa_mem_zero_region_add(trans, &ipa->mem[IPA_MEM_MODEM_PROC_CTX]);

	ipa_mem_zero_region_add(trans, &ipa->mem[IPA_MEM_MODEM]);

	gsi_trans_commit_wait(trans);

	return 0;
}

/**
 * ipa_imem_init() - Initialize IMEM memory used by the IPA
 * @ipa:	IPA pointer
 * @addr:	Physical address of the IPA region in IMEM
 * @size:	Size (bytes) of the IPA region in IMEM
 *
 * IMEM is a block of shared memory separate from system DRAM, and
 * a portion of this memory is available for the IPA to use.  The
 * modem accesses this memory directly, but the IPA accesses it
 * via the IOMMU, using the AP's credentials.
 *
 * If this region exists (size > 0) we map it for read/write access
 * through the IOMMU using the IPA device.
 *
 * Note: @addr and @size are not guaranteed to be page-aligned.
 */
static int ipa_imem_init(struct ipa *ipa, unsigned long addr, size_t size)
{
	struct device *dev = &ipa->pdev->dev;
	struct iommu_domain *domain;
	unsigned long iova;
	phys_addr_t phys;
	int ret;

	if (!size)
		return 0;	/* IMEM memory not used */

	domain = iommu_get_domain_for_dev(dev);
	if (!domain) {
		dev_err(dev, "no IOMMU domain found for IMEM\n");
		return -EINVAL;
	}

	/* Align the address down and the size up to page boundaries */
	phys = addr & PAGE_MASK;
	size = PAGE_ALIGN(size + addr - phys);
	iova = phys;	/* We just want a direct mapping */

	ret = iommu_map(domain, iova, phys, size, IOMMU_READ | IOMMU_WRITE);
	if (ret)
		return ret;

	ipa->imem_iova = iova;
	ipa->imem_size = size;

	return 0;
}

static void ipa_imem_exit(struct ipa *ipa)
{
	struct iommu_domain *domain;
	struct device *dev;

	if (!ipa->imem_size)
		return;

	dev = &ipa->pdev->dev;
	domain = iommu_get_domain_for_dev(dev);
	if (domain) {
		size_t size;

		size = iommu_unmap(domain, ipa->imem_iova, ipa->imem_size);
		if (size != ipa->imem_size)
			dev_warn(dev, "unmapped %zu IMEM bytes, expected %lu\n",
				 size, ipa->imem_size);
	} else {
		dev_err(dev, "couldn't get IPA IOMMU domain for IMEM\n");
	}

	ipa->imem_size = 0;
	ipa->imem_iova = 0;
}

/**
 * ipa_smem_init() - Initialize SMEM memory used by the IPA
 * @ipa:	IPA pointer
 * @item:	Item ID of SMEM memory
 * @size:	Size (bytes) of SMEM memory region
 *
 * SMEM is a managed block of shared DRAM, from which numbered "items"
 * can be allocated.  One item is designated for use by the IPA.
 *
 * The modem accesses SMEM memory directly, but the IPA accesses it
 * via the IOMMU, using the AP's credentials.
 *
 * If size provided is non-zero, we allocate it and map it for
 * access through the IOMMU.
 *
 * Note: @size and the item address are is not guaranteed to be page-aligned.
 */
static int ipa_smem_init(struct ipa *ipa, u32 item, size_t size)
{
	struct device *dev = &ipa->pdev->dev;
	struct iommu_domain *domain;
	unsigned long iova;
	phys_addr_t phys;
	phys_addr_t addr;
	size_t actual;
	void *virt;
	int ret;

	if (!size)
		return 0;	/* SMEM memory not used */

	/* SMEM is memory shared between the AP and another system entity
	 * (in this case, the modem).  An allocation from SMEM is persistent
	 * until the AP reboots; there is no way to free an allocated SMEM
	 * region.  Allocation only reserves the space; to use it you need
	 * to "get" a pointer it (this implies no reference counting).
	 * The item might have already been allocated, in which case we
	 * use it unless the size isn't what we expect.
	 */
	ret = qcom_smem_alloc(QCOM_SMEM_HOST_MODEM, item, size);
	if (ret && ret != -EEXIST) {
		dev_err(dev, "error %d allocating size %zu SMEM item %u\n",
			ret, size, item);
		return ret;
	}

	/* Now get the address of the SMEM memory region */
	virt = qcom_smem_get(QCOM_SMEM_HOST_MODEM, item, &actual);
	if (IS_ERR(virt)) {
		ret = PTR_ERR(virt);
		dev_err(dev, "error %d getting SMEM item %u\n", ret, item);
		return ret;
	}

	/* In case the region was already allocated, verify the size */
	if (ret && actual != size) {
		dev_err(dev, "SMEM item %u has size %zu, expected %zu\n",
			item, actual, size);
		return -EINVAL;
	}

	domain = iommu_get_domain_for_dev(dev);
	if (!domain) {
		dev_err(dev, "no IOMMU domain found for SMEM\n");
		return -EINVAL;
	}

	/* Align the address down and the size up to a page boundary */
	addr = qcom_smem_virt_to_phys(virt) & PAGE_MASK;
	phys = addr & PAGE_MASK;
	size = PAGE_ALIGN(size + addr - phys);
	iova = phys;	/* We just want a direct mapping */

	ret = iommu_map(domain, iova, phys, size, IOMMU_READ | IOMMU_WRITE);
	if (ret)
		return ret;

	ipa->smem_iova = iova;
	ipa->smem_size = size;

	return 0;
}

static void ipa_smem_exit(struct ipa *ipa)
{
	struct device *dev = &ipa->pdev->dev;
	struct iommu_domain *domain;

	domain = iommu_get_domain_for_dev(dev);
	if (domain) {
		size_t size;

		size = iommu_unmap(domain, ipa->smem_iova, ipa->smem_size);
		if (size != ipa->smem_size)
			dev_warn(dev, "unmapped %zu SMEM bytes, expected %lu\n",
				 size, ipa->smem_size);

	} else {
		dev_err(dev, "couldn't get IPA IOMMU domain for SMEM\n");
	}

	ipa->smem_size = 0;
	ipa->smem_iova = 0;
}

/* Perform memory region-related initialization */
int ipa_mem_init(struct ipa *ipa, const struct ipa_mem_data *mem_data)
{
	struct device *dev = &ipa->pdev->dev;
	struct resource *res;
	int ret;

	if (mem_data->local_count > IPA_MEM_COUNT) {
		dev_err(dev, "to many memory regions (%u > %u)\n",
			mem_data->local_count, IPA_MEM_COUNT);
		return -EINVAL;
	}

	ret = dma_set_mask_and_coherent(&ipa->pdev->dev, DMA_BIT_MASK(64));
	if (ret) {
		dev_err(dev, "error %d setting DMA mask\n", ret);
		return ret;
	}

	res = platform_get_resource_byname(ipa->pdev, IORESOURCE_MEM,
					   "ipa-shared");
	if (!res) {
		dev_err(dev,
			"DT error getting \"ipa-shared\" memory property\n");
		return -ENODEV;
	}

	ipa->mem_virt = memremap(res->start, resource_size(res), MEMREMAP_WC);
	if (!ipa->mem_virt) {
		dev_err(dev, "unable to remap \"ipa-shared\" memory\n");
		return -ENOMEM;
	}

	ipa->mem_addr = res->start;
	ipa->mem_size = resource_size(res);

	/* The ipa->mem[] array is indexed by enum ipa_mem_id values */
	ipa->mem = mem_data->local;

	ret = ipa_imem_init(ipa, mem_data->imem_addr, mem_data->imem_size);
	if (ret)
		goto err_unmap;

	ret = ipa_smem_init(ipa, mem_data->smem_id, mem_data->smem_size);
	if (ret)
		goto err_imem_exit;

	return 0;

err_imem_exit:
	ipa_imem_exit(ipa);
err_unmap:
	memunmap(ipa->mem_virt);

	return ret;
}

/* Inverse of ipa_mem_init() */
void ipa_mem_exit(struct ipa *ipa)
{
	ipa_smem_exit(ipa);
	ipa_imem_exit(ipa);
	memunmap(ipa->mem_virt);
}
