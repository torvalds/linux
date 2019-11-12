/*
 * Copyright 2017 Advanced Micro Devices, Inc.
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
 * Authors: Christian König
 */
#ifndef __AMDGPU_MN_H__
#define __AMDGPU_MN_H__

#include <linux/types.h>
#include <linux/hmm.h>
#include <linux/rwsem.h>
#include <linux/workqueue.h>
#include <linux/interval_tree.h>

enum amdgpu_mn_type {
	AMDGPU_MN_TYPE_GFX,
	AMDGPU_MN_TYPE_HSA,
};

/**
 * struct amdgpu_mn
 *
 * @adev: amdgpu device pointer
 * @type: type of MMU notifier
 * @work: destruction work item
 * @node: hash table node to find structure by adev and mn
 * @lock: rw semaphore protecting the notifier nodes
 * @mirror: HMM mirror function support
 *
 * Data for each amdgpu device and process address space.
 */
struct amdgpu_mn {
	/* constant after initialisation */
	struct amdgpu_device	*adev;
	enum amdgpu_mn_type	type;

	/* only used on destruction */
	struct work_struct	work;

	/* protected by adev->mn_lock */
	struct hlist_node	node;

	/* objects protected by lock */
	struct rw_semaphore	lock;

#ifdef CONFIG_HMM_MIRROR
	/* HMM mirror */
	struct hmm_mirror	mirror;
#endif
};

#if defined(CONFIG_HMM_MIRROR)
void amdgpu_mn_lock(struct amdgpu_mn *mn);
void amdgpu_mn_unlock(struct amdgpu_mn *mn);
struct amdgpu_mn *amdgpu_mn_get(struct amdgpu_device *adev,
				enum amdgpu_mn_type type);
int amdgpu_mn_register(struct amdgpu_bo *bo, unsigned long addr);
void amdgpu_mn_unregister(struct amdgpu_bo *bo);
void amdgpu_hmm_init_range(struct hmm_range *range);
#else
static inline void amdgpu_mn_lock(struct amdgpu_mn *mn) {}
static inline void amdgpu_mn_unlock(struct amdgpu_mn *mn) {}
static inline struct amdgpu_mn *amdgpu_mn_get(struct amdgpu_device *adev,
					      enum amdgpu_mn_type type)
{
	return NULL;
}
static inline int amdgpu_mn_register(struct amdgpu_bo *bo, unsigned long addr)
{
	DRM_WARN_ONCE("HMM_MIRROR kernel config option is not enabled, "
		      "add CONFIG_ZONE_DEVICE=y in config file to fix this\n");
	return -ENODEV;
}
static inline void amdgpu_mn_unregister(struct amdgpu_bo *bo) {}
#endif

#endif
