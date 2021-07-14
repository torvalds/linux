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
	of_node_put(data);
}

/**
 * drm_of_crtc_port_mask - find the mask of a registered CRTC by port OF node
 * @dev: DRM device
 * @port: port OF node
 *
 * Given a port OF node, return the possible mask of the corresponding
 * CRTC within a device's list of CRTCs.  Returns zero if not found.
 */
uint32_t drm_of_crtc_port_mask(struct drm_device *dev,
			    struct device_node *port)
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
				    struct device_node *port)
{
	struct device_node *remote_port, *ep;
	uint32_t possible_crtcs = 0;

	for_each_endpoint_of_node(port, ep) {
		remote_port = of_graph_get_remote_port(ep);
		if (!remote_port) {
			of_node_put(ep);
			return 0;
		}

		possible_crtcs |= drm_of_crtc_port_mask(dev, remote_port);

		of_node_put(remote_port);
	}

	return possible_crtcs;
}
EXPORT_SYMBOL(drm_of_find_possible_crtcs);

/**
 * drm_of_component_match_add - Add a component helper OF node match rule
 * @master: master device
 * @matchptr: component match pointer
 * @compare: compare function used for matching component
 * @node: of_node
 */
void drm_of_component_match_add(struct device *master,
				struct component_match **matchptr,
				int (*compare)(struct device *, void *),
				struct device_node *node)
{
	of_node_get(node);
	component_match_add_release(master, matchptr, drm_release_of,
				    compare, node);
}
EXPORT_SYMBOL_GPL(drm_of_component_match_add);

/**
 * drm_of_component_probe - Generic probe function for a component based master
 * @dev: master device containing the OF node
 * @compare_of: compare function used for matching components
 * @m_ops: component master ops to be used
 *
 * Parse the platform device OF node and bind all the components associated
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
	struct device_node *ep, *port, *remote;
	struct component_match *match = NULL;
	int i;

	if (!dev->of_node)
		return -EINVAL;

	/*
	 * Bind the crtc's ports first, so that drm_of_find_possible_crtcs()
	 * called from encoder's .bind callbacks works as expected
	 */
	for (i = 0; ; i++) {
		port = of_parse_phandle(dev->of_node, "ports", i);
		if (!port)
			break;

		if (of_device_is_available(port->parent))
			drm_of_component_match_add(dev, &match, compare_of,
						   port);

		of_node_put(port);
	}

	if (i == 0) {
		dev_err(dev, "missing 'ports' property\n");
		return -ENODEV;
	}

	if (!match) {
		dev_err(dev, "no available port\n");
		return -ENODEV;
	}

	/*
	 * For bound crtcs, bind the encoders attached to their remote endpoint
	 */
	for (i = 0; ; i++) {
		port = of_parse_phandle(dev->of_node, "ports", i);
		if (!port)
			break;

		if (!of_device_is_available(port->parent)) {
			of_node_put(port);
			continue;
		}

		for_each_child_of_node(port, ep) {
			remote = of_graph_get_remote_port_parent(ep);
			if (!remote || !of_device_is_available(remote)) {
				of_node_put(remote);
				continue;
			} else if (!of_device_is_available(remote->parent)) {
				dev_warn(dev, "parent device of %pOF is not available\n",
					 remote);
				of_node_put(remote);
				continue;
			}

			drm_of_component_match_add(dev, &match, compare_of,
						   remote);
			of_node_put(remote);
		}
		of_node_put(port);
	}

	return component_master_add_with_match(dev, m_ops, match);
}
EXPORT_SYMBOL(drm_of_component_probe);

/*
 * drm_of_encoder_active_endpoint - return the active encoder endpoint
 * @node: device tree node containing encoder input ports
 * @encoder: drm_encoder
 *
 * Given an encoder device node and a drm_encoder with a connected crtc,
 * parse the encoder endpoint connecting to the crtc port.
 */
int drm_of_encoder_active_endpoint(struct device_node *node,
				   struct drm_encoder *encoder,
				   struct of_endpoint *endpoint)
{
	struct device_node *ep;
	struct drm_crtc *crtc = encoder->crtc;
	struct device_node *port;
	int ret;

	if (!node || !crtc)
		return -EINVAL;

	for_each_endpoint_of_node(node, ep) {
		port = of_graph_get_remote_port(ep);
		of_node_put(port);
		if (port == crtc->port) {
			ret = of_graph_parse_endpoint(ep, endpoint);
			of_node_put(ep);
			return ret;
		}
	}

	return -EINVAL;
}
EXPORT_SYMBOL_GPL(drm_of_encoder_active_endpoint);

/**
 * drm_of_find_panel_or_bridge - return connected panel or bridge device
 * @np: device tree node containing encoder output ports
 * @port: port in the device tree node
 * @endpoint: endpoint in the device tree node
 * @panel: pointer to hold returned drm_panel
 * @bridge: pointer to hold returned drm_bridge
 *
 * Given a DT node's port and endpoint number, find the connected node and
 * return either the associated struct drm_panel or drm_bridge device. Either
 * @panel or @bridge must not be NULL.
 *
 * Returns zero if successful, or one of the standard error codes if it fails.
 */
