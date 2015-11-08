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

/**
 * DOC: overview
 *
 * drm_bridge represents a device that hangs on to an encoder. These are handy
 * when a regular drm_encoder entity isn't enough to represent the entire
 * encoder chain.
 *
 * A bridge is always associated to a single drm_encoder at a time, but can be
 * either connected to it directly, or through an intermediate bridge:
 *
 * encoder ---> bridge B ---> bridge A
 *
 * Here, the output of the encoder feeds to bridge B, and that furthers feeds to
 * bridge A.
 *
 * The driver using the bridge is responsible to make the associations between
 * the encoder and bridges. Once these links are made, the bridges will
 * participate along with encoder functions to perform mode_set/enable/disable
 * through the ops provided in drm_bridge_funcs.
 *
 * drm_bridge, like drm_panel, aren't drm_mode_object entities like planes,
 * crtcs, encoders or connectors. They just provide additional hooks to get the
 * desired output at the end of the encoder chain.
 */

static DEFINE_MUTEX(bridge_lock);
static LIST_HEAD(bridge_list);

/**
 * drm_bridge_add - add the given bridge to the global bridge list
 *
 * @bridge: bridge control structure
 *
 * RETURNS:
 * Unconditionally returns Zero.
 */
int drm_bridge_add(struct drm_bridge *bridge)
{
	mutex_lock(&bridge_lock);
	list_add_tail(&bridge->list, &bridge_list);
	mutex_unlock(&bridge_lock);

	return 0;
}
EXPORT_SYMBOL(drm_bridge_add);

/**
 * drm_bridge_remove - remove the given bridge from the global bridge list
 *
 * @bridge: bridge control structure
 */
void drm_bridge_remove(struct drm_bridge *bridge)
{
	mutex_lock(&bridge_lock);
	list_del_init(&bridge->list);
	mutex_unlock(&bridge_lock);
}
EXPORT_SYMBOL(drm_bridge_remove);

/**
 * drm_bridge_attach - associate given bridge to our DRM device
 *
 * @dev: DRM device
 * @bridge: bridge control structure
 *
 * called by a kms driver to link one of our encoder/bridge to the given
 * bridge.
 *
 * Note that setting up links between the bridge and our encoder/bridge
 * objects needs to be handled by the kms driver itself
 *
 * RETURNS:
 * Zero on success, error code on failure
 */
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
 * DOC: bridge callbacks
 *
 * The drm_bridge_funcs ops are populated by the bridge driver. The drm
 * internals(atomic and crtc helpers) use the helpers defined in drm_bridge.c
 * These helpers call a specific drm_bridge_funcs op for all the bridges
 * during encoder configuration.
 *
 * When creating a bridge driver, one can implement drm_bridge_funcs op with
 * the help of these rough rules:
 *
 * pre_enable: this contains things needed to be done for the bridge before
 * its clock and timings are enabled by its source. For a bridge, its source
 * is generally the encoder or bridge just before it in the encoder chain.
 *
 * enable: this contains things needed to be done for the bridge once its
 * source is enabled. In other words, enable is called once the source is
 * ready with clock and timing needed by the bridge.
 *
 * disable: this contains things needed to be done for the bridge assuming
 * that its source is still enabled, i.e. clock and timings are still on.
 *
 * post_disable: this contains things needed to be done for the bridge once
 * its source is disabled, i.e. once clocks and timings are off.
 *
 * mode_fixup: this should fixup the given mode for the bridge. It is called
 * after the encoder's mode fixup. mode_fixup can also reject a mode completely
 * if it's unsuitable for the hardware.
 *
 * mode_set: this sets up the mode for the bridge. It assumes that its source
 * (an encoder or a bridge) has set the mode too.
 */

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
/**
 * of_drm_find_bridge - find the bridge corresponding to the device node in
 *			the global bridge list
 *
 * @np: device node
 *
 * RETURNS:
 * drm_bridge control struct on success, NULL on failure
 */
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
