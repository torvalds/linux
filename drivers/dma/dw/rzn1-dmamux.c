// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2022 Schneider-Electric
 * Author: Miquel Raynal <miquel.raynal@bootlin.com
 * Based on TI crossbar driver written by Peter Ujfalusi <peter.ujfalusi@ti.com>
 */
#include <linux/bitops.h>
#include <linux/of_device.h>
#include <linux/of_dma.h>
#include <linux/slab.h>
#include <linux/soc/renesas/r9a06g032-sysctrl.h>
#include <linux/types.h>

#define RNZ1_DMAMUX_NCELLS 6
#define RZN1_DMAMUX_MAX_LINES 64
#define RZN1_DMAMUX_LINES_PER_CTLR 16

struct rzn1_dmamux_data {
	struct dma_router dmarouter;
	DECLARE_BITMAP(used_chans, 2 * RZN1_DMAMUX_LINES_PER_CTLR);
};

struct rzn1_dmamux_map {
	unsigned int req_idx;
};

static void rzn1_dmamux_free(struct device *dev, void *route_data)
{
	struct rzn1_dmamux_data *dmamux = dev_get_drvdata(dev);
	struct rzn1_dmamux_map *map = route_data;

	dev_dbg(dev, "Unmapping DMAMUX request %u\n", map->req_idx);

	clear_bit(map->req_idx, dmamux->used_chans);

	kfree(map);
}

static void *rzn1_dmamux_route_allocate(struct of_phandle_args *dma_spec,
					struct of_dma *ofdma)
{
	struct platform_device *pdev = of_find_device_by_node(ofdma->of_node);
	struct rzn1_dmamux_data *dmamux = platform_get_drvdata(pdev);
	struct rzn1_dmamux_map *map;
	unsigned int dmac_idx, chan, val;
	u32 mask;
	int ret;

	if (dma_spec->args_count != RNZ1_DMAMUX_NCELLS)
		return ERR_PTR(-EINVAL);

	map = kzalloc(sizeof(*map), GFP_KERNEL);
	if (!map)
		return ERR_PTR(-ENOMEM);

	chan = dma_spec->args[0];
	map->req_idx = dma_spec->args[4];
	val = dma_spec->args[5];
	dma_spec->args_count -= 2;

	if (chan >= RZN1_DMAMUX_LINES_PER_CTLR) {
		dev_err(&pdev->dev, "Invalid DMA request line: %u\n", chan);
		ret = -EINVAL;
		goto free_map;
	}

	if (map->req_idx >= RZN1_DMAMUX_MAX_LINES ||
	    (map->req_idx % RZN1_DMAMUX_LINES_PER_CTLR) != chan) {
		dev_err(&pdev->dev, "Invalid MUX request line: %u\n", map->req_idx);
		ret = -EINVAL;
		goto free_map;
	}

	dmac_idx = map->req_idx >= RZN1_DMAMUX_LINES_PER_CTLR ? 1 : 0;
	dma_spec->np = of_parse_phandle(ofdma->of_node, "dma-masters", dmac_idx);
	if (!dma_spec->np) {
		dev_err(&pdev->dev, "Can't get DMA master\n");
		ret = -EINVAL;
		goto free_map;
	}

	dev_dbg(&pdev->dev, "Mapping DMAMUX request %u to DMAC%u request %u\n",
		map->req_idx, dmac_idx, chan);

	if (test_and_set_bit(map->req_idx, dmamux->used_chans)) {
		ret = -EBUSY;
		goto free_map;
	}

	mask = BIT(map->req_idx);
	ret = r9a06g032_sysctrl_set_dmamux(mask, val ? mask : 0);
	if (ret)
		goto clear_bitmap;

	return map;

clear_bitmap:
	clear_bit(map->req_idx, dmamux->used_chans);
free_map:
	kfree(map);

	return ERR_PTR(ret);
}

static const struct of_device_id rzn1_dmac_match[] = {
	{ .compatible = "renesas,rzn1-dma" },
	{}
};

static int rzn1_dmamux_probe(struct platform_device *pdev)
{
	struct device_node *mux_node = pdev->dev.of_node;
	const struct of_device_id *match;
	struct device_node *dmac_node;
	struct rzn1_dmamux_data *dmamux;

	dmamux = devm_kzalloc(&pdev->dev, sizeof(*dmamux), GFP_KERNEL);
	if (!dmamux)
		return -ENOMEM;

	dmac_node = of_parse_phandle(mux_node, "dma-masters", 0);
	if (!dmac_node)
		return dev_err_probe(&pdev->dev, -ENODEV, "Can't get DMA master node\n");

	match = of_match_node(rzn1_dmac_match, dmac_node);
	of_node_put(dmac_node);
	if (!match)
		return dev_err_probe(&pdev->dev, -EINVAL, "DMA master is not supported\n");

	dmamux->dmarouter.dev = &pdev->dev;
	dmamux->dmarouter.route_free = rzn1_dmamux_free;

	platform_set_drvdata(pdev, dmamux);

	return of_dma_router_register(mux_node, rzn1_dmamux_route_allocate,
				      &dmamux->dmarouter);
}

static const struct of_device_id rzn1_dmamux_match[] = {
	{ .compatible = "renesas,rzn1-dmamux" },
	{}
};

static struct platform_driver rzn1_dmamux_driver = {
	.driver = {
		.name = "renesas,rzn1-dmamux",
		.of_match_table = rzn1_dmamux_match,
	},
	.probe	= rzn1_dmamux_probe,
};
module_platform_driver(rzn1_dmamux_driver);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Miquel Raynal <miquel.raynal@bootlin.com");
MODULE_DESCRIPTION("Renesas RZ/N1 DMAMUX driver");