int drm_of_find_panel_or_bridge(const struct device_node *np,
				int port, int endpoint,
				struct drm_panel **panel,
				struct drm_bridge **bridge)
{
	int ret = -EPROBE_DEFER;
	struct device_node *remote;

	if (!panel && !bridge)
		return -EINVAL;
	if (panel)
		*panel = NULL;

	/*
	 * of_graph_get_remote_node() produces a noisy error message if port
	 * node isn't found and the absence of the port is a legit case here,
	 * so at first we silently check whether graph presents in the
	 * device-tree node.
	 */
	if (!of_graph_is_present(np))
		return -ENODEV;

	remote = of_graph_get_remote_node(np, port, endpoint);
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

	of_node_put(remote);
	return ret;
}
EXPORT_SYMBOL_GPL(drm_of_find_panel_or_bridge);

enum drm_of_lvds_pixels {
	DRM_OF_LVDS_EVEN = BIT(0),
	DRM_OF_LVDS_ODD = BIT(1),
};

static int drm_of_lvds_get_port_pixels_type(struct device_node *port_node)
{
	bool even_pixels =
		of_property_read_bool(port_node, "dual-lvds-even-pixels");
	bool odd_pixels =
		of_property_read_bool(port_node, "dual-lvds-odd-pixels");

	return (even_pixels ? DRM_OF_LVDS_EVEN : 0) |
	       (odd_pixels ? DRM_OF_LVDS_ODD : 0);
}

static int drm_of_lvds_get_remote_pixels_type(
			const struct device_node *port_node)
{
	struct device_node *endpoint = NULL;
	int pixels_type = -EPIPE;

	for_each_child_of_node(port_node, endpoint) {
		struct device_node *remote_port;
		int current_pt;

		if (!of_node_name_eq(endpoint, "endpoint"))
			continue;

		remote_port = of_graph_get_remote_port(endpoint);
		if (!remote_port) {
			of_node_put(endpoint);
			return -EPIPE;
		}

		current_pt = drm_of_lvds_get_port_pixels_type(remote_port);
		of_node_put(remote_port);
		if (pixels_type < 0)
			pixels_type = current_pt;

		/*
		 * Sanity check, ensure that all remote endpoints have the same
		 * pixel type. We may lift this restriction later if we need to
		 * support multiple sinks with different dual-link
		 * configurations by passing the endpoints explicitly to
		 * drm_of_lvds_get_dual_link_pixel_order().
		 */
		if (!current_pt || pixels_type != current_pt) {
			of_node_put(endpoint);
			return -EINVAL;
		}
	}

	return pixels_type;
}

/**
 * drm_of_lvds_get_dual_link_pixel_order - Get LVDS dual-link pixel order
 * @port1: First DT port node of the Dual-link LVDS source
 * @port2: Second DT port node of the Dual-link LVDS source
 *
 * An LVDS dual-link connection is made of two links, with even pixels
 * transitting on one link, and odd pixels on the other link. This function
 * returns, for two ports of an LVDS dual-link source, which port shall transmit
 * the even and odd pixels, based on the requirements of the connected sink.
 *
 * The pixel order is determined from the dual-lvds-even-pixels and
 * dual-lvds-odd-pixels properties in the sink's DT port nodes. If those
 * properties are not present, or if their usage is not valid, this function
 * returns -EINVAL.
 *
 * If either port is not connected, this function returns -EPIPE.
 *
 * @port1 and @port2 are typically DT sibling nodes, but may have different
 * parents when, for instance, two separate LVDS encoders carry the even and odd
 * pixels.
 *
 * Return:
 * * DRM_LVDS_DUAL_LINK_EVEN_ODD_PIXELS - @port1 carries even pixels and @port2
 *   carries odd pixels
 * * DRM_LVDS_DUAL_LINK_ODD_EVEN_PIXELS - @port1 carries odd pixels and @port2
 *   carries even pixels
 * * -EINVAL - @port1 and @port2 are not connected to a dual-link LVDS sink, or
 *   the sink configuration is invalid
 * * -EPIPE - when @port1 or @port2 are not connected
 */
int drm_of_lvds_get_dual_link_pixel_order(const struct device_node *port1,
					  const struct device_node *port2)
{
	int remote_p1_pt, remote_p2_pt;

	if (!port1 || !port2)
		return -EINVAL;

	remote_p1_pt = drm_of_lvds_get_remote_pixels_type(port1);
	if (remote_p1_pt < 0)
		return remote_p1_pt;

	remote_p2_pt = drm_of_lvds_get_remote_pixels_type(port2);
	if (remote_p2_pt < 0)
		return remote_p2_pt;

	/*
	 * A valid dual-lVDS bus is found when one remote port is marked with
	 * "dual-lvds-even-pixels", and the other remote port is marked with
	 * "dual-lvds-odd-pixels", bail out if the markers are not right.
	 */
	if (remote_p1_pt + remote_p2_pt != DRM_OF_LVDS_EVEN + DRM_OF_LVDS_ODD)
		return -EINVAL;

	return remote_p1_pt == DRM_OF_LVDS_EVEN ?
		DRM_LVDS_DUAL_LINK_EVEN_ODD_PIXELS :
		DRM_LVDS_DUAL_LINK_ODD_EVEN_PIXELS;
}
EXPORT_SYMBOL_GPL(drm_of_lvds_get_dual_link_pixel_order);
