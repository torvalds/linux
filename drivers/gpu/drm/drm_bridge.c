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
#include <linux/media-bus-format.h>
#include <linux/module.h>
#include <linux/mutex.h>

#include <drm/drm_atomic_state_helper.h>
#include <drm/drm_debugfs.h>
#include <drm/drm_bridge.h>
#include <drm/drm_encoder.h>
#include <drm/drm_file.h>
#include <drm/drm_of.h>
#include <drm/drm_print.h>

#include "drm_crtc_internal.h"

/**
 * DOC: overview
 *
 * &struct drm_bridge represents a device that hangs on to an encoder. These are
 * handy when a regular &drm_encoder entity isn't enough to represent the entire
 * encoder chain.
 *
 * A bridge is always attached to a single &drm_encoder at a time, but can be
 * either connected to it directly, or through a chain of bridges::
 *
 *     [ CRTC ---> ] Encoder ---> Bridge A ---> Bridge B
 *
 * Here, the output of the encoder feeds to bridge A, and that furthers feeds to
 * bridge B. Bridge chains can be arbitrarily long, and shall be fully linear:
 * Chaining multiple bridges to the output of a bridge, or the same bridge to
 * the output of different bridges, is not supported.
 *
 * &drm_bridge, like &drm_panel, aren't &drm_mode_object entities like planes,
 * CRTCs, encoders or connectors and hence are not visible to userspace. They
 * just provide additional hooks to get the desired output at the end of the
 * encoder chain.
 */

/**
 * DOC:	display driver integration
 *
 * Display drivers are responsible for linking encoders with the first bridge
 * in the chains. This is done by acquiring the appropriate bridge with
 * devm_drm_of_get_bridge(). Once acquired, the bridge shall be attached to the
 * encoder with a call to drm_bridge_attach().
 *
 * Bridges are responsible for linking themselves with the next bridge in the
 * chain, if any. This is done the same way as for encoders, with the call to
 * drm_bridge_attach() occurring in the &drm_bridge_funcs.attach operation.
 *
 * Once these links are created, the bridges can participate along with encoder
 * functions to perform mode validation and fixup (through
 * drm_bridge_chain_mode_valid() and drm_atomic_bridge_chain_check()), mode
 * setting (through drm_bridge_chain_mode_set()), enable (through
 * drm_atomic_bridge_chain_pre_enable() and drm_atomic_bridge_chain_enable())
 * and disable (through drm_atomic_bridge_chain_disable() and
 * drm_atomic_bridge_chain_post_disable()). Those functions call the
 * corresponding operations provided in &drm_bridge_funcs in sequence for all
 * bridges in the chain.
 *
 * For display drivers that use the atomic helpers
 * drm_atomic_helper_check_modeset(),
 * drm_atomic_helper_commit_modeset_enables() and
 * drm_atomic_helper_commit_modeset_disables() (either directly in hand-rolled
 * commit check and commit tail handlers, or through the higher-level
 * drm_atomic_helper_check() and drm_atomic_helper_commit_tail() or
 * drm_atomic_helper_commit_tail_rpm() helpers), this is done transparently and
 * requires no intervention from the driver. For other drivers, the relevant
 * DRM bridge chain functions shall be called manually.
 *
 * Bridges also participate in implementing the &drm_connector at the end of
 * the bridge chain. Display drivers may use the drm_bridge_connector_init()
 * helper to create the &drm_connector, or implement it manually on top of the
 * connector-related operations exposed by the bridge (see the overview
 * documentation of bridge operations for more details).
 */

/**
 * DOC: special care dsi
 *
 * The interaction between the bridges and other frameworks involved in
 * the probing of the upstream driver and the bridge driver can be
 * challenging. Indeed, there's multiple cases that needs to be
 * considered:
 *
 * - The upstream driver doesn't use the component framework and isn't a
 *   MIPI-DSI host. In this case, the bridge driver will probe at some
 *   point and the upstream driver should try to probe again by returning
 *   EPROBE_DEFER as long as the bridge driver hasn't probed.
 *
 * - The upstream driver doesn't use the component framework, but is a
 *   MIPI-DSI host. The bridge device uses the MIPI-DCS commands to be
 *   controlled. In this case, the bridge device is a child of the
 *   display device and when it will probe it's assured that the display
 *   device (and MIPI-DSI host) is present. The upstream driver will be
 *   assured that the bridge driver is connected between the
 *   &mipi_dsi_host_ops.attach and &mipi_dsi_host_ops.detach operations.
 *   Therefore, it must run mipi_dsi_host_register() in its probe
 *   function, and then run drm_bridge_attach() in its
 *   &mipi_dsi_host_ops.attach hook.
 *
 * - The upstream driver uses the component framework and is a MIPI-DSI
 *   host. The bridge device uses the MIPI-DCS commands to be
 *   controlled. This is the same situation than above, and can run
 *   mipi_dsi_host_register() in either its probe or bind hooks.
 *
 * - The upstream driver uses the component framework and is a MIPI-DSI
 *   host. The bridge device uses a separate bus (such as I2C) to be
 *   controlled. In this case, there's no correlation between the probe
 *   of the bridge and upstream drivers, so care must be taken to avoid
 *   an endless EPROBE_DEFER loop, with each driver waiting for the
 *   other to probe.
 *
 * The ideal pattern to cover the last item (and all the others in the
 * MIPI-DSI host driver case) is to split the operations like this:
 *
 * - The MIPI-DSI host driver must run mipi_dsi_host_register() in its
 *   probe hook. It will make sure that the MIPI-DSI host sticks around,
 *   and that the driver's bind can be called.
 *
 * - In its probe hook, the bridge driver must try to find its MIPI-DSI
 *   host, register as a MIPI-DSI device and attach the MIPI-DSI device
 *   to its host. The bridge driver is now functional.
 *
 * - In its &struct mipi_dsi_host_ops.attach hook, the MIPI-DSI host can
 *   now add its component. Its bind hook will now be called and since
 *   the bridge driver is attached and registered, we can now look for
 *   and attach it.
 *
 * At this point, we're now certain that both the upstream driver and
 * the bridge driver are functional and we can't have a deadlock-like
 * situation when probing.
 */

