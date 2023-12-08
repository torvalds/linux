// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2015-2018, Intel Corporation.
 * Copyright (c) 2023, Aspeed Technology Inc.
 */
#define pr_fmt(fmt) "aspeed-kcs-bmc: " fmt

#include <linux/atomic.h>
#include <linux/errno.h>
#include <linux/interrupt.h>
#include <linux/bitfield.h>
#include <linux/io.h>
#include <linux/irq.h>
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

#include "kcs_bmc_device.h"

#define DEVICE_NAME	"aspeed-kcs-bmc"

static DEFINE_IDA(aspeed_kcs_bmc_ida);

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
#define HICR0			0x000
#define   HICR0_LPC3E		BIT(7)
#define   HICR0_LPC2E		BIT(6)
#define   HICR0_LPC1E		BIT(5)
#define HICR2			0x008
#define   HICR2_IBFIE3		BIT(3)
#define   HICR2_IBFIE2		BIT(2)
#define   HICR2_IBFIE1		BIT(1)
#define HICR4			0x010
#define   HICR4_LADR12AS	BIT(7)
#define   HICR4_KCSENBL		BIT(2)
#define LADR3H			0x014
#define LADR3L			0x018
#define LADR12H			0x01C
#define LADR12L			0x020
#define IDR1			0x024
#define IDR2			0x028
#define IDR3			0x02C
#define ODR1			0x030
#define ODR2			0x034
#define ODR3			0x038
#define STR1			0x03C
#define STR2			0x040
#define STR3			0x044
#define SIRQCR0			0x070
/* IRQ{12,1}E1 are deprecated as of AST2600 A3 but necessary for prior chips */
#define   SIRQCR0_IRQ12E1	BIT(1)
#define   SIRQCR0_IRQ1E1	BIT(0)
#define HICR5			0x080
#define   HICR5_ID3IRQX		GENMASK(23, 20)
#define   HICR5_ID2IRQX		GENMASK(19, 16)
#define   HICR5_SEL3IRQX	BIT(15)
#define   HICR5_IRQXE3		BIT(14)
#define   HICR5_SEL2IRQX	BIT(13)
#define   HICR5_IRQXE2		BIT(12)
#define HICRB			0x100
#define   HICRB_EN16LADR2	BIT(5)
#define   HICRB_EN16LADR1	BIT(4)
#define   HICRB_IBFIE4		BIT(1)
#define   HICRB_LPC4E		BIT(0)
#define HICRC			0x104
#define   HICRC_ID4IRQX		GENMASK(7, 4)
#define   HICRC_SEL4IRQX	BIT(2)
#define   HICRC_OBF4_AUTO_CLR	BIT(1)
#define   HICRC_IRQXE4		BIT(0)
#define LADR4			0x110
#define IDR4			0x114
#define ODR4			0x118
#define STR4			0x11C
#define LSADR12			0x120
#define   LSADR12_LSADR2	GENMASK(31, 16)
#define   LSADR12_LSADR1	GENMASK(15, 0)

#define KCS_HW_INSTANCE_NUM	4
#define KCS_OBE_POLL_PERIOD	(HZ / 2)

struct aspeed_kcs_bmc {
	struct kcs_bmc_device kcs_bmc;
	struct regmap *map;
	int irq;

	u32 io_addr;
	u32 hw_inst;

	struct {
		u32 id;
		u32 type;
	} sirq;

	struct {
		spinlock_t lock;
		bool remove;
		struct timer_list timer;
	} obe;
};

static const struct kcs_ioreg aspeed_kcs_ioregs[KCS_HW_INSTANCE_NUM] = {
	{ .idr = IDR1, .odr = ODR1, .str = STR1 },
	{ .idr = IDR2, .odr = ODR2, .str = STR2 },
	{ .idr = IDR3, .odr = ODR3, .str = STR3 },
	{ .idr = IDR4, .odr = ODR4, .str = STR4 },
};

