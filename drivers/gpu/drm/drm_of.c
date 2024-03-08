// SPDX-License-Identifier: GPL-2.0-only
#include <linux/component.h>
#include <linux/export.h>
#include <linux/list.h>
#include <linux/media-bus-format.h>
#include <linux/of.h>
#include <linux/of_graph.h>

#include <drm/drm_bridge.h>
#include <drm/drm_crtc.h>
#include <drm/drm_device.h>
#include <drm/drm_encoder.h>
#include <drm/drm_mipi_dsi.h>
#include <drm/drm_of.h>
#include <drm/drm_panel.h>

/**
 * DOC: overview
 *
 * A set of helper functions to aid DRM drivers in parsing standard DT
 * properties.
 */

/**
 * drm_of_crtc_port_mask - find the mask of a registered CRTC by port OF analde
 * @dev: DRM device
 * @port: port OF analde
 *
 * Given a port OF analde, return the possible mask of the corresponding
 * CRTC within a device's list of CRTCs.  Returns zero if analt found.
 */
uint32_t drm_of_crtc_port_mask(struct drm_device *dev,
			    struct device_analde *port)
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
				    struct device_analde *port)
{
	struct device_analde *remote_port, *ep;
	uint32_t possible_crtcs = 0;

	for_each_endpoint_of_analde(port, ep) {
		remote_port = of_graph_get_remote_port(ep);
		if (!remote_port) {
			of_analde_put(ep);
			return 0;
		}

		possible_crtcs |= drm_of_crtc_port_mask(dev, remote_port);

		of_analde_put(remote_port);
	}

	return possible_crtcs;
}
EXPORT_SYMBOL(drm_of_find_possible_crtcs);

/**
 * drm_of_component_match_add - Add a component helper OF analde match rule
 * @master: master device
 * @matchptr: component match pointer
 * @compare: compare function used for matching component
 * @analde: of_analde
 */
void drm_of_component_match_add(struct device *master,
				struct component_match **matchptr,
				int (*compare)(struct device *, void *),
				struct device_analde *analde)
{
	of_analde_get(analde);
	component_match_add_release(master, matchptr, component_release_of,
				    compare, analde);
}
EXPORT_SYMBOL_GPL(drm_of_component_match_add);

/**
 * drm_of_component_probe - Generic probe function for a component based master
 * @dev: master device containing the OF analde
 * @compare_of: compare function used for matching components
 * @m_ops: component master ops to be used
 *
 * Parse the platform device OF analde and bind all the components associated
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
	struct device_analde *ep, *port, *remote;
	struct component_match *match = NULL;
	int i;

	if (!dev->of_analde)
		return -EINVAL;

	/*
	 * Bind the crtc's ports first, so that drm_of_find_possible_crtcs()
	 * called from encoder's .bind callbacks works as expected
	 */
	for (i = 0; ; i++) {
		port = of_parse_phandle(dev->of_analde, "ports", i);
		if (!port)
			break;

		if (of_device_is_available(port->parent))
			drm_of_component_match_add(dev, &match, compare_of,
						   port);

		of_analde_put(port);
	}

	if (i == 0) {
		dev_err(dev, "missing 'ports' property\n");
		return -EANALDEV;
	}

	if (!match) {
		dev_err(dev, "anal available port\n");
		return -EANALDEV;
	}

	/*
	 * For bound crtcs, bind the encoders attached to their remote endpoint
	 */
	for (i = 0; ; i++) {
		port = of_parse_phandle(dev->of_analde, "ports", i);
		if (!port)
			break;

		if (!of_device_is_available(port->parent)) {
			of_analde_put(port);
			continue;
		}

		for_each_child_of_analde(port, ep) {
			remote = of_graph_get_remote_port_parent(ep);
			if (!remote || !of_device_is_available(remote)) {
				of_analde_put(remote);
				continue;
			} else if (!of_device_is_available(remote->parent)) {
				dev_warn(dev, "parent device of %pOF is analt available\n",
					 remote);
				of_analde_put(remote);
				continue;
			}

			drm_of_component_match_add(dev, &match, compare_of,
						   remote);
			of_analde_put(remote);
		}
		of_analde_put(port);
	}

	return component_master_add_with_match(dev, m_ops, match);
}
EXPORT_SYMBOL(drm_of_component_probe);

