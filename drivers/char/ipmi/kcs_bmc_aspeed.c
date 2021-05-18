// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2015-2018, Intel Corporation.
 */

#define pr_fmt(fmt) "aspeed-kcs-bmc: " fmt

#include <linux/atomic.h>
#include <linux/errno.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/mfd/syscon.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/platform_device.h>
#include <linux/poll.h>
#include <linux/regmap.h>
#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/timer.h>

#include "kcs_bmc.h"


#define DEVICE_NAME     "ast-kcs-bmc"

#define KCS_CHANNEL_MAX     4

#define LPC_HICR0            0x000
#define     LPC_HICR0_LPC3E          BIT(7)
#define     LPC_HICR0_LPC2E          BIT(6)
#define     LPC_HICR0_LPC1E          BIT(5)
#define LPC_HICR2            0x008
#define     LPC_HICR2_IBFIF3         BIT(3)
#define     LPC_HICR2_IBFIF2         BIT(2)
#define     LPC_HICR2_IBFIF1         BIT(1)
#define LPC_HICR4            0x010
#define     LPC_HICR4_LADR12AS       BIT(7)
#define     LPC_HICR4_KCSENBL        BIT(2)
#define LPC_LADR3H           0x014
#define LPC_LADR3L           0x018
#define LPC_LADR12H          0x01C
#define LPC_LADR12L          0x020
#define LPC_IDR1             0x024
#define LPC_IDR2             0x028
#define LPC_IDR3             0x02C
#define LPC_ODR1             0x030
#define LPC_ODR2             0x034
#define LPC_ODR3             0x038
#define LPC_STR1             0x03C
#define LPC_STR2             0x040
#define LPC_STR3             0x044
#define LPC_HICRB            0x100
#define     LPC_HICRB_IBFIF4         BIT(1)
#define     LPC_HICRB_LPC4E          BIT(0)
#define LPC_LADR4            0x110
#define LPC_IDR4             0x114
#define LPC_ODR4             0x118
#define LPC_STR4             0x11C

struct aspeed_kcs_bmc {
	struct regmap *map;
};


static u8 aspeed_kcs_inb(struct kcs_bmc *kcs_bmc, u32 reg)
{
	struct aspeed_kcs_bmc *priv = kcs_bmc_priv(kcs_bmc);
	u32 val = 0;
	int rc;

	rc = regmap_read(priv->map, reg, &val);
	WARN(rc != 0, "regmap_read() failed: %d\n", rc);

	return rc == 0 ? (u8) val : 0;
}

static void aspeed_kcs_outb(struct kcs_bmc *kcs_bmc, u32 reg, u8 data)
{
	struct aspeed_kcs_bmc *priv = kcs_bmc_priv(kcs_bmc);
	int rc;

	rc = regmap_write(priv->map, reg, data);
	WARN(rc != 0, "regmap_write() failed: %d\n", rc);
}


/*
 * AST_usrGuide_KCS.pdf
 * 2. Background:
 *   we note D for Data, and C for Cmd/Status, default rules are
 *     A. KCS1 / KCS2 ( D / C:X / X+4 )
 *        D / C : CA0h / CA4h
 *        D / C : CA8h / CACh
 *     B. KCS3 ( D / C:XX2h / XX3h )
 *        D / C : CA2h / CA3h
 *        D / C : CB2h / CB3h
 *     C. KCS4
 *        D / C : CA4h / CA5h
 */
