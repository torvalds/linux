// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2018, Nuvoton Corporation.
 * Copyright (c) 2018, Intel Corporation.
 */

#define pr_fmt(fmt) "nuvoton-kcs-bmc: " fmt

#include <linux/atomic.h>
#include <linux/errno.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/mfd/syscon.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>
#include <linux/slab.h>

#include "kcs_bmc.h"

#define DEVICE_NAME	"npcm-kcs-bmc"
#define KCS_CHANNEL_MAX	3

#define KCS1ST		0x0C
#define KCS2ST		0x1E
#define KCS3ST		0x30

#define KCS1DO		0x0E
#define KCS2DO		0x20
#define KCS3DO		0x32

#define KCS1DI		0x10
#define KCS2DI		0x22
#define KCS3DI		0x34

#define KCS1CTL		0x18
#define KCS2CTL		0x2A
#define KCS3CTL		0x3C
#define    KCS_CTL_IBFIE	BIT(0)

#define KCS1IE		0x1C
#define KCS2IE		0x2E
#define KCS3IE		0x40
#define    KCS_IE_IRQE          BIT(0)
#define    KCS_IE_HIRQE         BIT(3)

/*
 * 7.2.4 Core KCS Registers
 * Registers in this module are 8 bits. An 8-bit register must be accessed
 * by an 8-bit read or write.
 *
 * sts: KCS Channel n Status Register (KCSnST).
 * dob: KCS Channel n Data Out Buffer Register (KCSnDO).
 * dib: KCS Channel n Data In Buffer Register (KCSnDI).
 * ctl: KCS Channel n Control Register (KCSnCTL).
 * ie : KCS Channel n  Interrupt Enable Register (KCSnIE).
 */
struct npcm7xx_kcs_reg {
	u32 sts;
	u32 dob;
	u32 dib;
	u32 ctl;
	u32 ie;
};

struct npcm7xx_kcs_bmc {
	struct regmap *map;

	const struct npcm7xx_kcs_reg *reg;
};

static const struct npcm7xx_kcs_reg npcm7xx_kcs_reg_tbl[KCS_CHANNEL_MAX] = {
	{ .sts = KCS1ST, .dob = KCS1DO, .dib = KCS1DI, .ctl = KCS1CTL, .ie = KCS1IE },
	{ .sts = KCS2ST, .dob = KCS2DO, .dib = KCS2DI, .ctl = KCS2CTL, .ie = KCS2IE },
	{ .sts = KCS3ST, .dob = KCS3DO, .dib = KCS3DI, .ctl = KCS3CTL, .ie = KCS3IE },
};

static u8 npcm7xx_kcs_inb(struct kcs_bmc *kcs_bmc, u32 reg)
{
	struct npcm7xx_kcs_bmc *priv = kcs_bmc_priv(kcs_bmc);
	u32 val = 0;
	int rc;

	rc = regmap_read(priv->map, reg, &val);
	WARN(rc != 0, "regmap_read() failed: %d\n", rc);

	return rc == 0 ? (u8)val : 0;
}

static void npcm7xx_kcs_outb(struct kcs_bmc *kcs_bmc, u32 reg, u8 data)
{
	struct npcm7xx_kcs_bmc *priv = kcs_bmc_priv(kcs_bmc);
	int rc;

	rc = regmap_write(priv->map, reg, data);
	WARN(rc != 0, "regmap_write() failed: %d\n", rc);
}

static void npcm7xx_kcs_enable_channel(struct kcs_bmc *kcs_bmc, bool enable)
{
	struct npcm7xx_kcs_bmc *priv = kcs_bmc_priv(kcs_bmc);

	regmap_update_bits(priv->map, priv->reg->ctl, KCS_CTL_IBFIE,
			   enable ? KCS_CTL_IBFIE : 0);

	regmap_update_bits(priv->map, priv->reg->ie, KCS_IE_IRQE | KCS_IE_HIRQE,
			   enable ? KCS_IE_IRQE | KCS_IE_HIRQE : 0);
}

