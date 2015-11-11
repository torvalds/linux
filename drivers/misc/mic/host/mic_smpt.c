/*
 * Intel MIC Platform Software Stack (MPSS)
 *
 * Copyright(c) 2013 Intel Corporation.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License, version 2, as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * General Public License for more details.
 *
 * The full GNU General Public License is included in this distribution in
 * the file called "COPYING".
 *
 * Intel MIC Host driver.
 *
 */
#include <linux/pci.h>

#include "../common/mic_dev.h"
#include "mic_device.h"
#include "mic_smpt.h"

static inline u64 mic_system_page_mask(struct mic_device *mdev)
{
	return (1ULL << mdev->smpt->info.page_shift) - 1ULL;
}

static inline u8 mic_sys_addr_to_smpt(struct mic_device *mdev, dma_addr_t pa)
{
	return (pa - mdev->smpt->info.base) >> mdev->smpt->info.page_shift;
}

static inline u64 mic_smpt_to_pa(struct mic_device *mdev, u8 index)
{
	return mdev->smpt->info.base + (index * mdev->smpt->info.page_size);
}

static inline u64 mic_smpt_offset(struct mic_device *mdev, dma_addr_t pa)
{
	return pa & mic_system_page_mask(mdev);
}

static inline u64 mic_smpt_align_low(struct mic_device *mdev, dma_addr_t pa)
{
	return ALIGN(pa - mic_system_page_mask(mdev),
		mdev->smpt->info.page_size);
}

static inline u64 mic_smpt_align_high(struct mic_device *mdev, dma_addr_t pa)
{
	return ALIGN(pa, mdev->smpt->info.page_size);
}

/* Total Cumulative system memory accessible by MIC across all SMPT entries */
static inline u64 mic_max_system_memory(struct mic_device *mdev)
{
	return mdev->smpt->info.num_reg * mdev->smpt->info.page_size;
}

/* Maximum system memory address accessible by MIC */
static inline u64 mic_max_system_addr(struct mic_device *mdev)
{
	return mdev->smpt->info.base + mic_max_system_memory(mdev) - 1ULL;
}

/* Check if the DMA address is a MIC system memory address */
static inline bool
mic_is_system_addr(struct mic_device *mdev, dma_addr_t pa)
{
	return pa >= mdev->smpt->info.base && pa <= mic_max_system_addr(mdev);
}

/* Populate an SMPT entry and update the reference counts. */
static void mic_add_smpt_entry(int spt, s64 *ref, u64 addr,
			       int entries, struct mic_device *mdev)
{
	struct mic_smpt_info *smpt_info = mdev->smpt;
	int i;

	for (i = spt; i < spt + entries; i++,
		addr += smpt_info->info.page_size) {
		if (!smpt_info->entry[i].ref_count &&
		    (smpt_info->entry[i].dma_addr != addr)) {
			mdev->smpt_ops->set(mdev, addr, i);
			smpt_info->entry[i].dma_addr = addr;
		}
		smpt_info->entry[i].ref_count += ref[i - spt];
	}
}

/*
 * Find an available MIC address in MIC SMPT address space
 * for a given DMA address and size.
 */
static dma_addr_t mic_smpt_op(struct mic_device *mdev, u64 dma_addr,
			      int entries, s64 *ref, size_t size)
{
	int spt;
	int ae = 0;
	int i;
	unsigned long flags;
	dma_addr_t mic_addr = 0;
	dma_addr_t addr = dma_addr;
	struct mic_smpt_info *smpt_info = mdev->smpt;

	spin_lock_irqsave(&smpt_info->smpt_lock, flags);

	/* find existing entries */
	for (i = 0; i < smpt_info->info.num_reg; i++) {
		if (smpt_info->entry[i].dma_addr == addr) {
			ae++;
			addr += smpt_info->info.page_size;
		} else if (ae) /* cannot find contiguous entries */
			goto not_found;

		if (ae == entries)
			goto found;
	}

	/* find free entry */
	for (ae = 0, i = 0; i < smpt_info->info.num_reg; i++) {
		ae = (smpt_info->entry[i].ref_count == 0) ? ae + 1 : 0;
		if (ae == entries)
			goto found;
	}

not_found:
	spin_unlock_irqrestore(&smpt_info->smpt_lock, flags);
	return mic_addr;

found:
	spt = i - entries + 1;
	mic_addr = mic_smpt_to_pa(mdev, spt);
	mic_add_smpt_entry(spt, ref, dma_addr, entries, mdev);
	smpt_info->map_count++;
	smpt_info->ref_count += (s64)size;
	spin_unlock_irqrestore(&smpt_info->smpt_lock, flags);
	return mic_addr;
}

/*
 * Returns number of smpt entries needed for dma_addr to dma_addr + size
 * also returns the reference count array for each of those entries
 * and the starting smpt address
 */
