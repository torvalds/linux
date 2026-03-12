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

static struct drm_private_state *
komeda_layer_atomic_create_state(struct drm_private_obj *obj)
{
	struct komeda_layer_state *st;

	st = kzalloc_obj(*st);
	if (!st)
		return ERR_PTR(-ENOMEM);

	__drm_atomic_helper_private_obj_create_state(obj, &st->base.obj);
	komeda_component_state_reset(&st->base);
	st->base.component = to_component(obj);

	return &st->base.obj;
}

static const struct drm_private_state_funcs komeda_layer_obj_funcs = {
	.atomic_create_state	= komeda_layer_atomic_create_state,
	.atomic_duplicate_state	= komeda_layer_atomic_duplicate_state,
	.atomic_destroy_state	= komeda_layer_atomic_destroy_state,
};

static int komeda_layer_obj_add(struct komeda_kms_dev *kms,
				struct komeda_layer *layer)
{
	drm_atomic_private_obj_init(&kms->base, &layer->base.obj, NULL,
				    &komeda_layer_obj_funcs);
	return 0;
}

static struct drm_private_state *
komeda_scaler_atomic_duplicate_state(struct drm_private_obj *obj)
{
	struct komeda_scaler_state *st;

	st = kmemdup(obj->state, sizeof(*st), GFP_KERNEL);
	if (!st)
		return NULL;

	komeda_component_state_reset(&st->base);
	__drm_atomic_helper_private_obj_duplicate_state(obj, &st->base.obj);

	return &st->base.obj;
}

static void
komeda_scaler_atomic_destroy_state(struct drm_private_obj *obj,
				   struct drm_private_state *state)
{
	kfree(to_scaler_st(priv_to_comp_st(state)));
}

static struct drm_private_state *
komeda_scaler_atomic_create_state(struct drm_private_obj *obj)
{
	struct komeda_scaler_state *st;

	st = kzalloc_obj(*st);
	if (!st)
		return ERR_PTR(-ENOMEM);

	__drm_atomic_helper_private_obj_create_state(obj, &st->base.obj);
	komeda_component_state_reset(&st->base);
	st->base.component = to_component(obj);

	return &st->base.obj;
}

static const struct drm_private_state_funcs komeda_scaler_obj_funcs = {
	.atomic_create_state	= komeda_scaler_atomic_create_state,
	.atomic_duplicate_state	= komeda_scaler_atomic_duplicate_state,
	.atomic_destroy_state	= komeda_scaler_atomic_destroy_state,
};

static int komeda_scaler_obj_add(struct komeda_kms_dev *kms,
				 struct komeda_scaler *scaler)
{
	drm_atomic_private_obj_init(&kms->base,
				    &scaler->base.obj, NULL,
				    &komeda_scaler_obj_funcs);
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

static struct drm_private_state *
komeda_compiz_atomic_create_state(struct drm_private_obj *obj)
{
	struct komeda_compiz_state *st;

	st = kzalloc_obj(*st);
	if (!st)
		return ERR_PTR(-ENOMEM);

	__drm_atomic_helper_private_obj_create_state(obj, &st->base.obj);
	komeda_component_state_reset(&st->base);
	st->base.component = to_component(obj);

	return &st->base.obj;
}

static const struct drm_private_state_funcs komeda_compiz_obj_funcs = {
	.atomic_create_state	= komeda_compiz_atomic_create_state,
	.atomic_duplicate_state	= komeda_compiz_atomic_duplicate_state,
	.atomic_destroy_state	= komeda_compiz_atomic_destroy_state,
};

static int komeda_compiz_obj_add(struct komeda_kms_dev *kms,
				 struct komeda_compiz *compiz)
{
	drm_atomic_private_obj_init(&kms->base, &compiz->base.obj, NULL,
				    &komeda_compiz_obj_funcs);

	return 0;
}

static struct drm_private_state *
komeda_splitter_atomic_duplicate_state(struct drm_private_obj *obj)
{
	struct komeda_splitter_state *st;

	st = kmemdup(obj->state, sizeof(*st), GFP_KERNEL);
	if (!st)
		return NULL;

	komeda_component_state_reset(&st->base);
	__drm_atomic_helper_private_obj_duplicate_state(obj, &st->base.obj);

	return &st->base.obj;
}

static void
komeda_splitter_atomic_destroy_state(struct drm_private_obj *obj,
				     struct drm_private_state *state)
{
	kfree(to_splitter_st(priv_to_comp_st(state)));
}

static struct drm_private_state *
komeda_splitter_atomic_create_state(struct drm_private_obj *obj)
{
	struct komeda_splitter_state *st;

	st = kzalloc_obj(*st);
	if (!st)
		return ERR_PTR(-ENOMEM);

	__drm_atomic_helper_private_obj_create_state(obj, &st->base.obj);
	komeda_component_state_reset(&st->base);
	st->base.component = to_component(obj);

	return &st->base.obj;
}

static const struct drm_private_state_funcs komeda_splitter_obj_funcs = {
	.atomic_create_state	= komeda_splitter_atomic_create_state,
	.atomic_duplicate_state	= komeda_splitter_atomic_duplicate_state,
	.atomic_destroy_state	= komeda_splitter_atomic_destroy_state,
};

static int komeda_splitter_obj_add(struct komeda_kms_dev *kms,
				   struct komeda_splitter *splitter)
{
	drm_atomic_private_obj_init(&kms->base,
				    &splitter->base.obj, NULL,
				    &komeda_splitter_obj_funcs);

