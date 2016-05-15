/*
 * Copyright (C) 2015 Etnaviv Project
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published by
 * the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef __ETNAVIV_DRV_H__
#define __ETNAVIV_DRV_H__

#include <linux/kernel.h>
#include <linux/clk.h>
#include <linux/cpufreq.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/pm.h>
#include <linux/pm_runtime.h>
#include <linux/slab.h>
#include <linux/list.h>
#include <linux/iommu.h>
#include <linux/types.h>
#include <linux/sizes.h>

#include <drm/drmP.h>
#include <drm/drm_crtc_helper.h>
#include <drm/drm_fb_helper.h>
#include <drm/drm_gem.h>
#include <drm/etnaviv_drm.h>

struct etnaviv_cmdbuf;
struct etnaviv_gpu;
struct etnaviv_mmu;
struct etnaviv_gem_object;
struct etnaviv_gem_submit;

struct etnaviv_file_private {
	/* currently we don't do anything useful with this.. but when
	 * per-context address spaces are supported we'd keep track of
	 * the context's page-tables here.
	 */
	int dummy;
};

struct etnaviv_drm_private {
	int num_gpus;
	struct etnaviv_gpu *gpu[ETNA_MAX_PIPES];

	/* list of GEM objects: */
	struct mutex gem_lock;
	struct list_head gem_list;

	struct workqueue_struct *wq;
};

static inline void etnaviv_queue_work(struct drm_device *dev,
	struct work_struct *w)
{
	struct etnaviv_drm_private *priv = dev->dev_private;

	queue_work(priv->wq, w);
}

int etnaviv_ioctl_gem_submit(struct drm_device *dev, void *data,
		struct drm_file *file);

int etnaviv_gem_mmap(struct file *filp, struct vm_area_struct *vma);
int etnaviv_gem_fault(struct vm_area_struct *vma, struct vm_fault *vmf);
int etnaviv_gem_mmap_offset(struct drm_gem_object *obj, u64 *offset);
struct sg_table *etnaviv_gem_prime_get_sg_table(struct drm_gem_object *obj);
void *etnaviv_gem_prime_vmap(struct drm_gem_object *obj);
void etnaviv_gem_prime_vunmap(struct drm_gem_object *obj, void *vaddr);
struct drm_gem_object *etnaviv_gem_prime_import_sg_table(struct drm_device *dev,
	struct dma_buf_attachment *attach, struct sg_table *sg);
int etnaviv_gem_prime_pin(struct drm_gem_object *obj);
void etnaviv_gem_prime_unpin(struct drm_gem_object *obj);
void *etnaviv_gem_vmap(struct drm_gem_object *obj);
int etnaviv_gem_cpu_prep(struct drm_gem_object *obj, u32 op,
		struct timespec *timeout);
int etnaviv_gem_cpu_fini(struct drm_gem_object *obj);
void etnaviv_gem_free_object(struct drm_gem_object *obj);
int etnaviv_gem_new_handle(struct drm_device *dev, struct drm_file *file,
		u32 size, u32 flags, u32 *handle);
struct drm_gem_object *etnaviv_gem_new_locked(struct drm_device *dev,
		u32 size, u32 flags);
struct drm_gem_object *etnaviv_gem_new(struct drm_device *dev,
		u32 size, u32 flags);
int etnaviv_gem_new_userptr(struct drm_device *dev, struct drm_file *file,
	uintptr_t ptr, u32 size, u32 flags, u32 *handle);
u16 etnaviv_buffer_init(struct etnaviv_gpu *gpu);
void etnaviv_buffer_end(struct etnaviv_gpu *gpu);
void etnaviv_buffer_queue(struct etnaviv_gpu *gpu, unsigned int event,
	struct etnaviv_cmdbuf *cmdbuf);
void etnaviv_validate_init(void);
bool etnaviv_cmd_validate_one(struct etnaviv_gpu *gpu,
	u32 *stream, unsigned int size,
	struct drm_etnaviv_gem_submit_reloc *relocs, unsigned int reloc_size);

#ifdef CONFIG_DEBUG_FS
void etnaviv_gem_describe_objects(struct etnaviv_drm_private *priv,
	struct seq_file *m);
#endif

void __iomem *etnaviv_ioremap(struct platform_device *pdev, const char *name,
		const char *dbgname);
void etnaviv_writel(u32 data, void __iomem *addr);
u32 etnaviv_readl(const void __iomem *addr);

#define DBG(fmt, ...) DRM_DEBUG(fmt"\n", ##__VA_ARGS__)
#define VERB(fmt, ...) if (0) DRM_DEBUG(fmt"\n", ##__VA_ARGS__)

/*
 * Return the storage size of a structure with a variable length array.
 * The array is nelem elements of elem_size, where the base structure
 * is defined by base.  If the size overflows size_t, return zero.
 */
static inline size_t size_vstruct(size_t nelem, size_t elem_size, size_t base)
{
	if (elem_size && nelem > (SIZE_MAX - base) / elem_size)
		return 0;
	return base + nelem * elem_size;
}

/* returns true if fence a comes after fence b */
static inline bool fence_after(u32 a, u32 b)
{
	return (s32)(a - b) > 0;
}

static inline bool fence_after_eq(u32 a, u32 b)
{
	return (s32)(a - b) >= 0;
}

static inline unsigned long etnaviv_timeout_to_jiffies(
	const struct timespec *timeout)
{
	unsigned long timeout_jiffies = timespec_to_jiffies(timeout);
	unsigned long start_jiffies = jiffies;
	unsigned long remaining_jiffies;

	if (time_after(start_jiffies, timeout_jiffies))
		remaining_jiffies = 0;
	else
		remaining_jiffies = timeout_jiffies - start_jiffies;

	return remaining_jiffies;
}

#endif /* __ETNAVIV_DRV_H__ */
