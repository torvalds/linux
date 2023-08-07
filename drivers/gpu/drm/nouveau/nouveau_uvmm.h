/* SPDX-License-Identifier: MIT */

#ifndef __NOUVEAU_UVMM_H__
#define __NOUVEAU_UVMM_H__

#include <drm/drm_gpuva_mgr.h>

#include "nouveau_drv.h"

struct nouveau_uvmm {
	struct nouveau_vmm vmm;
	struct drm_gpuva_manager umgr;
	struct maple_tree region_mt;
	struct mutex mutex;
	struct dma_resv resv;

	u64 kernel_managed_addr;
	u64 kernel_managed_size;

	bool disabled;
};

struct nouveau_uvma_region {
	struct nouveau_uvmm *uvmm;

	struct {
		u64 addr;
		u64 range;
	} va;

	struct kref kref;

	struct completion complete;
	bool dirty;
};

struct nouveau_uvma {
	struct drm_gpuva va;

	struct nouveau_uvma_region *region;
	u8 kind;
};

#define uvmm_from_mgr(x) container_of((x), struct nouveau_uvmm, umgr)
#define uvma_from_va(x) container_of((x), struct nouveau_uvma, va)

#define to_uvmm(x) uvmm_from_mgr((x)->va.mgr)

struct nouveau_uvmm_bind_job {
	struct nouveau_job base;

	struct kref kref;
	struct list_head entry;
	struct work_struct work;
	struct completion complete;

	/* struct bind_job_op */
	struct list_head ops;
};

struct nouveau_uvmm_bind_job_args {
	struct drm_file *file_priv;
	struct nouveau_sched_entity *sched_entity;

	unsigned int flags;

	struct {
		struct drm_nouveau_sync *s;
		u32 count;
	} in_sync;

	struct {
		struct drm_nouveau_sync *s;
		u32 count;
	} out_sync;

	struct {
		struct drm_nouveau_vm_bind_op *s;
		u32 count;
	} op;
};

#define to_uvmm_bind_job(job) container_of((job), struct nouveau_uvmm_bind_job, base)

int nouveau_uvmm_init(struct nouveau_uvmm *uvmm, struct nouveau_cli *cli,
		      u64 kernel_managed_addr, u64 kernel_managed_size);
void nouveau_uvmm_fini(struct nouveau_uvmm *uvmm);

void nouveau_uvmm_bo_map_all(struct nouveau_bo *nvbov, struct nouveau_mem *mem);
void nouveau_uvmm_bo_unmap_all(struct nouveau_bo *nvbo);

int nouveau_uvmm_ioctl_vm_init(struct drm_device *dev, void *data,
			       struct drm_file *file_priv);

int nouveau_uvmm_ioctl_vm_bind(struct drm_device *dev, void *data,
			       struct drm_file *file_priv);

static inline void nouveau_uvmm_lock(struct nouveau_uvmm *uvmm)
{
	mutex_lock(&uvmm->mutex);
}

static inline void nouveau_uvmm_unlock(struct nouveau_uvmm *uvmm)
{
	mutex_unlock(&uvmm->mutex);
}

#endif
