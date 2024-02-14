/* SPDX-License-Identifier: GPL-2.0 OR MIT */
/* Copyright 2017-2019 Qiang Yu <yuq825@gmail.com> */

#ifndef __LIMA_DRV_H__
#define __LIMA_DRV_H__

#include <drm/drm_file.h>

#include "lima_ctx.h"

extern int lima_sched_timeout_ms;
extern uint lima_heap_init_nr_pages;
extern uint lima_max_error_tasks;
extern uint lima_job_hang_limit;

struct lima_vm;
struct lima_bo;
struct lima_sched_task;

struct drm_lima_gem_submit_bo;

struct lima_drm_priv {
	struct lima_vm *vm;
	struct lima_ctx_mgr ctx_mgr;
};

struct lima_submit {
	struct lima_ctx *ctx;
	int pipe;
	u32 flags;

	struct drm_lima_gem_submit_bo *bos;
	struct lima_bo **lbos;
	u32 nr_bos;

	u32 in_sync[2];
	u32 out_sync;

	struct lima_sched_task *task;
};

static inline struct lima_drm_priv *
to_lima_drm_priv(struct drm_file *file)
{
	return file->driver_priv;
}

#endif
