/*
 * Copyright 2021 Advanced Micro Devices, Inc.
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

#ifndef __AMDGPU_RESET_H__
#define __AMDGPU_RESET_H__

#include "amdgpu.h"

enum AMDGPU_RESET_FLAGS {

	AMDGPU_NEED_FULL_RESET = 0,
	AMDGPU_SKIP_HW_RESET = 1,
};

struct amdgpu_reset_context {
	enum amd_reset_method method;
	struct amdgpu_device *reset_req_dev;
	struct amdgpu_job *job;
	struct amdgpu_hive_info *hive;
	unsigned long flags;
};

struct amdgpu_reset_handler {
	enum amd_reset_method reset_method;
	struct list_head handler_list;
	int (*prepare_env)(struct amdgpu_reset_control *reset_ctl,
			   struct amdgpu_reset_context *context);
	int (*prepare_hwcontext)(struct amdgpu_reset_control *reset_ctl,
				 struct amdgpu_reset_context *context);
	int (*perform_reset)(struct amdgpu_reset_control *reset_ctl,
			     struct amdgpu_reset_context *context);
	int (*restore_hwcontext)(struct amdgpu_reset_control *reset_ctl,
				 struct amdgpu_reset_context *context);
	int (*restore_env)(struct amdgpu_reset_control *reset_ctl,
			   struct amdgpu_reset_context *context);

	int (*do_reset)(struct amdgpu_device *adev);
};

struct amdgpu_reset_control {
	void *handle;
	struct work_struct reset_work;
	struct mutex reset_lock;
	struct list_head reset_handlers;
	atomic_t in_reset;
	enum amd_reset_method active_reset;
	struct amdgpu_reset_handler *(*get_reset_handler)(
		struct amdgpu_reset_control *reset_ctl,
		struct amdgpu_reset_context *context);
	void (*async_reset)(struct work_struct *work);
};

int amdgpu_reset_init(struct amdgpu_device *adev);
int amdgpu_reset_fini(struct amdgpu_device *adev);

int amdgpu_reset_prepare_hwcontext(struct amdgpu_device *adev,
				   struct amdgpu_reset_context *reset_context);

int amdgpu_reset_perform_reset(struct amdgpu_device *adev,
			       struct amdgpu_reset_context *reset_context);

int amdgpu_reset_add_handler(struct amdgpu_reset_control *reset_ctl,
			     struct amdgpu_reset_handler *handler);

#endif