static void aspeed_kcs_set_address(struct kcs_bmc *kcs_bmc, u16 addr)
{
	struct aspeed_kcs_bmc *priv = kcs_bmc_priv(kcs_bmc);

	switch (kcs_bmc->channel) {
	case 1:
		regmap_update_bits(priv->map, LPC_HICR4,
				LPC_HICR4_LADR12AS, 0);
		regmap_write(priv->map, LPC_LADR12H, addr >> 8);
		regmap_write(priv->map, LPC_LADR12L, addr & 0xFF);
		break;

	case 2:
		regmap_update_bits(priv->map, LPC_HICR4,
				LPC_HICR4_LADR12AS, LPC_HICR4_LADR12AS);
		regmap_write(priv->map, LPC_LADR12H, addr >> 8);
		regmap_write(priv->map, LPC_LADR12L, addr & 0xFF);
		break;

	case 3:
		regmap_write(priv->map, LPC_LADR3H, addr >> 8);
		regmap_write(priv->map, LPC_LADR3L, addr & 0xFF);
		break;

	case 4:
		regmap_write(priv->map, LPC_LADR4, ((addr + 1) << 16) |
			addr);
		break;

	default:
		break;
	}
}

static void aspeed_kcs_enable_channel(struct kcs_bmc *kcs_bmc, bool enable)
{
	struct aspeed_kcs_bmc *priv = kcs_bmc_priv(kcs_bmc);

	switch (kcs_bmc->channel) {
	case 1:
		if (enable) {
			regmap_update_bits(priv->map, LPC_HICR2,
					LPC_HICR2_IBFIF1, LPC_HICR2_IBFIF1);
			regmap_update_bits(priv->map, LPC_HICR0,
					LPC_HICR0_LPC1E, LPC_HICR0_LPC1E);
		} else {
			regmap_update_bits(priv->map, LPC_HICR0,
					LPC_HICR0_LPC1E, 0);
			regmap_update_bits(priv->map, LPC_HICR2,
					LPC_HICR2_IBFIF1, 0);
		}
		break;

	case 2:
		if (enable) {
			regmap_update_bits(priv->map, LPC_HICR2,
					LPC_HICR2_IBFIF2, LPC_HICR2_IBFIF2);
			regmap_update_bits(priv->map, LPC_HICR0,
					LPC_HICR0_LPC2E, LPC_HICR0_LPC2E);
		} else {
			regmap_update_bits(priv->map, LPC_HICR0,
					LPC_HICR0_LPC2E, 0);
			regmap_update_bits(priv->map, LPC_HICR2,
					LPC_HICR2_IBFIF2, 0);
		}
		break;

	case 3:
		if (enable) {
			regmap_update_bits(priv->map, LPC_HICR2,
					LPC_HICR2_IBFIF3, LPC_HICR2_IBFIF3);
			regmap_update_bits(priv->map, LPC_HICR0,
					LPC_HICR0_LPC3E, LPC_HICR0_LPC3E);
			regmap_update_bits(priv->map, LPC_HICR4,
					LPC_HICR4_KCSENBL, LPC_HICR4_KCSENBL);
		} else {
			regmap_update_bits(priv->map, LPC_HICR0,
					LPC_HICR0_LPC3E, 0);
			regmap_update_bits(priv->map, LPC_HICR4,
					LPC_HICR4_KCSENBL, 0);
			regmap_update_bits(priv->map, LPC_HICR2,
					LPC_HICR2_IBFIF3, 0);
		}
		break;

	case 4:
		if (enable)
			regmap_update_bits(priv->map, LPC_HICRB,
					LPC_HICRB_IBFIF4 | LPC_HICRB_LPC4E,
					LPC_HICRB_IBFIF4 | LPC_HICRB_LPC4E);
		else
			regmap_update_bits(priv->map, LPC_HICRB,
					LPC_HICRB_IBFIF4 | LPC_HICRB_LPC4E,
					0);
		break;

	default:
		break;
	}
}

static irqreturn_t aspeed_kcs_irq(int irq, void *arg)
{
	struct kcs_bmc *kcs_bmc = arg;

	if (!kcs_bmc_handle_event(kcs_bmc))
		return IRQ_HANDLED;

	return IRQ_NONE;
}