static inline struct aspeed_kcs_bmc *to_aspeed_kcs_bmc(struct kcs_bmc_device *kcs_bmc)
{
	return container_of(kcs_bmc, struct aspeed_kcs_bmc, kcs_bmc);
}

static u8 aspeed_kcs_inb(struct kcs_bmc_device *kcs_bmc, u32 reg)
{
	struct aspeed_kcs_bmc *kcs_aspeed = to_aspeed_kcs_bmc(kcs_bmc);
	u32 val = 0;
	int rc;

	rc = regmap_read(kcs_aspeed->map, reg, &val);
	WARN(rc != 0, "regmap_read() failed: %d\n", rc);

	return rc == 0 ? (u8) val : 0;
}

static void aspeed_kcs_outb(struct kcs_bmc_device *kcs_bmc, u32 reg, u8 data)
{
	struct aspeed_kcs_bmc *kcs_aspeed = to_aspeed_kcs_bmc(kcs_bmc);
	int rc;

	rc = regmap_write(kcs_aspeed->map, reg, data);
	WARN(rc != 0, "regmap_write() failed: %d\n", rc);

	/* Trigger the upstream IRQ on ODR writes, if enabled */

	switch (reg) {
	case ODR1:
	case ODR2:
	case ODR3:
	case ODR4:
		break;
	default:
		return;
	}

	if (kcs_aspeed->sirq.type == IRQ_TYPE_NONE)
		return;

	switch (kcs_aspeed->hw_inst) {
	case 0:
		switch (kcs_aspeed->sirq.id) {
		case 12:
			regmap_update_bits(kcs_aspeed->map, SIRQCR0, SIRQCR0_IRQ12E1,
					   SIRQCR0_IRQ12E1);
			break;
		case 1:
			regmap_update_bits(kcs_aspeed->map, SIRQCR0, SIRQCR0_IRQ1E1,
					   SIRQCR0_IRQ1E1);
			break;
		default:
			break;
		}
		break;
	case 1:
		regmap_update_bits(kcs_aspeed->map, HICR5, HICR5_IRQXE2, HICR5_IRQXE2);
		break;
	case 2:
		regmap_update_bits(kcs_aspeed->map, HICR5, HICR5_IRQXE3, HICR5_IRQXE3);
		break;
	case 3:
		regmap_update_bits(kcs_aspeed->map, HICRC, HICRC_IRQXE4, HICRC_IRQXE4);
		break;
	default:
		break;
	}
}

static void aspeed_kcs_updateb(struct kcs_bmc_device *kcs_bmc, u32 reg, u8 mask, u8 val)
{
	struct aspeed_kcs_bmc *kcs_aspeed = to_aspeed_kcs_bmc(kcs_bmc);
	int rc;

	rc = regmap_update_bits(kcs_aspeed->map, reg, mask, val);
	WARN(rc != 0, "regmap_update_bits() failed: %d\n", rc);
}

