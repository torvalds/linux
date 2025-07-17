// SPDX-License-Identifier: GPL-2.0

/* Copyright (c) 2012-2018, The Linux Foundation. All rights reserved.
 * Copyright (C) 2019-2024 Linaro Ltd.
 */

#include <linux/dma-mapping.h>
#include <linux/io.h>
#include <linux/iommu.h>
#include <linux/platform_device.h>
#include <linux/types.h>

#include <linux/soc/qcom/smem.h>

#include "gsi_trans.h"
#include "ipa.h"
#include "ipa_cmd.h"
#include "ipa_data.h"
#include "ipa_mem.h"
#include "ipa_reg.h"
#include "ipa_table.h"

/* "Canary" value placed between memory regions to detect overflow */
#define IPA_MEM_CANARY_VAL		cpu_to_le32(0xdeadbeef)

/* SMEM host id representing the modem. */
#define QCOM_SMEM_HOST_MODEM	1

#define SMEM_IPA_FILTER_TABLE	497

const struct ipa_mem *ipa_mem_find(struct ipa *ipa, enum ipa_mem_id mem_id)
{
	u32 i;

	for (i = 0; i < ipa->mem_count; i++) {
		const struct ipa_mem *mem = &ipa->mem[i];

		if (mem->id == mem_id)
			return mem;
	}

	return NULL;
}

/* Add an immediate command to a transaction that zeroes a memory region */
static void
ipa_mem_zero_region_add(struct gsi_trans *trans, enum ipa_mem_id mem_id)
{
	struct ipa *ipa = container_of(trans->gsi, struct ipa, gsi);
	const struct ipa_mem *mem = ipa_mem_find(ipa, mem_id);
	dma_addr_t addr = ipa->zero_addr;

	if (!mem->size)
		return;

	ipa_cmd_dma_shared_mem_add(trans, mem->offset, mem->size, addr, true);
}

/**
 * ipa_mem_setup() - Set up IPA AP and modem shared memory areas
 * @ipa:	IPA pointer
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
 * There is no need for a matching ipa_mem_teardown() function.
 *
 * Return:	0 if successful, or a negative error code
 */
int ipa_mem_setup(struct ipa *ipa)
{
	dma_addr_t addr = ipa->zero_addr;
	const struct ipa_mem *mem;
	struct gsi_trans *trans;
	const struct reg *reg;
	u32 offset;
	u16 size;
	u32 val;

	/* Get a transaction to define the header memory region and to zero
	 * the processing context and modem memory regions.
	 */
	trans = ipa_cmd_trans_alloc(ipa, 4);
	if (!trans) {
		dev_err(ipa->dev, "no transaction for memory setup\n");
		return -EBUSY;
	}

	/* Initialize IPA-local header memory.  The AP header region, if
	 * present, is contiguous with and follows the modem header region,
	 * and they are initialized together.
	 */
	mem = ipa_mem_find(ipa, IPA_MEM_MODEM_HEADER);
	offset = mem->offset;
	size = mem->size;
	mem = ipa_mem_find(ipa, IPA_MEM_AP_HEADER);
	if (mem)
		size += mem->size;

	ipa_cmd_hdr_init_local_add(trans, offset, size, addr);

	ipa_mem_zero_region_add(trans, IPA_MEM_MODEM_PROC_CTX);
	ipa_mem_zero_region_add(trans, IPA_MEM_AP_PROC_CTX);
	ipa_mem_zero_region_add(trans, IPA_MEM_MODEM);

	gsi_trans_commit_wait(trans);

	/* Tell the hardware where the processing context area is located */
	mem = ipa_mem_find(ipa, IPA_MEM_MODEM_PROC_CTX);
	offset = ipa->mem_offset + mem->offset;

	reg = ipa_reg(ipa, LOCAL_PKT_PROC_CNTXT);
	val = reg_encode(reg, IPA_BASE_ADDR, offset);
	iowrite32(val, ipa->reg_virt + reg_offset(reg));

	return 0;
}

