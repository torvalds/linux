#ifndef ASMARM_DMA_MAPPING_H
#define ASMARM_DMA_MAPPING_H

#ifdef __KERNEL__

#include <linux/mm_types.h>
#include <linux/scatterlist.h>
#include <linux/dma-debug.h>

#include <asm-generic/dma-coherent.h>
#include <asm/memory.h>

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
	return (void *)__bus_to_virt(addr);
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
 * The DMA API is built upon the notion of "buffer ownership".  A buffer
 * is either exclusively owned by the CPU (and therefore may be accessed
 * by it) or exclusively owned by the DMA device.  These helper functions
 * represent the transitions between these two ownership states.
 *
 * Note, however, that on later ARMs, this notion does not work due to
 * speculative prefetches.  We model our approach on the assumption that
 * the CPU does do speculative prefetches, which means we clean caches
 * before transfers and delay cache invalidation until transfer completion.
 *
 * Private support functions: these are not part of the API and are
 * liable to change.  Drivers must not use these.
 */
static inline void __dma_single_cpu_to_dev(const void *kaddr, size_t size,
	enum dma_data_direction dir)
{
	extern void ___dma_single_cpu_to_dev(const void *, size_t,
		enum dma_data_direction);

	if (!arch_is_coherent())
		___dma_single_cpu_to_dev(kaddr, size, dir);
}

static inline void __dma_single_dev_to_cpu(const void *kaddr, size_t size,
	enum dma_data_direction dir)
{
	extern void ___dma_single_dev_to_cpu(const void *, size_t,
		enum dma_data_direction);

	if (!arch_is_coherent())
		___dma_single_dev_to_cpu(kaddr, size, dir);
}

static inline void __dma_page_cpu_to_dev(struct page *page, unsigned long off,
	size_t size, enum dma_data_direction dir)
{
	extern void ___dma_page_cpu_to_dev(struct page *, unsigned long,
		size_t, enum dma_data_direction);

	if (!arch_is_coherent())
		___dma_page_cpu_to_dev(page, off, size, dir);
}

static inline void __dma_page_dev_to_cpu(struct page *page, unsigned long off,
	size_t size, enum dma_data_direction dir)
{
	extern void ___dma_page_dev_to_cpu(struct page *, unsigned long,
		size_t, enum dma_data_direction);

	if (!arch_is_coherent())
		___dma_page_dev_to_cpu(page, off, size, dir);
}

/*
 * Return whether the given device DMA address mask can be supported
 * properly.  For example, if your device can only drive the low 24-bits
 * during bus mastering, then you would pass 0x00ffffff as the mask
 * to this function.
 *
 * FIXME: This should really be a platform specific issue - we should
 * return false if GFP_DMA allocations may not satisfy the supplied 'mask'.
 */
static inline int dma_supported(struct device *dev, u64 mask)
{
	if (mask < ISA_DMA_THRESHOLD)
		return 0;
	return 1;
}

static inline int dma_set_mask(struct device *dev, u64 dma_mask)
{
#ifdef CONFIG_DMABOUNCE
	if (dev->archdata.dmabounce) {
		if (dma_mask >= ISA_DMA_THRESHOLD)
			return 0;
		else
			return -EIO;
	}
#endif
	if (!dev->dma_mask || !dma_supported(dev, dma_mask))
		return -EIO;

	*dev->dma_mask = dma_mask;

	return 0;
}

/*
 * DMA errors are defined by all-bits-set in the DMA address.
 */
