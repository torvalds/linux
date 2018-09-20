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

static void of_coresight_get_ports(const struct device_node *node,
				   int *nr_inport, int *nr_outport)
{
	struct device_node *ep = NULL;
	int in = 0, out = 0;

	do {
		ep = of_graph_get_next_endpoint(node, ep);
		if (!ep)
			break;

		if (of_property_read_bool(ep, "slave-mode"))
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
	/* List of output port on this component */
	pdata->outports = devm_kcalloc(dev,
				       pdata->nr_outport,
				       sizeof(*pdata->outports),
				       GFP_KERNEL);
	if (!pdata->outports)
		return -ENOMEM;

	/* Children connected to this component via @outports */
	pdata->child_names = devm_kcalloc(dev,
					  pdata->nr_outport,
					  sizeof(*pdata->child_names),
					  GFP_KERNEL);
	if (!pdata->child_names)
		return -ENOMEM;

	/* Port number on the child this component is connected to */
	pdata->child_ports = devm_kcalloc(dev,
					  pdata->nr_outport,
					  sizeof(*pdata->child_ports),
					  GFP_KERNEL);
	if (!pdata->child_ports)
		return -ENOMEM;

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
 * and fill the connection information in @pdata[@i].
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
				       struct coresight_platform_data *pdata,
				       int i)
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

		pdata->outports[i] = endpoint.port;
		pdata->child_names[i] = devm_kstrdup(dev,
						     dev_name(rdev),
						     GFP_KERNEL);
		pdata->child_ports[i] = rendpoint.port;
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
	int i = 0, ret = 0;
	struct coresight_platform_data *pdata;
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

	/* Iterate through each port to discover topology */
	while ((ep = of_graph_get_next_endpoint(node, ep))) {
		/*
		 * No need to deal with input ports, as processing the
		 * output ports connected to them will process the details.
		 */
		if (of_find_property(ep, "slave-mode", NULL))
			continue;

		ret = of_coresight_parse_endpoint(dev, ep, pdata, i);
		switch (ret) {
		case 1:
			i++;		/* Fall through */
		case 0:
			break;
		default:
			return ERR_PTR(ret);
		}
	}

	return pdata;
}
EXPORT_SYMBOL_GPL(of_get_coresight_platform_data);
