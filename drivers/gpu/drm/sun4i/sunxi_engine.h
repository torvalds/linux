/*
 * Copyright (C) 2017 Icenowy Zheng <icenowy@aosc.io>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of
 * the License, or (at your option) any later version.
 */

#ifndef _SUNXI_ENGINE_H_
#define _SUNXI_ENGINE_H_

struct drm_plane;
struct drm_device;

struct sunxi_engine;

struct sunxi_engine_ops {
	void (*commit)(struct sunxi_engine *engine);
	struct drm_plane **(*layers_init)(struct drm_device *drm,
					  struct sunxi_engine *engine);

	void (*apply_color_correction)(struct sunxi_engine *engine);
	void (*disable_color_correction)(struct sunxi_engine *engine);
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
