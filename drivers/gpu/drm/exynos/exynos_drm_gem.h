/* exynos_drm_gem.h
 *
 * Copyright (c) 2011 Samsung Electronics Co., Ltd.
 * Authoer: Inki Dae <inki.dae@samsung.com>
 *
 * This program is free software; you can redistribute  it and/or modify it
 * under  the terms of  the GNU General  Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 */

#ifndef _EXYNOS_DRM_GEM_H_
#define _EXYNOS_DRM_GEM_H_

#define to_exynos_gem_obj(x)	container_of(x,\
			struct exynos_drm_gem_obj, base)

#define IS_NONCONTIG_BUFFER(f)		(f & EXYNOS_BO_NONCONTIG)

/*
 * exynos drm gem buffer structure.
 *
 * @kvaddr: kernel virtual address to allocated memory region.
 * *userptr: user space address.
 * @dma_addr: bus address(accessed by dma) to allocated memory region.
 *	- this address could be physical address without IOMMU and
 *	device address with IOMMU.
 * @write: whether pages will be written to by the caller.
 * @pages: Array of backing pages.
 * @sgt: sg table to transfer page data.
 * @size: size of allocated memory region.
 * @pfnmap: indicate whether memory region from userptr is mmaped with
 *	VM_PFNMAP or not.
 */
struct exynos_drm_gem_buf {
	void __iomem		*kvaddr;
	unsigned long		userptr;
	dma_addr_t		dma_addr;
	struct dma_attrs	dma_attrs;
	unsigned int		write;
	struct page		**pages;
	struct sg_table		*sgt;
	unsigned long		size;
	bool			pfnmap;
};

/*
 * exynos drm buffer structure.
 *
 * @base: a gem object.
 *	- a new handle to this gem object would be created
 *	by drm_gem_handle_create().
 * @buffer: a pointer to exynos_drm_gem_buffer object.
 *	- contain the information to memory region allocated
 *	by user request or at framebuffer creation.
 *	continuous memory region allocated by user request
 *	or at framebuffer creation.
 * @size: size requested from user, in bytes and this size is aligned
 *	in page unit.
 * @vma: a pointer to vm_area.
 * @flags: indicate memory type to allocated buffer and cache attruibute.
 *
 * P.S. this object would be transfered to user as kms_bo.handle so
 *	user can access the buffer through kms_bo.handle.
 */
struct exynos_drm_gem_obj {
	struct drm_gem_object		base;
	struct exynos_drm_gem_buf	*buffer;
	unsigned long			size;
	struct vm_area_struct		*vma;
	unsigned int			flags;
};

struct page **exynos_gem_get_pages(struct drm_gem_object *obj, gfp_t gfpmask);

/* destroy a buffer with gem object */
void exynos_drm_gem_destroy(struct exynos_drm_gem_obj *exynos_gem_obj);

/* create a private gem object and initialize it. */
struct exynos_drm_gem_obj *exynos_drm_gem_init(struct drm_device *dev,
						      unsigned long size);

/* create a new buffer with gem object */
struct exynos_drm_gem_obj *exynos_drm_gem_create(struct drm_device *dev,
						unsigned int flags,
						unsigned long size);

/*
 * request gem object creation and buffer allocation as the size
 * that it is calculated with framebuffer information such as width,
 * height and bpp.
 */
int exynos_drm_gem_create_ioctl(struct drm_device *dev, void *data,
				struct drm_file *file_priv);

/*
 * get dma address from gem handle and this function could be used for
 * other drivers such as 2d/3d acceleration drivers.
 * with this function call, gem object reference count would be increased.
 */
dma_addr_t *exynos_drm_gem_get_dma_addr(struct drm_device *dev,
					unsigned int gem_handle,
					struct drm_file *filp);

/*
 * put dma address from gem handle and this function could be used for
 * other drivers such as 2d/3d acceleration drivers.
 * with this function call, gem object reference count would be decreased.
 */
void exynos_drm_gem_put_dma_addr(struct drm_device *dev,
					unsigned int gem_handle,
					struct drm_file *filp);

/* get buffer offset to map to user space. */
int exynos_drm_gem_map_offset_ioctl(struct drm_device *dev, void *data,
				    struct drm_file *file_priv);

/*
 * mmap the physically continuous memory that a gem object contains
 * to user space.
 */
int exynos_drm_gem_mmap_ioctl(struct drm_device *dev, void *data,
			      struct drm_file *file_priv);

/* map user space allocated by malloc to pages. */
int exynos_drm_gem_userptr_ioctl(struct drm_device *dev, void *data,
				      struct drm_file *file_priv);

/* get buffer information to memory region allocated by gem. */
int exynos_drm_gem_get_ioctl(struct drm_device *dev, void *data,
				      struct drm_file *file_priv);

/* initialize gem object. */
int exynos_drm_gem_init_object(struct drm_gem_object *obj);

/* free gem object. */
void exynos_drm_gem_free_object(struct drm_gem_object *gem_obj);

/* create memory region for drm framebuffer. */
int exynos_drm_gem_dumb_create(struct drm_file *file_priv,
			       struct drm_device *dev,
			       struct drm_mode_create_dumb *args);

/* map memory region for drm framebuffer to user space. */
int exynos_drm_gem_dumb_map_offset(struct drm_file *file_priv,
				   struct drm_device *dev, uint32_t handle,
				   uint64_t *offset);

/*
 * destroy memory region allocated.
 *	- a gem handle and physical memory region pointed by a gem object
 *	would be released by drm_gem_handle_delete().
 */
int exynos_drm_gem_dumb_destroy(struct drm_file *file_priv,
				struct drm_device *dev,
				unsigned int handle);

/* page fault handler and mmap fault address(virtual) to physical memory. */
int exynos_drm_gem_fault(struct vm_area_struct *vma, struct vm_fault *vmf);

/* set vm_flags and we can change the vm attribute to other one at here. */
int exynos_drm_gem_mmap(struct file *filp, struct vm_area_struct *vma);

static inline int vma_is_io(struct vm_area_struct *vma)
{
	return !!(vma->vm_flags & (VM_IO | VM_PFNMAP));
}

/* get a copy of a virtual memory region. */
struct vm_area_struct *exynos_gem_get_vma(struct vm_area_struct *vma);

/* release a userspace virtual memory area. */
void exynos_gem_put_vma(struct vm_area_struct *vma);

/* get pages from user space. */
int exynos_gem_get_pages_from_userptr(unsigned long start,
						unsigned int npages,
						struct page **pages,
						struct vm_area_struct *vma);

/* drop the reference to pages. */
void exynos_gem_put_pages_to_userptr(struct page **pages,
					unsigned int npages,
					struct vm_area_struct *vma);

/* map sgt with dma region. */
int exynos_gem_map_sgt_with_dma(struct drm_device *drm_dev,
				struct sg_table *sgt,
				enum dma_data_direction dir);

/* unmap sgt from dma region. */
void exynos_gem_unmap_sgt_from_dma(struct drm_device *drm_dev,
				struct sg_table *sgt,
				enum dma_data_direction dir);

#endif
