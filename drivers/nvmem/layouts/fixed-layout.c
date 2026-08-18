// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright 2026 Bootlin
 *
 * Authors: Mathieu Dubois-Briand <mathieu.dubois-briand@bootlin.com>
 */

#include <linux/nvmem-provider.h>
#include <linux/of.h>

#include "../internals.h"

static int fixed_layout_add_cells(struct nvmem_layout *layout)
{
	struct device_node *np;
	int ret;

	np = of_nvmem_layout_get_container(layout->nvmem);
	if (!np)
		return -ENOENT;

	ret = nvmem_add_cells_from_dt(layout->nvmem, np);
	of_node_put(np);

	return ret;
}

static int fixed_layout_probe(struct nvmem_layout *layout)
{
	layout->add_cells = fixed_layout_add_cells;

	return nvmem_layout_register(layout);
}

static void fixed_layout_remove(struct nvmem_layout *layout)
{
	nvmem_layout_unregister(layout);
}

static const struct of_device_id fixed_layout_of_match_table[] = {
	{ .compatible = "fixed-layout", },
	{},
};

static struct nvmem_layout_driver fixed_layout_layout = {
	.driver = {
		.name = "fixed-layout",
		.of_match_table = fixed_layout_of_match_table,
	},
	.probe = fixed_layout_probe,
	.remove = fixed_layout_remove,
};
module_nvmem_layout_driver(fixed_layout_layout);

MODULE_AUTHOR("Mathieu Dubois-Briand");
MODULE_LICENSE("GPL");
MODULE_DEVICE_TABLE(of, fixed_layout_of_match_table);
MODULE_DESCRIPTION("NVMEM fixed-layout driver");
