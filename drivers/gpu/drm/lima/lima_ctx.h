/* SPDX-License-Identifier: GPL-2.0 OR MIT */
/* Copyright 2018-2019 Qiang Yu <yuq825@gmail.com> */

#ifndef __LIMA_CTX_H__
#define __LIMA_CTX_H__

#include <linux/xarray.h>
#include <linux/sched.h>

#include "lima_device.h"

struct lima_ctx {
	struct kref refcnt;
	struct lima_device *dev;
	struct lima_sched_context context[lima_pipe_num];

	/* debug info */
	char pname[TASK_COMM_LEN];
	pid_t pid;
};

struct lima_ctx_mgr {
	struct mutex lock;
	struct xarray handles;
};

int lima_ctx_create(struct lima_device *dev, struct lima_ctx_mgr *mgr, u32 *id);
int lima_ctx_free(struct lima_ctx_mgr *mgr, u32 id);
struct lima_ctx *lima_ctx_get(struct lima_ctx_mgr *mgr, u32 id);
void lima_ctx_put(struct lima_ctx *ctx);
void lima_ctx_mgr_init(struct lima_ctx_mgr *mgr);
void lima_ctx_mgr_fini(struct lima_ctx_mgr *mgr);

#endif
