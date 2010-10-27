/*
 * drivers/base/dma-mapping.c - arch-independent dma-mapping routines
 *
 * Copyright (c) 2006  SUSE Linux Products GmbH
 * Copyright (c) 2006  Tejun Heo <teheo@suse.de>
 *
 * This file is released under the GPLv2.
 */

#include <linux/dma-mapping.h>
#include <linux/gfp.h>

/*
 * Managed DMA API
 */
struct dma_devres {
	size_t		size;
	void		*vaddr;
	dma_addr_t	dma_handle;
};

static void dmam_coherent_release(struct device *dev, void *res)
{
	struct dma_devres *this = res;

	dma_free_coherent(dev, this->size, this->vaddr, this->dma_handle);
}

static void dmam_noncoherent_release(struct device *dev, void *res)
{
	struct dma_devres *this = res;

	dma_free_noncoherent(dev, this->size, this->vaddr, this->dma_handle);
}

static int dmam_match(struct device *dev, void *res, void *match_data)
{
	struct dma_devres *this = res, *match = match_data;

	if (this->vaddr == match->vaddr) {
		WARN_ON(this->size != match->size ||
			this->dma_handle != match->dma_handle);
		return 1;
	}
	return 0;
}

/**
 * dmam_alloc_coherent - Managed dma_alloc_coherent()
 * @dev: Device to allocate coherent memory for
 * @size: Size of allocation
 * @dma_handle: Out argument for allocated DMA handle
 * @gfp: Allocation flags
 *
 * Managed dma_alloc_coherent().  Memory allocated using this function
 * will be automatically released on driver detach.
 *
 * RETURNS:
 * Pointer to allocated memory on success, NULL on failure.
 */
void * dmam_alloc_coherent(struct device *dev, size_t size,
			   dma_addr_t *dma_handle, gfp_t gfp)
{
	struct dma_devres *dr;
	void *vaddr;

	dr = devres_alloc(dmam_coherent_release, sizeof(*dr), gfp);
	if (!dr)
		return NULL;

	vaddr = dma_alloc_coherent(dev, size, dma_handle, gfp);
	if (!vaddr) {
		devres_free(dr);
		return NULL;
	}

	dr->vaddr = vaddr;
	dr->dma_handle = *dma_handle;
	dr->size = size;

	devres_add(dev, dr);

	return vaddr;
}
EXPORT_SYMBOL(dmam_alloc_coherent);

/**
 * dmam_free_coherent - Managed dma_free_coherent()
 * @dev: Device to free coherent memory for
 * @size: Size of allocation
 * @vaddr: Virtual address of the memory to free
 * @dma_handle: DMA handle of the memory to free
 *
 * Managed dma_free_coherent().
 */
void dmam_free_coherent(struct device *dev, size_t size, void *vaddr,
			dma_addr_t dma_handle)
{
	struct dma_devres match_data = { size, vaddr, dma_handle };

	dma_free_coherent(dev, size, vaddr, dma_handle);
	WARN_ON(devres_destroy(dev, dmam_coherent_release, dmam_match,
			       &match_data));
}
EXPORT_SYMBOL(dmam_free_coherent);

/**
 * dmam_alloc_non_coherent - Managed dma_alloc_non_coherent()
 * @dev: Device to allocate non_coherent memory for
 * @size: Size of allocation
 * @dma_handle: Out argument for allocated DMA handle
 * @gfp: Allocation flags
 *
 * Managed dma_alloc_non_coherent().  Memory allocated using this
 * function will be automatically released on driver detach.
 *
 * RETURNS:
 * Pointer to allocated memory on success, NULL on failure.
 */
void *dmam_alloc_noncoherent(struct device *dev, size_t size,
			     dma_addr_t *dma_handle, gfp_t gfp)
{
	struct dma_devres *dr;
	void *vaddr;

	dr = devres_alloc(dmam_noncoherent_release, sizeof(*dr), gfp);
	if (!dr)
		return NULL;

	vaddr = dma_alloc_noncoherent(dev, size, dma_handle, gfp);
	if (!vaddr) {
		devres_free(dr);
		return NULL;
	}

	dr->vaddr = vaddr;
	dr->dma_handle = *dma_handle;
	dr->size = size;

	devres_add(dev, dr);

	return vaddr;
}
EXPORT_SYMBOL(dmam_alloc_noncoherent);

/**
 * dmam_free_coherent - Managed dma_free_noncoherent()
 * @dev: Device to free noncoherent memory for
 * @size: Size of allocation
 * @vaddr: Virtual address of the memory to free
 * @dma_handle: DMA handle of the memory to free
 *
 * Managed dma_free_noncoherent().
 */
void dmam_free_noncoherent(struct device *dev, size_t size, void *vaddr,
			   dma_addr_t dma_handle)
{
	struct dma_devres match_data = { size, vaddr, dma_handle };

	dma_free_noncoherent(dev, size, vaddr, dma_handle);
	WARN_ON(!devres_destroy(dev, dmam_noncoherent_release, dmam_match,
				&match_data));
}
EXPORT_SYMBOL(dmam_free_noncoherent);

#ifdef ARCH_HAS_DMA_DECLARE_COHERENT_MEMORY

static void dmam_coherent_decl_release(struct device *dev, void *res)
{
	dma_release_declared_memory(dev);
}

/**
 * dmam_declare_coherent_memory - Managed dma_declare_coherent_memory()
 * @dev: Device to declare coherent memory for
 * @bus_addr: Bus address of coherent memory to be declared
 * @device_addr: Device address of coherent memory to be declared
 * @size: Size of coherent memory to be declared
 * @flags: Flags
 *
 * Managed dma_declare_coherent_memory().
 *
 * RETURNS:
 * 0 on success, -errno on failure.
 */
int dmam_declare_coherent_memory(struct device *dev, dma_addr_t bus_addr,
				 dma_addr_t device_addr, size_t size, int flags)
{
	void *res;
	int rc;

	res = devres_alloc(dmam_coherent_decl_release, 0, GFP_KERNEL);
	if (!res)
		return -ENOMEM;

	rc = dma_declare_coherent_memory(dev, bus_addr, device_addr, size,
					 flags);
	if (rc == 0)
		devres_add(dev, res);
	else
		devres_free(res);

	return rc;
}
EXPORT_SYMBOL(dmam_declare_coherent_memory);

/**
 * dmam_release_declared_memory - Managed dma_release_declared_memory().
 * @dev: Device to release declared coherent memory for
 *
 * Managed dmam_release_declared_memory().
 */
void dmam_release_declared_memory(struct device *dev)
{
	WARN_ON(devres_destroy(dev, dmam_coherent_decl_release, NULL, NULL));
}
EXPORT_SYMBOL(dmam_release_declared_memory);

#endif