static int aspeed_kcs_config_irq(struct kcs_bmc *kcs_bmc,
			struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	int irq;

	irq = platform_get_irq(pdev, 0);
	if (irq < 0)
		return irq;

	return devm_request_irq(dev, irq, aspeed_kcs_irq, IRQF_SHARED,
				dev_name(dev), kcs_bmc);
}

static const struct kcs_ioreg ast_kcs_bmc_ioregs[KCS_CHANNEL_MAX] = {
	{ .idr = LPC_IDR1, .odr = LPC_ODR1, .str = LPC_STR1 },
	{ .idr = LPC_IDR2, .odr = LPC_ODR2, .str = LPC_STR2 },
	{ .idr = LPC_IDR3, .odr = LPC_ODR3, .str = LPC_STR3 },
	{ .idr = LPC_IDR4, .odr = LPC_ODR4, .str = LPC_STR4 },
};

static struct kcs_bmc *aspeed_kcs_probe_of_v1(struct platform_device *pdev)
{
	struct aspeed_kcs_bmc *priv;
	struct device_node *np;
	struct kcs_bmc *kcs;
	u32 channel;
	u32 slave;
	int rc;

	np = pdev->dev.of_node;

	rc = of_property_read_u32(np, "kcs_chan", &channel);
	if ((rc != 0) || (channel == 0 || channel > KCS_CHANNEL_MAX)) {
		dev_err(&pdev->dev, "no valid 'kcs_chan' configured\n");
		return ERR_PTR(-EINVAL);
	}

	kcs = kcs_bmc_alloc(&pdev->dev, sizeof(struct aspeed_kcs_bmc), channel);
	if (!kcs)
		return ERR_PTR(-ENOMEM);

	priv = kcs_bmc_priv(kcs);
	priv->map = syscon_node_to_regmap(pdev->dev.parent->of_node);
	if (IS_ERR(priv->map)) {
		dev_err(&pdev->dev, "Couldn't get regmap\n");
		return ERR_PTR(-ENODEV);
	}

	rc = of_property_read_u32(np, "kcs_addr", &slave);
	if (rc) {
		dev_err(&pdev->dev, "no valid 'kcs_addr' configured\n");
		return ERR_PTR(-EINVAL);
	}

	kcs->ioreg = ast_kcs_bmc_ioregs[channel - 1];
	aspeed_kcs_set_address(kcs, slave);

	return kcs;
}

static int aspeed_kcs_calculate_channel(const struct kcs_ioreg *regs)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(ast_kcs_bmc_ioregs); i++) {
		if (!memcmp(&ast_kcs_bmc_ioregs[i], regs, sizeof(*regs)))
			return i + 1;
	}

	return -EINVAL;
}

static struct kcs_bmc *aspeed_kcs_probe_of_v2(struct platform_device *pdev)
{
	struct aspeed_kcs_bmc *priv;
	struct device_node *np;
	struct kcs_ioreg ioreg;
	struct kcs_bmc *kcs;
	const __be32 *reg;
	int channel;
	u32 slave;
	int rc;

	np = pdev->dev.of_node;

	/* Don't translate addresses, we want offsets for the regmaps */
	reg = of_get_address(np, 0, NULL, NULL);
	if (!reg)
		return ERR_PTR(-EINVAL);
	ioreg.idr = be32_to_cpup(reg);

	reg = of_get_address(np, 1, NULL, NULL);
	if (!reg)
		return ERR_PTR(-EINVAL);
	ioreg.odr = be32_to_cpup(reg);

	reg = of_get_address(np, 2, NULL, NULL);
	if (!reg)
		return ERR_PTR(-EINVAL);
	ioreg.str = be32_to_cpup(reg);

	channel = aspeed_kcs_calculate_channel(&ioreg);
	if (channel < 0)
		return ERR_PTR(channel);

	kcs = kcs_bmc_alloc(&pdev->dev, sizeof(struct aspeed_kcs_bmc), channel);
	if (!kcs)
		return ERR_PTR(-ENOMEM);