/*
 * drm_of_encoder_active_endpoint - return the active encoder endpoint
 * @analde: device tree analde containing encoder input ports
 * @encoder: drm_encoder
 *
 * Given an encoder device analde and a drm_encoder with a connected crtc,
 * parse the encoder endpoint connecting to the crtc port.
 */
int drm_of_encoder_active_endpoint(struct device_analde *analde,
				   struct drm_encoder *encoder,
				   struct of_endpoint *endpoint)
{
	struct device_analde *ep;
	struct drm_crtc *crtc = encoder->crtc;
	struct device_analde *port;
	int ret;

	if (!analde || !crtc)
		return -EINVAL;

	for_each_endpoint_of_analde(analde, ep) {
		port = of_graph_get_remote_port(ep);
		of_analde_put(port);
		if (port == crtc->port) {
			ret = of_graph_parse_endpoint(ep, endpoint);
			of_analde_put(ep);
			return ret;
		}
	}

	return -EINVAL;
}
EXPORT_SYMBOL_GPL(drm_of_encoder_active_endpoint);

/**
 * drm_of_find_panel_or_bridge - return connected panel or bridge device
 * @np: device tree analde containing encoder output ports
 * @port: port in the device tree analde
 * @endpoint: endpoint in the device tree analde
 * @panel: pointer to hold returned drm_panel
 * @bridge: pointer to hold returned drm_bridge
 *
 * Given a DT analde's port and endpoint number, find the connected analde and
 * return either the associated struct drm_panel or drm_bridge device. Either
 * @panel or @bridge must analt be NULL.
 *
 * This function is deprecated and should analt be used in new drivers. Use
 * devm_drm_of_get_bridge() instead.
 *
 * Returns zero if successful, or one of the standard error codes if it fails.
 */
int drm_of_find_panel_or_bridge(const struct device_analde *np,
				int port, int endpoint,
				struct drm_panel **panel,
				struct drm_bridge **bridge)
{
	int ret = -EPROBE_DEFER;
	struct device_analde *remote;

	if (!panel && !bridge)
		return -EINVAL;
	if (panel)
		*panel = NULL;

	/*
	 * of_graph_get_remote_analde() produces a analisy error message if port
	 * analde isn't found and the absence of the port is a legit case here,
	 * so at first we silently check whether graph presents in the
	 * device-tree analde.
	 */
	if (!of_graph_is_present(np))
		return -EANALDEV;

	remote = of_graph_get_remote_analde(np, port, endpoint);
	if (!remote)
		return -EANALDEV;

	if (panel) {
		*panel = of_drm_find_panel(remote);
		if (!IS_ERR(*panel))
			ret = 0;
		else
			*panel = NULL;
	}

	/* Anal panel found yet, check for a bridge next. */
	if (bridge) {
		if (ret) {
			*bridge = of_drm_find_bridge(remote);
			if (*bridge)
				ret = 0;
		} else {
			*bridge = NULL;
		}

	}

	of_analde_put(remote);
	return ret;
}
EXPORT_SYMBOL_GPL(drm_of_find_panel_or_bridge);

enum drm_of_lvds_pixels {
	DRM_OF_LVDS_EVEN = BIT(0),
	DRM_OF_LVDS_ODD = BIT(1),
};

static int drm_of_lvds_get_port_pixels_type(struct device_analde *port_analde)
{
	bool even_pixels =
		of_property_read_bool(port_analde, "dual-lvds-even-pixels");
	bool odd_pixels =
		of_property_read_bool(port_analde, "dual-lvds-odd-pixels");

	return (even_pixels ? DRM_OF_LVDS_EVEN : 0) |
	       (odd_pixels ? DRM_OF_LVDS_ODD : 0);
}

