// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2015-2018, Intel Corporation.
 */

#define pr_fmt(fmt) "aspeed-kcs-bmc: " fmt

#include <linux/atomic.h>
#include <linux/errno.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/irq.h>
#include <linux/mfd/syscon.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/of_device.h>
#include <linux/platform_device.h>
#include <linux/poll.h>
#include <linux/regmap.h>
#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/timer.h>

#include "kcs_bmc_device.h"


#define DEVICE_NAME     "ast-kcs-bmc"

#define KCS_CHANNEL_MAX     4

/*
 * Field class descriptions
 *
 * LPCyE	Enable LPC channel y
 * IBFIEy	Input Buffer Full IRQ Enable for LPC channel y
 * IRQxEy	Assert SerIRQ x for LPC channel y (Deprecated, use IDyIRQX, IRQXEy)
 * IDyIRQX	Use the specified 4-bit SerIRQ for LPC channel y
 * SELyIRQX	SerIRQ polarity for LPC channel y (low: 0, high: 1)
 * IRQXEy	Assert the SerIRQ specified in IDyIRQX for LPC channel y
 */

#define LPC_TYIRQX_LOW       0b00
#define LPC_TYIRQX_HIGH      0b01
#define LPC_TYIRQX_RSVD      0b10
#define LPC_TYIRQX_RISING    0b11

#define LPC_HICR0            0x000
#define     LPC_HICR0_LPC3E          BIT(7)
#define     LPC_HICR0_LPC2E          BIT(6)
#define     LPC_HICR0_LPC1E          BIT(5)
#define LPC_HICR2            0x008
#define     LPC_HICR2_IBFIE3         BIT(3)
#define     LPC_HICR2_IBFIE2         BIT(2)
#define     LPC_HICR2_IBFIE1         BIT(1)
#define LPC_HICR4            0x010
#define     LPC_HICR4_LADR12AS       BIT(7)
#define     LPC_HICR4_KCSENBL        BIT(2)
#define LPC_SIRQCR0	     0x070
/* IRQ{12,1}E1 are deprecated as of AST2600 A3 but necessary for prior chips */
#define     LPC_SIRQCR0_IRQ12E1	     BIT(1)
#define     LPC_SIRQCR0_IRQ1E1	     BIT(0)
#define LPC_HICR5	     0x080
#define     LPC_HICR5_ID3IRQX_MASK   GENMASK(23, 20)
#define     LPC_HICR5_ID3IRQX_SHIFT  20
#define     LPC_HICR5_ID2IRQX_MASK   GENMASK(19, 16)
#define     LPC_HICR5_ID2IRQX_SHIFT  16
#define     LPC_HICR5_SEL3IRQX       BIT(15)
#define     LPC_HICR5_IRQXE3         BIT(14)
#define     LPC_HICR5_SEL2IRQX       BIT(13)
#define     LPC_HICR5_IRQXE2         BIT(12)
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
#define     LPC_HICRB_EN16LADR2      BIT(5)
#define     LPC_HICRB_EN16LADR1      BIT(4)
#define     LPC_HICRB_IBFIE4         BIT(1)
#define     LPC_HICRB_LPC4E          BIT(0)
#define LPC_HICRC            0x104
#define     LPC_HICRC_ID4IRQX_MASK   GENMASK(7, 4)
#define     LPC_HICRC_ID4IRQX_SHIFT  4
#define     LPC_HICRC_TY4IRQX_MASK   GENMASK(3, 2)
#define     LPC_HICRC_TY4IRQX_SHIFT  2
#define     LPC_HICRC_OBF4_AUTO_CLR  BIT(1)
#define     LPC_HICRC_IRQXE4         BIT(0)
#define LPC_LADR4            0x110
#define LPC_IDR4             0x114
#define LPC_ODR4             0x118
#define LPC_STR4             0x11C
#define LPC_LSADR12	     0x120
#define     LPC_LSADR12_LSADR2_MASK  GENMASK(31, 16)
#define     LPC_LSADR12_LSADR2_SHIFT 16
#define     LPC_LSADR12_LSADR1_MASK  GENMASK(15, 0)
#define     LPC_LSADR12_LSADR1_SHIFT 0

