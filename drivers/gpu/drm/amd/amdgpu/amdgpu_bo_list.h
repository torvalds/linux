/*
 * Copyright 2018 Advanced Micro Devices, Inc.
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
#ifndef __AMDGPU_BO_LIST_H__
#define __AMDGPU_BO_LIST_H__

#include <drm/amdgpu_drm.h>

struct hmm_range;

struct drm_file;

struct amdgpu_device;
struct amdgpu_bo;
struct amdgpu_bo_va;
struct amdgpu_fpriv;

struct amdgpu_bo_list_entry {
	struct amdgpu_bo		*bo;
	struct amdgpu_bo_va		*bo_va;
	uint32_t			priority;
	struct page			**user_pages;
	struct hmm_range		*range;
	bool				user_invalidated;
};

struct amdgpu_bo_list {
	struct rcu_head rhead;
	struct kref refcount;
	struct amdgpu_bo *gds_obj;
	struct amdgpu_bo *gws_obj;
	struct amdgpu_bo *oa_obj;
	unsigned first_userptr;
	unsigned num_entries;

	/* Protect access during command submission.
	 */
	struct mutex bo_list_mutex;

	struct amdgpu_bo_list_entry entries[] __counted_by(num_entries);
};

int amdgpu_bo_list_get(struct amdgpu_fpriv *fpriv, int id,
		       struct amdgpu_bo_list **result);
void amdgpu_bo_list_put(struct amdgpu_bo_list *list);
int amdgpu_bo_create_list_entry_array(struct drm_amdgpu_bo_list_in *in,
				      struct drm_amdgpu_bo_list_entry **info_param);

int amdgpu_bo_list_create(struct amdgpu_device *adev,
				 struct drm_file *filp,
				 struct drm_amdgpu_bo_list_entry *info,
				 size_t num_entries,
				 struct amdgpu_bo_list **list);

#define amdgpu_bo_list_for_each_entry(e, list) \
	for (e = list->entries; \
	     e != &list->entries[list->num_entries]; \
	     ++e)

#define amdgpu_bo_list_for_each_userptr_entry(e, list) \
	for (e = &list->entries[list->first_userptr]; \
	     e != &list->entries[list->num_entries]; \
	     ++e)

#endif
