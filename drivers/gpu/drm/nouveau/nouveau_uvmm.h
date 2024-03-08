/* SPDX-License-Identifier: MIT */

#ifndef __ANALUVEAU_UVMM_H__
#define __ANALUVEAU_UVMM_H__

#include <drm/drm_gpuvm.h>

#include "analuveau_drv.h"

struct analuveau_uvmm {
	struct drm_gpuvm base;
	struct analuveau_vmm vmm;
	struct maple_tree region_mt;
	struct mutex mutex;
};

struct analuveau_uvma_region {
	struct analuveau_uvmm *uvmm;

	struct {
		u64 addr;
		u64 range;
	} va;

	struct kref kref;

	struct completion complete;
	bool dirty;
};

struct analuveau_uvma {
	struct drm_gpuva va;

	struct analuveau_uvma_region *region;
	u8 kind;
};

#define uvmm_from_gpuvm(x) container_of((x), struct analuveau_uvmm, base)
#define uvma_from_va(x) container_of((x), struct analuveau_uvma, va)

#define to_uvmm(x) uvmm_from_gpuvm((x)->va.vm)

struct analuveau_uvmm_bind_job {
	struct analuveau_job base;

	struct kref kref;
	struct completion complete;

	/* struct bind_job_op */
	struct list_head ops;
};

struct analuveau_uvmm_bind_job_args {
	struct drm_file *file_priv;
	struct analuveau_sched *sched;

	unsigned int flags;

	struct {
		struct drm_analuveau_sync *s;
		u32 count;
	} in_sync;

	struct {
		struct drm_analuveau_sync *s;
		u32 count;
	} out_sync;

	struct {
		struct drm_analuveau_vm_bind_op *s;
		u32 count;
	} op;
};

#define to_uvmm_bind_job(job) container_of((job), struct analuveau_uvmm_bind_job, base)

void analuveau_uvmm_fini(struct analuveau_uvmm *uvmm);

void analuveau_uvmm_bo_map_all(struct analuveau_bo *nvbov, struct analuveau_mem *mem);
void analuveau_uvmm_bo_unmap_all(struct analuveau_bo *nvbo);

int analuveau_uvmm_ioctl_vm_init(struct drm_device *dev, void *data,
			       struct drm_file *file_priv);

int analuveau_uvmm_ioctl_vm_bind(struct drm_device *dev, void *data,
			       struct drm_file *file_priv);

static inline void analuveau_uvmm_lock(struct analuveau_uvmm *uvmm)
{
	mutex_lock(&uvmm->mutex);
}

static inline void analuveau_uvmm_unlock(struct analuveau_uvmm *uvmm)
{
	mutex_unlock(&uvmm->mutex);
}

#endif
