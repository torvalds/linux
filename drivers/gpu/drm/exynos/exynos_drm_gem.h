/* exynos_drm_gem.h
 *
 * Copyright (c) 2011 Samsung Electronics Co., Ltd.
 * Authoer: Inki Dae <inki.dae@samsung.com>
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * VA LINUX SYSTEMS AND/OR ITS SUPPLIERS BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 */

#ifndef _EXYNOS_DRM_GEM_H_
#define _EXYNOS_DRM_GEM_H_

#define to_exynos_gem_obj(x)	container_of(x,\
			struct exynos_drm_gem_obj, base)

/*
 * exynos drm gem buffer structure.
 *
 * @kvaddr: kernel virtual address to allocated memory region.
 * @dma_addr: bus address(accessed by dma) to allocated memory region.
 *	- this address could be physical address without IOMMU and
 *	device address with IOMMU.
 * @size: size of allocated memory region.
 */
struct exynos_drm_gem_buf {
	void __iomem		*kvaddr;
	dma_addr_t		dma_addr;
	unsigned long		size;
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
 *
 * P.S. this object would be transfered to user as kms_bo.handle so
 *	user can access the buffer through kms_bo.handle.
 */
struct exynos_drm_gem_obj {
	struct drm_gem_object base;
	struct exynos_drm_gem_buf *buffer;
};

/* create a new buffer and get a new gem handle. */
struct exynos_drm_gem_obj *exynos_drm_gem_create(struct drm_device *dev,
		struct drm_file *file_priv,
		unsigned int *handle, unsigned long size);

/*
 * request gem object creation and buffer allocation as the size
 * that it is calculated with framebuffer information such as width,
 * height and bpp.
 */
int exynos_drm_gem_create_ioctl(struct drm_device *dev, void *data,
		struct drm_file *file_priv);

/* get buffer offset to map to user space. */
int exynos_drm_gem_map_offset_ioctl(struct drm_device *dev, void *data,
		struct drm_file *file_priv);

/* unmap a buffer from user space. */
int exynos_drm_gem_munmap_ioctl(struct drm_device *dev, void *data,
		struct drm_file *file_priv);

/* initialize gem object. */
int exynos_drm_gem_init_object(struct drm_gem_object *obj);

/* free gem object. */
void exynos_drm_gem_free_object(struct drm_gem_object *gem_obj);

/* create memory region for drm framebuffer. */
int exynos_drm_gem_dumb_create(struct drm_file *file_priv,
		struct drm_device *dev, struct drm_mode_create_dumb *args);

/* map memory region for drm framebuffer to user space. */
int exynos_drm_gem_dumb_map_offset(struct drm_file *file_priv,
		struct drm_device *dev, uint32_t handle, uint64_t *offset);

/* page fault handler and mmap fault address(virtual) to physical memory. */
int exynos_drm_gem_fault(struct vm_area_struct *vma, struct vm_fault *vmf);

/*
 * mmap the physically continuous memory that a gem object contains
 * to user space.
 */
int exynos_drm_gem_mmap_ioctl(struct drm_device *dev, void *data,
		struct drm_file *file_priv);

/* set vm_flags and we can change the vm attribute to other one at here. */
int exynos_drm_gem_mmap(struct file *filp, struct vm_area_struct *vma);

/*
 * destroy memory region allocated.
 *	- a gem handle and physical memory region pointed by a gem object
 *	would be released by drm_gem_handle_delete().
 */
int exynos_drm_gem_dumb_destroy(struct drm_file *file_priv,
		struct drm_device *dev, unsigned int handle);

#endif
