#include <linux/export.h>
#include <linux/list.h>
#include <linux/of_graph.h>
#include <drm/drmP.h>
#include <drm/drm_crtc.h>
#include <drm/drm_of.h>

/**
 * drm_crtc_port_mask - find the mask of a registered CRTC by port OF node
 * @dev: DRM device
 * @port: port OF node
 *
 * Given a port OF node, return the possible mask of the corresponding
 * CRTC within a device's list of CRTCs.  Returns zero if not found.
 */
static uint32_t drm_crtc_port_mask(struct drm_device *dev,
				   struct device_node *port)
{
	unsigned int index = 0;
	struct drm_crtc *tmp;

	list_for_each_entry(tmp, &dev->mode_config.crtc_list, head) {
		if (tmp->port == port)
			return 1 << index;

		index++;
	}

	return 0;
}

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
	struct device_node *remote_port, *ep = NULL;
	uint32_t possible_crtcs = 0;

	do {
		ep = of_graph_get_next_endpoint(port, ep);
		if (!ep)
			break;

		remote_port = of_graph_get_remote_port(ep);
		if (!remote_port) {
			of_node_put(ep);
			return 0;
		}

		possible_crtcs |= drm_crtc_port_mask(dev, remote_port);

		of_node_put(remote_port);
	} while (1);

	return possible_crtcs;
}
EXPORT_SYMBOL(drm_of_find_possible_crtcs);
