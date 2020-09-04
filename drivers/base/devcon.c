// SPDX-License-Identifier: GPL-2.0
/**
 * Device connections
 *
 * Copyright (C) 2018 Intel Corporation
 * Author: Heikki Krogerus <heikki.krogerus@linux.intel.com>
 */

#include <linux/device.h>
#include <linux/property.h>

static void *
fwnode_graph_devcon_match(struct fwnode_handle *fwnode, const char *con_id,
			  void *data, devcon_match_fn_t match)
{
	struct fwnode_handle *node;
	struct fwnode_handle *ep;
	void *ret;

	fwnode_graph_for_each_endpoint(fwnode, ep) {
		node = fwnode_graph_get_remote_port_parent(ep);
		if (!fwnode_device_is_available(node))
			continue;

		ret = match(node, con_id, data);
		fwnode_handle_put(node);
		if (ret) {
			fwnode_handle_put(ep);
			return ret;
		}
	}
	return NULL;
}

static void *
fwnode_devcon_match(struct fwnode_handle *fwnode, const char *con_id,
		    void *data, devcon_match_fn_t match)
{
	struct fwnode_handle *node;
	void *ret;
	int i;

	for (i = 0; ; i++) {
		node = fwnode_find_reference(fwnode, con_id, i);
		if (IS_ERR(node))
			break;

		ret = match(node, NULL, data);
		fwnode_handle_put(node);
		if (ret)
			return ret;
	}

	return NULL;
}

/**
 * fwnode_connection_find_match - Find connection from a device node
 * @fwnode: Device node with the connection
 * @con_id: Identifier for the connection
 * @data: Data for the match function
 * @match: Function to check and convert the connection description
 *
 * Find a connection with unique identifier @con_id between @fwnode and another
 * device node. @match will be used to convert the connection description to
 * data the caller is expecting to be returned.
 */
void *fwnode_connection_find_match(struct fwnode_handle *fwnode,
				   const char *con_id, void *data,
				   devcon_match_fn_t match)
{
	void *ret;

	if (!fwnode || !match)
		return NULL;

	ret = fwnode_graph_devcon_match(fwnode, con_id, data, match);
	if (ret)
		return ret;

	return fwnode_devcon_match(fwnode, con_id, data, match);
}
EXPORT_SYMBOL_GPL(fwnode_connection_find_match);

/**
 * device_connection_find_match - Find physical connection to a device
 * @dev: Device with the connection
 * @con_id: Identifier for the connection
 * @data: Data for the match function
 * @match: Function to check and convert the connection description
 *
 * Find a connection with unique identifier @con_id between @dev and another
 * device. @match will be used to convert the connection description to data the
 * caller is expecting to be returned.
 */
void *device_connection_find_match(struct device *dev, const char *con_id,
				   void *data, devcon_match_fn_t match)
{
	return fwnode_connection_find_match(dev_fwnode(dev), con_id, data, match);
}
EXPORT_SYMBOL_GPL(device_connection_find_match);