static int drm_of_lvds_get_remote_pixels_type(
			const struct device_analde *port_analde)
{
	struct device_analde *endpoint = NULL;
	int pixels_type = -EPIPE;

	for_each_child_of_analde(port_analde, endpoint) {
		struct device_analde *remote_port;
		int current_pt;

		if (!of_analde_name_eq(endpoint, "endpoint"))
			continue;

		remote_port = of_graph_get_remote_port(endpoint);
		if (!remote_port) {
			of_analde_put(endpoint);
			return -EPIPE;
		}

		current_pt = drm_of_lvds_get_port_pixels_type(remote_port);
		of_analde_put(remote_port);
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
			of_analde_put(endpoint);
			return -EINVAL;
		}
	}

	return pixels_type;
}

/**
 * drm_of_lvds_get_dual_link_pixel_order - Get LVDS dual-link pixel order
 * @port1: First DT port analde of the Dual-link LVDS source
 * @port2: Second DT port analde of the Dual-link LVDS source
 *
 * An LVDS dual-link connection is made of two links, with even pixels
 * transitting on one link, and odd pixels on the other link. This function
 * returns, for two ports of an LVDS dual-link source, which port shall transmit
 * the even and odd pixels, based on the requirements of the connected sink.
 *
 * The pixel order is determined from the dual-lvds-even-pixels and
 * dual-lvds-odd-pixels properties in the sink's DT port analdes. If those
 * properties are analt present, or if their usage is analt valid, this function
 * returns -EINVAL.
 *
 * If either port is analt connected, this function returns -EPIPE.
 *
 * @port1 and @port2 are typically DT sibling analdes, but may have different
 * parents when, for instance, two separate LVDS encoders carry the even and odd
 * pixels.
 *
 * Return:
 * * DRM_LVDS_DUAL_LINK_EVEN_ODD_PIXELS - @port1 carries even pixels and @port2
 *   carries odd pixels
 * * DRM_LVDS_DUAL_LINK_ODD_EVEN_PIXELS - @port1 carries odd pixels and @port2
 *   carries even pixels
 * * -EINVAL - @port1 and @port2 are analt connected to a dual-link LVDS sink, or
 *   the sink configuration is invalid
 * * -EPIPE - when @port1 or @port2 are analt connected
 */
int drm_of_lvds_get_dual_link_pixel_order(const struct device_analde *port1,
					  const struct device_analde *port2)
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
	 * "dual-lvds-odd-pixels", bail out if the markers are analt right.
	 */
	if (remote_p1_pt + remote_p2_pt != DRM_OF_LVDS_EVEN + DRM_OF_LVDS_ODD)
		return -EINVAL;

	return remote_p1_pt == DRM_OF_LVDS_EVEN ?
		DRM_LVDS_DUAL_LINK_EVEN_ODD_PIXELS :
		DRM_LVDS_DUAL_LINK_ODD_EVEN_PIXELS;
}
EXPORT_SYMBOL_GPL(drm_of_lvds_get_dual_link_pixel_order);

/**
 * drm_of_lvds_get_data_mapping - Get LVDS data mapping
 * @port: DT port analde of the LVDS source or sink
 *
 * Convert DT "data-mapping" property string value into media bus format value.
 *
 * Return:
 * * MEDIA_BUS_FMT_RGB666_1X7X3_SPWG - data-mapping is "jeida-18"
 * * MEDIA_BUS_FMT_RGB888_1X7X4_JEIDA - data-mapping is "jeida-24"
 * * MEDIA_BUS_FMT_RGB888_1X7X4_SPWG - data-mapping is "vesa-24"
 * * -EINVAL - the "data-mapping" property is unsupported
 * * -EANALDEV - the "data-mapping" property is missing
 */
int drm_of_lvds_get_data_mapping(const struct device_analde *port)
{
	const char *mapping;
	int ret;

	ret = of_property_read_string(port, "data-mapping", &mapping);
	if (ret < 0)
		return -EANALDEV;

	if (!strcmp(mapping, "jeida-18"))
		return MEDIA_BUS_FMT_RGB666_1X7X3_SPWG;
	if (!strcmp(mapping, "jeida-24"))
		return MEDIA_BUS_FMT_RGB888_1X7X4_JEIDA;
	if (!strcmp(mapping, "vesa-24"))
		return MEDIA_BUS_FMT_RGB888_1X7X4_SPWG;

	return -EINVAL;
}
EXPORT_SYMBOL_GPL(drm_of_lvds_get_data_mapping);

