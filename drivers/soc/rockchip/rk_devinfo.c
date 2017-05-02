/*
 * Copyright (C) 2015 ROCKCHIP, Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */
#include <linux/module.h>
#include <linux/memblock.h>
#include <linux/of.h>
#include <linux/of_address.h>

#define ETH_MAC_OFFSET	36

static u8 eth_mac[6];

void rk_devinfo_get_eth_mac(u8 *mac)
{
	memcpy(mac, eth_mac, 6);
	pr_info("%s: %02x.%02x.%02x.%02x.%02x.%02x\n", __func__,
		eth_mac[0], eth_mac[1], eth_mac[2],
		eth_mac[3], eth_mac[4], eth_mac[5]);
}
EXPORT_SYMBOL_GPL(rk_devinfo_get_eth_mac);

static int __init rk_devinfo_init(void)
{
	int ret;
	u8 *vaddr;
	struct device_node *node;
	struct resource res;
	phys_addr_t start, size;
	void *begin, *end;

	node = of_find_node_by_name(NULL, "stb-devinfo");
	if (!node) {
		pr_err("%s.%d: fail to get node\n", __func__, __LINE__);
		goto out;
	}

	ret = of_address_to_resource(node, 0, &res);
	if (ret) {
		pr_err("%s.%d: fail to get resource\n", __func__, __LINE__);
		of_node_put(node);
		goto out;
	}

	of_node_put(node);

	start = res.start;
	size = resource_size(&res);
	if (!size) {
		pr_err("%s.%d: size = 0\n", __func__, __LINE__);
		goto out;
	}
	vaddr = phys_to_virt(start);
	/* copy eth mac addr */
	memcpy(eth_mac, vaddr + ETH_MAC_OFFSET, 6);

	begin = phys_to_virt(start);
	end = phys_to_virt(start + size);
	memblock_free(start, size);
	free_reserved_area(begin, end, -1, "stb_devinfo");

out:

	return 0;
}

module_init(rk_devinfo_init);