static void aspeed_kcs_irq_mask_update(struct kcs_bmc_device *kcs_bmc, u8 mask, u8 state)
{
	struct aspeed_kcs_bmc *kcs_aspeed = to_aspeed_kcs_bmc(kcs_bmc);
	int rc;
	u8 str;

	/* We don't have an OBE IRQ, emulate it */
	if (mask & KCS_BMC_EVENT_TYPE_OBE) {
		if (KCS_BMC_EVENT_TYPE_OBE & state) {
			/*
			 * Given we don't have an OBE IRQ, delay by polling briefly to see if we can
			 * observe such an event before returning to the caller. This is not
			 * incorrect because OBF may have already become clear before enabling the
			 * IRQ if we had one, under which circumstance no event will be propagated
			 * anyway.
			 *
			 * The onus is on the client to perform a race-free check that it hasn't
			 * missed the event.
			 */
			rc = read_poll_timeout_atomic(aspeed_kcs_inb, str,
						      !(str & KCS_BMC_STR_OBF), 1, 100, false,
						      &kcs_aspeed->kcs_bmc, kcs_aspeed->kcs_bmc.ioreg.str);
			/* Time for the slow path? */
			if (rc == -ETIMEDOUT)
				mod_timer(&kcs_aspeed->obe.timer, jiffies + KCS_OBE_POLL_PERIOD);
		} else {
			del_timer(&kcs_aspeed->obe.timer);
		}
	}

	if (mask & KCS_BMC_EVENT_TYPE_IBF) {
		const bool enable = !!(state & KCS_BMC_EVENT_TYPE_IBF);

		switch (kcs_aspeed->hw_inst) {
		case 0:
			regmap_update_bits(kcs_aspeed->map, HICR2, HICR2_IBFIE1,
					   enable * HICR2_IBFIE1);
			return;
		case 1:
			regmap_update_bits(kcs_aspeed->map, HICR2, HICR2_IBFIE2,
					   enable * HICR2_IBFIE2);
			return;
		case 2:
			regmap_update_bits(kcs_aspeed->map, HICR2, HICR2_IBFIE3,
					   enable * HICR2_IBFIE3);
			return;
		case 3:
			regmap_update_bits(kcs_aspeed->map, HICRB, HICRB_IBFIE4,
					   enable * HICRB_IBFIE4);
			return;
		default:
			pr_warn("%s: Unsupported channel: %d", __func__, kcs_bmc->channel);
			return;
		}
	}
}

static const struct kcs_bmc_device_ops aspeed_kcs_ops = {
	.io_inputb = aspeed_kcs_inb,
	.io_outputb = aspeed_kcs_outb,
	.io_updateb = aspeed_kcs_updateb,
	.irq_mask_update = aspeed_kcs_irq_mask_update,
};

/*
 * Follow IPMI v2.0, given a KCS IO base X,
 * the Data and Cmd/Status IO addresses are X and X+1.
 *
 * Note that the IO base of KCS channel 3/7/11/... must ends with 2
 * e.g. CA2h for KCS#3
 */