/**
 * DOC: dsi bridge operations
 *
 * DSI host interfaces are expected to be implemented as bridges rather than
 * encoders, however there are a few aspects of their operation that need to
 * be defined in order to provide a consistent interface.
 *
 * A DSI host should keep the PHY powered down until the pre_enable operation is
 * called. All lanes are in an undefined idle state up to this point, and it
 * must not be assumed that it is LP-11.
 * pre_enable should initialise the PHY, set the data lanes to LP-11, and the
 * clock lane to either LP-11 or HS depending on the mode_flag
 * %MIPI_DSI_CLOCK_NON_CONTINUOUS.
 *
 * Ordinarily the downstream bridge DSI peripheral pre_enable will have been
 * called before the DSI host. If the DSI peripheral requires LP-11 and/or
 * the clock lane to be in HS mode prior to pre_enable, then it can set the
 * &pre_enable_prev_first flag to request the pre_enable (and
 * post_disable) order to be altered to enable the DSI host first.
 *
 * Either the CRTC being enabled, or the DSI host enable operation should switch
 * the host to actively transmitting video on the data lanes.
 *
 * The reverse also applies. The DSI host disable operation or stopping the CRTC
 * should stop transmitting video, and the data lanes should return to the LP-11
 * state. The DSI host &post_disable operation should disable the PHY.
 * If the &pre_enable_prev_first flag is set, then the DSI peripheral's
 * bridge &post_disable will be called before the DSI host's post_disable.
 *
 * Whilst it is valid to call &host_transfer prior to pre_enable or after
 * post_disable, the exact state of the lanes is undefined at this point. The
 * DSI host should initialise the interface, transmit the data, and then disable
 * the interface again.
 *
 * Ultra Low Power State (ULPS) is not explicitly supported by DRM. If
 * implemented, it therefore needs to be handled entirely within the DSI Host
 * driver.
 */

static DEFINE_MUTEX(bridge_lock);
static LIST_HEAD(bridge_list);

/**
 * drm_bridge_add - add the given bridge to the global bridge list
 *
 * @bridge: bridge control structure
 */
void drm_bridge_add(struct drm_bridge *bridge)
{
	mutex_init(&bridge->hpd_mutex);

	mutex_lock(&bridge_lock);
	list_add_tail(&bridge->list, &bridge_list);
	mutex_unlock(&bridge_lock);
}
EXPORT_SYMBOL(drm_bridge_add);

static void drm_bridge_remove_void(void *bridge)
{
	drm_bridge_remove(bridge);
}

/**
 * devm_drm_bridge_add - devm managed version of drm_bridge_add()
 *
 * @dev: device to tie the bridge lifetime to
 * @bridge: bridge control structure
 *
 * This is the managed version of drm_bridge_add() which automatically
 * calls drm_bridge_remove() when @dev is unbound.
 *
 * Return: 0 if no error or negative error code.
 */
int devm_drm_bridge_add(struct device *dev, struct drm_bridge *bridge)
{
	drm_bridge_add(bridge);
	return devm_add_action_or_reset(dev, drm_bridge_remove_void, bridge);
}
EXPORT_SYMBOL(devm_drm_bridge_add);

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

	mutex_destroy(&bridge->hpd_mutex);
}
EXPORT_SYMBOL(drm_bridge_remove);

static struct drm_private_state *
drm_bridge_atomic_duplicate_priv_state(struct drm_private_obj *obj)
{
	struct drm_bridge *bridge = drm_priv_to_bridge(obj);
	struct drm_bridge_state *state;

	state = bridge->funcs->atomic_duplicate_state(bridge);
	return state ? &state->base : NULL;
}

static void
drm_bridge_atomic_destroy_priv_state(struct drm_private_obj *obj,
				     struct drm_private_state *s)
{
	struct drm_bridge_state *state = drm_priv_to_bridge_state(s);
	struct drm_bridge *bridge = drm_priv_to_bridge(obj);

	bridge->funcs->atomic_destroy_state(bridge, state);
}

static const struct drm_private_state_funcs drm_bridge_priv_state_funcs = {
	.atomic_duplicate_state = drm_bridge_atomic_duplicate_priv_state,
	.atomic_destroy_state = drm_bridge_atomic_destroy_priv_state,
};

/**
 * drm_bridge_attach - attach the bridge to an encoder's chain
 *
 * @encoder: DRM encoder
 * @bridge: bridge to attach
 * @previous: previous bridge in the chain (optional)
 * @flags: DRM_BRIDGE_ATTACH_* flags
 *
 * Called by a kms driver to link the bridge to an encoder's chain. The previous
 * argument specifies the previous bridge in the chain. If NULL, the bridge is
 * linked directly at the encoder's output. Otherwise it is linked at the
 * previous bridge's output.
 *
 * If non-NULL the previous bridge must be already attached by a call to this
 * function.
 *
 * Note that bridges attached to encoders are auto-detached during encoder
 * cleanup in drm_encoder_cleanup(), so drm_bridge_attach() should generally
 * *not* be balanced with a drm_bridge_detach() in driver code.
 *
 * RETURNS:
 * Zero on success, error code on failure
 */
int drm_bridge_attach(struct drm_encoder *encoder, struct drm_bridge *bridge,
		      struct drm_bridge *previous,
		      enum drm_bridge_attach_flags flags)
{
	int ret;

	if (!encoder || !bridge)
		return -EINVAL;

	if (previous && (!previous->dev || previous->encoder != encoder))
		return -EINVAL;

	if (bridge->dev)
		return -EBUSY;

	bridge->dev = encoder->dev;
	bridge->encoder = encoder;

	if (previous)
		list_add(&bridge->chain_node, &previous->chain_node);
	else
		list_add(&bridge->chain_node, &encoder->bridge_chain);

	if (bridge->funcs->attach) {
		ret = bridge->funcs->attach(bridge, flags);
		if (ret < 0)
			goto err_reset_bridge;
	}

	if (bridge->funcs->atomic_reset) {
		struct drm_bridge_state *state;

		state = bridge->funcs->atomic_reset(bridge);
		if (IS_ERR(state)) {
			ret = PTR_ERR(state);
			goto err_detach_bridge;
		}

		drm_atomic_private_obj_init(bridge->dev, &bridge->base,
					    &state->base,
					    &drm_bridge_priv_state_funcs);
	}