#define OBE_POLL_PERIOD	     (HZ / 2)

enum aspeed_kcs_irq_mode {
	aspeed_kcs_irq_none,
	aspeed_kcs_irq_serirq,
};

struct aspeed_kcs_bmc {
	struct kcs_bmc_device kcs_bmc;

	struct regmap *map;

	struct {
		enum aspeed_kcs_irq_mode mode;
		int id;
	} upstream_irq;

	struct {
		spinlock_t lock;
		bool remove;
		struct timer_list timer;
	} obe;
};

static inline struct aspeed_kcs_bmc *to_aspeed_kcs_bmc(struct kcs_bmc_device *kcs_bmc)
{
	return container_of(kcs_bmc, struct aspeed_kcs_bmc, kcs_bmc);
}

static u8 aspeed_kcs_inb(struct kcs_bmc_device *kcs_bmc, u32 reg)
{
	struct aspeed_kcs_bmc *priv = to_aspeed_kcs_bmc(kcs_bmc);
	u32 val = 0;
	int rc;

	rc = regmap_read(priv->map, reg, &val);
	WARN(rc != 0, "regmap_read() failed: %d\n", rc);

	return rc == 0 ? (u8) val : 0;
}

static void aspeed_kcs_outb(struct kcs_bmc_device *kcs_bmc, u32 reg, u8 data)
{
	struct aspeed_kcs_bmc *priv = to_aspeed_kcs_bmc(kcs_bmc);
	int rc;

	rc = regmap_write(priv->map, reg, data);
	WARN(rc != 0, "regmap_write() failed: %d\n", rc);

	/* Trigger the upstream IRQ on ODR writes, if enabled */

	switch (reg) {
	case LPC_ODR1:
	case LPC_ODR2:
	case LPC_ODR3:
	case LPC_ODR4:
		break;
	default:
		return;
	}

	if (priv->upstream_irq.mode != aspeed_kcs_irq_serirq)
		return;

	switch (kcs_bmc->channel) {
	case 1:
		switch (priv->upstream_irq.id) {
		case 12:
			regmap_update_bits(priv->map, LPC_SIRQCR0, LPC_SIRQCR0_IRQ12E1,
					   LPC_SIRQCR0_IRQ12E1);
			break;
		case 1:
			regmap_update_bits(priv->map, LPC_SIRQCR0, LPC_SIRQCR0_IRQ1E1,
					   LPC_SIRQCR0_IRQ1E1);
			break;
		default:
			break;
		}
		break;
	case 2:
		regmap_update_bits(priv->map, LPC_HICR5, LPC_HICR5_IRQXE2, LPC_HICR5_IRQXE2);
		break;
	case 3:
		regmap_update_bits(priv->map, LPC_HICR5, LPC_HICR5_IRQXE3, LPC_HICR5_IRQXE3);
		break;
	case 4:
		regmap_update_bits(priv->map, LPC_HICRC, LPC_HICRC_IRQXE4, LPC_HICRC_IRQXE4);
		break;
	default:
		break;
	}
}

static void aspeed_kcs_updateb(struct kcs_bmc_device *kcs_bmc, u32 reg, u8 mask, u8 val)
{
	struct aspeed_kcs_bmc *priv = to_aspeed_kcs_bmc(kcs_bmc);
	int rc;

	rc = regmap_update_bits(priv->map, reg, mask, val);
	WARN(rc != 0, "regmap_update_bits() failed: %d\n", rc);
}

/*
 * We note D for Data, and C for Cmd/Status, default rules are
 *
 * 1. Only the D address is given:
 *   A. KCS1/KCS2 (D/C: X/X+4)
 *      D/C: CA0h/CA4h
 *      D/C: CA8h/CACh
 *   B. KCS3 (D/C: XX2/XX3h)
 *      D/C: CA2h/CA3h
 *   C. KCS4 (D/C: X/X+1)
 *      D/C: CA4h/CA5h
 *
 * 2. Both the D/C addresses are given:
 *   A. KCS1/KCS2/KCS4 (D/C: X/Y)
 *      D/C: CA0h/CA1h
 *      D/C: CA8h/CA9h
 *      D/C: CA4h/CA5h
 *   B. KCS3 (D/C: XX2/XX3h)
 *      D/C: CA2h/CA3h
 */
