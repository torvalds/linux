/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2015-2021, The Linux Foundation. All rights reserved.
 *
 */

#ifndef __DRIVERS_IOMMU_QCOM_IOMMU_DEBUG_H__
#define __DRIVERS_IOMMU_QCOM_IOMMU_DEBUG_H__

#include <linux/device.h>
#include <linux/debugfs.h>
#include <linux/iommu.h>
#include <linux/completion.h>
#include <linux/mutex.h>

#define MSI_IOVA_BASE			0x8000000
#define MSI_IOVA_LENGTH			0x100000
#define ARM_SMMU_SMR_ID			GENMASK(15, 0)

struct iommu_debug_device {
	struct device *self;
	u32 nr_children;
	char *buffer;
	struct dentry *root_dir;
	/* for usecase under test */
	struct device *test_dev;
	struct iommu_domain *domain;
	u32 usecase_nr;
	/* Protects test_dev */
	struct mutex state_lock;
	/* For waiting for child probe to complete */
	struct completion probe_wait;
	/* Used for atos */
	u64 iova;
	/* number of iterations */
	u32 nr_iters;
};

struct device *iommu_debug_usecase_reset(struct iommu_debug_device *ddev);
struct device *iommu_debug_switch_usecase(struct iommu_debug_device *ddev, u32 usecase_nr);

int iommu_debug_check_mapping_flags(struct device *dev, dma_addr_t iova, size_t size,
				    phys_addr_t expected_pa, u32 flags);
#define iommu_debug_check_mapping(d, i, s, p) \
	iommu_debug_check_mapping_flags(d, i, s, p, 0)
/* Only checks a single page */
#define iommu_debug_check_mapping_fast(d, i, s, p) \
	iommu_debug_check_mapping_flags(d, i, PAGE_SIZE, p, 0)

int iommu_debug_check_mapping_sg_flags(struct device *dev, struct scatterlist *sgl,
				       unsigned int pgoffset, unsigned int dma_nents,
				       unsigned int nents, u32 flags);
#define iommu_debug_check_mapping_sg(d, s, o, e1, e2) \
	iommu_debug_check_mapping_sg_flags(d, s, o, e1, e2, 0)

/* Only checks the last page of first sgl */
static inline int iommu_debug_check_mapping_sg_fast(struct device *dev, struct scatterlist *sgl,
						    unsigned int pgoffset, unsigned int dma_nents,
						    unsigned int nents)
{
	pgoffset = PAGE_ALIGN(sgl->offset + sgl->length) >> PAGE_SHIFT;
	return iommu_debug_check_mapping_sg_flags(dev, sgl, pgoffset - 1, dma_nents, 1, 0);
}

extern const struct file_operations iommu_debug_functional_arm_dma_api_fops;
extern const struct file_operations iommu_debug_functional_fast_dma_api_fops;
extern const struct file_operations iommu_debug_atos_fops;
extern const struct file_operations iommu_debug_map_fops;
extern const struct file_operations iommu_debug_unmap_fops;
extern const struct file_operations iommu_debug_dma_map_fops;
extern const struct file_operations iommu_debug_dma_unmap_fops;
extern const struct file_operations iommu_debug_test_virt_addr_fops;
extern const struct file_operations iommu_debug_profiling_fops;

#endif