	return 0;

err_detach_bridge:
	if (bridge->funcs->detach)
		bridge->funcs->detach(bridge);

err_reset_bridge:
	bridge->dev = NULL;
	bridge->encoder = NULL;
	list_del(&bridge->chain_node);

#ifdef CONFIG_OF
	DRM_ERROR("failed to attach bridge %pOF to encoder %s: %d\n",
		  bridge->of_node, encoder->name, ret);
#else
	DRM_ERROR("failed to attach bridge to encoder %s: %d\n",
		  encoder->name, ret);
#endif

	return ret;
}
EXPORT_SYMBOL(drm_bridge_attach);

void drm_bridge_detach(struct drm_bridge *bridge)
{
	if (WARN_ON(!bridge))
		return;

	if (WARN_ON(!bridge->dev))
		return;

	if (bridge->funcs->atomic_reset)
		drm_atomic_private_obj_fini(&bridge->base);

	if (bridge->funcs->detach)
		bridge->funcs->detach(bridge);

	list_del(&bridge->chain_node);
	bridge->dev = NULL;
}

/**
 * DOC: bridge operations
 *
 * Bridge drivers expose operations through the &drm_bridge_funcs structure.
 * The DRM internals (atomic and CRTC helpers) use the helpers defined in
 * drm_bridge.c to call bridge operations. Those operations are divided in
 * three big categories to support different parts of the bridge usage.
 *
 * - The encoder-related operations support control of the bridges in the
 *   chain, and are roughly counterparts to the &drm_encoder_helper_funcs
 *   operations. They are used by the legacy CRTC and the atomic modeset
 *   helpers to perform mode validation, fixup and setting, and enable and
 *   disable the bridge automatically.
 *
 *   The enable and disable operations are split in
 *   &drm_bridge_funcs.pre_enable, &drm_bridge_funcs.enable,
 *   &drm_bridge_funcs.disable and &drm_bridge_funcs.post_disable to provide
 *   finer-grained control.
 *
 *   Bridge drivers may implement the legacy version of those operations, or
 *   the atomic version (prefixed with atomic\_), in which case they shall also
 *   implement the atomic state bookkeeping operations
 *   (&drm_bridge_funcs.atomic_duplicate_state,
 *   &drm_bridge_funcs.atomic_destroy_state and &drm_bridge_funcs.reset).
 *   Mixing atomic and non-atomic versions of the operations is not supported.
 *
 * - The bus format negotiation operations
 *   &drm_bridge_funcs.atomic_get_output_bus_fmts and
 *   &drm_bridge_funcs.atomic_get_input_bus_fmts allow bridge drivers to
 *   negotiate the formats transmitted between bridges in the chain when
 *   multiple formats are supported. Negotiation for formats is performed
 *   transparently for display drivers by the atomic modeset helpers. Only
 *   atomic versions of those operations exist, bridge drivers that need to
 *   implement them shall thus also implement the atomic version of the
 *   encoder-related operations. This feature is not supported by the legacy
 *   CRTC helpers.
 *
 * - The connector-related operations support implementing a &drm_connector
 *   based on a chain of bridges. DRM bridges traditionally create a
 *   &drm_connector for bridges meant to be used at the end of the chain. This
 *   puts additional burden on bridge drivers, especially for bridges that may
 *   be used in the middle of a chain or at the end of it. Furthermore, it
 *   requires all operations of the &drm_connector to be handled by a single
 *   bridge, which doesn't always match the hardware architecture.
 *
 *   To simplify bridge drivers and make the connector implementation more
 *   flexible, a new model allows bridges to unconditionally skip creation of
 *   &drm_connector and instead expose &drm_bridge_funcs operations to support
 *   an externally-implemented &drm_connector. Those operations are
 *   &drm_bridge_funcs.detect, &drm_bridge_funcs.get_modes,
 *   &drm_bridge_funcs.get_edid, &drm_bridge_funcs.hpd_notify,
 *   &drm_bridge_funcs.hpd_enable and &drm_bridge_funcs.hpd_disable. When
 *   implemented, display drivers shall create a &drm_connector instance for
 *   each chain of bridges, and implement those connector instances based on
 *   the bridge connector operations.
 *
 *   Bridge drivers shall implement the connector-related operations for all
 *   the features that the bridge hardware support. For instance, if a bridge
 *   supports reading EDID, the &drm_bridge_funcs.get_edid shall be
 *   implemented. This however doesn't mean that the DDC lines are wired to the
 *   bridge on a particular platform, as they could also be connected to an I2C
 *   controller of the SoC. Support for the connector-related operations on the
 *   running platform is reported through the &drm_bridge.ops flags. Bridge
 *   drivers shall detect which operations they can support on the platform
 *   (usually this information is provided by ACPI or DT), and set the
 *   &drm_bridge.ops flags for all supported operations. A flag shall only be
 *   set if the corresponding &drm_bridge_funcs operation is implemented, but
 *   an implemented operation doesn't necessarily imply that the corresponding
 *   flag will be set. Display drivers shall use the &drm_bridge.ops flags to
 *   decide which bridge to delegate a connector operation to. This mechanism
 *   allows providing a single static const &drm_bridge_funcs instance in
 *   bridge drivers, improving security by storing function pointers in
 *   read-only memory.
 *
 *   In order to ease transition, bridge drivers may support both the old and
 *   new models by making connector creation optional and implementing the
 *   connected-related bridge operations. Connector creation is then controlled
 *   by the flags argument to the drm_bridge_attach() function. Display drivers
 *   that support the new model and create connectors themselves shall set the
 *   %DRM_BRIDGE_ATTACH_NO_CONNECTOR flag, and bridge drivers shall then skip
 *   connector creation. For intermediate bridges in the chain, the flag shall
 *   be passed to the drm_bridge_attach() call for the downstream bridge.
 *   Bridge drivers that implement the new model only shall return an error
 *   from their &drm_bridge_funcs.attach handler when the
 *   %DRM_BRIDGE_ATTACH_NO_CONNECTOR flag is not set. New display drivers
 *   should use the new model, and convert the bridge drivers they use if
 *   needed, in order to gradually transition to the new model.
 */