static irqreturn_t npcm7xx_kcs_irq(int irq, void *arg)
{
	struct kcs_bmc *kcs_bmc = arg;

	if (!kcs_bmc_handle_event(kcs_bmc))
		return IRQ_HANDLED;

	return IRQ_NONE;
}

static int npcm7xx_kcs_config_irq(struct kcs_bmc *kcs_bmc,
				  struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	int irq;

	irq = platform_get_irq(pdev, 0);
	if (irq < 0)
		return irq;

	return devm_request_irq(dev, irq, npcm7xx_kcs_irq, IRQF_SHARED,
				dev_name(dev), kcs_bmc);
}

static int npcm7xx_kcs_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct npcm7xx_kcs_bmc *priv;
	struct kcs_bmc *kcs_bmc;
	u32 chan;
	int rc;

	rc = of_property_read_u32(dev->of_node, "kcs_chan", &chan);
	if (rc != 0 || chan == 0 || chan > KCS_CHANNEL_MAX) {
		dev_err(dev, "no valid 'kcs_chan' configured\n");
		return -ENODEV;
	}

	kcs_bmc = kcs_bmc_alloc(dev, sizeof(*priv), chan);
	if (!kcs_bmc)
		return -ENOMEM;

	priv = kcs_bmc_priv(kcs_bmc);
	priv->map = syscon_node_to_regmap(dev->parent->of_node);
	if (IS_ERR(priv->map)) {
		dev_err(dev, "Couldn't get regmap\n");
		return -ENODEV;
	}
	priv->reg = &npcm7xx_kcs_reg_tbl[chan - 1];

	kcs_bmc->ioreg.idr = priv->reg->dib;
	kcs_bmc->ioreg.odr = priv->reg->dob;
	kcs_bmc->ioreg.str = priv->reg->sts;
	kcs_bmc->io_inputb = npcm7xx_kcs_inb;
	kcs_bmc->io_outputb = npcm7xx_kcs_outb;

	dev_set_drvdata(dev, kcs_bmc);

	npcm7xx_kcs_enable_channel(kcs_bmc, true);
	rc = npcm7xx_kcs_config_irq(kcs_bmc, pdev);
	if (rc)
		return rc;

	rc = misc_register(&kcs_bmc->miscdev);
	if (rc) {
		dev_err(dev, "Unable to register device\n");
		return rc;
	}

	pr_info("channel=%u idr=0x%x odr=0x%x str=0x%x\n",
		chan,
		kcs_bmc->ioreg.idr, kcs_bmc->ioreg.odr, kcs_bmc->ioreg.str);

	return 0;
}

static int npcm7xx_kcs_remove(struct platform_device *pdev)
{
	struct kcs_bmc *kcs_bmc = dev_get_drvdata(&pdev->dev);

	misc_deregister(&kcs_bmc->miscdev);

	return 0;
}

static const struct of_device_id npcm_kcs_bmc_match[] = {
	{ .compatible = "nuvoton,npcm750-kcs-bmc" },
	{ }
};
MODULE_DEVICE_TABLE(of, npcm_kcs_bmc_match);

static struct platform_driver npcm_kcs_bmc_driver = {
	.driver = {
		.name		= DEVICE_NAME,
		.of_match_table	= npcm_kcs_bmc_match,
	},
	.probe	= npcm7xx_kcs_probe,
	.remove	= npcm7xx_kcs_remove,
};
module_platform_driver(npcm_kcs_bmc_driver);

MODULE_LICENSE("GPL v2");
MODULE_AUTHOR("Avi Fishman <avifishman70@gmail.com>");
MODULE_AUTHOR("Haiyue Wang <haiyue.wang@linux.intel.com>");
MODULE_DESCRIPTION("NPCM7xx device interface to the KCS BMC device");
