// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2008-2010
 *
 * - Kurt Van Dijck, EIA Electronics
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/slab.h>

#include <pcmcia/cistpl.h>
#include <pcmcia/ds.h>

#include "softing_platform.h"

static int softingcs_index;
static DEFINE_SPINLOCK(softingcs_index_lock);

static int softingcs_reset(struct platform_device *pdev, int v);
static int softingcs_enable_irq(struct platform_device *pdev, int v);

/*
 * platform_data descriptions
 */
#define MHZ (1000*1000)
static const struct softing_platform_data softingcs_platform_data[] = {
{
	.name = "CANcard",
	.manf = 0x0168, .prod = 0x001,
	.generation = 1,
	.nbus = 2,
	.freq = 16 * MHZ, .max_brp = 32, .max_sjw = 4,
	.dpram_size = 0x0800,
	.boot = {0x0000, 0x000000, fw_dir "bcard.bin",},
	.load = {0x0120, 0x00f600, fw_dir "ldcard.bin",},
	.app = {0x0010, 0x0d0000, fw_dir "cancard.bin",},
	.reset = softingcs_reset,
	.enable_irq = softingcs_enable_irq,
}, {
	.name = "CANcard-NEC",
	.manf = 0x0168, .prod = 0x002,
	.generation = 1,
	.nbus = 2,
	.freq = 16 * MHZ, .max_brp = 32, .max_sjw = 4,
	.dpram_size = 0x0800,
	.boot = {0x0000, 0x000000, fw_dir "bcard.bin",},
	.load = {0x0120, 0x00f600, fw_dir "ldcard.bin",},
	.app = {0x0010, 0x0d0000, fw_dir "cancard.bin",},
	.reset = softingcs_reset,
	.enable_irq = softingcs_enable_irq,
}, {
	.name = "CANcard-SJA",
	.manf = 0x0168, .prod = 0x004,
	.generation = 1,
	.nbus = 2,
	.freq = 20 * MHZ, .max_brp = 32, .max_sjw = 4,
	.dpram_size = 0x0800,
	.boot = {0x0000, 0x000000, fw_dir "bcard.bin",},
	.load = {0x0120, 0x00f600, fw_dir "ldcard.bin",},
	.app = {0x0010, 0x0d0000, fw_dir "cansja.bin",},
	.reset = softingcs_reset,
	.enable_irq = softingcs_enable_irq,
}, {
	.name = "CANcard-2",
	.manf = 0x0168, .prod = 0x005,
	.generation = 2,
	.nbus = 2,
	.freq = 24 * MHZ, .max_brp = 64, .max_sjw = 4,
	.dpram_size = 0x1000,
	.boot = {0x0000, 0x000000, fw_dir "bcard2.bin",},
	.load = {0x0120, 0x00f600, fw_dir "ldcard2.bin",},
	.app = {0x0010, 0x0d0000, fw_dir "cancrd2.bin",},
	.reset = softingcs_reset,
	.enable_irq = NULL,
}, {
	.name = "Vector-CANcard",
	.manf = 0x0168, .prod = 0x081,
	.generation = 1,
	.nbus = 2,
	.freq = 16 * MHZ, .max_brp = 64, .max_sjw = 4,
	.dpram_size = 0x0800,
	.boot = {0x0000, 0x000000, fw_dir "bcard.bin",},
	.load = {0x0120, 0x00f600, fw_dir "ldcard.bin",},
	.app = {0x0010, 0x0d0000, fw_dir "cancard.bin",},
	.reset = softingcs_reset,
	.enable_irq = softingcs_enable_irq,
}, {
	.name = "Vector-CANcard-SJA",
	.manf = 0x0168, .prod = 0x084,
	.generation = 1,
	.nbus = 2,
	.freq = 20 * MHZ, .max_brp = 32, .max_sjw = 4,
	.dpram_size = 0x0800,
	.boot = {0x0000, 0x000000, fw_dir "bcard.bin",},
	.load = {0x0120, 0x00f600, fw_dir "ldcard.bin",},
	.app = {0x0010, 0x0d0000, fw_dir "cansja.bin",},
	.reset = softingcs_reset,
	.enable_irq = softingcs_enable_irq,
}, {
	.name = "Vector-CANcard-2",
	.manf = 0x0168, .prod = 0x085,
	.generation = 2,
	.nbus = 2,
	.freq = 24 * MHZ, .max_brp = 64, .max_sjw = 4,
	.dpram_size = 0x1000,
	.boot = {0x0000, 0x000000, fw_dir "bcard2.bin",},
	.load = {0x0120, 0x00f600, fw_dir "ldcard2.bin",},
	.app = {0x0010, 0x0d0000, fw_dir "cancrd2.bin",},
	.reset = softingcs_reset,
	.enable_irq = NULL,
}, {
	.name = "EDICcard-NEC",
	.manf = 0x0168, .prod = 0x102,
	.generation = 1,
	.nbus = 2,
	.freq = 16 * MHZ, .max_brp = 64, .max_sjw = 4,
	.dpram_size = 0x0800,
	.boot = {0x0000, 0x000000, fw_dir "bcard.bin",},
	.load = {0x0120, 0x00f600, fw_dir "ldcard.bin",},
	.app = {0x0010, 0x0d0000, fw_dir "cancard.bin",},
	.reset = softingcs_reset,
	.enable_irq = softingcs_enable_irq,
}, {
	.name = "EDICcard-2",
	.manf = 0x0168, .prod = 0x105,
	.generation = 2,
	.nbus = 2,
	.freq = 24 * MHZ, .max_brp = 64, .max_sjw = 4,
	.dpram_size = 0x1000,
	.boot = {0x0000, 0x000000, fw_dir "bcard2.bin",},
	.load = {0x0120, 0x00f600, fw_dir "ldcard2.bin",},
	.app = {0x0010, 0x0d0000, fw_dir "cancrd2.bin",},
	.reset = softingcs_reset,
	.enable_irq = NULL,
}, {
	0, 0,
},
};

