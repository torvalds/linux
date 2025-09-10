// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2025 Inochi Amaoto <inochiama@gmail.com>
 */

#include <linux/bitops.h>
#include <linux/cleanup.h>
#include <linux/module.h>
#include <linux/of_dma.h>
#include <linux/of_address.h>
#include <linux/of_platform.h>
#include <linux/platform_device.h>
#include <linux/llist.h>
#include <linux/regmap.h>
#include <linux/spinlock.h>
#include <linux/mfd/syscon.h>

#define REG_DMA_CHANNEL_REMAP0		0x154
#define REG_DMA_CHANNEL_REMAP1		0x158
#define REG_DMA_INT_MUX			0x298

#define DMAMUX_NCELLS			2
#define MAX_DMA_MAPPING_ID		42
#define MAX_DMA_CPU_ID			2
#define MAX_DMA_CH_ID			7

#define DMAMUX_INTMUX_REGISTER_LEN	4
#define DMAMUX_NR_CH_PER_REGISTER	4
#define DMAMUX_BIT_PER_CH		8
#define DMAMUX_CH_MASk			GENMASK(5, 0)
#define DMAMUX_INT_BIT_PER_CPU		10
#define DMAMUX_CH_UPDATE_BIT		BIT(31)

#define DMAMUX_CH_REGPOS(chid) \
	((chid) / DMAMUX_NR_CH_PER_REGISTER)
#define DMAMUX_CH_REGOFF(chid) \
	((chid) % DMAMUX_NR_CH_PER_REGISTER)
#define DMAMUX_CH_REG(chid) \
	((DMAMUX_CH_REGPOS(chid) * sizeof(u32)) + \
	 REG_DMA_CHANNEL_REMAP0)
#define DMAMUX_CH_SET(chid, val) \
	(((val) << (DMAMUX_CH_REGOFF(chid) * DMAMUX_BIT_PER_CH)) | \
	 DMAMUX_CH_UPDATE_BIT)
#define DMAMUX_CH_MASK(chid) \
	DMAMUX_CH_SET(chid, DMAMUX_CH_MASk)

#define DMAMUX_INT_BIT(chid, cpuid) \
	BIT((cpuid) * DMAMUX_INT_BIT_PER_CPU + (chid))
#define DMAMUX_INTEN_BIT(cpuid) \
	DMAMUX_INT_BIT(8, cpuid)
#define DMAMUX_INT_CH_BIT(chid, cpuid) \
	(DMAMUX_INT_BIT(chid, cpuid) | DMAMUX_INTEN_BIT(cpuid))
#define DMAMUX_INT_MASK(chid) \
	(DMAMUX_INT_BIT(chid, 0) | \
	 DMAMUX_INT_BIT(chid, 1) | \
	 DMAMUX_INT_BIT(chid, 2))
#define DMAMUX_INT_CH_MASK(chid, cpuid) \
	(DMAMUX_INT_MASK(chid) | DMAMUX_INTEN_BIT(cpuid))

struct cv1800_dmamux_data {
	struct dma_router	dmarouter;
	struct regmap		*regmap;
	spinlock_t		lock;
	struct llist_head	free_maps;
	struct llist_head	reserve_maps;
	DECLARE_BITMAP(mapped_peripherals, MAX_DMA_MAPPING_ID);
};

struct cv1800_dmamux_map {
	struct llist_node node;
	unsigned int channel;
	unsigned int peripheral;
	unsigned int cpu;
};

static void cv1800_dmamux_free(struct device *dev, void *route_data)
{
	struct cv1800_dmamux_data *dmamux = dev_get_drvdata(dev);
	struct cv1800_dmamux_map *map = route_data;

	guard(spinlock_irqsave)(&dmamux->lock);

	regmap_update_bits(dmamux->regmap,
			   DMAMUX_CH_REG(map->channel),
			   DMAMUX_CH_MASK(map->channel),
			   DMAMUX_CH_UPDATE_BIT);

	regmap_update_bits(dmamux->regmap, REG_DMA_INT_MUX,
			   DMAMUX_INT_CH_MASK(map->channel, map->cpu),
			   DMAMUX_INTEN_BIT(map->cpu));

	dev_dbg(dev, "free channel %u for req %u (cpu %u)\n",
		map->channel, map->peripheral, map->cpu);
}

static void *cv1800_dmamux_route_allocate(struct of_phandle_args *dma_spec,
					  struct of_dma *ofdma)
{
	struct platform_device *pdev = of_find_device_by_node(ofdma->of_node);
	struct cv1800_dmamux_data *dmamux = platform_get_drvdata(pdev);
	struct cv1800_dmamux_map *map;
	struct llist_node *node;
	unsigned long flags;
	unsigned int chid, devid, cpuid;
	int ret;

	if (dma_spec->args_count != DMAMUX_NCELLS) {
		dev_err(&pdev->dev, "invalid number of dma mux args\n");
		return ERR_PTR(-EINVAL);
	}

