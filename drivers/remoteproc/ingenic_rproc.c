// SPDX-License-Identifier: GPL-2.0+
/*
 * Ingenic JZ47xx remoteproc driver
 * Copyright 2019, Paul Cercueil <paul@crapouillou.net>
 */

#include <linux/bitops.h>
#include <linux/clk.h>
#include <linux/err.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/remoteproc.h>

#include "remoteproc_internal.h"

#define REG_AUX_CTRL		0x0
#define REG_AUX_MSG_ACK		0x10
#define REG_AUX_MSG		0x14
#define REG_CORE_MSG_ACK	0x18
#define REG_CORE_MSG		0x1C

#define AUX_CTRL_SLEEP		BIT(31)
#define AUX_CTRL_MSG_IRQ_EN	BIT(3)
#define AUX_CTRL_NMI_RESETS	BIT(2)
#define AUX_CTRL_NMI		BIT(1)
#define AUX_CTRL_SW_RESET	BIT(0)

static bool auto_boot;
module_param(auto_boot, bool, 0400);
MODULE_PARM_DESC(auto_boot,
		 "Auto-boot the remote processor [default=false]");

struct vpu_mem_map {
	const char *name;
	unsigned int da;
};

struct vpu_mem_info {
	const struct vpu_mem_map *map;
	unsigned long len;
	void __iomem *base;
};

static const struct vpu_mem_map vpu_mem_map[] = {
	{ "tcsm0", 0x132b0000 },
	{ "tcsm1", 0xf4000000 },
	{ "sram",  0x132f0000 },
};

/**
 * struct vpu - Ingenic VPU remoteproc private structure
 * @irq: interrupt number
 * @clks: pointers to the VPU and AUX clocks
 * @aux_base: raw pointer to the AUX interface registers
 * @mem_info: array of struct vpu_mem_info, which contain the mapping info of
 *            each of the external memories
 * @dev: private pointer to the device
 */
struct vpu {
	int irq;
	struct clk_bulk_data clks[2];
	void __iomem *aux_base;
	struct vpu_mem_info mem_info[ARRAY_SIZE(vpu_mem_map)];
	struct device *dev;
};

static int ingenic_rproc_prepare(struct rproc *rproc)
{
	struct vpu *vpu = rproc->priv;
	int ret;

	/* The clocks must be enabled for the firmware to be loaded in TCSM */
	ret = clk_bulk_prepare_enable(ARRAY_SIZE(vpu->clks), vpu->clks);
	if (ret)
		dev_err(vpu->dev, "Unable to start clocks: %d\n", ret);

	return ret;
}

static int ingenic_rproc_unprepare(struct rproc *rproc)
{
	struct vpu *vpu = rproc->priv;

	clk_bulk_disable_unprepare(ARRAY_SIZE(vpu->clks), vpu->clks);

	return 0;
}

static int ingenic_rproc_start(struct rproc *rproc)
{
	struct vpu *vpu = rproc->priv;
	u32 ctrl;

	enable_irq(vpu->irq);

	/* Reset the AUX and enable message IRQ */
	ctrl = AUX_CTRL_NMI_RESETS | AUX_CTRL_NMI | AUX_CTRL_MSG_IRQ_EN;
	writel(ctrl, vpu->aux_base + REG_AUX_CTRL);

	return 0;
}

static int ingenic_rproc_stop(struct rproc *rproc)
{
	struct vpu *vpu = rproc->priv;

	disable_irq(vpu->irq);

	/* Keep AUX in reset mode */
	writel(AUX_CTRL_SW_RESET, vpu->aux_base + REG_AUX_CTRL);

	return 0;
}

static void ingenic_rproc_kick(struct rproc *rproc, int vqid)
{
	struct vpu *vpu = rproc->priv;

	writel(vqid, vpu->aux_base + REG_CORE_MSG);
}