static int aspeed_kcs_set_address(struct kcs_bmc_device *kcs_bmc, u32 addrs[2], int nr_addrs)
{
	struct aspeed_kcs_bmc *priv = to_aspeed_kcs_bmc(kcs_bmc);

	if (WARN_ON(nr_addrs < 1 || nr_addrs > 2))
		return -EINVAL;

	switch (priv->kcs_bmc.channel) {
	case 1:
		regmap_update_bits(priv->map, LPC_HICR4, LPC_HICR4_LADR12AS, 0);
		regmap_write(priv->map, LPC_LADR12H, addrs[0] >> 8);
		regmap_write(priv->map, LPC_LADR12L, addrs[0] & 0xFF);
		if (nr_addrs == 2) {
			regmap_update_bits(priv->map, LPC_LSADR12, LPC_LSADR12_LSADR1_MASK,
					   addrs[1] << LPC_LSADR12_LSADR1_SHIFT);

			regmap_update_bits(priv->map, LPC_HICRB, LPC_HICRB_EN16LADR1,
					   LPC_HICRB_EN16LADR1);
		}
		break;

	case 2:
		regmap_update_bits(priv->map, LPC_HICR4, LPC_HICR4_LADR12AS, LPC_HICR4_LADR12AS);
		regmap_write(priv->map, LPC_LADR12H, addrs[0] >> 8);
		regmap_write(priv->map, LPC_LADR12L, addrs[0] & 0xFF);
		if (nr_addrs == 2) {
			regmap_update_bits(priv->map, LPC_LSADR12, LPC_LSADR12_LSADR2_MASK,
					   addrs[1] << LPC_LSADR12_LSADR2_SHIFT);

			regmap_update_bits(priv->map, LPC_HICRB, LPC_HICRB_EN16LADR2,
					   LPC_HICRB_EN16LADR2);
		}
		break;

	case 3:
		if (nr_addrs == 2) {
			dev_err(priv->kcs_bmc.dev,
				"Channel 3 only supports inferred status IO address\n");
			return -EINVAL;
		}

		regmap_write(priv->map, LPC_LADR3H, addrs[0] >> 8);
		regmap_write(priv->map, LPC_LADR3L, addrs[0] & 0xFF);
		break;

	case 4:
		if (nr_addrs == 1)
			regmap_write(priv->map, LPC_LADR4, ((addrs[0] + 1) << 16) | addrs[0]);
		else
			regmap_write(priv->map, LPC_LADR4, (addrs[1] << 16) | addrs[0]);

		break;

	default:
		return -EINVAL;
	}

	return 0;
}

static inline int aspeed_kcs_map_serirq_type(u32 dt_type)
{
	switch (dt_type) {
	case IRQ_TYPE_EDGE_RISING:
		return LPC_TYIRQX_RISING;
	case IRQ_TYPE_LEVEL_HIGH:
		return LPC_TYIRQX_HIGH;
	case IRQ_TYPE_LEVEL_LOW:
		return LPC_TYIRQX_LOW;
	default:
		return -EINVAL;
	}
}

