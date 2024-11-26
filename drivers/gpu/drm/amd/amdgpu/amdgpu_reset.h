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

#define AMDGPU_RESET_MAX_HANDLERS 5

enum AMDGPU_RESET_FLAGS {

	AMDGPU_NEED_FULL_RESET = 0,
	AMDGPU_SKIP_HW_RESET = 1,
	AMDGPU_SKIP_COREDUMP = 2,
	AMDGPU_HOST_FLR = 3,
};

enum AMDGPU_RESET_SRCS {
	AMDGPU_RESET_SRC_UNKNOWN,
	AMDGPU_RESET_SRC_JOB,
	AMDGPU_RESET_SRC_RAS,
	AMDGPU_RESET_SRC_MES,
	AMDGPU_RESET_SRC_HWS,
	AMDGPU_RESET_SRC_USER,
};

struct amdgpu_reset_context {
	enum amd_reset_method method;
	struct amdgpu_device *reset_req_dev;
	struct amdgpu_job *job;
	struct amdgpu_hive_info *hive;
	struct list_head *reset_device_list;
	unsigned long flags;
	enum AMDGPU_RESET_SRCS src;
};

struct amdgpu_reset_handler {
	enum amd_reset_method reset_method;
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
	struct amdgpu_reset_handler *(
		*reset_handlers)[AMDGPU_RESET_MAX_HANDLERS];
	atomic_t in_reset;
	enum amd_reset_method active_reset;
	struct amdgpu_reset_handler *(*get_reset_handler)(
		struct amdgpu_reset_control *reset_ctl,
		struct amdgpu_reset_context *context);
	void (*async_reset)(struct work_struct *work);
};


enum amdgpu_reset_domain_type {
	SINGLE_DEVICE,
	XGMI_HIVE
};

struct amdgpu_reset_domain {
	struct kref refcount;
	struct workqueue_struct *wq;
	enum amdgpu_reset_domain_type type;
	struct rw_semaphore sem;
	atomic_t in_gpu_reset;
	atomic_t reset_res;
};

int amdgpu_reset_init(struct amdgpu_device *adev);
int amdgpu_reset_fini(struct amdgpu_device *adev);

int amdgpu_reset_prepare_hwcontext(struct amdgpu_device *adev,
				   struct amdgpu_reset_context *reset_context);

int amdgpu_reset_perform_reset(struct amdgpu_device *adev,
			       struct amdgpu_reset_context *reset_context);

int amdgpu_reset_prepare_env(struct amdgpu_device *adev,
			     struct amdgpu_reset_context *reset_context);
int amdgpu_reset_restore_env(struct amdgpu_device *adev,
			     struct amdgpu_reset_context *reset_context);

struct amdgpu_reset_domain *amdgpu_reset_create_reset_domain(enum amdgpu_reset_domain_type type,
							     char *wq_name);

void amdgpu_reset_destroy_reset_domain(struct kref *ref);

static inline bool amdgpu_reset_get_reset_domain(struct amdgpu_reset_domain *domain)
{
	return kref_get_unless_zero(&domain->refcount) != 0;
}

static inline void amdgpu_reset_put_reset_domain(struct amdgpu_reset_domain *domain)
{
	if (domain)
		kref_put(&domain->refcount, amdgpu_reset_destroy_reset_domain);
}

static inline bool amdgpu_reset_domain_schedule(struct amdgpu_reset_domain *domain,
						struct work_struct *work)
{
	return queue_work(domain->wq, work);
}

static inline bool amdgpu_reset_pending(struct amdgpu_reset_domain *domain)
{
	lockdep_assert_held(&domain->sem);
	return rwsem_is_contended(&domain->sem);
}

void amdgpu_device_lock_reset_domain(struct amdgpu_reset_domain *reset_domain);

void amdgpu_device_unlock_reset_domain(struct amdgpu_reset_domain *reset_domain);

void amdgpu_reset_get_desc(struct amdgpu_reset_context *rst_ctxt, char *buf,
			   size_t len);

#define for_each_handler(i, handler, reset_ctl)                  \
	for (i = 0; (i < AMDGPU_RESET_MAX_HANDLERS) &&           \
		    (handler = (*reset_ctl->reset_handlers)[i]); \
	     ++i)

extern struct amdgpu_reset_handler xgmi_reset_on_init_handler;
int amdgpu_reset_do_xgmi_reset_on_init(
	struct amdgpu_reset_context *reset_context);

#endif
