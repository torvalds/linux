/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) Rockchip Electronics Co., Ltd.
 *
 * Author: Huang Lee <Putin.li@rock-chips.com>
 */

#ifndef __LINUX_RKRGA_JOB_H_
#define __LINUX_RKRGA_JOB_H_

#include <linux/spinlock.h>
#include <linux/dma-fence.h>

#include "rga_drv.h"

enum job_flags {
	RGA_JOB_DONE			= 1 << 0,
	RGA_JOB_ASYNC			= 1 << 1,
	RGA_JOB_SYNC			= 1 << 2,
	RGA_JOB_USE_HANDLE		= 1 << 3,
	RGA_JOB_UNSUPPORT_RGA2		= 1 << 4,
};

struct rga_scheduler_t *rga_job_get_scheduler(int core);

void rga_job_done(struct rga_scheduler_t *rga_scheduler, int ret);
int rga_job_commit(struct rga_req *rga_command_base, struct rga_internal_ctx_t *ctx);

int rga_job_mpi_commit(struct rga_req *rga_command_base,
		       struct rga_mpi_job_t *mpi_job, struct rga_internal_ctx_t *ctx);

int rga_job_assign(struct rga_job *job);

int rga_ctx_manager_init(struct rga_pending_ctx_manager **ctx_manager_session);
int rga_ctx_manager_remove(struct rga_pending_ctx_manager **ctx_manager_session);

struct rga_internal_ctx_t *
	rga_internal_ctx_lookup(struct rga_pending_ctx_manager *ctx_manager, uint32_t id);
uint32_t rga_internal_ctx_alloc_to_get_idr_id(void);
void rga_internel_ctx_kref_release(struct kref *ref);
int rga_job_config_by_user_ctx(struct rga_user_ctx_t *user_ctx);
int rga_job_commit_by_user_ctx(struct rga_user_ctx_t *user_ctx);
int rga_job_cancel_by_user_ctx(uint32_t ctx_id);

struct rga_job *
rga_scheduler_get_pending_job_list(struct rga_scheduler_t *scheduler);

struct rga_job *
rga_scheduler_get_running_job(struct rga_scheduler_t *scheduler);

#endif /* __LINUX_RKRGA_JOB_H_ */