/**
 * drm_bridge_chain_mode_fixup - fixup proposed mode for all bridges in the
 *				 encoder chain
 * @bridge: bridge control structure
 * @mode: desired mode to be set for the bridge
 * @adjusted_mode: updated mode that works for this bridge
 *
 * Calls &drm_bridge_funcs.mode_fixup for all the bridges in the
 * encoder chain, starting from the first bridge to the last.
 *
 * Note: the bridge passed should be the one closest to the encoder
 *
 * RETURNS:
 * true on success, false on failure
 */
bool drm_bridge_chain_mode_fixup(struct drm_bridge *bridge,
				 const struct drm_display_mode *mode,
				 struct drm_display_mode *adjusted_mode)
{
	struct drm_encoder *encoder;

	if (!bridge)
		return true;

	encoder = bridge->encoder;
	list_for_each_entry_from(bridge, &encoder->bridge_chain, chain_node) {
		if (!bridge->funcs->mode_fixup)
			continue;

		if (!bridge->funcs->mode_fixup(bridge, mode, adjusted_mode))
			return false;
	}

	return true;
}
EXPORT_SYMBOL(drm_bridge_chain_mode_fixup);

/**
 * drm_bridge_chain_mode_valid - validate the mode against all bridges in the
 *				 encoder chain.
 * @bridge: bridge control structure
 * @info: display info against which the mode shall be validated
 * @mode: desired mode to be validated
 *
 * Calls &drm_bridge_funcs.mode_valid for all the bridges in the encoder
 * chain, starting from the first bridge to the last. If at least one bridge
 * does not accept the mode the function returns the error code.
 *
 * Note: the bridge passed should be the one closest to the encoder.
 *
 * RETURNS:
 * MODE_OK on success, drm_mode_status Enum error code on failure
 */
enum drm_mode_status
drm_bridge_chain_mode_valid(struct drm_bridge *bridge,
			    const struct drm_display_info *info,
			    const struct drm_display_mode *mode)
{
	struct drm_encoder *encoder;

	if (!bridge)
		return MODE_OK;

	encoder = bridge->encoder;
	list_for_each_entry_from(bridge, &encoder->bridge_chain, chain_node) {
		enum drm_mode_status ret;

		if (!bridge->funcs->mode_valid)
			continue;

		ret = bridge->funcs->mode_valid(bridge, info, mode);
		if (ret != MODE_OK)
			return ret;
	}

	return MODE_OK;
}
EXPORT_SYMBOL(drm_bridge_chain_mode_valid);

/**
 * drm_bridge_chain_mode_set - set proposed mode for all bridges in the
 *			       encoder chain
 * @bridge: bridge control structure
 * @mode: desired mode to be set for the encoder chain
 * @adjusted_mode: updated mode that works for this encoder chain
 *
 * Calls &drm_bridge_funcs.mode_set op for all the bridges in the
 * encoder chain, starting from the first bridge to the last.
 *
 * Note: the bridge passed should be the one closest to the encoder
 */
void drm_bridge_chain_mode_set(struct drm_bridge *bridge,
			       const struct drm_display_mode *mode,
			       const struct drm_display_mode *adjusted_mode)
{
	struct drm_encoder *encoder;

	if (!bridge)
		return;

	encoder = bridge->encoder;
	list_for_each_entry_from(bridge, &encoder->bridge_chain, chain_node) {
		if (bridge->funcs->mode_set)
			bridge->funcs->mode_set(bridge, mode, adjusted_mode);
	}
}
EXPORT_SYMBOL(drm_bridge_chain_mode_set);

/**
 * drm_atomic_bridge_chain_disable - disables all bridges in the encoder chain
 * @bridge: bridge control structure
 * @old_state: old atomic state
 *
 * Calls &drm_bridge_funcs.atomic_disable (falls back on
 * &drm_bridge_funcs.disable) op for all the bridges in the encoder chain,
 * starting from the last bridge to the first. These are called before calling
 * &drm_encoder_helper_funcs.atomic_disable
 *
 * Note: the bridge passed should be the one closest to the encoder
 */
void drm_atomic_bridge_chain_disable(struct drm_bridge *bridge,
				     struct drm_atomic_state *old_state)
{
	struct drm_encoder *encoder;
	struct drm_bridge *iter;

	if (!bridge)
		return;

	encoder = bridge->encoder;
	list_for_each_entry_reverse(iter, &encoder->bridge_chain, chain_node) {
		if (iter->funcs->atomic_disable) {
			struct drm_bridge_state *old_bridge_state;

			old_bridge_state =
				drm_atomic_get_old_bridge_state(old_state,
								iter);
			if (WARN_ON(!old_bridge_state))
				return;

			iter->funcs->atomic_disable(iter, old_bridge_state);
		} else if (iter->funcs->disable) {
			iter->funcs->disable(iter);
		}

		if (iter == bridge)
			break;
	}
}
EXPORT_SYMBOL(drm_atomic_bridge_chain_disable);

static void drm_atomic_bridge_call_post_disable(struct drm_bridge *bridge,
						struct drm_atomic_state *old_state)
{
	if (old_state && bridge->funcs->atomic_post_disable) {
		struct drm_bridge_state *old_bridge_state;

		old_bridge_state =
			drm_atomic_get_old_bridge_state(old_state,
							bridge);
		if (WARN_ON(!old_bridge_state))
			return;

		bridge->funcs->atomic_post_disable(bridge,
						   old_bridge_state);
	} else if (bridge->funcs->post_disable) {
		bridge->funcs->post_disable(bridge);
	}
}

/**
 * drm_atomic_bridge_chain_post_disable - cleans up after disabling all bridges
 *					  in the encoder chain
 * @bridge: bridge control structure
 * @old_state: old atomic state
 *
 * Calls &drm_bridge_funcs.atomic_post_disable (falls back on
 * &drm_bridge_funcs.post_disable) op for all the bridges in the encoder chain,
 * starting from the first bridge to the last. These are called after completing
 * &drm_encoder_helper_funcs.atomic_disable
 *
 * If a bridge sets @pre_enable_prev_first, then the @post_disable for that
 * bridge will be called before the previous one to reverse the @pre_enable
 * calling direction.
 *
 * Note: the bridge passed should be the one closest to the encoder
 */
