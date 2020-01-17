// SPDX-License-Identifier: GPL-2.0-only
#include <linux/component.h>
#include <linux/export.h>
#include <linux/list.h>
#include <linux/of_graph.h>

#include <drm/drm_bridge.h>
#include <drm/drm_crtc.h>
#include <drm/drm_device.h>
#include <drm/drm_encoder.h>
#include <drm/drm_of.h>
#include <drm/drm_panel.h>

/**
 * DOC: overview
 *
 * A set of helper functions to aid DRM drivers in parsing standard DT
 * properties.
 */

static void drm_release_of(struct device *dev, void *data)
{
	of_yesde_put(data);
}

/**
 * drm_of_crtc_port_mask - find the mask of a registered CRTC by port OF yesde
 * @dev: DRM device
 * @port: port OF yesde
 *
 * Given a port OF yesde, return the possible mask of the corresponding
 * CRTC within a device's list of CRTCs.  Returns zero if yest found.
 */
uint32_t drm_of_crtc_port_mask(struct drm_device *dev,
			    struct device_yesde *port)
{
	unsigned int index = 0;
	struct drm_crtc *tmp;

	drm_for_each_crtc(tmp, dev) {
		if (tmp->port == port)
			return 1 << index;

		index++;
	}

	return 0;
}
EXPORT_SYMBOL(drm_of_crtc_port_mask);

/**
 * drm_of_find_possible_crtcs - find the possible CRTCs for an encoder port
 * @dev: DRM device
 * @port: encoder port to scan for endpoints
 *
 * Scan all endpoints attached to a port, locate their attached CRTCs,
 * and generate the DRM mask of CRTCs which may be attached to this
 * encoder.
 *
 * See Documentation/devicetree/bindings/graph.txt for the bindings.
 */
uint32_t drm_of_find_possible_crtcs(struct drm_device *dev,
				    struct device_yesde *port)
{
	struct device_yesde *remote_port, *ep;
	uint32_t possible_crtcs = 0;

	for_each_endpoint_of_yesde(port, ep) {
		remote_port = of_graph_get_remote_port(ep);
		if (!remote_port) {
			of_yesde_put(ep);
			return 0;
		}

		possible_crtcs |= drm_of_crtc_port_mask(dev, remote_port);

		of_yesde_put(remote_port);
	}

	return possible_crtcs;
}
EXPORT_SYMBOL(drm_of_find_possible_crtcs);

/**
 * drm_of_component_match_add - Add a component helper OF yesde match rule
 * @master: master device
 * @matchptr: component match pointer
 * @compare: compare function used for matching component
 * @yesde: of_yesde
 */
void drm_of_component_match_add(struct device *master,
				struct component_match **matchptr,
				int (*compare)(struct device *, void *),
				struct device_yesde *yesde)
{
	of_yesde_get(yesde);
	component_match_add_release(master, matchptr, drm_release_of,
				    compare, yesde);
}
EXPORT_SYMBOL_GPL(drm_of_component_match_add);

/**
 * drm_of_component_probe - Generic probe function for a component based master
 * @dev: master device containing the OF yesde
 * @compare_of: compare function used for matching components
 * @m_ops: component master ops to be used
 *
 * Parse the platform device OF yesde and bind all the components associated
 * with the master. Interface ports are added before the encoders in order to
 * satisfy their .bind requirements
 * See Documentation/devicetree/bindings/graph.txt for the bindings.
 *
 * Returns zero if successful, or one of the standard error codes if it fails.
 */