	kcs->ioreg = ioreg;

	priv = kcs_bmc_priv(kcs);
	priv->map = syscon_node_to_regmap(pdev->dev.parent->of_node);
	if (IS_ERR(priv->map)) {
		dev_err(&pdev->dev, "Couldn't get regmap\n");
		return ERR_PTR(-ENODEV);
	}

	rc = of_property_read_u32(np, "aspeed,lpc-io-reg", &slave);
	if (rc)
		return ERR_PTR(rc);

	aspeed_kcs_set_address(kcs, slave);

	return kcs;
}

static int aspeed_kcs_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct kcs_bmc *kcs_bmc;
	struct device_node *np;
	int rc;

	np = dev->of_node->parent;
	if (!of_device_is_compatible(np, "aspeed,ast2400-lpc-v2") &&
	    !of_device_is_compatible(np, "aspeed,ast2500-lpc-v2") &&
	    !of_device_is_compatible(np, "aspeed,ast2600-lpc-v2")) {
		dev_err(dev, "unsupported LPC device binding\n");
		return -ENODEV;
	}

	np = dev->of_node;
	if (of_device_is_compatible(np, "aspeed,ast2400-kcs-bmc") ||
	    of_device_is_compatible(np, "aspeed,ast2500-kcs-bmc"))
		kcs_bmc = aspeed_kcs_probe_of_v1(pdev);
	else if (of_device_is_compatible(np, "aspeed,ast2400-kcs-bmc-v2") ||
		 of_device_is_compatible(np, "aspeed,ast2500-kcs-bmc-v2"))
		kcs_bmc = aspeed_kcs_probe_of_v2(pdev);
	else
		return -EINVAL;

	if (IS_ERR(kcs_bmc))
		return PTR_ERR(kcs_bmc);

	kcs_bmc->io_inputb = aspeed_kcs_inb;
	kcs_bmc->io_outputb = aspeed_kcs_outb;

	rc = aspeed_kcs_config_irq(kcs_bmc, pdev);
	if (rc)
		return rc;

	dev_set_drvdata(dev, kcs_bmc);

	aspeed_kcs_enable_channel(kcs_bmc, true);

	rc = misc_register(&kcs_bmc->miscdev);
	if (rc) {
		dev_err(dev, "Unable to register device\n");
		return rc;
	}

	dev_dbg(&pdev->dev,
		"Probed KCS device %d (IDR=0x%x, ODR=0x%x, STR=0x%x)\n",
		kcs_bmc->channel, kcs_bmc->ioreg.idr, kcs_bmc->ioreg.odr,
		kcs_bmc->ioreg.str);

	return 0;
}

static int aspeed_kcs_remove(struct platform_device *pdev)
{
	struct kcs_bmc *kcs_bmc = dev_get_drvdata(&pdev->dev);

	misc_deregister(&kcs_bmc->miscdev);

	return 0;
}

static const struct of_device_id ast_kcs_bmc_match[] = {
	{ .compatible = "aspeed,ast2400-kcs-bmc" },
	{ .compatible = "aspeed,ast2500-kcs-bmc" },
	{ .compatible = "aspeed,ast2400-kcs-bmc-v2" },
	{ .compatible = "aspeed,ast2500-kcs-bmc-v2" },
	{ }
};
MODULE_DEVICE_TABLE(of, ast_kcs_bmc_match);

static struct platform_driver ast_kcs_bmc_driver = {
	.driver = {
		.name           = DEVICE_NAME,
		.of_match_table = ast_kcs_bmc_match,
	},
	.probe  = aspeed_kcs_probe,
	.remove = aspeed_kcs_remove,
};
module_platform_driver(ast_kcs_bmc_driver);

MODULE_LICENSE("GPL v2");
MODULE_AUTHOR("Haiyue Wang <haiyue.wang@linux.intel.com>");
MODULE_DESCRIPTION("Aspeed device interface to the KCS BMC device");
