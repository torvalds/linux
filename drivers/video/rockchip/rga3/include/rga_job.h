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
int rga_job_commit(struct rga_req *rga_command_base, int flags);

int rga_job_mpi_commit(struct rga_req *rga_command_base,
		       struct rga_mpi_job_t *mpi_job, int flags);

int rga_job_assign(struct rga_job *job);

struct rga_job *
rga_scheduler_get_pending_job_list(struct rga_scheduler_t *scheduler);

struct rga_job *
rga_scheduler_get_running_job(struct rga_scheduler_t *scheduler);

#endif /* __LINUX_RKRGA_JOB_H_ */
