// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2012, The Linux Foundation. All rights reserved.
 */

#include <linux/types.h>
#include <linux/err.h>
#include <linux/slab.h>
#include <linux/clk.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/of_graph.h>
#include <linux/of_platform.h>
#include <linux/platform_device.h>
#include <linux/amba/bus.h>
#include <linux/coresight.h>
#include <linux/cpumask.h>
#include <asm/smp_plat.h>


static int of_dev_node_match(struct device *dev, void *data)
{
	return dev->of_node == data;
}

static struct device *
of_coresight_get_endpoint_device(struct device_node *endpoint)
{
	struct device *dev = NULL;

	/*
	 * If we have a non-configurable replicator, it will be found on the
	 * platform bus.
	 */
	dev = bus_find_device(&platform_bus_type, NULL,
			      endpoint, of_dev_node_match);
	if (dev)
		return dev;

	/*
	 * We have a configurable component - circle through the AMBA bus
	 * looking for the device that matches the endpoint node.
	 */
	return bus_find_device(&amba_bustype, NULL,
			       endpoint, of_dev_node_match);
}

static inline bool of_coresight_ep_is_input(struct device_node *ep)
{
	return of_property_read_bool(ep, "slave-mode");
}

static void of_coresight_get_ports(const struct device_node *node,
				   int *nr_inport, int *nr_outport)
{
	struct device_node *ep = NULL;
	int in = 0, out = 0;

	do {
		ep = of_graph_get_next_endpoint(node, ep);
		if (!ep)
			break;

		if (of_coresight_ep_is_input(ep))
			in++;
		else
			out++;

	} while (ep);

	*nr_inport = in;
	*nr_outport = out;
}

static int of_coresight_alloc_memory(struct device *dev,
			struct coresight_platform_data *pdata)
{
	if (pdata->nr_outport) {
		pdata->conns = devm_kzalloc(dev, pdata->nr_outport *
					    sizeof(*pdata->conns),
					    GFP_KERNEL);
		if (!pdata->conns)
			return -ENOMEM;
	}

	return 0;
}

int of_coresight_get_cpu(const struct device_node *node)
{
	int cpu;
	struct device_node *dn;

	dn = of_parse_phandle(node, "cpu", 0);
	/* Affinity defaults to CPU0 */
	if (!dn)
		return 0;
	cpu = of_cpu_node_to_id(dn);
	of_node_put(dn);

	/* Affinity to CPU0 if no cpu nodes are found */
	return (cpu < 0) ? 0 : cpu;
}
EXPORT_SYMBOL_GPL(of_coresight_get_cpu);

/*
 * of_coresight_parse_endpoint : Parse the given output endpoint @ep
 * and fill the connection information in @conn
 *
 * Parses the local port, remote device name and the remote port.
 *
 * Returns :
 *	 1	- If the parsing is successful and a connection record
 *		  was created for an output connection.
 *	 0	- If the parsing completed without any fatal errors.
 *	-Errno	- Fatal error, abort the scanning.
 */
static int of_coresight_parse_endpoint(struct device *dev,
				       struct device_node *ep,
				       struct coresight_connection *conn)
{
	int ret = 0;
	struct of_endpoint endpoint, rendpoint;
	struct device_node *rparent = NULL;
	struct device_node *rep = NULL;
	struct device *rdev = NULL;

	do {
		/* Parse the local port details */
		if (of_graph_parse_endpoint(ep, &endpoint))
			break;
		/*
		 * Get a handle on the remote endpoint and the device it is
		 * attached to.
		 */
		rep = of_graph_get_remote_endpoint(ep);
		if (!rep)
			break;
		rparent = of_graph_get_port_parent(rep);
		if (!rparent)
			break;
		if (of_graph_parse_endpoint(rep, &rendpoint))
			break;

		/* If the remote device is not available, defer probing */
		rdev = of_coresight_get_endpoint_device(rparent);
		if (!rdev) {
			ret = -EPROBE_DEFER;
			break;
		}

		conn->outport = endpoint.port;
		conn->child_name = devm_kstrdup(dev,
						dev_name(rdev),
						GFP_KERNEL);
		conn->child_port = rendpoint.port;
		/* Connection record updated */
		ret = 1;
	} while (0);

	if (rparent)
		of_node_put(rparent);
	if (rep)
		of_node_put(rep);
	if (rdev)
		put_device(rdev);

	return ret;
}

struct coresight_platform_data *
of_get_coresight_platform_data(struct device *dev,
			       const struct device_node *node)
{
	int ret = 0;
	struct coresight_platform_data *pdata;
	struct coresight_connection *conn;
	struct device_node *ep = NULL;

	pdata = devm_kzalloc(dev, sizeof(*pdata), GFP_KERNEL);
	if (!pdata)
		return ERR_PTR(-ENOMEM);

	/* Use device name as sysfs handle */
	pdata->name = dev_name(dev);
	pdata->cpu = of_coresight_get_cpu(node);

	/* Get the number of input and output port for this component */
	of_coresight_get_ports(node, &pdata->nr_inport, &pdata->nr_outport);

	/* If there are no output connections, we are done */
	if (!pdata->nr_outport)
		return pdata;

	ret = of_coresight_alloc_memory(dev, pdata);
	if (ret)
		return ERR_PTR(ret);

	conn = pdata->conns;
	/* Iterate through each port to discover topology */
	while ((ep = of_graph_get_next_endpoint(node, ep))) {
		/*
		 * No need to deal with input ports, as processing the
		 * output ports connected to them will process the details.
		 */
		if (of_coresight_ep_is_input(ep))
			continue;

		ret = of_coresight_parse_endpoint(dev, ep, conn);
		switch (ret) {
		case 1:
			conn++;		/* Fall through */
		case 0:
			break;
		default:
			return ERR_PTR(ret);
		}
	}

	return pdata;
}
EXPORT_SYMBOL_GPL(of_get_coresight_platform_data);