void drm_atomic_bridge_chain_post_disable(struct drm_bridge *bridge,
					  struct drm_atomic_state *old_state)
{
	struct drm_encoder *encoder;
	struct drm_bridge *next, *limit;

	if (!bridge)
		return;

	encoder = bridge->encoder;

	list_for_each_entry_from(bridge, &encoder->bridge_chain, chain_node) {
		limit = NULL;

		if (!list_is_last(&bridge->chain_node, &encoder->bridge_chain)) {
			next = list_next_entry(bridge, chain_node);

			if (next->pre_enable_prev_first) {
				/* next bridge had requested that prev
				 * was enabled first, so disabled last
				 */
				limit = next;

				/* Find the next bridge that has NOT requested
				 * prev to be enabled first / disabled last
				 */
				list_for_each_entry_from(next, &encoder->bridge_chain,
							 chain_node) {
					if (next->pre_enable_prev_first) {
						next = list_prev_entry(next, chain_node);
						limit = next;
						break;
					}
				}

				/* Call these bridges in reverse order */
				list_for_each_entry_from_reverse(next, &encoder->bridge_chain,
								 chain_node) {
					if (next == bridge)
						break;

					drm_atomic_bridge_call_post_disable(next,
									    old_state);
				}
			}
		}

		drm_atomic_bridge_call_post_disable(bridge, old_state);

		if (limit)
			/* Jump all bridges that we have already post_disabled */
			bridge = limit;
	}
}
EXPORT_SYMBOL(drm_atomic_bridge_chain_post_disable);

static void drm_atomic_bridge_call_pre_enable(struct drm_bridge *bridge,
					      struct drm_atomic_state *old_state)
{
	if (old_state && bridge->funcs->atomic_pre_enable) {
		struct drm_bridge_state *old_bridge_state;

		old_bridge_state =
			drm_atomic_get_old_bridge_state(old_state,
							bridge);
		if (WARN_ON(!old_bridge_state))
			return;

		bridge->funcs->atomic_pre_enable(bridge, old_bridge_state);
	} else if (bridge->funcs->pre_enable) {
		bridge->funcs->pre_enable(bridge);
	}
}

/**
 * drm_atomic_bridge_chain_pre_enable - prepares for enabling all bridges in
 *					the encoder chain
 * @bridge: bridge control structure
 * @old_state: old atomic state
 *
 * Calls &drm_bridge_funcs.atomic_pre_enable (falls back on
 * &drm_bridge_funcs.pre_enable) op for all the bridges in the encoder chain,
 * starting from the last bridge to the first. These are called before calling
 * &drm_encoder_helper_funcs.atomic_enable
 *
 * If a bridge sets @pre_enable_prev_first, then the pre_enable for the
 * prev bridge will be called before pre_enable of this bridge.
 *
 * Note: the bridge passed should be the one closest to the encoder
 */
void drm_atomic_bridge_chain_pre_enable(struct drm_bridge *bridge,
					struct drm_atomic_state *old_state)
{
	struct drm_encoder *encoder;
	struct drm_bridge *iter, *next, *limit;

	if (!bridge)
		return;

	encoder = bridge->encoder;

	list_for_each_entry_reverse(iter, &encoder->bridge_chain, chain_node) {
		if (iter->pre_enable_prev_first) {
			next = iter;
			limit = bridge;
			list_for_each_entry_from_reverse(next,
							 &encoder->bridge_chain,
							 chain_node) {
				if (next == bridge)
					break;

				if (!next->pre_enable_prev_first) {
					/* Found first bridge that does NOT
					 * request prev to be enabled first
					 */
					limit = list_prev_entry(next, chain_node);
					break;
				}
			}

			list_for_each_entry_from(next, &encoder->bridge_chain, chain_node) {
				/* Call requested prev bridge pre_enable
				 * in order.
				 */
				if (next == iter)
					/* At the first bridge to request prev
					 * bridges called first.
					 */
					break;

				drm_atomic_bridge_call_pre_enable(next, old_state);
			}
		}

		drm_atomic_bridge_call_pre_enable(iter, old_state);

		if (iter->pre_enable_prev_first)
			/* Jump all bridges that we have already pre_enabled */
			iter = limit;

		if (iter == bridge)
			break;
	}
}
EXPORT_SYMBOL(drm_atomic_bridge_chain_pre_enable);

/**
 * drm_atomic_bridge_chain_enable - enables all bridges in the encoder chain
 * @bridge: bridge control structure
 * @old_state: old atomic state
 *
 * Calls &drm_bridge_funcs.atomic_enable (falls back on
 * &drm_bridge_funcs.enable) op for all the bridges in the encoder chain,
 * starting from the first bridge to the last. These are called after completing
 * &drm_encoder_helper_funcs.atomic_enable
 *
 * Note: the bridge passed should be the one closest to the encoder
 */
void drm_atomic_bridge_chain_enable(struct drm_bridge *bridge,
				    struct drm_atomic_state *old_state)
{
	struct drm_encoder *encoder;

	if (!bridge)
		return;

	encoder = bridge->encoder;
	list_for_each_entry_from(bridge, &encoder->bridge_chain, chain_node) {
		if (bridge->funcs->atomic_enable) {
			struct drm_bridge_state *old_bridge_state;

			old_bridge_state =
				drm_atomic_get_old_bridge_state(old_state,
								bridge);
			if (WARN_ON(!old_bridge_state))
				return;

			bridge->funcs->atomic_enable(bridge, old_bridge_state);
		} else if (bridge->funcs->enable) {
			bridge->funcs->enable(bridge);
		}
	}
}
EXPORT_SYMBOL(drm_atomic_bridge_chain_enable);

static int drm_atomic_bridge_check(struct drm_bridge *bridge,
				   struct drm_crtc_state *crtc_state,
				   struct drm_connector_state *conn_state)
{
	if (bridge->funcs->atomic_check) {
		struct drm_bridge_state *bridge_state;
		int ret;

		bridge_state = drm_atomic_get_new_bridge_state(crtc_state->state,
							       bridge);
		if (WARN_ON(!bridge_state))
			return -EINVAL;

		ret = bridge->funcs->atomic_check(bridge, bridge_state,
						  crtc_state, conn_state);
		if (ret)
			return ret;
	} else if (bridge->funcs->mode_fixup) {
		if (!bridge->funcs->mode_fixup(bridge, &crtc_state->mode,
					       &crtc_state->adjusted_mode))
			return -EINVAL;
	}

