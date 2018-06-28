/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2015-2018 Etnaviv Project
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
#include <linux/time64.h>
#include <linux/types.h>
#include <linux/sizes.h>

#include <drm/drmP.h>
#include <drm/drm_crtc_helper.h>
#include <drm/drm_fb_helper.h>
#include <drm/drm_gem.h>
#include <drm/etnaviv_drm.h>
#include <drm/gpu_scheduler.h>

struct etnaviv_cmdbuf;
struct etnaviv_gpu;
struct etnaviv_mmu;
struct etnaviv_gem_object;
struct etnaviv_gem_submit;

struct etnaviv_file_private {
	/*
	 * When per-context address spaces are supported we'd keep track of
	 * the context's page-tables here.
	 */
	struct drm_sched_entity		sched_entity[ETNA_MAX_PIPES];
};

struct etnaviv_drm_private {
	int num_gpus;
	struct etnaviv_gpu *gpu[ETNA_MAX_PIPES];

	/* list of GEM objects: */
	struct mutex gem_lock;
	struct list_head gem_list;
};

int etnaviv_ioctl_gem_submit(struct drm_device *dev, void *data,
		struct drm_file *file);

int etnaviv_gem_mmap(struct file *filp, struct vm_area_struct *vma);
int etnaviv_gem_fault(struct vm_fault *vmf);
int etnaviv_gem_mmap_offset(struct drm_gem_object *obj, u64 *offset);
struct sg_table *etnaviv_gem_prime_get_sg_table(struct drm_gem_object *obj);
void *etnaviv_gem_prime_vmap(struct drm_gem_object *obj);
void etnaviv_gem_prime_vunmap(struct drm_gem_object *obj, void *vaddr);
int etnaviv_gem_prime_mmap(struct drm_gem_object *obj,
			   struct vm_area_struct *vma);
struct reservation_object *etnaviv_gem_prime_res_obj(struct drm_gem_object *obj);
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
int etnaviv_gem_new_userptr(struct drm_device *dev, struct drm_file *file,
	uintptr_t ptr, u32 size, u32 flags, u32 *handle);
u16 etnaviv_buffer_init(struct etnaviv_gpu *gpu);
u16 etnaviv_buffer_config_mmuv2(struct etnaviv_gpu *gpu, u32 mtlb_addr, u32 safe_addr);
u16 etnaviv_buffer_config_pta(struct etnaviv_gpu *gpu);
void etnaviv_buffer_end(struct etnaviv_gpu *gpu);
void etnaviv_sync_point_queue(struct etnaviv_gpu *gpu, unsigned int event);
void etnaviv_buffer_queue(struct etnaviv_gpu *gpu, u32 exec_state,
	unsigned int event, struct etnaviv_cmdbuf *cmdbuf);
void etnaviv_validate_init(void);
bool etnaviv_cmd_validate_one(struct etnaviv_gpu *gpu,
	u32 *stream, unsigned int size,
	struct drm_etnaviv_gem_submit_reloc *relocs, unsigned int reloc_size);

#ifdef CONFIG_DEBUG_FS
void etnaviv_gem_describe_objects(struct etnaviv_drm_private *priv,
	struct seq_file *m);
#endif

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

/*
 * Etnaviv timeouts are specified wrt CLOCK_MONOTONIC, not jiffies.
 * We need to calculate the timeout in terms of number of jiffies
 * between the specified timeout and the current CLOCK_MONOTONIC time.
 */
static inline unsigned long etnaviv_timeout_to_jiffies(
	const struct timespec *timeout)
{
	struct timespec64 ts, to;

	to = timespec_to_timespec64(*timeout);

	ktime_get_ts64(&ts);

	/* timeouts before "now" have already expired */
	if (timespec64_compare(&to, &ts) <= 0)
		return 0;

	ts = timespec64_sub(to, ts);

	return timespec64_to_jiffies(&ts);
}

#endif /* __ETNAVIV_DRV_H__ */