	devid = dma_spec->args[0];
	cpuid = dma_spec->args[1];
	dma_spec->args_count = 1;

	if (devid > MAX_DMA_MAPPING_ID) {
		dev_err(&pdev->dev, "invalid device id: %u\n", devid);
		return ERR_PTR(-EINVAL);
	}

	if (cpuid > MAX_DMA_CPU_ID) {
		dev_err(&pdev->dev, "invalid cpu id: %u\n", cpuid);
		return ERR_PTR(-EINVAL);
	}

	dma_spec->np = of_parse_phandle(ofdma->of_node, "dma-masters", 0);
	if (!dma_spec->np) {
		dev_err(&pdev->dev, "can't get dma master\n");
		return ERR_PTR(-EINVAL);
	}

	spin_lock_irqsave(&dmamux->lock, flags);

	if (test_bit(devid, dmamux->mapped_peripherals)) {
		llist_for_each_entry(map, dmamux->reserve_maps.first, node) {
			if (map->peripheral == devid && map->cpu == cpuid)
				goto found;
		}

		ret = -EINVAL;
		goto failed;
	} else {
		node = llist_del_first(&dmamux->free_maps);
		if (!node) {
			ret = -ENODEV;
			goto failed;
		}

		map = llist_entry(node, struct cv1800_dmamux_map, node);
		llist_add(&map->node, &dmamux->reserve_maps);
		set_bit(devid, dmamux->mapped_peripherals);
	}

found:
	chid = map->channel;
	map->peripheral = devid;
	map->cpu = cpuid;

	regmap_set_bits(dmamux->regmap,
			DMAMUX_CH_REG(chid),
			DMAMUX_CH_SET(chid, devid));

	regmap_update_bits(dmamux->regmap, REG_DMA_INT_MUX,
			   DMAMUX_INT_CH_MASK(chid, cpuid),
			   DMAMUX_INT_CH_BIT(chid, cpuid));

	spin_unlock_irqrestore(&dmamux->lock, flags);

	dma_spec->args[0] = chid;

	dev_dbg(&pdev->dev, "register channel %u for req %u (cpu %u)\n",
		chid, devid, cpuid);

	return map;

failed:
	spin_unlock_irqrestore(&dmamux->lock, flags);
	of_node_put(dma_spec->np);
	dev_err(&pdev->dev, "errno %d\n", ret);
	return ERR_PTR(ret);
}

static int cv1800_dmamux_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct device_node *mux_node = dev->of_node;
	struct cv1800_dmamux_data *data;
	struct cv1800_dmamux_map *tmp;
	struct device *parent = dev->parent;
	struct regmap *regmap = NULL;
	unsigned int i;

	if (!parent)
		return -ENODEV;

	regmap = device_node_to_regmap(parent->of_node);
	if (IS_ERR(regmap))
		return PTR_ERR(regmap);

	data = devm_kzalloc(dev, sizeof(*data), GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	spin_lock_init(&data->lock);
	init_llist_head(&data->free_maps);
	init_llist_head(&data->reserve_maps);

	for (i = 0; i <= MAX_DMA_CH_ID; i++) {
		tmp = devm_kmalloc(dev, sizeof(*tmp), GFP_KERNEL);
		if (!tmp) {
			/* It is OK for not allocating all channel */
			dev_warn(dev, "can not allocate channel %u\n", i);
			continue;
		}

		init_llist_node(&tmp->node);
		tmp->channel = i;
		llist_add(&tmp->node, &data->free_maps);
	}

	/* if no channel is allocated, the probe must fail */
	if (llist_empty(&data->free_maps))
		return -ENOMEM;

	data->regmap = regmap;
	data->dmarouter.dev = dev;
	data->dmarouter.route_free = cv1800_dmamux_free;

	platform_set_drvdata(pdev, data);

	return of_dma_router_register(mux_node,
				      cv1800_dmamux_route_allocate,
				      &data->dmarouter);
}

static void cv1800_dmamux_remove(struct platform_device *pdev)
{
	of_dma_controller_free(pdev->dev.of_node);
}

static const struct of_device_id cv1800_dmamux_ids[] = {
	{ .compatible = "sophgo,cv1800b-dmamux", },
	{ }
};
MODULE_DEVICE_TABLE(of, cv1800_dmamux_ids);

static struct platform_driver cv1800_dmamux_driver = {
	.probe = cv1800_dmamux_probe,
	.remove = cv1800_dmamux_remove,
	.driver = {
		.name = "cv1800-dmamux",
		.of_match_table = cv1800_dmamux_ids,
	},
};
module_platform_driver(cv1800_dmamux_driver);

MODULE_AUTHOR("Inochi Amaoto <inochiama@gmail.com>");
MODULE_DESCRIPTION("Sophgo CV1800/SG2000 Series SoC DMAMUX driver");
MODULE_LICENSE("GPL");
