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

#ifndef AMDGPU_USERQUEUE_H_
#define AMDGPU_USERQUEUE_H_

#define AMDGPU_MAX_USERQ_COUNT 512

struct amdgpu_mqd_prop;

struct amdgpu_userq_obj {
	void		 *cpu_ptr;
	uint64_t	 gpu_addr;
	struct amdgpu_bo *obj;
};

struct amdgpu_usermode_queue {
	int			queue_type;
	uint64_t		doorbell_handle;
	uint64_t		doorbell_index;
	uint64_t		flags;
	struct amdgpu_mqd_prop	*userq_prop;
	struct amdgpu_userq_mgr *userq_mgr;
	struct amdgpu_vm	*vm;
	struct amdgpu_userq_obj mqd;
};

struct amdgpu_userq_funcs {
	int (*mqd_create)(struct amdgpu_userq_mgr *uq_mgr,
			  struct drm_amdgpu_userq_in *args,
			  struct amdgpu_usermode_queue *queue);
	void (*mqd_destroy)(struct amdgpu_userq_mgr *uq_mgr,
			    struct amdgpu_usermode_queue *uq);
};

/* Usermode queues for gfx */
struct amdgpu_userq_mgr {
	struct idr			userq_idr;
	struct mutex			userq_mutex;
	struct amdgpu_device		*adev;
};

int amdgpu_userq_ioctl(struct drm_device *dev, void *data, struct drm_file *filp);

int amdgpu_userq_mgr_init(struct amdgpu_userq_mgr *userq_mgr, struct amdgpu_device *adev);

void amdgpu_userq_mgr_fini(struct amdgpu_userq_mgr *userq_mgr);

int amdgpu_userqueue_create_object(struct amdgpu_userq_mgr *uq_mgr,
				   struct amdgpu_userq_obj *userq_obj,
				   int size);

void amdgpu_userqueue_destroy_object(struct amdgpu_userq_mgr *uq_mgr,
				     struct amdgpu_userq_obj *userq_obj);
#endif