/* Is the given memory region ID is valid for the current IPA version? */
static bool ipa_mem_id_valid(struct ipa *ipa, enum ipa_mem_id mem_id)
{
	enum ipa_version version = ipa->version;

	switch (mem_id) {
	case IPA_MEM_UC_SHARED:
	case IPA_MEM_UC_INFO:
	case IPA_MEM_V4_FILTER_HASHED:
	case IPA_MEM_V4_FILTER:
	case IPA_MEM_V6_FILTER_HASHED:
	case IPA_MEM_V6_FILTER:
	case IPA_MEM_V4_ROUTE_HASHED:
	case IPA_MEM_V4_ROUTE:
	case IPA_MEM_V6_ROUTE_HASHED:
	case IPA_MEM_V6_ROUTE:
	case IPA_MEM_MODEM_HEADER:
	case IPA_MEM_AP_HEADER:
	case IPA_MEM_MODEM_PROC_CTX:
	case IPA_MEM_AP_PROC_CTX:
	case IPA_MEM_MODEM:
	case IPA_MEM_UC_EVENT_RING:
	case IPA_MEM_PDN_CONFIG:
	case IPA_MEM_STATS_QUOTA_MODEM:
	case IPA_MEM_STATS_QUOTA_AP:
	case IPA_MEM_END_MARKER:	/* pseudo region */
		break;

	case IPA_MEM_STATS_TETHERING:
	case IPA_MEM_STATS_DROP:
		if (version < IPA_VERSION_4_0)
			return false;
		break;

	case IPA_MEM_STATS_V4_FILTER:
	case IPA_MEM_STATS_V6_FILTER:
	case IPA_MEM_STATS_V4_ROUTE:
	case IPA_MEM_STATS_V6_ROUTE:
		if (version < IPA_VERSION_4_0 || version > IPA_VERSION_4_2)
			return false;
		break;

	case IPA_MEM_AP_V4_FILTER:
	case IPA_MEM_AP_V6_FILTER:
		if (version < IPA_VERSION_5_0)
			return false;
		break;

	case IPA_MEM_NAT_TABLE:
	case IPA_MEM_STATS_FILTER_ROUTE:
		if (version < IPA_VERSION_4_5)
			return false;
		break;

	default:
		return false;
	}

	return true;
}

/* Must the given memory region be present in the configuration? */
static bool ipa_mem_id_required(struct ipa *ipa, enum ipa_mem_id mem_id)
{
	switch (mem_id) {
	case IPA_MEM_UC_SHARED:
	case IPA_MEM_UC_INFO:
	case IPA_MEM_V4_FILTER_HASHED:
	case IPA_MEM_V4_FILTER:
	case IPA_MEM_V6_FILTER_HASHED:
	case IPA_MEM_V6_FILTER:
	case IPA_MEM_V4_ROUTE_HASHED:
	case IPA_MEM_V4_ROUTE:
	case IPA_MEM_V6_ROUTE_HASHED:
	case IPA_MEM_V6_ROUTE:
	case IPA_MEM_MODEM_HEADER:
	case IPA_MEM_MODEM_PROC_CTX:
	case IPA_MEM_AP_PROC_CTX:
	case IPA_MEM_MODEM:
		return true;

	case IPA_MEM_PDN_CONFIG:
	case IPA_MEM_STATS_QUOTA_MODEM:
		return ipa->version >= IPA_VERSION_4_0;

	case IPA_MEM_STATS_TETHERING:
		return ipa->version >= IPA_VERSION_4_0 &&
			ipa->version != IPA_VERSION_5_0;

	default:
		return false;		/* Anything else is optional */
	}
}

static bool ipa_mem_valid_one(struct ipa *ipa, const struct ipa_mem *mem)
{
	enum ipa_mem_id mem_id = mem->id;
	struct device *dev = ipa->dev;
	u16 size_multiple;

	/* Make sure the memory region is valid for this version of IPA */
	if (!ipa_mem_id_valid(ipa, mem_id)) {
		dev_err(dev, "region id %u not valid\n", mem_id);
		return false;
	}

	if (!mem->size && !mem->canary_count) {
		dev_err(dev, "empty memory region %u\n", mem_id);
		return false;
	}

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
	else if (mem_id == IPA_MEM_END_MARKER && mem->size)
		dev_err(dev, "non-zero end marker region size\n");
	else
		return true;

	return false;
}

/* Verify each defined memory region is valid. */
static bool ipa_mem_valid(struct ipa *ipa, const struct ipa_mem_data *mem_data)
{
	DECLARE_BITMAP(regions, IPA_MEM_COUNT) = { };
	struct device *dev = ipa->dev;
	enum ipa_mem_id mem_id;
	u32 i;

	if (mem_data->local_count > IPA_MEM_COUNT) {
		dev_err(dev, "too many memory regions (%u > %u)\n",
			mem_data->local_count, IPA_MEM_COUNT);
		return false;
	}

	for (i = 0; i < mem_data->local_count; i++) {
		const struct ipa_mem *mem = &mem_data->local[i];

		if (__test_and_set_bit(mem->id, regions)) {
			dev_err(dev, "duplicate memory region %u\n", mem->id);
			return false;
		}

		/* Defined regions have non-zero size and/or canary count */
		if (!ipa_mem_valid_one(ipa, mem))
			return false;
	}

	/* Now see if any required regions are not defined */
	for_each_clear_bit(mem_id, regions, IPA_MEM_COUNT) {
		if (ipa_mem_id_required(ipa, mem_id))
			dev_err(dev, "required memory region %u missing\n",
				mem_id);
	}

	return true;
}