	return 0;
}

static int select_bus_fmt_recursive(struct drm_bridge *first_bridge,
				    struct drm_bridge *cur_bridge,
				    struct drm_crtc_state *crtc_state,
				    struct drm_connector_state *conn_state,
				    u32 out_bus_fmt)
{
	unsigned int i, num_in_bus_fmts = 0;
	struct drm_bridge_state *cur_state;
	struct drm_bridge *prev_bridge;
	u32 *in_bus_fmts;
	int ret;

	prev_bridge = drm_bridge_get_prev_bridge(cur_bridge);
	cur_state = drm_atomic_get_new_bridge_state(crtc_state->state,
						    cur_bridge);

	/*
	 * If bus format negotiation is not supported by this bridge, let's
	 * pass MEDIA_BUS_FMT_FIXED to the previous bridge in the chain and
	 * hope that it can handle this situation gracefully (by providing
	 * appropriate default values).
	 */
	if (!cur_bridge->funcs->atomic_get_input_bus_fmts) {
		if (cur_bridge != first_bridge) {
			ret = select_bus_fmt_recursive(first_bridge,
						       prev_bridge, crtc_state,
						       conn_state,
						       MEDIA_BUS_FMT_FIXED);
			if (ret)
				return ret;
		}

		/*
		 * Driver does not implement the atomic state hooks, but that's
		 * fine, as long as it does not access the bridge state.
		 */
		if (cur_state) {
			cur_state->input_bus_cfg.format = MEDIA_BUS_FMT_FIXED;
			cur_state->output_bus_cfg.format = out_bus_fmt;
		}

		return 0;
	}

	/*
	 * If the driver implements ->atomic_get_input_bus_fmts() it
	 * should also implement the atomic state hooks.
	 */
	if (WARN_ON(!cur_state))
		return -EINVAL;

	in_bus_fmts = cur_bridge->funcs->atomic_get_input_bus_fmts(cur_bridge,
							cur_state,
							crtc_state,
							conn_state,
							out_bus_fmt,
							&num_in_bus_fmts);
	if (!num_in_bus_fmts)
		return -ENOTSUPP;
	else if (!in_bus_fmts)
		return -ENOMEM;

	if (first_bridge == cur_bridge) {
		cur_state->input_bus_cfg.format = in_bus_fmts[0];
		cur_state->output_bus_cfg.format = out_bus_fmt;
		kfree(in_bus_fmts);
		return 0;
	}

	for (i = 0; i < num_in_bus_fmts; i++) {
		ret = select_bus_fmt_recursive(first_bridge, prev_bridge,
					       crtc_state, conn_state,
					       in_bus_fmts[i]);
		if (ret != -ENOTSUPP)
			break;
	}

	if (!ret) {
		cur_state->input_bus_cfg.format = in_bus_fmts[i];
		cur_state->output_bus_cfg.format = out_bus_fmt;
	}

	kfree(in_bus_fmts);
	return ret;
}

/*
 * This function is called by &drm_atomic_bridge_chain_check() just before
 * calling &drm_bridge_funcs.atomic_check() on all elements of the chain.
 * It performs bus format negotiation between bridge elements. The negotiation
 * happens in reverse order, starting from the last element in the chain up to
 * @bridge.
 *
 * Negotiation starts by retrieving supported output bus formats on the last
 * bridge element and testing them one by one. The test is recursive, meaning
 * that for each tested output format, the whole chain will be walked backward,
 * and each element will have to choose an input bus format that can be
 * transcoded to the requested output format. When a bridge element does not
 * support transcoding into a specific output format -ENOTSUPP is returned and
 * the next bridge element will have to try a different format. If none of the
 * combinations worked, -ENOTSUPP is returned and the atomic modeset will fail.
 *
 * This implementation is relying on
 * &drm_bridge_funcs.atomic_get_output_bus_fmts() and
 * &drm_bridge_funcs.atomic_get_input_bus_fmts() to gather supported
 * input/output formats.
 *
 * When &drm_bridge_funcs.atomic_get_output_bus_fmts() is not implemented by
 * the last element of the chain, &drm_atomic_bridge_chain_select_bus_fmts()
 * tries a single format: &drm_connector.display_info.bus_formats[0] if
 * available, MEDIA_BUS_FMT_FIXED otherwise.
 *
 * When &drm_bridge_funcs.atomic_get_input_bus_fmts() is not implemented,
 * &drm_atomic_bridge_chain_select_bus_fmts() skips the negotiation on the
 * bridge element that lacks this hook and asks the previous element in the
 * chain to try MEDIA_BUS_FMT_FIXED. It's up to bridge drivers to decide what
 * to do in that case (fail if they want to enforce bus format negotiation, or
 * provide a reasonable default if they need to support pipelines where not
 * all elements support bus format negotiation).
 */
static int
drm_atomic_bridge_chain_select_bus_fmts(struct drm_bridge *bridge,
					struct drm_crtc_state *crtc_state,
					struct drm_connector_state *conn_state)
{
	struct drm_connector *conn = conn_state->connector;
	struct drm_encoder *encoder = bridge->encoder;
	struct drm_bridge_state *last_bridge_state;
	unsigned int i, num_out_bus_fmts = 0;
	struct drm_bridge *last_bridge;
	u32 *out_bus_fmts;
	int ret = 0;

	last_bridge = list_last_entry(&encoder->bridge_chain,
				      struct drm_bridge, chain_node);
	last_bridge_state = drm_atomic_get_new_bridge_state(crtc_state->state,
							    last_bridge);