MODULE_FIRMWARE(fw_dir "bcard.bin");
MODULE_FIRMWARE(fw_dir "ldcard.bin");
MODULE_FIRMWARE(fw_dir "cancard.bin");
MODULE_FIRMWARE(fw_dir "cansja.bin");

MODULE_FIRMWARE(fw_dir "bcard2.bin");
MODULE_FIRMWARE(fw_dir "ldcard2.bin");
MODULE_FIRMWARE(fw_dir "cancrd2.bin");

static const struct softing_platform_data
*softingcs_find_platform_data(unsigned int manf, unsigned int prod)
{
	const struct softing_platform_data *lp;

	for (lp = softingcs_platform_data; lp->manf; ++lp) {
		if ((lp->manf == manf) && (lp->prod == prod))
			return lp;
	}
	return NULL;
}

/*
 * platformdata callbacks
 */
static int softingcs_reset(struct platform_device *pdev, int v)
{
	struct pcmcia_device *pcmcia = to_pcmcia_dev(pdev->dev.parent);

	dev_dbg(&pdev->dev, "pcmcia config [2] %02x\n", v ? 0 : 0x20);
	return pcmcia_write_config_byte(pcmcia, 2, v ? 0 : 0x20);
}

static int softingcs_enable_irq(struct platform_device *pdev, int v)
{
	struct pcmcia_device *pcmcia = to_pcmcia_dev(pdev->dev.parent);

	dev_dbg(&pdev->dev, "pcmcia config [0] %02x\n", v ? 0x60 : 0);
	return pcmcia_write_config_byte(pcmcia, 0, v ? 0x60 : 0);
}

/*
 * pcmcia check
 */
static int softingcs_probe_config(struct pcmcia_device *pcmcia, void *priv_data)
{
	struct softing_platform_data *pdat = priv_data;
	struct resource *pres;
	int memspeed = 0;

	WARN_ON(!pdat);
	pres = pcmcia->resource[PCMCIA_IOMEM_0];
	if (resource_size(pres) < 0x1000)
		return -ERANGE;

	pres->flags |= WIN_MEMORY_TYPE_CM | WIN_ENABLE;
	if (pdat->generation < 2) {
		pres->flags |= WIN_USE_WAIT | WIN_DATA_WIDTH_8;
		memspeed = 3;
	} else {
		pres->flags |= WIN_DATA_WIDTH_16;
	}
	return pcmcia_request_window(pcmcia, pres, memspeed);
}

static void softingcs_remove(struct pcmcia_device *pcmcia)
{
	struct platform_device *pdev = pcmcia->priv;

	/* free bits */
	platform_device_unregister(pdev);
	/* release pcmcia stuff */
	pcmcia_disable_device(pcmcia);
}

/*
 * platform_device wrapper
 * pdev->resource has 2 entries: io & irq
 */