/* Do all memory regions fit within the IPA local memory? */
static bool ipa_mem_size_valid(struct ipa *ipa)
{
	struct device *dev = ipa->dev;
	u32 limit = ipa->mem_size;
	u32 i;

	for (i = 0; i < ipa->mem_count; i++) {
		const struct ipa_mem *mem = &ipa->mem[i];

		if (mem->offset + mem->size <= limit)
			continue;

		dev_err(dev, "region %u ends beyond memory limit (0x%08x)\n",
			mem->id, limit);

		return false;
	}

	return true;
}

/**
 * ipa_mem_config() - Configure IPA shared memory
 * @ipa:	IPA pointer
 *
 * Return:	0 if successful, or a negative error code
 */
int ipa_mem_config(struct ipa *ipa)
{
	struct device *dev = ipa->dev;
	const struct ipa_mem *mem;
	const struct reg *reg;
	dma_addr_t addr;
	u32 mem_size;
	void *virt;
	u32 val;
	u32 i;

	/* Check the advertised location and size of the shared memory area */
	reg = ipa_reg(ipa, SHARED_MEM_SIZE);
	val = ioread32(ipa->reg_virt + reg_offset(reg));

	/* The fields in the register are in 8 byte units */
	ipa->mem_offset = 8 * reg_decode(reg, MEM_BADDR, val);

	/* Make sure the end is within the region's mapped space */
	mem_size = 8 * reg_decode(reg, MEM_SIZE, val);

	/* If the sizes don't match, issue a warning */
	if (ipa->mem_offset + mem_size < ipa->mem_size) {
		dev_warn(dev, "limiting IPA memory size to 0x%08x\n",
			 mem_size);
		ipa->mem_size = mem_size;
	} else if (ipa->mem_offset + mem_size > ipa->mem_size) {
		dev_dbg(dev, "ignoring larger reported memory size: 0x%08x\n",
			mem_size);
	}

	/* We know our memory size; make sure regions are all in range */
	if (!ipa_mem_size_valid(ipa))
		return -EINVAL;

	/* Prealloc DMA memory for zeroing regions */
	virt = dma_alloc_coherent(dev, IPA_MEM_MAX, &addr, GFP_KERNEL);
	if (!virt)
		return -ENOMEM;
	ipa->zero_addr = addr;
	ipa->zero_virt = virt;
	ipa->zero_size = IPA_MEM_MAX;

	/* For each defined region, write "canary" values in the
	 * space prior to the region's base address if indicated.
	 */
	for (i = 0; i < ipa->mem_count; i++) {
		u16 canary_count = ipa->mem[i].canary_count;
		__le32 *canary;

		if (!canary_count)
			continue;

		/* Write canary values in the space before the region */
		canary = ipa->mem_virt + ipa->mem_offset + ipa->mem[i].offset;
		do
			*--canary = IPA_MEM_CANARY_VAL;
		while (--canary_count);
	}

	/* Verify the microcontroller ring alignment (if defined) */
	mem = ipa_mem_find(ipa, IPA_MEM_UC_EVENT_RING);
	if (mem && mem->offset % 1024) {
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
	struct device *dev = ipa->dev;

	dma_free_coherent(dev, ipa->zero_size, ipa->zero_virt, ipa->zero_addr);
	ipa->zero_size = 0;
	ipa->zero_virt = NULL;
	ipa->zero_addr = 0;
}

/**
 * ipa_mem_zero_modem() - Zero IPA-local memory regions owned by the modem
 * @ipa:	IPA pointer
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
		dev_err(ipa->dev, "no transaction to zero modem memory\n");
		return -EBUSY;
	}

	ipa_mem_zero_region_add(trans, IPA_MEM_MODEM_HEADER);
	ipa_mem_zero_region_add(trans, IPA_MEM_MODEM_PROC_CTX);
	ipa_mem_zero_region_add(trans, IPA_MEM_MODEM);

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
	struct device *dev = ipa->dev;
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

	ret = iommu_map(domain, iova, phys, size, IOMMU_READ | IOMMU_WRITE,
			GFP_KERNEL);
	if (ret)
		return ret;

	ipa->imem_iova = iova;
	ipa->imem_size = size;

	return 0;
}

static void ipa_imem_exit(struct ipa *ipa)
{
	struct device *dev = ipa->dev;
	struct iommu_domain *domain;

	if (!ipa->imem_size)
		return;

	domain = iommu_get_domain_for_dev(dev);
	if (domain) {
		size_t size;

		size = iommu_unmap(domain, ipa->imem_iova, ipa->imem_size);
		if (size != ipa->imem_size)
			dev_warn(dev, "unmapped %zu IMEM bytes, expected %zu\n",
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
static int ipa_smem_init(struct ipa *ipa, size_t size)
{
	struct device *dev = ipa->dev;
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
	 * to "get" a pointer it (this does not imply reference counting).
	 * The item might have already been allocated, in which case we
	 * use it unless the size isn't what we expect.
	 */
	ret = qcom_smem_alloc(QCOM_SMEM_HOST_MODEM, SMEM_IPA_FILTER_TABLE, size);
	if (ret && ret != -EEXIST) {
		dev_err(dev, "error %d allocating size %zu SMEM item\n",
			ret, size);
		return ret;
	}

	/* Now get the address of the SMEM memory region */
	virt = qcom_smem_get(QCOM_SMEM_HOST_MODEM, SMEM_IPA_FILTER_TABLE, &actual);
	if (IS_ERR(virt)) {
		ret = PTR_ERR(virt);
		dev_err(dev, "error %d getting SMEM item\n", ret);
		return ret;
	}

	/* In case the region was already allocated, verify the size */
	if (ret && actual != size) {
		dev_err(dev, "SMEM item has size %zu, expected %zu\n",
			actual, size);
		return -EINVAL;
	}

	domain = iommu_get_domain_for_dev(dev);
	if (!domain) {
		dev_err(dev, "no IOMMU domain found for SMEM\n");
		return -EINVAL;
	}

	/* Align the address down and the size up to a page boundary */
	addr = qcom_smem_virt_to_phys(virt);
	phys = addr & PAGE_MASK;
	size = PAGE_ALIGN(size + addr - phys);
	iova = phys;	/* We just want a direct mapping */

	ret = iommu_map(domain, iova, phys, size, IOMMU_READ | IOMMU_WRITE,
			GFP_KERNEL);
	if (ret)
		return ret;

	ipa->smem_iova = iova;
	ipa->smem_size = size;

	return 0;
}

