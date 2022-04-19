/* SPDX-License-Identifier: GPL-2.0 */
#ifndef ASMARM_DMA_MAPPING_H
#define ASMARM_DMA_MAPPING_H

#ifdef __KERNEL__

#include <linux/mm_types.h>
#include <linux/scatterlist.h>

#include <xen/xen.h>
#include <asm/xen/hypervisor.h>

extern const struct dma_map_ops arm_dma_ops;
extern const struct dma_map_ops arm_coherent_dma_ops;

static inline const struct dma_map_ops *get_arch_dma_ops(struct bus_type *bus)
{
	if (IS_ENABLED(CONFIG_MMU) && !IS_ENABLED(CONFIG_ARM_LPAE))
		return &arm_dma_ops;
	return NULL;
}

/**
 * arm_dma_alloc - allocate consistent memory for DMA
 * @dev: valid struct device pointer, or NULL for ISA and EISA-like devices
 * @size: required memory size
 * @handle: bus-specific DMA address
 * @attrs: optinal attributes that specific mapping properties
 *
 * Allocate some memory for a device for performing DMA.  This function
 * allocates pages, and will return the CPU-viewed address, and sets @handle
 * to be the device-viewed address.
 */
extern void *arm_dma_alloc(struct device *dev, size_t size, dma_addr_t *handle,
			   gfp_t gfp, unsigned long attrs);

/**
 * arm_dma_free - free memory allocated by arm_dma_alloc
 * @dev: valid struct device pointer, or NULL for ISA and EISA-like devices
 * @size: size of memory originally requested in dma_alloc_coherent
 * @cpu_addr: CPU-view address returned from dma_alloc_coherent
 * @handle: device-view address returned from dma_alloc_coherent
 * @attrs: optinal attributes that specific mapping properties
 *
 * Free (and unmap) a DMA buffer previously allocated by
 * arm_dma_alloc().
 *
 * References to memory and mappings associated with cpu_addr/handle
 * during and after this call executing are illegal.
 */
extern void arm_dma_free(struct device *dev, size_t size, void *cpu_addr,
			 dma_addr_t handle, unsigned long attrs);

/**
 * arm_dma_mmap - map a coherent DMA allocation into user space
 * @dev: valid struct device pointer, or NULL for ISA and EISA-like devices
 * @vma: vm_area_struct describing requested user mapping
 * @cpu_addr: kernel CPU-view address returned from dma_alloc_coherent
 * @handle: device-view address returned from dma_alloc_coherent
 * @size: size of memory originally requested in dma_alloc_coherent
 * @attrs: optinal attributes that specific mapping properties
 *
 * Map a coherent DMA buffer previously allocated by dma_alloc_coherent
 * into user space.  The coherent DMA buffer must not be freed by the
 * driver until the user space mapping has been released.
 */
extern int arm_dma_mmap(struct device *dev, struct vm_area_struct *vma,
			void *cpu_addr, dma_addr_t dma_addr, size_t size,
			unsigned long attrs);

/*
 * For SA-1111, IXP425, and ADI systems  the dma-mapping functions are "magic"
 * and utilize bounce buffers as needed to work around limited DMA windows.
 *
 * On the SA-1111, a bug limits DMA to only certain regions of RAM.
 * On the IXP425, the PCI inbound window is 64MB (256MB total RAM)
 * On some ADI engineering systems, PCI inbound window is 32MB (12MB total RAM)
 *
 * The following are helper functions used by the dmabounce subystem
 *
 */

/*
 * The scatter list versions of the above methods.
 */
extern int arm_dma_map_sg(struct device *, struct scatterlist *, int,
		enum dma_data_direction, unsigned long attrs);
extern void arm_dma_unmap_sg(struct device *, struct scatterlist *, int,
		enum dma_data_direction, unsigned long attrs);
extern void arm_dma_sync_sg_for_cpu(struct device *, struct scatterlist *, int,
		enum dma_data_direction);
extern void arm_dma_sync_sg_for_device(struct device *, struct scatterlist *, int,
		enum dma_data_direction);
extern int arm_dma_get_sgtable(struct device *dev, struct sg_table *sgt,
		void *cpu_addr, dma_addr_t dma_addr, size_t size,
		unsigned long attrs);

#endif /* __KERNEL__ */
#endif