static void softingcs_pdev_release(struct device *dev)
{
	struct platform_device *pdev = to_platform_device(dev);
	kfree(pdev);
}

static int softingcs_probe(struct pcmcia_device *pcmcia)
{
	int ret;
	struct platform_device *pdev;
	const struct softing_platform_data *pdat;
	struct resource *pres;
	struct dev {
		struct platform_device pdev;
		struct resource res[2];
	} *dev;

	/* find matching platform_data */
	pdat = softingcs_find_platform_data(pcmcia->manf_id, pcmcia->card_id);
	if (!pdat)
		return -ENOTTY;

	/* setup pcmcia device */
	pcmcia->config_flags |= CONF_ENABLE_IRQ | CONF_AUTO_SET_IOMEM |
		CONF_AUTO_SET_VPP | CONF_AUTO_CHECK_VCC;
	ret = pcmcia_loop_config(pcmcia, softingcs_probe_config, (void *)pdat);
	if (ret)
		goto pcmcia_failed;

	ret = pcmcia_enable_device(pcmcia);
	if (ret < 0)
		goto pcmcia_failed;

	pres = pcmcia->resource[PCMCIA_IOMEM_0];
	if (!pres) {
		ret = -EBADF;
		goto pcmcia_bad;
	}

	/* create softing platform device */
	dev = kzalloc(sizeof(*dev), GFP_KERNEL);
	if (!dev) {
		ret = -ENOMEM;
		goto mem_failed;
	}
	dev->pdev.resource = dev->res;
	dev->pdev.num_resources = ARRAY_SIZE(dev->res);
	dev->pdev.dev.release = softingcs_pdev_release;

	pdev = &dev->pdev;
	pdev->dev.platform_data = (void *)pdat;
	pdev->dev.parent = &pcmcia->dev;
	pcmcia->priv = pdev;

	/* platform device resources */
	pdev->resource[0].flags = IORESOURCE_MEM;
	pdev->resource[0].start = pres->start;
	pdev->resource[0].end = pres->end;

	pdev->resource[1].flags = IORESOURCE_IRQ;
	pdev->resource[1].start = pcmcia->irq;
	pdev->resource[1].end = pdev->resource[1].start;

	/* platform device setup */
	spin_lock(&softingcs_index_lock);
	pdev->id = softingcs_index++;
	spin_unlock(&softingcs_index_lock);
	pdev->name = "softing";
	dev_set_name(&pdev->dev, "softingcs.%i", pdev->id);
	ret = platform_device_register(pdev);
	if (ret < 0)
		goto platform_failed;

	dev_info(&pcmcia->dev, "created %s\n", dev_name(&pdev->dev));
	return 0;

platform_failed:
	platform_device_put(pdev);
mem_failed:
pcmcia_bad:
pcmcia_failed:
	pcmcia_disable_device(pcmcia);
	pcmcia->priv = NULL;
	return ret;
}

static const struct pcmcia_device_id softingcs_ids[] = {
	/* softing */
	PCMCIA_DEVICE_MANF_CARD(0x0168, 0x0001),
	PCMCIA_DEVICE_MANF_CARD(0x0168, 0x0002),
	PCMCIA_DEVICE_MANF_CARD(0x0168, 0x0004),
	PCMCIA_DEVICE_MANF_CARD(0x0168, 0x0005),
	/* vector, manufacturer? */
	PCMCIA_DEVICE_MANF_CARD(0x0168, 0x0081),
	PCMCIA_DEVICE_MANF_CARD(0x0168, 0x0084),
	PCMCIA_DEVICE_MANF_CARD(0x0168, 0x0085),
	/* EDIC */
	PCMCIA_DEVICE_MANF_CARD(0x0168, 0x0102),
	PCMCIA_DEVICE_MANF_CARD(0x0168, 0x0105),
	PCMCIA_DEVICE_NULL,
};

MODULE_DEVICE_TABLE(pcmcia, softingcs_ids);

static struct pcmcia_driver softingcs_driver = {
	.owner		= THIS_MODULE,
	.name		= "softingcs",
	.id_table	= softingcs_ids,
	.probe		= softingcs_probe,
	.remove		= softingcs_remove,
};

module_pcmcia_driver(softingcs_driver);

MODULE_DESCRIPTION("softing CANcard driver"
		", links PCMCIA card to softing driver");
MODULE_LICENSE("GPL v2");
