/* SPDX-License-Identifier: MIT */
/*
 * Copyright 2023 Advanced Micro Devices, Inc.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE COPYRIGHT HOLDER(S) OR AUTHOR(S) BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 *
 */

#ifndef AMDGPU_USERQ_H_
#define AMDGPU_USERQ_H_
#include "amdgpu_eviction_fence.h"

#define AMDGPU_MAX_USERQ_COUNT 512

#define to_ev_fence(f) container_of(f, struct amdgpu_eviction_fence, base)
#define uq_mgr_to_fpriv(u) container_of(u, struct amdgpu_fpriv, userq_mgr)
#define work_to_uq_mgr(w, name) container_of(w, struct amdgpu_userq_mgr, name)

enum amdgpu_userq_state {
	AMDGPU_USERQ_STATE_UNMAPPED = 0,
	AMDGPU_USERQ_STATE_MAPPED,
	AMDGPU_USERQ_STATE_PREEMPTED,
	AMDGPU_USERQ_STATE_HUNG,
};

struct amdgpu_mqd_prop;

struct amdgpu_userq_obj {
	void		 *cpu_ptr;
	uint64_t	 gpu_addr;
	struct amdgpu_bo *obj;
};

struct amdgpu_usermode_queue {
	int			queue_type;
	enum amdgpu_userq_state state;
	uint64_t		doorbell_handle;
	uint64_t		doorbell_index;
	uint64_t		flags;
	struct amdgpu_mqd_prop	*userq_prop;
	struct amdgpu_userq_mgr *userq_mgr;
	struct amdgpu_vm	*vm;
	struct amdgpu_userq_obj mqd;
	struct amdgpu_userq_obj	db_obj;
	struct amdgpu_userq_obj fw_obj;
	struct amdgpu_userq_obj wptr_obj;
	struct xarray		fence_drv_xa;
	struct amdgpu_userq_fence_driver *fence_drv;
	struct dma_fence	*last_fence;
	u32			xcp_id;
	int			priority;
	struct dentry		*debugfs_queue;
};

struct amdgpu_userq_funcs {
	int (*mqd_create)(struct amdgpu_userq_mgr *uq_mgr,
			  struct drm_amdgpu_userq_in *args,
			  struct amdgpu_usermode_queue *queue);
	void (*mqd_destroy)(struct amdgpu_userq_mgr *uq_mgr,
			    struct amdgpu_usermode_queue *uq);
	int (*unmap)(struct amdgpu_userq_mgr *uq_mgr,
		     struct amdgpu_usermode_queue *queue);
	int (*map)(struct amdgpu_userq_mgr *uq_mgr,
		   struct amdgpu_usermode_queue *queue);
	int (*preempt)(struct amdgpu_userq_mgr *uq_mgr,
		   struct amdgpu_usermode_queue *queue);
	int (*restore)(struct amdgpu_userq_mgr *uq_mgr,
		   struct amdgpu_usermode_queue *queue);
	int (*detect_and_reset)(struct amdgpu_device *adev,
		  int queue_type);
};

/* Usermode queues for gfx */
struct amdgpu_userq_mgr {
	struct idr			userq_idr;
	struct mutex			userq_mutex;
	struct amdgpu_device		*adev;
	struct delayed_work		resume_work;
	struct list_head		list;
	struct drm_file			*file;
};

struct amdgpu_db_info {
	uint64_t doorbell_handle;
	uint32_t queue_type;
	uint32_t doorbell_offset;
	struct amdgpu_userq_obj	*db_obj;
};

int amdgpu_userq_ioctl(struct drm_device *dev, void *data, struct drm_file *filp);

int amdgpu_userq_mgr_init(struct amdgpu_userq_mgr *userq_mgr, struct drm_file *file_priv,
			  struct amdgpu_device *adev);

void amdgpu_userq_mgr_fini(struct amdgpu_userq_mgr *userq_mgr);

int amdgpu_userq_create_object(struct amdgpu_userq_mgr *uq_mgr,
			       struct amdgpu_userq_obj *userq_obj,
			       int size);

void amdgpu_userq_destroy_object(struct amdgpu_userq_mgr *uq_mgr,
				 struct amdgpu_userq_obj *userq_obj);

void amdgpu_userq_evict(struct amdgpu_userq_mgr *uq_mgr,
			struct amdgpu_eviction_fence *ev_fence);

void amdgpu_userq_ensure_ev_fence(struct amdgpu_userq_mgr *userq_mgr,
				  struct amdgpu_eviction_fence_mgr *evf_mgr);

uint64_t amdgpu_userq_get_doorbell_index(struct amdgpu_userq_mgr *uq_mgr,
					 struct amdgpu_db_info *db_info,
					     struct drm_file *filp);

u32 amdgpu_userq_get_supported_ip_mask(struct amdgpu_device *adev);

int amdgpu_userq_suspend(struct amdgpu_device *adev);
int amdgpu_userq_resume(struct amdgpu_device *adev);

int amdgpu_userq_stop_sched_for_enforce_isolation(struct amdgpu_device *adev,
						  u32 idx);
int amdgpu_userq_start_sched_for_enforce_isolation(struct amdgpu_device *adev,
						   u32 idx);

int amdgpu_userq_input_va_validate(struct amdgpu_vm *vm, u64 addr,
				   u64 expected_size);
#endif
