// SPDX-License-Identifier: GPL-2.0 OR MIT
/* Copyright 2018-2019 Qiang Yu <yuq825@gmail.com> */

#include <linux/slab.h>

#include "lima_device.h"
#include "lima_ctx.h"

int lima_ctx_create(struct lima_device *dev, struct lima_ctx_mgr *mgr, u32 *id)
{
	struct lima_ctx *ctx;
	int i, err;

	ctx = kzalloc(sizeof(*ctx), GFP_KERNEL);
	if (!ctx)
		return -ENOMEM;
	ctx->dev = dev;
	kref_init(&ctx->refcnt);

	for (i = 0; i < lima_pipe_num; i++) {
		err = lima_sched_context_init(dev->pipe + i, ctx->context + i, &ctx->guilty);
		if (err)
			goto err_out0;
	}

	err = xa_alloc(&mgr->handles, id, ctx, xa_limit_32b, GFP_KERNEL);
	if (err < 0)
		goto err_out0;

	ctx->pid = task_pid_nr(current);
	get_task_comm(ctx->pname, current);

	return 0;

err_out0:
	for (i--; i >= 0; i--)
		lima_sched_context_fini(dev->pipe + i, ctx->context + i);
	kfree(ctx);
	return err;
}

static void lima_ctx_do_release(struct kref *ref)
{
	struct lima_ctx *ctx = container_of(ref, struct lima_ctx, refcnt);
	int i;

	for (i = 0; i < lima_pipe_num; i++)
		lima_sched_context_fini(ctx->dev->pipe + i, ctx->context + i);
	kfree(ctx);
}

int lima_ctx_free(struct lima_ctx_mgr *mgr, u32 id)
{
	struct lima_ctx *ctx;
	int ret = 0;

	mutex_lock(&mgr->lock);
	ctx = xa_erase(&mgr->handles, id);
	if (ctx)
		kref_put(&ctx->refcnt, lima_ctx_do_release);
	else
		ret = -EINVAL;
	mutex_unlock(&mgr->lock);
	return ret;
}

struct lima_ctx *lima_ctx_get(struct lima_ctx_mgr *mgr, u32 id)
{
	struct lima_ctx *ctx;

	mutex_lock(&mgr->lock);
	ctx = xa_load(&mgr->handles, id);
	if (ctx)
		kref_get(&ctx->refcnt);
	mutex_unlock(&mgr->lock);
	return ctx;
}

void lima_ctx_put(struct lima_ctx *ctx)
{
	kref_put(&ctx->refcnt, lima_ctx_do_release);
}

void lima_ctx_mgr_init(struct lima_ctx_mgr *mgr)
{
	mutex_init(&mgr->lock);
	xa_init_flags(&mgr->handles, XA_FLAGS_ALLOC);
}

void lima_ctx_mgr_fini(struct lima_ctx_mgr *mgr)
{
	struct lima_ctx *ctx;
	unsigned long id;

	xa_for_each(&mgr->handles, id, ctx) {
		kref_put(&ctx->refcnt, lima_ctx_do_release);
	}

	xa_destroy(&mgr->handles);
	mutex_destroy(&mgr->lock);
}