static int aspeed_kcs_config_upstream_irq(struct aspeed_kcs_bmc *priv, u32 id, u32 dt_type)
{
	unsigned int mask, val, hw_type;
	int ret;

	if (id > 15)
		return -EINVAL;

	ret = aspeed_kcs_map_serirq_type(dt_type);
	if (ret < 0)
		return ret;
	hw_type = ret;

	priv->upstream_irq.mode = aspeed_kcs_irq_serirq;
	priv->upstream_irq.id = id;

	switch (priv->kcs_bmc.channel) {
	case 1:
		/* Needs IRQxE1 rather than (ID1IRQX, SEL1IRQX, IRQXE1) before AST2600 A3 */
		break;
	case 2:
		if (!(hw_type == LPC_TYIRQX_LOW || hw_type == LPC_TYIRQX_HIGH))
			return -EINVAL;

		mask = LPC_HICR5_SEL2IRQX | LPC_HICR5_ID2IRQX_MASK;
		val = (id << LPC_HICR5_ID2IRQX_SHIFT);
		val |= (hw_type == LPC_TYIRQX_HIGH) ? LPC_HICR5_SEL2IRQX : 0;
		regmap_update_bits(priv->map, LPC_HICR5, mask, val);

		break;
	case 3:
		if (!(hw_type == LPC_TYIRQX_LOW || hw_type == LPC_TYIRQX_HIGH))
			return -EINVAL;

		mask = LPC_HICR5_SEL3IRQX | LPC_HICR5_ID3IRQX_MASK;
		val = (id << LPC_HICR5_ID3IRQX_SHIFT);
		val |= (hw_type == LPC_TYIRQX_HIGH) ? LPC_HICR5_SEL3IRQX : 0;
		regmap_update_bits(priv->map, LPC_HICR5, mask, val);

		break;
	case 4:
		mask = LPC_HICRC_ID4IRQX_MASK | LPC_HICRC_TY4IRQX_MASK | LPC_HICRC_OBF4_AUTO_CLR;
		val = (id << LPC_HICRC_ID4IRQX_SHIFT) | (hw_type << LPC_HICRC_TY4IRQX_SHIFT);
		regmap_update_bits(priv->map, LPC_HICRC, mask, val);
		break;
	default:
		dev_warn(priv->kcs_bmc.dev,
			 "SerIRQ configuration not supported on KCS channel %d\n",
			 priv->kcs_bmc.channel);
		return -EINVAL;
	}

	return 0;
}

static void aspeed_kcs_enable_channel(struct kcs_bmc_device *kcs_bmc, bool enable)
{
	struct aspeed_kcs_bmc *priv = to_aspeed_kcs_bmc(kcs_bmc);

	switch (kcs_bmc->channel) {
	case 1:
		regmap_update_bits(priv->map, LPC_HICR0, LPC_HICR0_LPC1E, enable * LPC_HICR0_LPC1E);
		return;
	case 2:
		regmap_update_bits(priv->map, LPC_HICR0, LPC_HICR0_LPC2E, enable * LPC_HICR0_LPC2E);
		return;
	case 3:
		regmap_update_bits(priv->map, LPC_HICR0, LPC_HICR0_LPC3E, enable * LPC_HICR0_LPC3E);
		regmap_update_bits(priv->map, LPC_HICR4,
				   LPC_HICR4_KCSENBL, enable * LPC_HICR4_KCSENBL);
		return;
	case 4:
		regmap_update_bits(priv->map, LPC_HICRB, LPC_HICRB_LPC4E, enable * LPC_HICRB_LPC4E);
		return;
	default:
		pr_warn("%s: Unsupported channel: %d", __func__, kcs_bmc->channel);
		return;
	}
}

static void aspeed_kcs_check_obe(struct timer_list *timer)
{
	struct aspeed_kcs_bmc *priv = container_of(timer, struct aspeed_kcs_bmc, obe.timer);
	unsigned long flags;
	u8 str;

	spin_lock_irqsave(&priv->obe.lock, flags);
	if (priv->obe.remove) {
		spin_unlock_irqrestore(&priv->obe.lock, flags);
		return;
	}

	str = aspeed_kcs_inb(&priv->kcs_bmc, priv->kcs_bmc.ioreg.str);
	if (str & KCS_BMC_STR_OBF) {
		mod_timer(timer, jiffies + OBE_POLL_PERIOD);
		spin_unlock_irqrestore(&priv->obe.lock, flags);
		return;
	}
	spin_unlock_irqrestore(&priv->obe.lock, flags);

	kcs_bmc_handle_event(&priv->kcs_bmc);
}