static inline int dma_mapping_error(struct device *dev, dma_addr_t dma_addr)
{
	return dma_addr == ~0;
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

/**
 * dma_alloc_coherent - allocate consistent memory for DMA
 * @dev: valid struct device pointer, or NULL for ISA and EISA-like devices
 * @size: required memory size
 * @handle: bus-specific DMA address
 *
 * Allocate some uncached, unbuffered memory for a device for
 * performing DMA.  This function allocates pages, and will
 * return the CPU-viewed address, and sets @handle to be the
 * device-viewed address.
 */
extern void *dma_alloc_coherent(struct device *, size_t, dma_addr_t *, gfp_t);

/**
 * dma_free_coherent - free memory allocated by dma_alloc_coherent
 * @dev: valid struct device pointer, or NULL for ISA and EISA-like devices
 * @size: size of memory originally requested in dma_alloc_coherent
 * @cpu_addr: CPU-view address returned from dma_alloc_coherent
 * @handle: device-view address returned from dma_alloc_coherent
 *
 * Free (and unmap) a DMA buffer previously allocated by
 * dma_alloc_coherent().
 *
 * References to memory and mappings associated with cpu_addr/handle
 * during and after this call executing are illegal.
 */
extern void dma_free_coherent(struct device *, size_t, void *, dma_addr_t);

/**
 * dma_mmap_coherent - map a coherent DMA allocation into user space
 * @dev: valid struct device pointer, or NULL for ISA and EISA-like devices
 * @vma: vm_area_struct describing requested user mapping
 * @cpu_addr: kernel CPU-view address returned from dma_alloc_coherent
 * @handle: device-view address returned from dma_alloc_coherent
 * @size: size of memory originally requested in dma_alloc_coherent
 *
 * Map a coherent DMA buffer previously allocated by dma_alloc_coherent
 * into user space.  The coherent DMA buffer must not be freed by the
 * driver until the user space mapping has been released.
 */
int dma_mmap_coherent(struct device *, struct vm_area_struct *,
		void *, dma_addr_t, size_t);


/**
 * dma_alloc_writecombine - allocate writecombining memory for DMA
 * @dev: valid struct device pointer, or NULL for ISA and EISA-like devices
 * @size: required memory size
 * @handle: bus-specific DMA address
 *
 * Allocate some uncached, buffered memory for a device for
 * performing DMA.  This function allocates pages, and will
 * return the CPU-viewed address, and sets @handle to be the
 * device-viewed address.
 */
extern void *dma_alloc_writecombine(struct device *, size_t, dma_addr_t *,
		gfp_t);

#define dma_free_writecombine(dev,size,cpu_addr,handle) \
	dma_free_coherent(dev,size,cpu_addr,handle)

int dma_mmap_writecombine(struct device *, struct vm_area_struct *,
		void *, dma_addr_t, size_t);


#ifdef CONFIG_DMABOUNCE
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
 *
 * This function should be called by low-level platform code to register
 * a device as requireing DMA buffer bouncing. The function will allocate
 * appropriate DMA pools for the device.
 *
 */
extern int dmabounce_register_dev(struct device *, unsigned long,
		unsigned long);

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

/**
 * dma_needs_bounce
 *
 * @dev: valid struct device pointer
 * @dma_handle: dma_handle of unbounced buffer
 * @size: size of region being mapped
 *
 * Platforms that utilize the dmabounce mechanism must implement
 * this function.
 *
 * The dmabounce routines call this function whenever a dma-mapping
 * is requested to determine whether a given buffer needs to be bounced
 * or not. The function must return 0 if the buffer is OK for
 * DMA access and 1 if the buffer needs to be bounced.
 *
 */
extern int dma_needs_bounce(struct device*, dma_addr_t, size_t);

/*
 * The DMA API, implemented by dmabounce.c.  See below for descriptions.
 */
extern dma_addr_t __dma_map_single(struct device *, void *, size_t,
		enum dma_data_direction);
extern void __dma_unmap_single(struct device *, dma_addr_t, size_t,
		enum dma_data_direction);
extern dma_addr_t __dma_map_page(struct device *, struct page *,
		unsigned long, size_t, enum dma_data_direction);
extern void __dma_unmap_page(struct device *, dma_addr_t, size_t,
		enum dma_data_direction);

/*
 * Private functions
 */
int dmabounce_sync_for_cpu(struct device *, dma_addr_t, unsigned long,
		size_t, enum dma_data_direction);
int dmabounce_sync_for_device(struct device *, dma_addr_t, unsigned long,
		size_t, enum dma_data_direction);
#else
static inline int dmabounce_sync_for_cpu(struct device *d, dma_addr_t addr,
	unsigned long offset, size_t size, enum dma_data_direction dir)
{
	return 1;
}

static inline int dmabounce_sync_for_device(struct device *d, dma_addr_t addr,
	unsigned long offset, size_t size, enum dma_data_direction dir)
{
	return 1;
}


static inline dma_addr_t __dma_map_single(struct device *dev, void *cpu_addr,
		size_t size, enum dma_data_direction dir)
{
	__dma_single_cpu_to_dev(cpu_addr, size, dir);
	return virt_to_dma(dev, cpu_addr);
}

static inline dma_addr_t __dma_map_page(struct device *dev, struct page *page,
	     unsigned long offset, size_t size, enum dma_data_direction dir)
{
	__dma_page_cpu_to_dev(page, offset, size, dir);
	return pfn_to_dma(dev, page_to_pfn(page)) + offset;
}

static inline void __dma_unmap_single(struct device *dev, dma_addr_t handle,
		size_t size, enum dma_data_direction dir)
{
	__dma_single_dev_to_cpu(dma_to_virt(dev, handle), size, dir);
}

static inline void __dma_unmap_page(struct device *dev, dma_addr_t handle,
		size_t size, enum dma_data_direction dir)
{
	__dma_page_dev_to_cpu(pfn_to_page(dma_to_pfn(dev, handle)),
		handle & ~PAGE_MASK, size, dir);
}
#endif /* CONFIG_DMABOUNCE */

/**
 * dma_map_single - map a single buffer for streaming DMA
 * @dev: valid struct device pointer, or NULL for ISA and EISA-like devices
 * @cpu_addr: CPU direct mapped address of buffer
 * @size: size of buffer to map
 * @dir: DMA transfer direction
 *
 * Ensure that any data held in the cache is appropriately discarded
 * or written back.
 *
 * The device owns this memory once this call has completed.  The CPU
 * can regain ownership by calling dma_unmap_single() or
 * dma_sync_single_for_cpu().
 */
static inline dma_addr_t dma_map_single(struct device *dev, void *cpu_addr,
		size_t size, enum dma_data_direction dir)
{
	dma_addr_t addr;

	BUG_ON(!valid_dma_direction(dir));

	addr = __dma_map_single(dev, cpu_addr, size, dir);
	debug_dma_map_page(dev, virt_to_page(cpu_addr),
			(unsigned long)cpu_addr & ~PAGE_MASK, size,
			dir, addr, true);

	return addr;
}

/**
 * dma_map_page - map a portion of a page for streaming DMA
 * @dev: valid struct device pointer, or NULL for ISA and EISA-like devices
 * @page: page that buffer resides in
 * @offset: offset into page for start of buffer
 * @size: size of buffer to map
 * @dir: DMA transfer direction
 *
 * Ensure that any data held in the cache is appropriately discarded
 * or written back.
 *
 * The device owns this memory once this call has completed.  The CPU
 * can regain ownership by calling dma_unmap_page().
 */
static inline dma_addr_t dma_map_page(struct device *dev, struct page *page,
	     unsigned long offset, size_t size, enum dma_data_direction dir)
{
	dma_addr_t addr;

	BUG_ON(!valid_dma_direction(dir));

	addr = __dma_map_page(dev, page, offset, size, dir);
	debug_dma_map_page(dev, page, offset, size, dir, addr, false);

	return addr;
}

/**
 * dma_unmap_single - unmap a single buffer previously mapped
 * @dev: valid struct device pointer, or NULL for ISA and EISA-like devices
 * @handle: DMA address of buffer
 * @size: size of buffer (same as passed to dma_map_single)
 * @dir: DMA transfer direction (same as passed to dma_map_single)
 *
 * Unmap a single streaming mode DMA translation.  The handle and size
 * must match what was provided in the previous dma_map_single() call.
 * All other usages are undefined.
 *
 * After this call, reads by the CPU to the buffer are guaranteed to see
 * whatever the device wrote there.
 */
static inline void dma_unmap_single(struct device *dev, dma_addr_t handle,
		size_t size, enum dma_data_direction dir)
{
	debug_dma_unmap_page(dev, handle, size, dir, true);
	__dma_unmap_single(dev, handle, size, dir);
}

/**
 * dma_unmap_page - unmap a buffer previously mapped through dma_map_page()
 * @dev: valid struct device pointer, or NULL for ISA and EISA-like devices
 * @handle: DMA address of buffer
 * @size: size of buffer (same as passed to dma_map_page)
 * @dir: DMA transfer direction (same as passed to dma_map_page)
 *
 * Unmap a page streaming mode DMA translation.  The handle and size
 * must match what was provided in the previous dma_map_page() call.
 * All other usages are undefined.
 *
 * After this call, reads by the CPU to the buffer are guaranteed to see
 * whatever the device wrote there.
 */
static inline void dma_unmap_page(struct device *dev, dma_addr_t handle,
		size_t size, enum dma_data_direction dir)
{
	debug_dma_unmap_page(dev, handle, size, dir, false);
	__dma_unmap_page(dev, handle, size, dir);
}

/**
 * dma_sync_single_range_for_cpu
 * @dev: valid struct device pointer, or NULL for ISA and EISA-like devices
 * @handle: DMA address of buffer
 * @offset: offset of region to start sync
 * @size: size of region to sync
 * @dir: DMA transfer direction (same as passed to dma_map_single)
 *
 * Make physical memory consistent for a single streaming mode DMA
 * translation after a transfer.
 *
 * If you perform a dma_map_single() but wish to interrogate the
 * buffer using the cpu, yet do not wish to teardown the PCI dma
 * mapping, you must call this function before doing so.  At the
 * next point you give the PCI dma address back to the card, you
 * must first the perform a dma_sync_for_device, and then the
 * device again owns the buffer.
 */
static inline void dma_sync_single_range_for_cpu(struct device *dev,
		dma_addr_t handle, unsigned long offset, size_t size,
		enum dma_data_direction dir)
{
	BUG_ON(!valid_dma_direction(dir));

	debug_dma_sync_single_for_cpu(dev, handle + offset, size, dir);

	if (!dmabounce_sync_for_cpu(dev, handle, offset, size, dir))
		return;

	__dma_single_dev_to_cpu(dma_to_virt(dev, handle) + offset, size, dir);
}

static inline void dma_sync_single_range_for_device(struct device *dev,
		dma_addr_t handle, unsigned long offset, size_t size,
		enum dma_data_direction dir)
{
	BUG_ON(!valid_dma_direction(dir));

	debug_dma_sync_single_for_device(dev, handle + offset, size, dir);

	if (!dmabounce_sync_for_device(dev, handle, offset, size, dir))
		return;

	__dma_single_cpu_to_dev(dma_to_virt(dev, handle) + offset, size, dir);
}

static inline void dma_sync_single_for_cpu(struct device *dev,
		dma_addr_t handle, size_t size, enum dma_data_direction dir)
{
	dma_sync_single_range_for_cpu(dev, handle, 0, size, dir);
}

static inline void dma_sync_single_for_device(struct device *dev,
		dma_addr_t handle, size_t size, enum dma_data_direction dir)
{
	dma_sync_single_range_for_device(dev, handle, 0, size, dir);
}

/*
 * The scatter list versions of the above methods.
 */
extern int dma_map_sg(struct device *, struct scatterlist *, int,
		enum dma_data_direction);
extern void dma_unmap_sg(struct device *, struct scatterlist *, int,
		enum dma_data_direction);
extern void dma_sync_sg_for_cpu(struct device *, struct scatterlist *, int,
		enum dma_data_direction);
extern void dma_sync_sg_for_device(struct device *, struct scatterlist *, int,
		enum dma_data_direction);


#endif /* __KERNEL__ */
#endif
