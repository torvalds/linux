/* SPDX-License-Identifier: GPL-2.0 */
/*
 * SVA library for IOMMU drivers
 */
#ifndef _IOMMU_SVA_LIB_H
#define _IOMMU_SVA_LIB_H

#include <linux/ioasid.h>
#include <linux/mm_types.h>

int iommu_sva_alloc_pasid(struct mm_struct *mm, ioasid_t min, ioasid_t max);
void iommu_sva_free_pasid(struct mm_struct *mm);
struct mm_struct *iommu_sva_find(ioasid_t pasid);

/* I/O Page fault */
struct device;
struct iommu_fault;
struct iopf_queue;

#ifdef CONFIG_IOMMU_SVA_LIB
int iommu_queue_iopf(struct iommu_fault *fault, void *cookie);

int iopf_queue_add_device(struct iopf_queue *queue, struct device *dev);
int iopf_queue_remove_device(struct iopf_queue *queue,
			     struct device *dev);
int iopf_queue_flush_dev(struct device *dev);
struct iopf_queue *iopf_queue_alloc(const char *name);
void iopf_queue_free(struct iopf_queue *queue);
int iopf_queue_discard_partial(struct iopf_queue *queue);

#else /* CONFIG_IOMMU_SVA_LIB */
static inline int iommu_queue_iopf(struct iommu_fault *fault, void *cookie)
{
	return -ENODEV;
}

static inline int iopf_queue_add_device(struct iopf_queue *queue,
					struct device *dev)
{
	return -ENODEV;
}

static inline int iopf_queue_remove_device(struct iopf_queue *queue,
					   struct device *dev)
{
	return -ENODEV;
}

static inline int iopf_queue_flush_dev(struct device *dev)
{
	return -ENODEV;
}

static inline struct iopf_queue *iopf_queue_alloc(const char *name)
{
	return NULL;
}

static inline void iopf_queue_free(struct iopf_queue *queue)
{
}

static inline int iopf_queue_discard_partial(struct iopf_queue *queue)
{
	return -ENODEV;
}
#endif /* CONFIG_IOMMU_SVA_LIB */
#endif /* _IOMMU_SVA_LIB_H */