static int mic_get_smpt_ref_count(struct mic_device *mdev, dma_addr_t dma_addr,
				  size_t size, s64 *ref,  u64 *smpt_start)
{
	u64 start =  dma_addr;
	u64 end = dma_addr + size;
	int i = 0;

	while (start < end) {
		ref[i++] = min(mic_smpt_align_high(mdev, start + 1),
			end) - start;
		start = mic_smpt_align_high(mdev, start + 1);
	}

	if (smpt_start)
		*smpt_start = mic_smpt_align_low(mdev, dma_addr);

	return i;
}

/*
 * mic_to_dma_addr - Converts a MIC address to a DMA address.
 *
 * @mdev: pointer to mic_device instance.
 * @mic_addr: MIC address.
 *
 * returns a DMA address.
 */
dma_addr_t mic_to_dma_addr(struct mic_device *mdev, dma_addr_t mic_addr)
{
	struct mic_smpt_info *smpt_info = mdev->smpt;
	int spt;
	dma_addr_t dma_addr;

	if (!mic_is_system_addr(mdev, mic_addr)) {
		dev_err(&mdev->pdev->dev,
			"mic_addr is invalid. mic_addr = 0x%llx\n", mic_addr);
		return -EINVAL;
	}
	spt = mic_sys_addr_to_smpt(mdev, mic_addr);
	dma_addr = smpt_info->entry[spt].dma_addr +
		mic_smpt_offset(mdev, mic_addr);
	return dma_addr;
}

/**
 * mic_map - Maps a DMA address to a MIC physical address.
 *
 * @mdev: pointer to mic_device instance.
 * @dma_addr: DMA address.
 * @size: Size of the region to be mapped.
 *
 * This API converts the DMA address provided to a DMA address understood
 * by MIC. Caller should check for errors by calling mic_map_error(..).
 *
 * returns DMA address as required by MIC.
 */
dma_addr_t mic_map(struct mic_device *mdev, dma_addr_t dma_addr, size_t size)
{
	dma_addr_t mic_addr = 0;
	int num_entries;
	s64 *ref;
	u64 smpt_start;

	if (!size || size > mic_max_system_memory(mdev))
		return mic_addr;

	ref = kmalloc_array(mdev->smpt->info.num_reg, sizeof(s64), GFP_ATOMIC);
	if (!ref)
		return mic_addr;

	num_entries = mic_get_smpt_ref_count(mdev, dma_addr, size,
					     ref, &smpt_start);

	/* Set the smpt table appropriately and get 16G aligned mic address */
	mic_addr = mic_smpt_op(mdev, smpt_start, num_entries, ref, size);

	kfree(ref);

	/*
	 * If mic_addr is zero then its an error case
	 * since mic_addr can never be zero.
	 * else generate mic_addr by adding the 16G offset in dma_addr
	 */
	if (!mic_addr && MIC_FAMILY_X100 == mdev->family) {
		dev_err(&mdev->pdev->dev,
			"mic_map failed dma_addr 0x%llx size 0x%lx\n",
			dma_addr, size);
		return mic_addr;
	} else {
		return mic_addr + mic_smpt_offset(mdev, dma_addr);
	}
}

/**
 * mic_unmap - Unmaps a MIC physical address.
 *
 * @mdev: pointer to mic_device instance.
 * @mic_addr: MIC physical address.
 * @size: Size of the region to be unmapped.
 *
 * This API unmaps the mappings created by mic_map(..).
 *
 * returns None.
 */
void mic_unmap(struct mic_device *mdev, dma_addr_t mic_addr, size_t size)
{
	struct mic_smpt_info *smpt_info = mdev->smpt;
	s64 *ref;
	int num_smpt;
	int spt;
	int i;
	unsigned long flags;

	if (!size)
		return;

	if (!mic_is_system_addr(mdev, mic_addr)) {
		dev_err(&mdev->pdev->dev,
			"invalid address: 0x%llx\n", mic_addr);
		return;
	}

	spt = mic_sys_addr_to_smpt(mdev, mic_addr);
	ref = kmalloc_array(mdev->smpt->info.num_reg, sizeof(s64), GFP_ATOMIC);
	if (!ref)
		return;

	/* Get number of smpt entries to be mapped, ref count array */
	num_smpt = mic_get_smpt_ref_count(mdev, mic_addr, size, ref, NULL);

	spin_lock_irqsave(&smpt_info->smpt_lock, flags);
	smpt_info->unmap_count++;
	smpt_info->ref_count -= (s64)size;

	for (i = spt; i < spt + num_smpt; i++) {
		smpt_info->entry[i].ref_count -= ref[i - spt];
		if (smpt_info->entry[i].ref_count < 0)
			dev_warn(&mdev->pdev->dev,
				 "ref count for entry %d is negative\n", i);
	}
	spin_unlock_irqrestore(&smpt_info->smpt_lock, flags);
	kfree(ref);
}

/**
 * mic_map_single - Maps a virtual address to a MIC physical address.
 *
 * @mdev: pointer to mic_device instance.
 * @va: Kernel direct mapped virtual address.
 * @size: Size of the region to be mapped.
 *
 * This API calls pci_map_single(..) for the direct mapped virtual address
 * and then converts the DMA address provided to a DMA address understood
 * by MIC. Caller should check for errors by calling mic_map_error(..).
 *
 * returns DMA address as required by MIC.
 */