static void aspeed_kcs_irq_mask_update(struct kcs_bmc_device *kcs_bmc, u8 mask, u8 state)
{
	struct aspeed_kcs_bmc *priv = to_aspeed_kcs_bmc(kcs_bmc);

	/* We don't have an OBE IRQ, emulate it */
	if (mask & KCS_BMC_EVENT_TYPE_OBE) {
		if (KCS_BMC_EVENT_TYPE_OBE & state)
			mod_timer(&priv->obe.timer, jiffies + OBE_POLL_PERIOD);
		else
			del_timer(&priv->obe.timer);
	}

	if (mask & KCS_BMC_EVENT_TYPE_IBF) {
		const bool enable = !!(state & KCS_BMC_EVENT_TYPE_IBF);

		switch (kcs_bmc->channel) {
		case 1:
			regmap_update_bits(priv->map, LPC_HICR2, LPC_HICR2_IBFIE1,
					   enable * LPC_HICR2_IBFIE1);
			return;
		case 2:
			regmap_update_bits(priv->map, LPC_HICR2, LPC_HICR2_IBFIE2,
					   enable * LPC_HICR2_IBFIE2);
			return;
		case 3:
			regmap_update_bits(priv->map, LPC_HICR2, LPC_HICR2_IBFIE3,
					   enable * LPC_HICR2_IBFIE3);
			return;
		case 4:
			regmap_update_bits(priv->map, LPC_HICRB, LPC_HICRB_IBFIE4,
					   enable * LPC_HICRB_IBFIE4);
			return;
		default:
			pr_warn("%s: Unsupported channel: %d", __func__, kcs_bmc->channel);
			return;
		}
	}
}

static const struct kcs_bmc_device_ops aspeed_kcs_ops = {
	.irq_mask_update = aspeed_kcs_irq_mask_update,
	.io_inputb = aspeed_kcs_inb,
	.io_outputb = aspeed_kcs_outb,
	.io_updateb = aspeed_kcs_updateb,
};

static irqreturn_t aspeed_kcs_irq(int irq, void *arg)
{
	struct kcs_bmc_device *kcs_bmc = arg;

	return kcs_bmc_handle_event(kcs_bmc);
}

