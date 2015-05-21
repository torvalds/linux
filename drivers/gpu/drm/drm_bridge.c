/*
 * Copyright (c) 2014 Samsung Electronics Co., Ltd
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sub license,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the
 * next paragraph) shall be included in all copies or substantial portions
 * of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 */

#include <linux/err.h>
#include <linux/module.h>

#include <drm/drm_crtc.h>

#include "drm/drmP.h"

static DEFINE_MUTEX(bridge_lock);
static LIST_HEAD(bridge_list);

int drm_bridge_add(struct drm_bridge *bridge)
{
	mutex_lock(&bridge_lock);
	list_add_tail(&bridge->list, &bridge_list);
	mutex_unlock(&bridge_lock);

	return 0;
}
EXPORT_SYMBOL(drm_bridge_add);

void drm_bridge_remove(struct drm_bridge *bridge)
{
	mutex_lock(&bridge_lock);
	list_del_init(&bridge->list);
	mutex_unlock(&bridge_lock);
}
EXPORT_SYMBOL(drm_bridge_remove);

int drm_bridge_attach(struct drm_device *dev, struct drm_bridge *bridge)
{
	if (!dev || !bridge)
		return -EINVAL;

	if (bridge->dev)
		return -EBUSY;

	bridge->dev = dev;

	if (bridge->funcs->attach)
		return bridge->funcs->attach(bridge);

	return 0;
}
EXPORT_SYMBOL(drm_bridge_attach);

/**
 * drm_bridge_mode_fixup - fixup proposed mode for all bridges in the
 *			   encoder chain
 * @bridge: bridge control structure
 * @mode: desired mode to be set for the bridge
 * @adjusted_mode: updated mode that works for this bridge
 *
 * Calls 'mode_fixup' drm_bridge_funcs op for all the bridges in the
 * encoder chain, starting from the first bridge to the last.
 *
 * Note: the bridge passed should be the one closest to the encoder
 *
 * RETURNS:
 * true on success, false on failure
 */
bool drm_bridge_mode_fixup(struct drm_bridge *bridge,
			const struct drm_display_mode *mode,
			struct drm_display_mode *adjusted_mode)
{
	bool ret = true;

	if (!bridge)
		return true;

	if (bridge->funcs->mode_fixup)
		ret = bridge->funcs->mode_fixup(bridge, mode, adjusted_mode);

	ret = ret && drm_bridge_mode_fixup(bridge->next, mode, adjusted_mode);

	return ret;
}
EXPORT_SYMBOL(drm_bridge_mode_fixup);

/**
 * drm_bridge_disable - calls 'disable' drm_bridge_funcs op for all
 *			bridges in the encoder chain.
 * @bridge: bridge control structure
 *
 * Calls 'disable' drm_bridge_funcs op for all the bridges in the encoder
 * chain, starting from the last bridge to the first. These are called before
 * calling the encoder's prepare op.
 *
 * Note: the bridge passed should be the one closest to the encoder
 */
void drm_bridge_disable(struct drm_bridge *bridge)
{
	if (!bridge)
		return;

	drm_bridge_disable(bridge->next);

	bridge->funcs->disable(bridge);
}
EXPORT_SYMBOL(drm_bridge_disable);

/**
 * drm_bridge_post_disable - calls 'post_disable' drm_bridge_funcs op for
 *			     all bridges in the encoder chain.
 * @bridge: bridge control structure
 *
 * Calls 'post_disable' drm_bridge_funcs op for all the bridges in the
 * encoder chain, starting from the first bridge to the last. These are called
 * after completing the encoder's prepare op.
 *
 * Note: the bridge passed should be the one closest to the encoder
 */
void drm_bridge_post_disable(struct drm_bridge *bridge)
{
	if (!bridge)
		return;

	bridge->funcs->post_disable(bridge);

	drm_bridge_post_disable(bridge->next);
}
EXPORT_SYMBOL(drm_bridge_post_disable);

/**
 * drm_bridge_mode_set - set proposed mode for all bridges in the
 *			 encoder chain
 * @bridge: bridge control structure
 * @mode: desired mode to be set for the bridge
 * @adjusted_mode: updated mode that works for this bridge
 *
 * Calls 'mode_set' drm_bridge_funcs op for all the bridges in the
 * encoder chain, starting from the first bridge to the last.
 *
 * Note: the bridge passed should be the one closest to the encoder
 */
void drm_bridge_mode_set(struct drm_bridge *bridge,
			struct drm_display_mode *mode,
			struct drm_display_mode *adjusted_mode)
{
	if (!bridge)
		return;

	if (bridge->funcs->mode_set)
		bridge->funcs->mode_set(bridge, mode, adjusted_mode);

	drm_bridge_mode_set(bridge->next, mode, adjusted_mode);
}
EXPORT_SYMBOL(drm_bridge_mode_set);

/**
 * drm_bridge_pre_enable - calls 'pre_enable' drm_bridge_funcs op for all
 *			   bridges in the encoder chain.
 * @bridge: bridge control structure
 *
 * Calls 'pre_enable' drm_bridge_funcs op for all the bridges in the encoder
 * chain, starting from the last bridge to the first. These are called
 * before calling the encoder's commit op.
 *
 * Note: the bridge passed should be the one closest to the encoder
 */
void drm_bridge_pre_enable(struct drm_bridge *bridge)
{
	if (!bridge)
		return;

	drm_bridge_pre_enable(bridge->next);

	bridge->funcs->pre_enable(bridge);
}
EXPORT_SYMBOL(drm_bridge_pre_enable);

/**
 * drm_bridge_enable - calls 'enable' drm_bridge_funcs op for all bridges
 *		       in the encoder chain.
 * @bridge: bridge control structure
 *
 * Calls 'enable' drm_bridge_funcs op for all the bridges in the encoder
 * chain, starting from the first bridge to the last. These are called
 * after completing the encoder's commit op.
 *
 * Note that the bridge passed should be the one closest to the encoder
 */
void drm_bridge_enable(struct drm_bridge *bridge)
{
	if (!bridge)
		return;

	bridge->funcs->enable(bridge);

	drm_bridge_enable(bridge->next);
}
EXPORT_SYMBOL(drm_bridge_enable);

#ifdef CONFIG_OF
struct drm_bridge *of_drm_find_bridge(struct device_node *np)
{
	struct drm_bridge *bridge;

	mutex_lock(&bridge_lock);

	list_for_each_entry(bridge, &bridge_list, list) {
		if (bridge->of_node == np) {
			mutex_unlock(&bridge_lock);
			return bridge;
		}
	}

	mutex_unlock(&bridge_lock);
	return NULL;
}
EXPORT_SYMBOL(of_drm_find_bridge);
#endif

MODULE_AUTHOR("Ajay Kumar <ajaykumar.rs@samsung.com>");
MODULE_DESCRIPTION("DRM bridge infrastructure");
MODULE_LICENSE("GPL and additional rights");
