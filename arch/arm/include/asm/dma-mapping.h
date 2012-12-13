#ifndef ASMARM_DMA_MAPPING_H
#define ASMARM_DMA_MAPPING_H

#ifdef __KERNEL__

#include <linux/mm_types.h>
#include <linux/scatterlist.h>
#include <linux/dma-attrs.h>
#include <linux/dma-debug.h>

#include <asm-generic/dma-coherent.h>
#include <asm/memory.h>

#define DMA_ERROR_CODE	(~0)
extern struct dma_map_ops arm_dma_ops;
extern struct dma_map_ops arm_coherent_dma_ops;

static inline struct dma_map_ops *get_dma_ops(struct device *dev)
{
	if (dev && dev->archdata.dma_ops)
		return dev->archdata.dma_ops;
	return &arm_dma_ops;
}

static inline void set_dma_ops(struct device *dev, struct dma_map_ops *ops)
{
	BUG_ON(!dev);
	dev->archdata.dma_ops = ops;
}

#include <asm-generic/dma-mapping-common.h>

static inline int dma_set_mask(struct device *dev, u64 mask)
{
	return get_dma_ops(dev)->set_dma_mask(dev, mask);
}

#ifdef __arch_page_to_dma
#error Please update to __arch_pfn_to_dma
#endif

/*
 * dma_to_pfn/pfn_to_dma/dma_to_virt/virt_to_dma are architecture private
 * functions used internally by the DMA-mapping API to provide DMA
 * addresses. They must not be used by drivers.
 */
#ifndef __arch_pfn_to_dma
static inline dma_addr_t pfn_to_dma(struct device *dev, unsigned long pfn)
{
	return (dma_addr_t)__pfn_to_bus(pfn);
}

static inline unsigned long dma_to_pfn(struct device *dev, dma_addr_t addr)
{
	return __bus_to_pfn(addr);
}

static inline void *dma_to_virt(struct device *dev, dma_addr_t addr)
{
	return (void *)__bus_to_virt((unsigned long)addr);
}

static inline dma_addr_t virt_to_dma(struct device *dev, void *addr)
{
	return (dma_addr_t)__virt_to_bus((unsigned long)(addr));
}
#else
static inline dma_addr_t pfn_to_dma(struct device *dev, unsigned long pfn)
{
	return __arch_pfn_to_dma(dev, pfn);
}

static inline unsigned long dma_to_pfn(struct device *dev, dma_addr_t addr)
{
	return __arch_dma_to_pfn(dev, addr);
}

static inline void *dma_to_virt(struct device *dev, dma_addr_t addr)
{
	return __arch_dma_to_virt(dev, addr);
}

static inline dma_addr_t virt_to_dma(struct device *dev, void *addr)
{
	return __arch_virt_to_dma(dev, addr);
}
#endif

/*
 * DMA errors are defined by all-bits-set in the DMA address.
 */
static inline int dma_mapping_error(struct device *dev, dma_addr_t dma_addr)
{
	return dma_addr == DMA_ERROR_CODE;
}

/*
 * Dummy noncoherent implementation.  We don't provide a dma_cache_sync
 * function so drivers using this API are highlighted with build warnings.
 */
static inline void *dma_alloc_noncoherent(struct device *dev, size_t size,
		dma_addr_t *handle, gfp_t gfp)
{
	return NULL;
}

static inline void dma_free_noncoherent(struct device *dev, size_t size,
		void *cpu_addr, dma_addr_t handle)
{
}

extern int dma_supported(struct device *dev, u64 mask);

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
			   gfp_t gfp, struct dma_attrs *attrs);

#define dma_alloc_coherent(d, s, h, f) dma_alloc_attrs(d, s, h, f, NULL)

static inline void *dma_alloc_attrs(struct device *dev, size_t size,
				       dma_addr_t *dma_handle, gfp_t flag,
				       struct dma_attrs *attrs)
{
	struct dma_map_ops *ops = get_dma_ops(dev);
	void *cpu_addr;
	BUG_ON(!ops);

	cpu_addr = ops->alloc(dev, size, dma_handle, flag, attrs);
	debug_dma_alloc_coherent(dev, size, *dma_handle, cpu_addr);
	return cpu_addr;
}

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
			 dma_addr_t handle, struct dma_attrs *attrs);

#define dma_free_coherent(d, s, c, h) dma_free_attrs(d, s, c, h, NULL)

static inline void dma_free_attrs(struct device *dev, size_t size,
				     void *cpu_addr, dma_addr_t dma_handle,
				     struct dma_attrs *attrs)
{
	struct dma_map_ops *ops = get_dma_ops(dev);
	BUG_ON(!ops);

	debug_dma_free_coherent(dev, size, cpu_addr, dma_handle);
	ops->free(dev, size, cpu_addr, dma_handle, attrs);
}

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
			struct dma_attrs *attrs);

static inline void *dma_alloc_writecombine(struct device *dev, size_t size,
				       dma_addr_t *dma_handle, gfp_t flag)
{
	DEFINE_DMA_ATTRS(attrs);
	dma_set_attr(DMA_ATTR_WRITE_COMBINE, &attrs);
	return dma_alloc_attrs(dev, size, dma_handle, flag, &attrs);
}

static inline void dma_free_writecombine(struct device *dev, size_t size,
				     void *cpu_addr, dma_addr_t dma_handle)
{
	DEFINE_DMA_ATTRS(attrs);
	dma_set_attr(DMA_ATTR_WRITE_COMBINE, &attrs);
	return dma_free_attrs(dev, size, cpu_addr, dma_handle, &attrs);
}

/*
 * This can be called during early boot to increase the size of the atomic
 * coherent DMA pool above the default value of 256KiB. It must be called
 * before postcore_initcall.
 */
extern void __init init_dma_coherent_pool_size(unsigned long size);

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

/**
 * dmabounce_register_dev
 *
 * @dev: valid struct device pointer
 * @small_buf_size: size of buffers to use with small buffer pool
 * @large_buf_size: size of buffers to use with large buffer pool (can be 0)
 * @needs_bounce_fn: called to determine whether buffer needs bouncing
 *
 * This function should be called by low-level platform code to register
 * a device as requireing DMA buffer bouncing. The function will allocate
 * appropriate DMA pools for the device.
 */
extern int dmabounce_register_dev(struct device *, unsigned long,
		unsigned long, int (*)(struct device *, dma_addr_t, size_t));

/**
 * dmabounce_unregister_dev
 *
 * @dev: valid struct device pointer
 *
 * This function should be called by low-level platform code when device
 * that was previously registered with dmabounce_register_dev is removed
 * from the system.
 *
 */
extern void dmabounce_unregister_dev(struct device *);



/*
 * The scatter list versions of the above methods.
 */
extern int arm_dma_map_sg(struct device *, struct scatterlist *, int,
		enum dma_data_direction, struct dma_attrs *attrs);
extern void arm_dma_unmap_sg(struct device *, struct scatterlist *, int,
		enum dma_data_direction, struct dma_attrs *attrs);
extern void arm_dma_sync_sg_for_cpu(struct device *, struct scatterlist *, int,
		enum dma_data_direction);
extern void arm_dma_sync_sg_for_device(struct device *, struct scatterlist *, int,
		enum dma_data_direction);
extern int arm_dma_get_sgtable(struct device *dev, struct sg_table *sgt,
		void *cpu_addr, dma_addr_t dma_addr, size_t size,
		struct dma_attrs *attrs);

#endif /* __KERNEL__ */
#endif