	if (last_bridge->funcs->atomic_get_output_bus_fmts) {
		const struct drm_bridge_funcs *funcs = last_bridge->funcs;

		/*
		 * If the driver implements ->atomic_get_output_bus_fmts() it
		 * should also implement the atomic state hooks.
		 */
		if (WARN_ON(!last_bridge_state))
			return -EINVAL;

		out_bus_fmts = funcs->atomic_get_output_bus_fmts(last_bridge,
							last_bridge_state,
							crtc_state,
							conn_state,
							&num_out_bus_fmts);
		if (!num_out_bus_fmts)
			return -ENOTSUPP;
		else if (!out_bus_fmts)
			return -ENOMEM;
	} else {
		num_out_bus_fmts = 1;
		out_bus_fmts = kmalloc(sizeof(*out_bus_fmts), GFP_KERNEL);
		if (!out_bus_fmts)
			return -ENOMEM;

		if (conn->display_info.num_bus_formats &&
		    conn->display_info.bus_formats)
			out_bus_fmts[0] = conn->display_info.bus_formats[0];
		else
			out_bus_fmts[0] = MEDIA_BUS_FMT_FIXED;
	}

	for (i = 0; i < num_out_bus_fmts; i++) {
		ret = select_bus_fmt_recursive(bridge, last_bridge, crtc_state,
					       conn_state, out_bus_fmts[i]);
		if (ret != -ENOTSUPP)
			break;
	}

	kfree(out_bus_fmts);

	return ret;
}

static void
drm_atomic_bridge_propagate_bus_flags(struct drm_bridge *bridge,
				      struct drm_connector *conn,
				      struct drm_atomic_state *state)
{
	struct drm_bridge_state *bridge_state, *next_bridge_state;
	struct drm_bridge *next_bridge;
	u32 output_flags = 0;

	bridge_state = drm_atomic_get_new_bridge_state(state, bridge);

	/* No bridge state attached to this bridge => nothing to propagate. */
	if (!bridge_state)
		return;

	next_bridge = drm_bridge_get_next_bridge(bridge);

	/*
	 * Let's try to apply the most common case here, that is, propagate
	 * display_info flags for the last bridge, and propagate the input
	 * flags of the next bridge element to the output end of the current
	 * bridge when the bridge is not the last one.
	 * There are exceptions to this rule, like when signal inversion is
	 * happening at the board level, but that's something drivers can deal
	 * with from their &drm_bridge_funcs.atomic_check() implementation by
	 * simply overriding the flags value we've set here.
	 */
	if (!next_bridge) {
		output_flags = conn->display_info.bus_flags;
	} else {
		next_bridge_state = drm_atomic_get_new_bridge_state(state,
								next_bridge);
		/*
		 * No bridge state attached to the next bridge, just leave the
		 * flags to 0.
		 */
		if (next_bridge_state)
			output_flags = next_bridge_state->input_bus_cfg.flags;
	}

	bridge_state->output_bus_cfg.flags = output_flags;

	/*
	 * Propagate the output flags to the input end of the bridge. Again, it's
	 * not necessarily what all bridges want, but that's what most of them
	 * do, and by doing that by default we avoid forcing drivers to
	 * duplicate the "dummy propagation" logic.
	 */
	bridge_state->input_bus_cfg.flags = output_flags;
}

/**
 * drm_atomic_bridge_chain_check() - Do an atomic check on the bridge chain
 * @bridge: bridge control structure
 * @crtc_state: new CRTC state
 * @conn_state: new connector state
 *
 * First trigger a bus format negotiation before calling
 * &drm_bridge_funcs.atomic_check() (falls back on
 * &drm_bridge_funcs.mode_fixup()) op for all the bridges in the encoder chain,
 * starting from the last bridge to the first. These are called before calling
 * &drm_encoder_helper_funcs.atomic_check()
 *
 * RETURNS:
 * 0 on success, a negative error code on failure
 */
int drm_atomic_bridge_chain_check(struct drm_bridge *bridge,
				  struct drm_crtc_state *crtc_state,
				  struct drm_connector_state *conn_state)
{
	struct drm_connector *conn = conn_state->connector;
	struct drm_encoder *encoder;
	struct drm_bridge *iter;
	int ret;

	if (!bridge)
		return 0;

	ret = drm_atomic_bridge_chain_select_bus_fmts(bridge, crtc_state,
						      conn_state);
	if (ret)
		return ret;

	encoder = bridge->encoder;
	list_for_each_entry_reverse(iter, &encoder->bridge_chain, chain_node) {
		int ret;

		/*
		 * Bus flags are propagated by default. If a bridge needs to
		 * tweak the input bus flags for any reason, it should happen
		 * in its &drm_bridge_funcs.atomic_check() implementation such
		 * that preceding bridges in the chain can propagate the new
		 * bus flags.
		 */
		drm_atomic_bridge_propagate_bus_flags(iter, conn,
						      crtc_state->state);

		ret = drm_atomic_bridge_check(iter, crtc_state, conn_state);
		if (ret)
			return ret;

		if (iter == bridge)
			break;
	}

	return 0;
}
EXPORT_SYMBOL(drm_atomic_bridge_chain_check);

/**
 * drm_bridge_detect - check if anything is attached to the bridge output
 * @bridge: bridge control structure
 *
 * If the bridge supports output detection, as reported by the
 * DRM_BRIDGE_OP_DETECT bridge ops flag, call &drm_bridge_funcs.detect for the
 * bridge and return the connection status. Otherwise return
 * connector_status_unknown.
 *
 * RETURNS:
 * The detection status on success, or connector_status_unknown if the bridge
 * doesn't support output detection.
 */
enum drm_connector_status drm_bridge_detect(struct drm_bridge *bridge)
{
	if (!(bridge->ops & DRM_BRIDGE_OP_DETECT))
		return connector_status_unknown;

	return bridge->funcs->detect(bridge);
}
EXPORT_SYMBOL_GPL(drm_bridge_detect);

/**
 * drm_bridge_get_modes - fill all modes currently valid for the sink into the
 * @connector
 * @bridge: bridge control structure
 * @connector: the connector to fill with modes
 *
 * If the bridge supports output modes retrieval, as reported by the
 * DRM_BRIDGE_OP_MODES bridge ops flag, call &drm_bridge_funcs.get_modes to
 * fill the connector with all valid modes and return the number of modes
 * added. Otherwise return 0.
 *
 * RETURNS:
 * The number of modes added to the connector.
 */
