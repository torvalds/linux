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
#ifndef MIC_SMPT_H
#define MIC_SMPT_H
/**
 * struct mic_smpt_ops - MIC HW specific SMPT operations.
 * @init: Initialize hardware specific SMPT information in mic_smpt_hw_info.
 * @set: Set the value for a particular SMPT entry.
 */
struct mic_smpt_ops {
	void (*init)(struct mic_device *mdev);
	void (*set)(struct mic_device *mdev, dma_addr_t dma_addr, u8 index);
};

/**
 * struct mic_smpt - MIC SMPT entry information.
 * @dma_addr: Base DMA address for this SMPT entry.
 * @ref_count: Number of active mappings for this SMPT entry in bytes.
 */
struct mic_smpt {
	dma_addr_t dma_addr;
	s64 ref_count;
};

/**
 * struct mic_smpt_hw_info - MIC SMPT hardware specific information.
 * @num_reg: Number of SMPT registers.
 * @page_shift: System memory page shift.
 * @page_size: System memory page size.
 * @base: System address base.
 */
struct mic_smpt_hw_info {
	u8 num_reg;
	u8 page_shift;
	u64 page_size;
	u64 base;
};

/**
 * struct mic_smpt_info - MIC SMPT information.
 * @entry: Array of SMPT entries.
 * @smpt_lock: Spin lock protecting access to SMPT data structures.
 * @info: Hardware specific SMPT information.
 * @ref_count: Number of active SMPT mappings (for debug).
 * @map_count: Number of SMPT mappings created (for debug).
 * @unmap_count: Number of SMPT mappings destroyed (for debug).
 */
struct mic_smpt_info {
	struct mic_smpt *entry;
	spinlock_t smpt_lock;
	struct mic_smpt_hw_info info;
	s64 ref_count;
	s64 map_count;
	s64 unmap_count;
};

dma_addr_t mic_map_single(struct mic_device *mdev, void *va, size_t size);
void mic_unmap_single(struct mic_device *mdev,
	dma_addr_t mic_addr, size_t size);
dma_addr_t mic_map(struct mic_device *mdev,
	dma_addr_t dma_addr, size_t size);
void mic_unmap(struct mic_device *mdev, dma_addr_t mic_addr, size_t size);

/**
 * mic_map_error - Check a MIC address for errors.
 *
 * @mdev: pointer to mic_device instance.
 *
 * returns Whether there was an error during mic_map..(..) APIs.
 */
static inline bool mic_map_error(dma_addr_t mic_addr)
{
	return !mic_addr;
}

int mic_smpt_init(struct mic_device *mdev);
void mic_smpt_uninit(struct mic_device *mdev);
void mic_smpt_restore(struct mic_device *mdev);

#endif