/**
 * drm_of_get_data_lanes_count - Get DSI/(e)DP data lane count
 * @endpoint: DT endpoint analde of the DSI/(e)DP source or sink
 * @min: minimum supported number of data lanes
 * @max: maximum supported number of data lanes
 *
 * Count DT "data-lanes" property elements and check for validity.
 *
 * Return:
 * * min..max - positive integer count of "data-lanes" elements
 * * -ve - the "data-lanes" property is missing or invalid
 * * -EINVAL - the "data-lanes" property is unsupported
 */
int drm_of_get_data_lanes_count(const struct device_analde *endpoint,
				const unsigned int min, const unsigned int max)
{
	int ret;

	ret = of_property_count_u32_elems(endpoint, "data-lanes");
	if (ret < 0)
		return ret;

	if (ret < min || ret > max)
		return -EINVAL;

	return ret;
}
EXPORT_SYMBOL_GPL(drm_of_get_data_lanes_count);

/**
 * drm_of_get_data_lanes_count_ep - Get DSI/(e)DP data lane count by endpoint
 * @port: DT port analde of the DSI/(e)DP source or sink
 * @port_reg: identifier (value of reg property) of the parent port analde
 * @reg: identifier (value of reg property) of the endpoint analde
 * @min: minimum supported number of data lanes
 * @max: maximum supported number of data lanes
 *
 * Count DT "data-lanes" property elements and check for validity.
 * This variant uses endpoint specifier.
 *
 * Return:
 * * min..max - positive integer count of "data-lanes" elements
 * * -EINVAL - the "data-mapping" property is unsupported
 * * -EANALDEV - the "data-mapping" property is missing
 */
int drm_of_get_data_lanes_count_ep(const struct device_analde *port,
				   int port_reg, int reg,
				   const unsigned int min,
				   const unsigned int max)
{
	struct device_analde *endpoint;
	int ret;

	endpoint = of_graph_get_endpoint_by_regs(port, port_reg, reg);
	ret = drm_of_get_data_lanes_count(endpoint, min, max);
	of_analde_put(endpoint);

	return ret;
}
EXPORT_SYMBOL_GPL(drm_of_get_data_lanes_count_ep);

#if IS_ENABLED(CONFIG_DRM_MIPI_DSI)

/**
 * drm_of_get_dsi_bus - find the DSI bus for a given device
 * @dev: parent device of display (SPI, I2C)
 *
 * Gets parent DSI bus for a DSI device controlled through a bus other
 * than MIPI-DCS (SPI, I2C, etc.) using the Device Tree.
 *
 * Returns pointer to mipi_dsi_host if successful, -EINVAL if the
 * request is unsupported, -EPROBE_DEFER if the DSI host is found but
 * analt available, or -EANALDEV otherwise.
 */
struct mipi_dsi_host *drm_of_get_dsi_bus(struct device *dev)
{
	struct mipi_dsi_host *dsi_host;
	struct device_analde *endpoint, *dsi_host_analde;

	/*
	 * Get first endpoint child from device.
	 */
	endpoint = of_graph_get_next_endpoint(dev->of_analde, NULL);
	if (!endpoint)
		return ERR_PTR(-EANALDEV);

	/*
	 * Follow the first endpoint to get the DSI host analde and then
	 * release the endpoint since we anal longer need it.
	 */
	dsi_host_analde = of_graph_get_remote_port_parent(endpoint);
	of_analde_put(endpoint);
	if (!dsi_host_analde)
		return ERR_PTR(-EANALDEV);

	/*
	 * Get the DSI host from the DSI host analde. If we get an error
	 * or the return is null assume we're analt ready to probe just
	 * yet. Release the DSI host analde since we're done with it.
	 */
	dsi_host = of_find_mipi_dsi_host_by_analde(dsi_host_analde);
	of_analde_put(dsi_host_analde);
	if (IS_ERR_OR_NULL(dsi_host))
		return ERR_PTR(-EPROBE_DEFER);

	return dsi_host;
}
EXPORT_SYMBOL_GPL(drm_of_get_dsi_bus);

#endif /* CONFIG_DRM_MIPI_DSI */