static void ipa_smem_exit(struct ipa *ipa)
{
	struct device *dev = ipa->dev;
	struct iommu_domain *domain;

	domain = iommu_get_domain_for_dev(dev);
	if (domain) {
		size_t size;

		size = iommu_unmap(domain, ipa->smem_iova, ipa->smem_size);
		if (size != ipa->smem_size)
			dev_warn(dev, "unmapped %zu SMEM bytes, expected %zu\n",
				 size, ipa->smem_size);

	} else {
		dev_err(dev, "couldn't get IPA IOMMU domain for SMEM\n");
	}

	ipa->smem_size = 0;
	ipa->smem_iova = 0;
}

/* Perform memory region-related initialization */
int ipa_mem_init(struct ipa *ipa, struct platform_device *pdev,
		 const struct ipa_mem_data *mem_data)
{
	struct device *dev = &pdev->dev;
	struct resource *res;
	int ret;

	/* Make sure the set of defined memory regions is valid */
	if (!ipa_mem_valid(ipa, mem_data))
		return -EINVAL;

	ipa->mem_count = mem_data->local_count;
	ipa->mem = mem_data->local;

	/* Check the route and filter table memory regions */
	if (!ipa_table_mem_valid(ipa, false))
		return -EINVAL;
	if (!ipa_table_mem_valid(ipa, true))
		return -EINVAL;

	ret = dma_set_mask_and_coherent(dev, DMA_BIT_MASK(64));
	if (ret) {
		dev_err(dev, "error %d setting DMA mask\n", ret);
		return ret;
	}

	res = platform_get_resource_byname(pdev, IORESOURCE_MEM, "ipa-shared");
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

	ret = ipa_imem_init(ipa, mem_data->imem_addr, mem_data->imem_size);
	if (ret)
		goto err_unmap;

	ret = ipa_smem_init(ipa, mem_data->smem_size);
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
