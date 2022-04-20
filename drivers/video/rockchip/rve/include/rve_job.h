/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) Rockchip Electronics Co., Ltd.
 *
 * Author: Huang Lee <Putin.li@rock-chips.com>
 */

#ifndef __LINUX_RKRVE_JOB_H_
#define __LINUX_RKRVE_JOB_H_

#include <linux/spinlock.h>
#include <linux/dma-fence.h>

#include "rve_drv.h"

enum job_flags {
	RVE_JOB_DONE			= 1 << 0,
	RVE_ASYNC			= 1 << 1,
	RVE_SYNC			= 1 << 2,
	RVE_JOB_USE_HANDLE		= 1 << 3,
	RVE_JOB_UNSUPPORT_RVE2		= 1 << 4,
};

struct rve_scheduler_t *rve_job_get_scheduler(struct rve_job *job);
struct rve_internal_ctx_t *rve_job_get_internal_ctx(struct rve_job *job);

void rve_job_done(struct rve_scheduler_t *rve_scheduler, int ret);
int rve_job_commit(struct rve_internal_ctx_t *ctx);

int rve_job_config_by_user_ctx(struct rve_user_ctx_t *user_ctx);
int rve_job_commit_by_user_ctx(struct rve_user_ctx_t *user_ctx);
int rve_job_cancel_by_user_ctx(uint32_t ctx_id);

void rve_job_session_destroy(struct rve_session *session);

int rve_ctx_manager_init(struct rve_pending_ctx_manager **ctx_manager_session);
int rve_ctx_manager_remove(struct rve_pending_ctx_manager **ctx_manager_session);

int rve_internal_ctx_alloc_to_get_idr_id(struct rve_session *session);
void rve_internal_ctx_kref_release(struct kref *ref);

int rve_internal_ctx_signal(struct rve_job *job);

struct rve_internal_ctx_t *
rve_internal_ctx_lookup(struct rve_pending_ctx_manager *ctx_manager, uint32_t id);

struct rve_job *
rve_scheduler_get_pending_job_list(struct rve_scheduler_t *scheduler);

struct rve_job *
rve_scheduler_get_running_job(struct rve_scheduler_t *scheduler);

#endif /* __LINUX_RKRVE_JOB_H_ */