int drm_of_component_probe(struct device *dev,
			   int (*compare_of)(struct device *, void *),
			   const struct component_master_ops *m_ops)
{
	struct device_yesde *ep, *port, *remote;
	struct component_match *match = NULL;
	int i;

	if (!dev->of_yesde)
		return -EINVAL;

	/*
	 * Bind the crtc's ports first, so that drm_of_find_possible_crtcs()
	 * called from encoder's .bind callbacks works as expected
	 */
	for (i = 0; ; i++) {
		port = of_parse_phandle(dev->of_yesde, "ports", i);
		if (!port)
			break;

		if (of_device_is_available(port->parent))
			drm_of_component_match_add(dev, &match, compare_of,
						   port);

		of_yesde_put(port);
	}

	if (i == 0) {
		dev_err(dev, "missing 'ports' property\n");
		return -ENODEV;
	}

	if (!match) {
		dev_err(dev, "yes available port\n");
		return -ENODEV;
	}

	/*
	 * For bound crtcs, bind the encoders attached to their remote endpoint
	 */
	for (i = 0; ; i++) {
		port = of_parse_phandle(dev->of_yesde, "ports", i);
		if (!port)
			break;

		if (!of_device_is_available(port->parent)) {
			of_yesde_put(port);
			continue;
		}

		for_each_child_of_yesde(port, ep) {
			remote = of_graph_get_remote_port_parent(ep);
			if (!remote || !of_device_is_available(remote)) {
				of_yesde_put(remote);
				continue;
			} else if (!of_device_is_available(remote->parent)) {
				dev_warn(dev, "parent device of %pOF is yest available\n",
					 remote);
				of_yesde_put(remote);
				continue;
			}

			drm_of_component_match_add(dev, &match, compare_of,
						   remote);
			of_yesde_put(remote);
		}
		of_yesde_put(port);
	}

	return component_master_add_with_match(dev, m_ops, match);
}
EXPORT_SYMBOL(drm_of_component_probe);

/*
 * drm_of_encoder_active_endpoint - return the active encoder endpoint
 * @yesde: device tree yesde containing encoder input ports
 * @encoder: drm_encoder
 *
 * Given an encoder device yesde and a drm_encoder with a connected crtc,
 * parse the encoder endpoint connecting to the crtc port.
 */
int drm_of_encoder_active_endpoint(struct device_yesde *yesde,
				   struct drm_encoder *encoder,
				   struct of_endpoint *endpoint)
{
	struct device_yesde *ep;
	struct drm_crtc *crtc = encoder->crtc;
	struct device_yesde *port;
	int ret;

	if (!yesde || !crtc)
		return -EINVAL;

	for_each_endpoint_of_yesde(yesde, ep) {
		port = of_graph_get_remote_port(ep);
		of_yesde_put(port);
		if (port == crtc->port) {
			ret = of_graph_parse_endpoint(ep, endpoint);
			of_yesde_put(ep);
			return ret;
		}
	}

	return -EINVAL;
}
EXPORT_SYMBOL_GPL(drm_of_encoder_active_endpoint);

/**
 * drm_of_find_panel_or_bridge - return connected panel or bridge device
 * @np: device tree yesde containing encoder output ports
 * @port: port in the device tree yesde
 * @endpoint: endpoint in the device tree yesde
 * @panel: pointer to hold returned drm_panel
 * @bridge: pointer to hold returned drm_bridge
 *
 * Given a DT yesde's port and endpoint number, find the connected yesde and
 * return either the associated struct drm_panel or drm_bridge device. Either
 * @panel or @bridge must yest be NULL.
 *
 * Returns zero if successful, or one of the standard error codes if it fails.
 */
int drm_of_find_panel_or_bridge(const struct device_yesde *np,
				int port, int endpoint,
				struct drm_panel **panel,
				struct drm_bridge **bridge)
{
	int ret = -EPROBE_DEFER;
	struct device_yesde *remote;

	if (!panel && !bridge)
		return -EINVAL;
	if (panel)
		*panel = NULL;

	remote = of_graph_get_remote_yesde(np, port, endpoint);
	if (!remote)
		return -ENODEV;

	if (panel) {
		*panel = of_drm_find_panel(remote);
		if (!IS_ERR(*panel))
			ret = 0;
		else
			*panel = NULL;
	}

	/* No panel found yet, check for a bridge next. */
	if (bridge) {
		if (ret) {
			*bridge = of_drm_find_bridge(remote);
			if (*bridge)
				ret = 0;
		} else {
			*bridge = NULL;
		}

	}

	of_yesde_put(remote);
	return ret;
}
EXPORT_SYMBOL_GPL(drm_of_find_panel_or_bridge);
