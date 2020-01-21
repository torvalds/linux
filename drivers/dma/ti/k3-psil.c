// SPDX-License-Identifier: GPL-2.0
/*
 *  Copyright (C) 2019 Texas Instruments Incorporated - http://www.ti.com
 *  Author: Peter Ujfalusi <peter.ujfalusi@ti.com>
 */

#include <linux/kernel.h>
#include <linux/device.h>
#include <linux/init.h>
#include <linux/mutex.h>
#include <linux/of.h>

#include "k3-psil-priv.h"

static DEFINE_MUTEX(ep_map_mutex);
static struct psil_ep_map *soc_ep_map;

struct psil_endpoint_config *psil_get_ep_config(u32 thread_id)
{
	int i;

	mutex_lock(&ep_map_mutex);
	if (!soc_ep_map) {
		if (of_machine_is_compatible("ti,am654")) {
			soc_ep_map = &am654_ep_map;
		} else if (of_machine_is_compatible("ti,j721e")) {
			soc_ep_map = &j721e_ep_map;
		} else {
			pr_err("PSIL: No compatible machine found for map\n");
			return ERR_PTR(-ENOTSUPP);
		}
		pr_debug("%s: Using map for %s\n", __func__, soc_ep_map->name);
	}
	mutex_unlock(&ep_map_mutex);

	if (thread_id & K3_PSIL_DST_THREAD_ID_OFFSET && soc_ep_map->dst) {
		/* check in destination thread map */
		for (i = 0; i < soc_ep_map->dst_count; i++) {
			if (soc_ep_map->dst[i].thread_id == thread_id)
				return &soc_ep_map->dst[i].ep_config;
		}
	}

	thread_id &= ~K3_PSIL_DST_THREAD_ID_OFFSET;
	if (soc_ep_map->src) {
		for (i = 0; i < soc_ep_map->src_count; i++) {
			if (soc_ep_map->src[i].thread_id == thread_id)
				return &soc_ep_map->src[i].ep_config;
		}
	}

	return ERR_PTR(-ENOENT);
}
EXPORT_SYMBOL_GPL(psil_get_ep_config);

int psil_set_new_ep_config(struct device *dev, const char *name,
			   struct psil_endpoint_config *ep_config)
{
	struct psil_endpoint_config *dst_ep_config;
	struct of_phandle_args dma_spec;
	u32 thread_id;
	int index;

	if (!dev || !dev->of_node)
		return -EINVAL;

	index = of_property_match_string(dev->of_node, "dma-names", name);
	if (index < 0)
		return index;

	if (of_parse_phandle_with_args(dev->of_node, "dmas", "#dma-cells",
				       index, &dma_spec))
		return -ENOENT;

	thread_id = dma_spec.args[0];

	dst_ep_config = psil_get_ep_config(thread_id);
	if (IS_ERR(dst_ep_config)) {
		pr_err("PSIL: thread ID 0x%04x not defined in map\n",
		       thread_id);
		of_node_put(dma_spec.np);
		return PTR_ERR(dst_ep_config);
	}

	memcpy(dst_ep_config, ep_config, sizeof(*dst_ep_config));

	of_node_put(dma_spec.np);
	return 0;
}
EXPORT_SYMBOL_GPL(psil_set_new_ep_config);