static int aspeed_kcs_config_downstream_irq(struct kcs_bmc_device *kcs_bmc,
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

static int aspeed_kcs_of_get_channel(struct platform_device *pdev)
{
	struct device_node *np;
	struct kcs_ioreg ioreg;
	const __be32 *reg;
	int i;

	np = pdev->dev.of_node;

	/* Don't translate addresses, we want offsets for the regmaps */
	reg = of_get_address(np, 0, NULL, NULL);
	if (!reg)
		return -EINVAL;
	ioreg.idr = be32_to_cpup(reg);

	reg = of_get_address(np, 1, NULL, NULL);
	if (!reg)
		return -EINVAL;
	ioreg.odr = be32_to_cpup(reg);

	reg = of_get_address(np, 2, NULL, NULL);
	if (!reg)
		return -EINVAL;
	ioreg.str = be32_to_cpup(reg);

	for (i = 0; i < ARRAY_SIZE(ast_kcs_bmc_ioregs); i++) {
		if (!memcmp(&ast_kcs_bmc_ioregs[i], &ioreg, sizeof(ioreg)))
			return i + 1;
	}
	return -EINVAL;
}

static int
aspeed_kcs_of_get_io_address(struct platform_device *pdev, u32 addrs[2])
{
	int rc;

	rc = of_property_read_variable_u32_array(pdev->dev.of_node,
						 "aspeed,lpc-io-reg",
						 addrs, 1, 2);
	if (rc < 0) {
		dev_err(&pdev->dev, "No valid 'aspeed,lpc-io-reg' configured\n");
		return rc;
	}

	if (addrs[0] > 0xffff) {
		dev_err(&pdev->dev, "Invalid data address in 'aspeed,lpc-io-reg'\n");
		return -EINVAL;
	}

	if (rc == 2 && addrs[1] > 0xffff) {
		dev_err(&pdev->dev, "Invalid status address in 'aspeed,lpc-io-reg'\n");
		return -EINVAL;
	}

	return rc;
}

static int aspeed_kcs_probe(struct platform_device *pdev)
{
	struct kcs_bmc_device *kcs_bmc;
	struct aspeed_kcs_bmc *priv;
	struct device_node *np;
	bool have_upstream_irq;
	u32 upstream_irq[2];
	int rc, channel;
	int nr_addrs;
	u32 addrs[2];

	np = pdev->dev.of_node->parent;
	if (!of_device_is_compatible(np, "aspeed,ast2400-lpc-v2") &&
	    !of_device_is_compatible(np, "aspeed,ast2500-lpc-v2") &&
	    !of_device_is_compatible(np, "aspeed,ast2600-lpc-v2")) {
		dev_err(&pdev->dev, "unsupported LPC device binding\n");
		return -ENODEV;
	}

	channel = aspeed_kcs_of_get_channel(pdev);
	if (channel < 0)
		return channel;

	nr_addrs = aspeed_kcs_of_get_io_address(pdev, addrs);
	if (nr_addrs < 0)
		return nr_addrs;

	np = pdev->dev.of_node;
	rc = of_property_read_u32_array(np, "aspeed,lpc-interrupts", upstream_irq, 2);
	if (rc && rc != -EINVAL)
		return -EINVAL;

	have_upstream_irq = !rc;

	priv = devm_kzalloc(&pdev->dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	kcs_bmc = &priv->kcs_bmc;
	kcs_bmc->dev = &pdev->dev;
	kcs_bmc->channel = channel;
	kcs_bmc->ioreg = ast_kcs_bmc_ioregs[channel - 1];
	kcs_bmc->ops = &aspeed_kcs_ops;

	priv->map = syscon_node_to_regmap(pdev->dev.parent->of_node);
	if (IS_ERR(priv->map)) {
		dev_err(&pdev->dev, "Couldn't get regmap\n");
		return -ENODEV;
	}

	spin_lock_init(&priv->obe.lock);
	priv->obe.remove = false;
	timer_setup(&priv->obe.timer, aspeed_kcs_check_obe, 0);

	rc = aspeed_kcs_set_address(kcs_bmc, addrs, nr_addrs);
	if (rc)
		return rc;

	/* Host to BMC IRQ */
	rc = aspeed_kcs_config_downstream_irq(kcs_bmc, pdev);
	if (rc)
		return rc;

	/* BMC to Host IRQ */
	if (have_upstream_irq) {
		rc = aspeed_kcs_config_upstream_irq(priv, upstream_irq[0], upstream_irq[1]);
		if (rc < 0)
			return rc;
	} else {
		priv->upstream_irq.mode = aspeed_kcs_irq_none;
	}

	platform_set_drvdata(pdev, priv);

	aspeed_kcs_irq_mask_update(kcs_bmc, (KCS_BMC_EVENT_TYPE_IBF | KCS_BMC_EVENT_TYPE_OBE), 0);
	aspeed_kcs_enable_channel(kcs_bmc, true);

	rc = kcs_bmc_add_device(&priv->kcs_bmc);
	if (rc) {
		dev_warn(&pdev->dev, "Failed to register channel %d: %d\n", kcs_bmc->channel, rc);
		return rc;
	}

	dev_info(&pdev->dev, "Initialised channel %d at 0x%x\n",
			kcs_bmc->channel, addrs[0]);

	return 0;
}

static int aspeed_kcs_remove(struct platform_device *pdev)
{
	struct aspeed_kcs_bmc *priv = platform_get_drvdata(pdev);
	struct kcs_bmc_device *kcs_bmc = &priv->kcs_bmc;

	kcs_bmc_remove_device(kcs_bmc);

	aspeed_kcs_enable_channel(kcs_bmc, false);
	aspeed_kcs_irq_mask_update(kcs_bmc, (KCS_BMC_EVENT_TYPE_IBF | KCS_BMC_EVENT_TYPE_OBE), 0);

	/* Make sure it's proper dead */
	spin_lock_irq(&priv->obe.lock);
	priv->obe.remove = true;
	spin_unlock_irq(&priv->obe.lock);
	del_timer_sync(&priv->obe.timer);

	return 0;
}

static const struct of_device_id ast_kcs_bmc_match[] = {
	{ .compatible = "aspeed,ast2400-kcs-bmc-v2" },
	{ .compatible = "aspeed,ast2500-kcs-bmc-v2" },
	{ .compatible = "aspeed,ast2600-kcs-bmc" },
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
MODULE_AUTHOR("Andrew Jeffery <andrew@aj.id.au>");
MODULE_DESCRIPTION("Aspeed device interface to the KCS BMC device");
