// SPDX-License-Identifier: GPL-2.0
/*
 * (C) COPYRIGHT 2018 ARM Limited. All rights reserved.
 * Author: James.Qian.Wang <james.qian.wang@arm.com>
 *
 */
#include "komeda_dev.h"
#include "komeda_kms.h"

static void
komeda_component_state_reset(struct komeda_component_state *st)
{
	st->binding_user = NULL;
	st->affected_inputs = st->active_inputs;
	st->active_inputs = 0;
	st->changed_active_inputs = 0;
}

static struct drm_private_state *
komeda_layer_atomic_duplicate_state(struct drm_private_obj *obj)
{
	struct komeda_layer_state *st;

	st = kmemdup(obj->state, sizeof(*st), GFP_KERNEL);
	if (!st)
		return NULL;

	komeda_component_state_reset(&st->base);
	__drm_atomic_helper_private_obj_duplicate_state(obj, &st->base.obj);

	return &st->base.obj;
}

static void
komeda_layer_atomic_destroy_state(struct drm_private_obj *obj,
				  struct drm_private_state *state)
{
	struct komeda_layer_state *st = to_layer_st(priv_to_comp_st(state));

	kfree(st);
}

static const struct drm_private_state_funcs komeda_layer_obj_funcs = {
	.atomic_duplicate_state	= komeda_layer_atomic_duplicate_state,
	.atomic_destroy_state	= komeda_layer_atomic_destroy_state,
};

static int komeda_layer_obj_add(struct komeda_kms_dev *kms,
				struct komeda_layer *layer)
{
	struct komeda_layer_state *st;

	st = kzalloc(sizeof(*st), GFP_KERNEL);
	if (!st)
		return -ENOMEM;

	st->base.component = &layer->base;
	drm_atomic_private_obj_init(&kms->base, &layer->base.obj, &st->base.obj,
				    &komeda_layer_obj_funcs);
	return 0;
}

static struct drm_private_state *
komeda_compiz_atomic_duplicate_state(struct drm_private_obj *obj)
{
	struct komeda_compiz_state *st;

	st = kmemdup(obj->state, sizeof(*st), GFP_KERNEL);
	if (!st)
		return NULL;

	komeda_component_state_reset(&st->base);
	__drm_atomic_helper_private_obj_duplicate_state(obj, &st->base.obj);

	return &st->base.obj;
}

static void
komeda_compiz_atomic_destroy_state(struct drm_private_obj *obj,
				   struct drm_private_state *state)
{
	kfree(to_compiz_st(priv_to_comp_st(state)));
}

static const struct drm_private_state_funcs komeda_compiz_obj_funcs = {
	.atomic_duplicate_state	= komeda_compiz_atomic_duplicate_state,
	.atomic_destroy_state	= komeda_compiz_atomic_destroy_state,
};

static int komeda_compiz_obj_add(struct komeda_kms_dev *kms,
				 struct komeda_compiz *compiz)
{
	struct komeda_compiz_state *st;

	st = kzalloc(sizeof(*st), GFP_KERNEL);
	if (!st)
		return -ENOMEM;

	st->base.component = &compiz->base;
	drm_atomic_private_obj_init(&kms->base, &compiz->base.obj, &st->base.obj,
				    &komeda_compiz_obj_funcs);

	return 0;
}

static struct drm_private_state *
komeda_improc_atomic_duplicate_state(struct drm_private_obj *obj)
{
	struct komeda_improc_state *st;

	st = kmemdup(obj->state, sizeof(*st), GFP_KERNEL);
	if (!st)
		return NULL;

	komeda_component_state_reset(&st->base);
	__drm_atomic_helper_private_obj_duplicate_state(obj, &st->base.obj);

	return &st->base.obj;
}

static void
komeda_improc_atomic_destroy_state(struct drm_private_obj *obj,
				   struct drm_private_state *state)
{
	kfree(to_improc_st(priv_to_comp_st(state)));
}

static const struct drm_private_state_funcs komeda_improc_obj_funcs = {
	.atomic_duplicate_state	= komeda_improc_atomic_duplicate_state,
	.atomic_destroy_state	= komeda_improc_atomic_destroy_state,
};

static int komeda_improc_obj_add(struct komeda_kms_dev *kms,
				 struct komeda_improc *improc)
{
	struct komeda_improc_state *st;

	st = kzalloc(sizeof(*st), GFP_KERNEL);
	if (!st)
		return -ENOMEM;

	st->base.component = &improc->base;
	drm_atomic_private_obj_init(&kms->base, &improc->base.obj, &st->base.obj,
				    &komeda_improc_obj_funcs);

	return 0;
}

static struct drm_private_state *
komeda_timing_ctrlr_atomic_duplicate_state(struct drm_private_obj *obj)
{
	struct komeda_timing_ctrlr_state *st;

	st = kmemdup(obj->state, sizeof(*st), GFP_KERNEL);
	if (!st)
		return NULL;

	komeda_component_state_reset(&st->base);
	__drm_atomic_helper_private_obj_duplicate_state(obj, &st->base.obj);

	return &st->base.obj;
}

static void
komeda_timing_ctrlr_atomic_destroy_state(struct drm_private_obj *obj,
					 struct drm_private_state *state)
{
	kfree(to_ctrlr_st(priv_to_comp_st(state)));
}

static const struct drm_private_state_funcs komeda_timing_ctrlr_obj_funcs = {
	.atomic_duplicate_state	= komeda_timing_ctrlr_atomic_duplicate_state,
	.atomic_destroy_state	= komeda_timing_ctrlr_atomic_destroy_state,
};

static int komeda_timing_ctrlr_obj_add(struct komeda_kms_dev *kms,
				       struct komeda_timing_ctrlr *ctrlr)
{
	struct komeda_compiz_state *st;

	st = kzalloc(sizeof(*st), GFP_KERNEL);
	if (!st)
		return -ENOMEM;

	st->base.component = &ctrlr->base;
	drm_atomic_private_obj_init(&kms->base, &ctrlr->base.obj, &st->base.obj,
				    &komeda_timing_ctrlr_obj_funcs);

	return 0;
}

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
	int i, j, err;

	for (i = 0; i < mdev->n_pipelines; i++) {
		pipe = mdev->pipelines[i];

		err = komeda_pipeline_obj_add(kms, pipe);
		if (err)
			return err;

		for (j = 0; j < pipe->n_layers; j++) {
			err = komeda_layer_obj_add(kms, pipe->layers[j]);
			if (err)
				return err;
		}

		err = komeda_compiz_obj_add(kms, pipe->compiz);
		if (err)
			return err;

		err = komeda_improc_obj_add(kms, pipe->improc);
		if (err)
			return err;

		err = komeda_timing_ctrlr_obj_add(kms, pipe->ctrlr);
		if (err)
			return err;
	}

	return 0;
}

void komeda_kms_cleanup_private_objs(struct komeda_kms_dev *kms)
{
	struct drm_mode_config *config = &kms->base.mode_config;
	struct drm_private_obj *obj, *next;

	list_for_each_entry_safe(obj, next, &config->privobj_list, head)
		drm_atomic_private_obj_fini(obj);
}
