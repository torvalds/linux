/* linux/arch/arm/plat-s5p/include/plat/iovmm.h
 *
 * Copyright (c) 2011 Samsung Electronics Co., Ltd.
 *		http://www.samsung.com
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef __ASM_PLAT_IOVMM_H
#define __ASM_PLAT_IOVMM_H

#ifdef CONFIG_DRM_EXYNOS_IOMMU
#ifdef CONFIG_IOVMM
void *iovmm_setup(unsigned long s_iova, unsigned long size);
void iovmm_cleanup(void *in_vmm);
int iovmm_activate(void *in_vmm, struct device *dev);
void iovmm_deactivate(void *in_vmm, struct device *dev);

/* iovmm_map() - Maps a list of physical memory chunks
 * @dev: the owner of the IO address space where the mapping is created
 * @sg: list of physical memory chunks to map
 * @offset: length in bytes where the mapping starts
 * @size: how much memory to map in bytes. @offset + @size must not exceed
 *        total size of @sg
 *
 * This function returns mapped IO address in the address space of @dev.
 * Returns 0 if mapping fails.
 *
 * The caller of this function must ensure that iovmm_cleanup() is not called
 * while this function is called.
 *
 */
dma_addr_t iovmm_map(void *in_vmm, struct scatterlist *sg, off_t offset,
								size_t size);

/* iovmm_map() - unmaps the given IO address
 * @dev: the owner of the IO address space where @iova belongs
 * @iova: IO address that needs to be unmapped and freed.
 *
 * The caller of this function must ensure that iovmm_cleanup() is not called
 * while this function is called.
 */
void iovmm_unmap(void *in_vmm, dma_addr_t iova);

#else
#define iovmm_setup(s_iova, size)		(ERR_PTR(-ENOSYS))
#define iovmm_cleanup(in_vmm)
#define iovmm_activate(in_vmm, dev)		(-ENOSYS)
#define iovmm_deactivate(in_vmm, dev)
#define iovmm_map(in_vmm, sg, offset, size)	(0)
#define iovmm_unmap(in_vmm, iova)
#endif /* CONFIG_IOVMM */

#else

#ifdef CONFIG_IOVMM
int iovmm_setup(struct device *dev);
void iovmm_cleanup(struct device *dev);
int iovmm_activate(struct device *dev);
void iovmm_deactivate(struct device *dev);

/* iovmm_map() - Maps a list of physical memory chunks
 * @dev: the owner of the IO address space where the mapping is created
 * @sg: list of physical memory chunks to map
 * @offset: length in bytes where the mapping starts
 * @size: how much memory to map in bytes. @offset + @size must not exceed
 *        total size of @sg
 *
 * This function returns mapped IO address in the address space of @dev.
 * Returns 0 if mapping fails.
 *
 * The caller of this function must ensure that iovmm_cleanup() is not called
 * while this function is called.
 *
 */
dma_addr_t iovmm_map(struct device *dev, struct scatterlist *sg, off_t offset,
								size_t size);

/* iovmm_map() - unmaps the given IO address
 * @dev: the owner of the IO address space where @iova belongs
 * @iova: IO address that needs to be unmapped and freed.
 *
 * The caller of this function must ensure that iovmm_cleanup() is not called
 * while this function is called.
 */
void iovmm_unmap(struct device *dev, dma_addr_t iova);

#else
#define iovmm_setup(dev)	(-ENOSYS)
#define iovmm_cleanup(dev)
#define iovmm_activate(dev)	(-ENOSYS)
#define iovmm_deactivate(dev)
#define iovmm_map(dev, sg)	(0)
#define iovmm_unmap(dev, iova)
#endif /* CONFIG_IOVMM */

#endif /* CONFIG_DRM_EXYNOS_IOMMU */

#endif /*__ASM_PLAT_IOVMM_H*/