int drm_bridge_get_modes(struct drm_bridge *bridge,
			 struct drm_connector *connector)
{
	if (!(bridge->ops & DRM_BRIDGE_OP_MODES))
		return 0;

	return bridge->funcs->get_modes(bridge, connector);
}
EXPORT_SYMBOL_GPL(drm_bridge_get_modes);

/**
 * drm_bridge_get_edid - get the EDID data of the connected display
 * @bridge: bridge control structure
 * @connector: the connector to read EDID for
 *
 * If the bridge supports output EDID retrieval, as reported by the
 * DRM_BRIDGE_OP_EDID bridge ops flag, call &drm_bridge_funcs.get_edid to
 * get the EDID and return it. Otherwise return NULL.
 *
 * RETURNS:
 * The retrieved EDID on success, or NULL otherwise.
 */
struct edid *drm_bridge_get_edid(struct drm_bridge *bridge,
				 struct drm_connector *connector)
{
	if (!(bridge->ops & DRM_BRIDGE_OP_EDID))
		return NULL;

	return bridge->funcs->get_edid(bridge, connector);
}
EXPORT_SYMBOL_GPL(drm_bridge_get_edid);

/**
 * drm_bridge_hpd_enable - enable hot plug detection for the bridge
 * @bridge: bridge control structure
 * @cb: hot-plug detection callback
 * @data: data to be passed to the hot-plug detection callback
 *
 * Call &drm_bridge_funcs.hpd_enable if implemented and register the given @cb
 * and @data as hot plug notification callback. From now on the @cb will be
 * called with @data when an output status change is detected by the bridge,
 * until hot plug notification gets disabled with drm_bridge_hpd_disable().
 *
 * Hot plug detection is supported only if the DRM_BRIDGE_OP_HPD flag is set in
 * bridge->ops. This function shall not be called when the flag is not set.
 *
 * Only one hot plug detection callback can be registered at a time, it is an
 * error to call this function when hot plug detection is already enabled for
 * the bridge.
 */
void drm_bridge_hpd_enable(struct drm_bridge *bridge,
			   void (*cb)(void *data,
				      enum drm_connector_status status),
			   void *data)
{
	if (!(bridge->ops & DRM_BRIDGE_OP_HPD))
		return;

	mutex_lock(&bridge->hpd_mutex);

	if (WARN(bridge->hpd_cb, "Hot plug detection already enabled\n"))
		goto unlock;

	bridge->hpd_cb = cb;
	bridge->hpd_data = data;

	if (bridge->funcs->hpd_enable)
		bridge->funcs->hpd_enable(bridge);

unlock:
	mutex_unlock(&bridge->hpd_mutex);
}
EXPORT_SYMBOL_GPL(drm_bridge_hpd_enable);

/**
 * drm_bridge_hpd_disable - disable hot plug detection for the bridge
 * @bridge: bridge control structure
 *
 * Call &drm_bridge_funcs.hpd_disable if implemented and unregister the hot
 * plug detection callback previously registered with drm_bridge_hpd_enable().
 * Once this function returns the callback will not be called by the bridge
 * when an output status change occurs.
 *
 * Hot plug detection is supported only if the DRM_BRIDGE_OP_HPD flag is set in
 * bridge->ops. This function shall not be called when the flag is not set.
 */
void drm_bridge_hpd_disable(struct drm_bridge *bridge)
{
	if (!(bridge->ops & DRM_BRIDGE_OP_HPD))
		return;

	mutex_lock(&bridge->hpd_mutex);
	if (bridge->funcs->hpd_disable)
		bridge->funcs->hpd_disable(bridge);

	bridge->hpd_cb = NULL;
	bridge->hpd_data = NULL;
	mutex_unlock(&bridge->hpd_mutex);
}
EXPORT_SYMBOL_GPL(drm_bridge_hpd_disable);

/**
 * drm_bridge_hpd_notify - notify hot plug detection events
 * @bridge: bridge control structure
 * @status: output connection status
 *
 * Bridge drivers shall call this function to report hot plug events when they
 * detect a change in the output status, when hot plug detection has been
 * enabled by drm_bridge_hpd_enable().
 *
 * This function shall be called in a context that can sleep.
 */
void drm_bridge_hpd_notify(struct drm_bridge *bridge,
			   enum drm_connector_status status)
{
	mutex_lock(&bridge->hpd_mutex);
	if (bridge->hpd_cb)
		bridge->hpd_cb(bridge->hpd_data, status);
	mutex_unlock(&bridge->hpd_mutex);
}
EXPORT_SYMBOL_GPL(drm_bridge_hpd_notify);

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

#ifdef CONFIG_DEBUG_FS
static int drm_bridge_chains_info(struct seq_file *m, void *data)
{
	struct drm_debugfs_entry *entry = m->private;
	struct drm_device *dev = entry->dev;
	struct drm_printer p = drm_seq_file_printer(m);
	struct drm_mode_config *config = &dev->mode_config;
	struct drm_encoder *encoder;
	unsigned int bridge_idx = 0;

	list_for_each_entry(encoder, &config->encoder_list, head) {
		struct drm_bridge *bridge;

		drm_printf(&p, "encoder[%u]\n", encoder->base.id);

		drm_for_each_bridge_in_chain(encoder, bridge) {
			drm_printf(&p, "\tbridge[%u] type: %u, ops: %#x",
				   bridge_idx, bridge->type, bridge->ops);

#ifdef CONFIG_OF
			if (bridge->of_node)
				drm_printf(&p, ", OF: %pOFfc", bridge->of_node);
#endif

			drm_printf(&p, "\n");

			bridge_idx++;
		}
	}

	return 0;
}

static const struct drm_debugfs_info drm_bridge_debugfs_list[] = {
	{ "bridge_chains", drm_bridge_chains_info, 0 },
};

void drm_bridge_debugfs_init(struct drm_minor *minor)
{
	drm_debugfs_add_files(minor->dev, drm_bridge_debugfs_list,
			      ARRAY_SIZE(drm_bridge_debugfs_list));
}
#endif

MODULE_AUTHOR("Ajay Kumar <ajaykumar.rs@samsung.com>");
MODULE_DESCRIPTION("DRM bridge infrastructure");
MODULE_LICENSE("GPL and additional rights");