	return 0;
}

static struct drm_private_state *
komeda_merger_atomic_duplicate_state(struct drm_private_obj *obj)
{
	struct komeda_merger_state *st;

	st = kmemdup(obj->state, sizeof(*st), GFP_KERNEL);
	if (!st)
		return NULL;

	komeda_component_state_reset(&st->base);
	__drm_atomic_helper_private_obj_duplicate_state(obj, &st->base.obj);

	return &st->base.obj;
}

static void komeda_merger_atomic_destroy_state(struct drm_private_obj *obj,
					       struct drm_private_state *state)
{
	kfree(to_merger_st(priv_to_comp_st(state)));
}

static struct drm_private_state *
komeda_merger_atomic_create_state(struct drm_private_obj *obj)
{
	struct komeda_merger_state *st;

	st = kzalloc_obj(*st);
	if (!st)
		return ERR_PTR(-ENOMEM);

	__drm_atomic_helper_private_obj_create_state(obj, &st->base.obj);
	komeda_component_state_reset(&st->base);
	st->base.component = to_component(obj);

	return &st->base.obj;
}

static const struct drm_private_state_funcs komeda_merger_obj_funcs = {
	.atomic_create_state	= komeda_merger_atomic_create_state,
	.atomic_duplicate_state	= komeda_merger_atomic_duplicate_state,
	.atomic_destroy_state	= komeda_merger_atomic_destroy_state,
};

static int komeda_merger_obj_add(struct komeda_kms_dev *kms,
				 struct komeda_merger *merger)
{
	drm_atomic_private_obj_init(&kms->base,
				    &merger->base.obj, NULL,
				    &komeda_merger_obj_funcs);

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

static struct drm_private_state *
komeda_improc_atomic_create_state(struct drm_private_obj *obj)
{
	struct komeda_improc_state *st;

	st = kzalloc_obj(*st);
	if (!st)
		return ERR_PTR(-ENOMEM);

	__drm_atomic_helper_private_obj_create_state(obj, &st->base.obj);
	komeda_component_state_reset(&st->base);
	st->base.component = to_component(obj);

	return &st->base.obj;
}

static const struct drm_private_state_funcs komeda_improc_obj_funcs = {
	.atomic_create_state	= komeda_improc_atomic_create_state,
	.atomic_duplicate_state	= komeda_improc_atomic_duplicate_state,
	.atomic_destroy_state	= komeda_improc_atomic_destroy_state,
};

static int komeda_improc_obj_add(struct komeda_kms_dev *kms,
				 struct komeda_improc *improc)
{
	drm_atomic_private_obj_init(&kms->base, &improc->base.obj, NULL,
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

static struct drm_private_state *
komeda_timing_ctrlr_atomic_create_state(struct drm_private_obj *obj)
{
	struct komeda_timing_ctrlr_state *st;

	st = kzalloc_obj(*st);
	if (!st)
		return ERR_PTR(-ENOMEM);

	__drm_atomic_helper_private_obj_create_state(obj, &st->base.obj);
	komeda_component_state_reset(&st->base);
	st->base.component = to_component(obj);

	return &st->base.obj;
}

static const struct drm_private_state_funcs komeda_timing_ctrlr_obj_funcs = {
	.atomic_create_state	= komeda_timing_ctrlr_atomic_create_state,
	.atomic_duplicate_state	= komeda_timing_ctrlr_atomic_duplicate_state,
	.atomic_destroy_state	= komeda_timing_ctrlr_atomic_destroy_state,
};

static int komeda_timing_ctrlr_obj_add(struct komeda_kms_dev *kms,
				       struct komeda_timing_ctrlr *ctrlr)
{
	drm_atomic_private_obj_init(&kms->base, &ctrlr->base.obj, NULL,
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

static struct drm_private_state *
komeda_pipeline_atomic_create_state(struct drm_private_obj *obj)
{
	struct komeda_pipeline_state *st;

	st = kzalloc_obj(*st);
	if (!st)
		return ERR_PTR(-ENOMEM);

	__drm_atomic_helper_private_obj_create_state(obj, &st->obj);
	st->active_comps = 0;
	st->pipe = container_of(obj, struct komeda_pipeline, obj);

	return &st->obj;
}

static const struct drm_private_state_funcs komeda_pipeline_obj_funcs = {
	.atomic_create_state	= komeda_pipeline_atomic_create_state,
	.atomic_duplicate_state	= komeda_pipeline_atomic_duplicate_state,
	.atomic_destroy_state	= komeda_pipeline_atomic_destroy_state,
};

static int komeda_pipeline_obj_add(struct komeda_kms_dev *kms,
				   struct komeda_pipeline *pipe)
{
	drm_atomic_private_obj_init(&kms->base, &pipe->obj, NULL,
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

		if (pipe->wb_layer) {
			err = komeda_layer_obj_add(kms, pipe->wb_layer);
			if (err)
				return err;
		}

		for (j = 0; j < pipe->n_scalers; j++) {
			err = komeda_scaler_obj_add(kms, pipe->scalers[j]);
			if (err)
				return err;
		}

		err = komeda_compiz_obj_add(kms, pipe->compiz);
		if (err)
			return err;

		if (pipe->splitter) {
			err = komeda_splitter_obj_add(kms, pipe->splitter);
			if (err)
				return err;
		}

		if (pipe->merger) {
			err = komeda_merger_obj_add(kms, pipe->merger);
			if (err)
				return err;
		}

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
