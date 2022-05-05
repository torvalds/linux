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

struct rga_scheduler_t *rga_job_get_scheduler(struct rga_job *job);

void rga_job_session_destroy(struct rga_session *session);

void rga_job_done(struct rga_scheduler_t *scheduler, int ret);
struct rga_job *rga_job_commit(struct rga_req *rga_command_base, struct rga_request *request);
int rga_job_mpi_commit(struct rga_req *rga_command_base, struct rga_request *request);

int rga_job_assign(struct rga_job *job);


int rga_request_check(struct rga_user_request *req);
struct rga_request *rga_request_lookup(struct rga_pending_request_manager *request_manager,
				       uint32_t id);

int rga_request_commit(struct rga_request *user_request);
int rga_request_put(struct rga_request *request);
void rga_request_get(struct rga_request *request);
uint32_t rga_request_alloc(uint32_t flags, struct rga_session *session);

struct rga_request *rga_request_config(struct rga_user_request *user_request);
int rga_request_submit(struct rga_request *request);
int rga_request_release_signal(struct rga_scheduler_t *scheduler, struct rga_job *job);

int rga_request_manager_init(struct rga_pending_request_manager **request_manager_session);
int rga_request_manager_remove(struct rga_pending_request_manager **request_manager_session);

struct rga_job *
rga_scheduler_get_pending_job_list(struct rga_scheduler_t *scheduler);

struct rga_job *
rga_scheduler_get_running_job(struct rga_scheduler_t *scheduler);

#endif /* __LINUX_RKRGA_JOB_H_ */
