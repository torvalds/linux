// SPDX-License-Identifier: GPL-2.0
/*
 * (C) COPYRIGHT 2018 ARM Limited. All rights reserved.
 * Author: James.Qian.Wang <james.qian.wang@arm.com>
 *
 */
#include "komeda_dev.h"
#include "komeda_kms.h"

static struct drm_private_state *
komeda_pipeline_atomic_duplicate_state(struct drm_private_obj *obj)
{
	struct komeda_pipeline_state *st;

	st = kmemdup(obj->state, sizeof(*st), GFP_KERNEL);
	if (!st)
		return NULL;

	st->active_comps = 0;

	__drm_atomic_helper_private_obj_duplicate_state(obj, &st->obj);

	return &st->obj;
}

static void
komeda_pipeline_atomic_destroy_state(struct drm_private_obj *obj,
				     struct drm_private_state *state)
{
	kfree(priv_to_pipe_st(state));
}

static const struct drm_private_state_funcs komeda_pipeline_obj_funcs = {
	.atomic_duplicate_state	= komeda_pipeline_atomic_duplicate_state,
	.atomic_destroy_state	= komeda_pipeline_atomic_destroy_state,
};

static int komeda_pipeline_obj_add(struct komeda_kms_dev *kms,
				   struct komeda_pipeline *pipe)
{
	struct komeda_pipeline_state *st;

	st = kzalloc(sizeof(*st), GFP_KERNEL);
	if (!st)
		return -ENOMEM;

	st->pipe = pipe;
	drm_atomic_private_obj_init(&kms->base, &pipe->obj, &st->obj,
				    &komeda_pipeline_obj_funcs);

	return 0;
}

int komeda_kms_add_private_objs(struct komeda_kms_dev *kms,
				struct komeda_dev *mdev)
{
	struct komeda_pipeline *pipe;
	int i, err;

	for (i = 0; i < mdev->n_pipelines; i++) {
		pipe = mdev->pipelines[i];

		err = komeda_pipeline_obj_add(kms, pipe);
		if (err)
			return err;

		/* Add component */
	}

	return 0;
}

void komeda_kms_cleanup_private_objs(struct komeda_dev *mdev)
{
	struct komeda_pipeline *pipe;
	struct komeda_component *c;
	int i, id;

	for (i = 0; i < mdev->n_pipelines; i++) {
		pipe = mdev->pipelines[i];
		dp_for_each_set_bit(id, pipe->avail_comps) {
			c = komeda_pipeline_get_component(pipe, id);

			drm_atomic_private_obj_fini(&c->obj);
		}
		drm_atomic_private_obj_fini(&pipe->obj);
	}
}