static int aspeed_kcs_config_io_address(struct aspeed_kcs_bmc *kcs_aspeed)
{
	u32 io_addr;

	io_addr = kcs_aspeed->io_addr;

	switch (kcs_aspeed->hw_inst) {
	case 0:
		regmap_update_bits(kcs_aspeed->map, HICR4, HICR4_LADR12AS, 0);
		regmap_write(kcs_aspeed->map, LADR12H, io_addr >> 8);
		regmap_write(kcs_aspeed->map, LADR12L, io_addr & 0xFF);
		regmap_update_bits(kcs_aspeed->map, LSADR12, LSADR12_LSADR1,
				   FIELD_PREP(LSADR12_LSADR1, io_addr + 1));
		regmap_update_bits(kcs_aspeed->map, HICRB, HICRB_EN16LADR1,
				   HICRB_EN16LADR1);
		break;
	case 1:
		regmap_update_bits(kcs_aspeed->map, HICR4, HICR4_LADR12AS, HICR4_LADR12AS);
		regmap_write(kcs_aspeed->map, LADR12H, io_addr >> 8);
		regmap_write(kcs_aspeed->map, LADR12L, io_addr & 0xFF);
		regmap_update_bits(kcs_aspeed->map, LSADR12, LSADR12_LSADR2,
				   FIELD_PREP(LSADR12_LSADR2, io_addr + 1));
		regmap_update_bits(kcs_aspeed->map, HICRB, HICRB_EN16LADR2,
				   HICRB_EN16LADR2);
		break;
	case 2:
		regmap_write(kcs_aspeed->map, LADR3H, io_addr >> 8);
		regmap_write(kcs_aspeed->map, LADR3L, io_addr & 0xFF);
		break;
	case 3:
		regmap_write(kcs_aspeed->map, LADR4, ((io_addr + 1) << 16) | io_addr);
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

static int aspeed_kcs_config_upstream_serirq(struct aspeed_kcs_bmc *kcs_aspeed)
{
	unsigned int mask, val;
	u32 sirq_id, sirq_type;

	if (kcs_aspeed->sirq.type == IRQ_TYPE_NONE)
		return 0;

	sirq_id = kcs_aspeed->sirq.id;
	sirq_type = kcs_aspeed->sirq.type;

	switch (kcs_aspeed->hw_inst) {
	case 0:
		/* Needs IRQxE1 rather than (ID1IRQX, SEL1IRQX, IRQXE1) before AST2600 A3 */
		break;
	case 1:
		mask = HICR5_SEL2IRQX | HICR5_ID2IRQX;
		val = FIELD_PREP(HICR5_ID2IRQX, sirq_id);
		val |= (sirq_type == IRQ_TYPE_LEVEL_HIGH) ? HICR5_SEL2IRQX : 0;
		regmap_update_bits(kcs_aspeed->map, HICR5, mask, val);
		break;
	case 2:
		mask = HICR5_SEL3IRQX | HICR5_ID3IRQX;
		val = FIELD_PREP(HICR5_ID3IRQX, sirq_id);
		val |= (sirq_type == IRQ_TYPE_LEVEL_HIGH) ? HICR5_SEL3IRQX : 0;
		regmap_update_bits(kcs_aspeed->map, HICR5, mask, val);
		break;
	case 3:
		mask = HICRC_ID4IRQX | HICRC_SEL4IRQX | HICRC_OBF4_AUTO_CLR;
		val = FIELD_PREP(HICRC_ID4IRQX, sirq_id);
		val |= (sirq_type == IRQ_TYPE_LEVEL_HIGH) ? HICRC_SEL4IRQX : 0;
		regmap_update_bits(kcs_aspeed->map, HICRC, mask, val);
		break;
	default:
		dev_warn(kcs_aspeed->kcs_bmc.dev,
			 "SerIRQ configuration not supported on KCS channel %d\n",
			 kcs_aspeed->kcs_bmc.channel);
		return -EINVAL;
	}

	return 0;
}

static int aspeed_kcs_enable_channel(struct aspeed_kcs_bmc *kcs_aspeed, bool enable)
{
	switch (kcs_aspeed->hw_inst) {
	case 0:
		regmap_update_bits(kcs_aspeed->map, HICR0, HICR0_LPC1E, enable * HICR0_LPC1E);
		break;
	case 1:
		regmap_update_bits(kcs_aspeed->map, HICR0, HICR0_LPC2E, enable * HICR0_LPC2E);
		break;
	case 2:
		regmap_update_bits(kcs_aspeed->map, HICR0, HICR0_LPC3E, enable * HICR0_LPC3E);
		regmap_update_bits(kcs_aspeed->map, HICR4, HICR4_KCSENBL, enable * HICR4_KCSENBL);
		break;
	case 3:
		regmap_update_bits(kcs_aspeed->map, HICRB, HICRB_LPC4E, enable * HICRB_LPC4E);
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

static void aspeed_kcs_check_obe(struct timer_list *timer)
{
	struct aspeed_kcs_bmc *kcs_aspeed = container_of(timer, struct aspeed_kcs_bmc, obe.timer);
	unsigned long flags;
	u8 str;

	spin_lock_irqsave(&kcs_aspeed->obe.lock, flags);
	if (kcs_aspeed->obe.remove) {
		spin_unlock_irqrestore(&kcs_aspeed->obe.lock, flags);
		return;
	}

	str = aspeed_kcs_inb(&kcs_aspeed->kcs_bmc, kcs_aspeed->kcs_bmc.ioreg.str);
	if (str & KCS_BMC_STR_OBF) {
		mod_timer(timer, jiffies + KCS_OBE_POLL_PERIOD);
		spin_unlock_irqrestore(&kcs_aspeed->obe.lock, flags);
		return;
	}
	spin_unlock_irqrestore(&kcs_aspeed->obe.lock, flags);

	kcs_bmc_handle_event(&kcs_aspeed->kcs_bmc);
}

static irqreturn_t aspeed_kcs_isr(int irq, void *arg)
{
	struct kcs_bmc_device *kcs_bmc = arg;

	return kcs_bmc_handle_event(kcs_bmc);
}

static int aspeed_kcs_probe(struct platform_device *pdev)
{
	struct aspeed_kcs_bmc *kcs_aspeed;
	struct kcs_bmc_device *kcs_bmc;
	struct device *dev;
	const __be32 *reg;
	int i, rc, chan;

	dev = &pdev->dev;

	kcs_aspeed = devm_kzalloc(dev, sizeof(*kcs_aspeed), GFP_KERNEL);
	if (!kcs_aspeed)
		return -ENOMEM;

	kcs_bmc = &kcs_aspeed->kcs_bmc;
	kcs_bmc->ops = &aspeed_kcs_ops;
	kcs_bmc->dev = dev;

	kcs_aspeed->map = syscon_node_to_regmap(pdev->dev.parent->of_node);
	if (IS_ERR(kcs_aspeed->map)) {
		dev_err(&pdev->dev, "cannot get regmap\n");
		return -ENODEV;
	}

	kcs_aspeed->irq = platform_get_irq(pdev, 0);
	if (kcs_aspeed->irq < 0) {
		dev_err(dev, "cannot get IRQ number\n");
		return kcs_aspeed->irq;
	}

	reg = of_get_address(dev->of_node, 0, NULL, NULL);
	if (!reg) {
		dev_err(dev, "cannot get IDR\n");
		return -ENODEV;
	}

	kcs_bmc->ioreg.idr = be32_to_cpup(reg);

	reg = of_get_address(dev->of_node, 1, NULL, NULL);
	if (!reg) {
		dev_err(dev, "cannot get ODR\n");
		return -ENODEV;
	}

	kcs_bmc->ioreg.odr = be32_to_cpup(reg);

	reg = of_get_address(dev->of_node, 2, NULL, NULL);
	if (!reg) {
		dev_err(dev, "cannot get STR\n");
		return -ENODEV;
	}

	kcs_bmc->ioreg.str = be32_to_cpup(reg);

	for (i = 0; i < KCS_HW_INSTANCE_NUM; ++i) {
		if (aspeed_kcs_ioregs[i].idr == kcs_bmc->ioreg.idr &&
		    aspeed_kcs_ioregs[i].odr == kcs_bmc->ioreg.odr &&
		    aspeed_kcs_ioregs[i].str == kcs_bmc->ioreg.str) {
			kcs_aspeed->hw_inst = i;
			break;
		}
	}

	if (i >= KCS_HW_INSTANCE_NUM) {
		dev_err(dev, "invalid IDR/ODR/STR register\n");
		return -EINVAL;
	}

	rc = of_property_read_u32(dev->of_node, "kcs-io-addr", &kcs_aspeed->io_addr);
	if (rc || kcs_aspeed->io_addr > (USHRT_MAX - 1)) {
		dev_err(dev, "invalid IO address\n");
		return -ENODEV;
	}

	rc = of_property_read_u32(dev->of_node, "kcs-channel", &chan);
	if (rc) {
		chan = ida_alloc(&aspeed_kcs_bmc_ida, GFP_KERNEL);
		if (chan < 0) {
			dev_err(dev, "cannot allocate ID for KCS channel\n");
			return chan;
		}
	}

	kcs_bmc->channel = chan;

	rc = of_property_read_u32_array(dev->of_node, "kcs-upstream-serirq", (u32 *)&kcs_aspeed->sirq, 2);
	if (rc) {
		kcs_aspeed->sirq.type = IRQ_TYPE_NONE;
	} else {
		if (kcs_aspeed->sirq.id > 15) {
			dev_err(dev, "invalid SerIRQ number, expected sirq <= 15\n");
			return -EINVAL;
		}

		if (kcs_aspeed->sirq.type != IRQ_TYPE_LEVEL_HIGH &&
		    kcs_aspeed->sirq.type != IRQ_TYPE_LEVEL_LOW) {
			dev_err(dev, "invalid SerIRQ type, expected IRQ_TYPE_LEVEL_HIGH/LOW only\n");
			return -EINVAL;
		}
	}

	timer_setup(&kcs_aspeed->obe.timer, aspeed_kcs_check_obe, 0);
	spin_lock_init(&kcs_aspeed->obe.lock);
	kcs_aspeed->obe.remove = false;

	aspeed_kcs_irq_mask_update(kcs_bmc, (KCS_BMC_EVENT_TYPE_IBF | KCS_BMC_EVENT_TYPE_OBE), 0);

	rc = aspeed_kcs_config_io_address(kcs_aspeed);
	if (rc)
		return rc;

	rc = aspeed_kcs_config_upstream_serirq(kcs_aspeed);
	if (rc)
		return rc;

	rc = devm_request_irq(dev, kcs_aspeed->irq, aspeed_kcs_isr, IRQF_SHARED,
			      dev_name(dev), kcs_bmc);
	if (rc) {
		dev_err(dev, "cannot request IRQ\n");
		return rc;
	}

	platform_set_drvdata(pdev, kcs_aspeed);

	rc = aspeed_kcs_enable_channel(kcs_aspeed, true);
	if (rc) {
		dev_err(dev, "cannot enable channel %d: %d\n",
			kcs_bmc->channel, rc);
		return rc;
	}

	rc = kcs_bmc_add_device(kcs_bmc);
	if (rc) {
		dev_warn(dev, "cannot register channel %d: %d\n",
			 kcs_bmc->channel, rc);
		return rc;
	}

	dev_info(dev, "Initialised channel %d at IO address 0x%x\n",
		 kcs_bmc->channel, kcs_aspeed->io_addr);

	return 0;
}

static int aspeed_kcs_remove(struct platform_device *pdev)
{
	struct aspeed_kcs_bmc *kcs_aspeed = platform_get_drvdata(pdev);
	struct kcs_bmc_device *kcs_bmc = &kcs_aspeed->kcs_bmc;

	kcs_bmc_remove_device(kcs_bmc);

	aspeed_kcs_enable_channel(kcs_aspeed, false);
	aspeed_kcs_irq_mask_update(kcs_bmc, (KCS_BMC_EVENT_TYPE_IBF | KCS_BMC_EVENT_TYPE_OBE), 0);

	/* Make sure it's proper dead */
	spin_lock_irq(&kcs_aspeed->obe.lock);
	kcs_aspeed->obe.remove = true;
	spin_unlock_irq(&kcs_aspeed->obe.lock);
	del_timer_sync(&kcs_aspeed->obe.timer);

	return 0;
}

static const struct of_device_id aspeed_kcs_bmc_match[] = {
	{ .compatible = "aspeed,ast2400-kcs-bmc" },
	{ .compatible = "aspeed,ast2500-kcs-bmc" },
	{ .compatible = "aspeed,ast2600-kcs-bmc" },
	{ .compatible = "aspeed,ast2700-kcs-bmc" },
	{ }
};
MODULE_DEVICE_TABLE(of, aspeed_kcs_bmc_match);

static struct platform_driver aspeed_kcs_bmc_driver = {
	.driver = {
		.name		= DEVICE_NAME,
		.of_match_table = aspeed_kcs_bmc_match,
	},
	.probe	= aspeed_kcs_probe,
	.remove = aspeed_kcs_remove,
};
module_platform_driver(aspeed_kcs_bmc_driver);

MODULE_LICENSE("GPL v2");
MODULE_AUTHOR("Haiyue Wang <haiyue.wang@linux.intel.com>");
MODULE_AUTHOR("Andrew Jeffery <andrew@aj.id.au>");
MODULE_AUTHOR("Chia-Wei Wang <chiawei_wang@aspeedtech.com>");
MODULE_DESCRIPTION("Aspeed device interface to the KCS BMC device");