dma_addr_t mic_map_single(struct mic_device *mdev, void *va, size_t size)
{
	dma_addr_t mic_addr = 0;
	struct pci_dev *pdev = mdev->pdev;
	dma_addr_t dma_addr =
		pci_map_single(pdev, va, size, PCI_DMA_BIDIRECTIONAL);

	if (!pci_dma_mapping_error(pdev, dma_addr)) {
		mic_addr = mic_map(mdev, dma_addr, size);
		if (!mic_addr) {
			dev_err(&mdev->pdev->dev,
				"mic_map failed dma_addr 0x%llx size 0x%lx\n",
				dma_addr, size);
			pci_unmap_single(pdev, dma_addr,
					 size, PCI_DMA_BIDIRECTIONAL);
		}
	}
	return mic_addr;
}

/**
 * mic_unmap_single - Unmaps a MIC physical address.
 *
 * @mdev: pointer to mic_device instance.
 * @mic_addr: MIC physical address.
 * @size: Size of the region to be unmapped.
 *
 * This API unmaps the mappings created by mic_map_single(..).
 *
 * returns None.
 */
void
mic_unmap_single(struct mic_device *mdev, dma_addr_t mic_addr, size_t size)
{
	struct pci_dev *pdev = mdev->pdev;
	dma_addr_t dma_addr = mic_to_dma_addr(mdev, mic_addr);
	mic_unmap(mdev, mic_addr, size);
	pci_unmap_single(pdev, dma_addr, size, PCI_DMA_BIDIRECTIONAL);
}

/**
 * mic_smpt_init - Initialize MIC System Memory Page Tables.
 *
 * @mdev: pointer to mic_device instance.
 *
 * returns 0 for success and -errno for error.
 */
int mic_smpt_init(struct mic_device *mdev)
{
	int i, err = 0;
	dma_addr_t dma_addr;
	struct mic_smpt_info *smpt_info;

	mdev->smpt = kmalloc(sizeof(*mdev->smpt), GFP_KERNEL);
	if (!mdev->smpt)
		return -ENOMEM;

	smpt_info = mdev->smpt;
	mdev->smpt_ops->init(mdev);
	smpt_info->entry = kmalloc_array(smpt_info->info.num_reg,
					 sizeof(*smpt_info->entry), GFP_KERNEL);
	if (!smpt_info->entry) {
		err = -ENOMEM;
		goto free_smpt;
	}
	spin_lock_init(&smpt_info->smpt_lock);
	for (i = 0; i < smpt_info->info.num_reg; i++) {
		dma_addr = i * smpt_info->info.page_size;
		smpt_info->entry[i].dma_addr = dma_addr;
		smpt_info->entry[i].ref_count = 0;
		mdev->smpt_ops->set(mdev, dma_addr, i);
	}
	smpt_info->ref_count = 0;
	smpt_info->map_count = 0;
	smpt_info->unmap_count = 0;
	return 0;
free_smpt:
	kfree(smpt_info);
	return err;
}

/**
 * mic_smpt_uninit - UnInitialize MIC System Memory Page Tables.
 *
 * @mdev: pointer to mic_device instance.
 *
 * returns None.
 */
void mic_smpt_uninit(struct mic_device *mdev)
{
	struct mic_smpt_info *smpt_info = mdev->smpt;
	int i;

	dev_dbg(&mdev->pdev->dev,
		"nodeid %d SMPT ref count %lld map %lld unmap %lld\n",
		mdev->id, smpt_info->ref_count,
		smpt_info->map_count, smpt_info->unmap_count);

	for (i = 0; i < smpt_info->info.num_reg; i++) {
		dev_dbg(&mdev->pdev->dev,
			"SMPT entry[%d] dma_addr = 0x%llx ref_count = %lld\n",
			i, smpt_info->entry[i].dma_addr,
			smpt_info->entry[i].ref_count);
		if (smpt_info->entry[i].ref_count)
			dev_warn(&mdev->pdev->dev,
				 "ref count for entry %d is not zero\n", i);
	}
	kfree(smpt_info->entry);
	kfree(smpt_info);
}

/**
 * mic_smpt_restore - Restore MIC System Memory Page Tables.
 *
 * @mdev: pointer to mic_device instance.
 *
 * Restore the SMPT registers to values previously stored in the
 * SW data structures. Some MIC steppings lose register state
 * across resets and this API should be called for performing
 * a restore operation if required.
 *
 * returns None.
 */
void mic_smpt_restore(struct mic_device *mdev)
{
	int i;
	dma_addr_t dma_addr;

	for (i = 0; i < mdev->smpt->info.num_reg; i++) {
		dma_addr = mdev->smpt->entry[i].dma_addr;
		mdev->smpt_ops->set(mdev, dma_addr, i);
	}
}
