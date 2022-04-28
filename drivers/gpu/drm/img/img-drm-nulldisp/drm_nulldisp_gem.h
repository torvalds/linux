/*
 * @File
 * @Codingstyle LinuxKernel
 * @Copyright   Copyright (c) Imagination Technologies Ltd. All Rights Reserved
 * @License     Dual MIT/GPLv2
 *
 * The contents of this file are subject to the MIT license as set out below.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * Alternatively, the contents of this file may be used under the terms of
 * the GNU General Public License Version 2 ("GPL") in which case the provisions
 * of GPL are applicable instead of those above.
 *
 * If you wish to allow use of your version of this file only under the terms of
 * GPL, and not to allow others to use your version of this file under the terms
 * of the MIT license, indicate your decision by deleting the provisions above
 * and replace them with the notice and other provisions required by GPL as set
 * out in the file called "GPL-COPYING" included in this distribution. If you do
 * not delete the provisions above, a recipient may use your version of this file
 * under the terms of either the MIT license or GPL.
 *
 * This License is also included in this distribution in the file called
 * "MIT-COPYING".
 *
 * EXCEPT AS OTHERWISE STATED IN A NEGOTIATED AGREEMENT: (A) THE SOFTWARE IS
 * PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING
 * BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR
 * PURPOSE AND NONINFRINGEMENT; AND (B) IN NO EVENT SHALL THE AUTHORS OR
 * COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
 * IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */

#if !defined(__DRM_NULLDISP_H__)
#define __DRM_NULLDISP_H__

#include <linux/version.h>

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(5, 5, 0))
#include <drm/drm_device.h>
#include <drm/drm_file.h>
#include <linux/types.h>
#include <linux/vmalloc.h>

struct dma_buf_attachment;
struct vm_area_struct;
struct vm_fault;
#else
#include <drm/drmP.h>
#endif

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(5, 9, 0))
extern const struct drm_gem_object_funcs nulldisp_gem_funcs;
#endif /* LINUX_VERSION_CODE >= KERNEL_VERSION(5, 9, 0) */

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(3, 18, 0))
#include <drm/drm_gem.h>
#endif

int nulldisp_gem_object_get_pages(struct drm_gem_object *obj);

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(4, 11, 0))
#if (LINUX_VERSION_CODE < KERNEL_VERSION(4, 17, 0))
typedef int vm_fault_t;
#endif
vm_fault_t nulldisp_gem_object_vm_fault(struct vm_fault *vmf);
#else
int nulldisp_gem_object_vm_fault(struct vm_area_struct *vma,
				 struct vm_fault *vmf);
#endif

void nulldisp_gem_vm_open(struct vm_area_struct *vma);

void nulldisp_gem_vm_close(struct vm_area_struct *vma);

void nulldisp_gem_object_free(struct drm_gem_object *obj);

int nulldisp_gem_prime_pin(struct drm_gem_object *obj);

void nulldisp_gem_prime_unpin(struct drm_gem_object *obj);

struct sg_table *nulldisp_gem_prime_get_sg_table(struct drm_gem_object *obj);

struct drm_gem_object *
nulldisp_gem_prime_import_sg_table(struct drm_device *dev,
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(3, 18, 0))
				   struct dma_buf_attachment *attach,
#else
				   size_t size,
#endif
				   struct sg_table *sgt);

#if (LINUX_VERSION_CODE < KERNEL_VERSION(5, 4, 0))
struct dma_buf *nulldisp_gem_prime_export(struct drm_device *dev,
					  struct drm_gem_object *obj,
					  int flags);
#endif

#if (LINUX_VERSION_CODE < KERNEL_VERSION(5, 11, 0))
void *nulldisp_gem_prime_vmap(struct drm_gem_object *obj);
#else
int nulldisp_gem_prime_vmap(struct drm_gem_object *obj, struct dma_buf_map *map);
#endif /* LINUX_VERSION_CODE < KERNEL_VERSION(5, 11, 0) */

#if (LINUX_VERSION_CODE < KERNEL_VERSION(5, 11, 0))
void nulldisp_gem_prime_vunmap(struct drm_gem_object *obj, void *vaddr);
#else
void nulldisp_gem_prime_vunmap(struct drm_gem_object *obj, struct dma_buf_map *map);
#endif /* LINUX_VERSION_CODE < KERNEL_VERSION(5, 11, 0) */

int nulldisp_gem_prime_mmap(struct drm_gem_object *obj,
			    struct vm_area_struct *vma);

#if (LINUX_VERSION_CODE < KERNEL_VERSION(5, 4, 0))
struct dma_resv *
nulldisp_gem_prime_res_obj(struct drm_gem_object *obj);
#endif

int nulldisp_gem_dumb_create(struct drm_file *file,
			     struct drm_device *dev,
			     struct drm_mode_create_dumb *args);

int nulldisp_gem_dumb_map_offset(struct drm_file *file,
				 struct drm_device *dev,
				 uint32_t handle,
				 uint64_t *offset);

/* internal interfaces */
struct dma_resv *nulldisp_gem_get_resv(struct drm_gem_object *obj);

int nulldisp_gem_object_mmap_ioctl(struct drm_device *dev,
				   void *data,
				   struct drm_file *file);

int nulldisp_gem_object_cpu_prep_ioctl(struct drm_device *dev,
				       void *data,
				       struct drm_file *file);

int nulldisp_gem_object_cpu_fini_ioctl(struct drm_device *dev,
				       void *data,
				       struct drm_file *file);

int nulldisp_gem_object_create_ioctl(struct drm_device *dev,
				     void *data,
				     struct drm_file *file);

#endif	/* !defined(__DRM_NULLDISP_H__) */