static void *ingenic_rproc_da_to_va(struct rproc *rproc, u64 da, size_t len, bool *is_iomem)
{
	struct vpu *vpu = rproc->priv;
	void __iomem *va = NULL;
	unsigned int i;

	for (i = 0; i < ARRAY_SIZE(vpu_mem_map); i++) {
		const struct vpu_mem_info *info = &vpu->mem_info[i];
		const struct vpu_mem_map *map = info->map;

		if (da >= map->da && (da + len) < (map->da + info->len)) {
			va = info->base + (da - map->da);
			break;
		}
	}

	return (__force void *)va;
}

static const struct rproc_ops ingenic_rproc_ops = {
	.prepare = ingenic_rproc_prepare,
	.unprepare = ingenic_rproc_unprepare,
	.start = ingenic_rproc_start,
	.stop = ingenic_rproc_stop,
	.kick = ingenic_rproc_kick,
	.da_to_va = ingenic_rproc_da_to_va,
};

static irqreturn_t vpu_interrupt(int irq, void *data)
{
	struct rproc *rproc = data;
	struct vpu *vpu = rproc->priv;
	u32 vring;

	vring = readl(vpu->aux_base + REG_AUX_MSG);

	/* Ack the interrupt */
	writel(0, vpu->aux_base + REG_AUX_MSG_ACK);

	return rproc_vq_interrupt(rproc, vring);
}

static int ingenic_rproc_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct resource *mem;
	struct rproc *rproc;
	struct vpu *vpu;
	unsigned int i;
	int ret;

	rproc = devm_rproc_alloc(dev, "ingenic-vpu",
				 &ingenic_rproc_ops, NULL, sizeof(*vpu));
	if (!rproc)
		return -ENOMEM;

	rproc->auto_boot = auto_boot;

	vpu = rproc->priv;
	vpu->dev = &pdev->dev;
	platform_set_drvdata(pdev, vpu);

	mem = platform_get_resource_byname(pdev, IORESOURCE_MEM, "aux");
	vpu->aux_base = devm_ioremap_resource(dev, mem);
	if (IS_ERR(vpu->aux_base)) {
		dev_err(dev, "Failed to ioremap\n");
		return PTR_ERR(vpu->aux_base);
	}

	for (i = 0; i < ARRAY_SIZE(vpu_mem_map); i++) {
		mem = platform_get_resource_byname(pdev, IORESOURCE_MEM,
						   vpu_mem_map[i].name);

		vpu->mem_info[i].base = devm_ioremap_resource(dev, mem);
		if (IS_ERR(vpu->mem_info[i].base)) {
			ret = PTR_ERR(vpu->mem_info[i].base);
			dev_err(dev, "Failed to ioremap\n");
			return ret;
		}

		vpu->mem_info[i].len = resource_size(mem);
		vpu->mem_info[i].map = &vpu_mem_map[i];
	}

	vpu->clks[0].id = "vpu";
	vpu->clks[1].id = "aux";

	ret = devm_clk_bulk_get(dev, ARRAY_SIZE(vpu->clks), vpu->clks);
	if (ret) {
		dev_err(dev, "Failed to get clocks\n");
		return ret;
	}

	vpu->irq = platform_get_irq(pdev, 0);
	if (vpu->irq < 0)
		return vpu->irq;

	ret = devm_request_irq(dev, vpu->irq, vpu_interrupt, 0, "VPU", rproc);
	if (ret < 0) {
		dev_err(dev, "Failed to request IRQ\n");
		return ret;
	}

	disable_irq(vpu->irq);

	ret = devm_rproc_add(dev, rproc);
	if (ret) {
		dev_err(dev, "Failed to register remote processor\n");
		return ret;
	}

	return 0;
}

static const struct of_device_id ingenic_rproc_of_matches[] = {
	{ .compatible = "ingenic,jz4770-vpu-rproc", },
	{}
};
MODULE_DEVICE_TABLE(of, ingenic_rproc_of_matches);

static struct platform_driver ingenic_rproc_driver = {
	.probe = ingenic_rproc_probe,
	.driver = {
		.name = "ingenic-vpu",
		.of_match_table = ingenic_rproc_of_matches,
	},
};
module_platform_driver(ingenic_rproc_driver);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Paul Cercueil <paul@crapouillou.net>");
MODULE_DESCRIPTION("Ingenic JZ47xx Remote Processor control driver");
