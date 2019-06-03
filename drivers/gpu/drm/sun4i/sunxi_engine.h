/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Copyright (C) 2017 Icenowy Zheng <icenowy@aosc.io>
 */

#ifndef _SUNXI_ENGINE_H_
#define _SUNXI_ENGINE_H_

struct drm_plane;
struct drm_device;
struct drm_crtc_state;

struct sunxi_engine;

/**
 * struct sunxi_engine_ops - helper operations for sunXi engines
 *
 * These hooks are used by the common part of the DRM driver to
 * implement the proper behaviour.
 */
struct sunxi_engine_ops {
	/**
	 * @atomic_begin:
	 *
	 * This callback allows to prepare our engine for an atomic
	 * update. This is mirroring the
	 * &drm_crtc_helper_funcs.atomic_begin callback, so any
	 * documentation there applies.
	 *
	 * This function is optional.
	 */
	void (*atomic_begin)(struct sunxi_engine *engine,
			     struct drm_crtc_state *old_state);

	/**
	 * @atomic_check:
	 *
	 * This callback allows to validate plane-update related CRTC
	 * constraints specific to engines. This is mirroring the
	 * &drm_crtc_helper_funcs.atomic_check callback, so any
	 * documentation there applies.
	 *
	 * This function is optional.
	 *
	 * RETURNS:
	 *
	 * 0 on success or a negative error code.
	 */
	int (*atomic_check)(struct sunxi_engine *engine,
			    struct drm_crtc_state *state);

	/**
	 * @commit:
	 *
	 * This callback will trigger the hardware switch to commit
	 * the new configuration that has been setup during the next
	 * vblank period.
	 *
	 * This function is optional.
	 */
	void (*commit)(struct sunxi_engine *engine);

	/**
	 * @layers_init:
	 *
	 * This callback is used to allocate, initialize and register
	 * the layers supported by that engine.
	 *
	 * This function is mandatory.
	 *
	 * RETURNS:
	 *
	 * The array of struct drm_plane backing the layers, or an
	 * error pointer on failure.
	 */
	struct drm_plane **(*layers_init)(struct drm_device *drm,
					  struct sunxi_engine *engine);

	/**
	 * @apply_color_correction:
	 *
	 * This callback will enable the color correction in the
	 * engine. This is useful only for the composite output.
	 *
	 * This function is optional.
	 */
	void (*apply_color_correction)(struct sunxi_engine *engine);

	/**
	 * @disable_color_correction:
	 *
	 * This callback will stop the color correction in the
	 * engine. This is useful only for the composite output.
	 *
	 * This function is optional.
	 */
	void (*disable_color_correction)(struct sunxi_engine *engine);

	/**
	 * @vblank_quirk:
	 *
	 * This callback is used to implement engine-specific
	 * behaviour part of the VBLANK event. It is run with all the
	 * constraints of an interrupt (can't sleep, all local
	 * interrupts disabled) and therefore should be as fast as
	 * possible.
	 *
	 * This function is optional.
	 */
	void (*vblank_quirk)(struct sunxi_engine *engine);
};

/**
 * struct sunxi_engine - the common parts of an engine for sun4i-drm driver
 * @ops:	the operations of the engine
 * @node:	the of device node of the engine
 * @regs:	the regmap of the engine
 * @id:		the id of the engine (-1 if not used)
 */
struct sunxi_engine {
	const struct sunxi_engine_ops	*ops;

	struct device_node		*node;
	struct regmap			*regs;

	int id;

	/* Engine list management */
	struct list_head		list;
};

/**
 * sunxi_engine_commit() - commit all changes of the engine
 * @engine:	pointer to the engine
 */
static inline void
sunxi_engine_commit(struct sunxi_engine *engine)
{
	if (engine->ops && engine->ops->commit)
		engine->ops->commit(engine);
}

/**
 * sunxi_engine_layers_init() - Create planes (layers) for the engine
 * @drm:	pointer to the drm_device for which planes will be created
 * @engine:	pointer to the engine
 */
static inline struct drm_plane **
sunxi_engine_layers_init(struct drm_device *drm, struct sunxi_engine *engine)
{
	if (engine->ops && engine->ops->layers_init)
		return engine->ops->layers_init(drm, engine);
	return ERR_PTR(-ENOSYS);
}

/**
 * sunxi_engine_apply_color_correction - Apply the RGB2YUV color correction
 * @engine:	pointer to the engine
 *
 * This functionality is optional for an engine, however, if the engine is
 * intended to be used with TV Encoder, the output will be incorrect
 * without the color correction, due to TV Encoder expects the engine to
 * output directly YUV signal.
 */
static inline void
sunxi_engine_apply_color_correction(struct sunxi_engine *engine)
{
	if (engine->ops && engine->ops->apply_color_correction)
		engine->ops->apply_color_correction(engine);
}

/**
 * sunxi_engine_disable_color_correction - Disable the color space correction
 * @engine:	pointer to the engine
 *
 * This function is paired with apply_color_correction().
 */
static inline void
sunxi_engine_disable_color_correction(struct sunxi_engine *engine)
{
	if (engine->ops && engine->ops->disable_color_correction)
		engine->ops->disable_color_correction(engine);
}
#endif /* _SUNXI_ENGINE_H_ */
