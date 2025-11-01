// SPDX-License-Identifier: GPL-2.0-only
/*
 * Reset driver for the Mobileye EyeQ5, EyeQ6L and EyeQ6H platforms.
 *
 * Controllers live in a shared register region called OLB. EyeQ5 and EyeQ6L
 * have a single OLB instance for a single reset controller. EyeQ6H has seven
 * OLB instances; three host reset controllers.
 *
 * Each reset controller has one or more domain. Domains are of a given type
 * (see enum eqr_domain_type), with a valid offset mask (up to 32 resets per
 * domain).
 *
 * Domain types define expected behavior: one-register-per-reset,
 * one-bit-per-reset, status detection method, busywait duration, etc.
 *
 * We use eqr_ as prefix, as-in "EyeQ Reset", but way shorter.
 *
 * Known resets in EyeQ5 domain 0 (type EQR_EYEQ5_SARCR):
 *  3. CAN0	 4. CAN1	 5. CAN2	 6. SPI0
 *  7. SPI1	 8. SPI2	 9. SPI3	10. UART0
 * 11. UART1	12. UART2	13. I2C0	14. I2C1
 * 15. I2C2	16. I2C3	17. I2C4	18. TIMER0
 * 19. TIMER1	20. TIMER2	21. TIMER3	22. TIMER4
 * 23. WD0	24. EXT0	25. EXT1	26. GPIO
 * 27. WD1
 *
 * Known resets in EyeQ5 domain 1 (type EQR_EYEQ5_ACRP):
 *  0. VMP0	 1. VMP1	 2. VMP2	 3. VMP3
 *  4. PMA0	 5. PMA1	 6. PMAC0	 7. PMAC1
 *  8. MPC0	 9. MPC1	10. MPC2	11. MPC3
 * 12. MPC4
 *
 * Known resets in EyeQ5 domain 2 (type EQR_EYEQ5_PCIE):
 *  0. PCIE0_CORE	 1. PCIE0_APB		 2. PCIE0_LINK_AXI	 3. PCIE0_LINK_MGMT
 *  4. PCIE0_LINK_HOT	 5. PCIE0_LINK_PIPE	 6. PCIE1_CORE		 7. PCIE1_APB
 *  8. PCIE1_LINK_AXI	 9. PCIE1_LINK_MGMT	10. PCIE1_LINK_HOT	11. PCIE1_LINK_PIPE
 * 12. MULTIPHY		13. MULTIPHY_APB	15. PCIE0_LINK_MGMT	16. PCIE1_LINK_MGMT
 * 17. PCIE0_LINK_PM	18. PCIE1_LINK_PM
 *
 * Known resets in EyeQ6L domain 0 (type EQR_EYEQ5_SARCR):
 *  0. SPI0	 1. SPI1	 2. UART0	 3. I2C0
 *  4. I2C1	 5. TIMER0	 6. TIMER1	 7. TIMER2
 *  8. TIMER3	 9. WD0		10. WD1		11. EXT0
 * 12. EXT1	13. GPIO
 *
 * Known resets in EyeQ6L domain 1 (type EQR_EYEQ5_ACRP):
 *  0. VMP0	 1. VMP1	 2. VMP2	 3. VMP3
 *  4. PMA0	 5. PMA1	 6. PMAC0	 7. PMAC1
 *  8. MPC0	 9. MPC1	10. MPC2	11. MPC3
 * 12. MPC4
 *
 * Known resets in EyeQ6H west/east (type EQR_EYEQ6H_SARCR):
 *  0. CAN	 1. SPI0	 2. SPI1	 3. UART0
 *  4. UART1	 5. I2C0	 6. I2C1	 7. -hole-
 *  8. TIMER0	 9. TIMER1	10. WD		11. EXT TIMER
 * 12. GPIO
 *
 * Known resets in EyeQ6H acc (type EQR_EYEQ5_ACRP):
 *  1. XNN0	 2. XNN1	 3. XNN2	 4. XNN3
 *  5. VMP0	 6. VMP1	 7. VMP2	 8. VMP3
 *  9. PMA0	10. PMA1	11. MPC0	12. MPC1
 * 13. MPC2	14. MPC3	15. PERIPH
 *
 * Abbreviations:
 *  - PMA: Programmable Macro Array
 *  - MPC: Multi-threaded Processing Clusters
 *  - VMP: Vector Microcode Processors
 *
 * Copyright (C) 2024 Mobileye Vision Technologies Ltd.
 */

#include <linux/array_size.h>
#include <linux/auxiliary_bus.h>
#include <linux/bitfield.h>
#include <linux/bits.h>
#include <linux/bug.h>
#include <linux/cleanup.h>
#include <linux/container_of.h>
#include <linux/device.h>
#include <linux/err.h>
#include <linux/errno.h>
#include <linux/init.h>
#include <linux/io.h>
#include <linux/iopoll.h>
#include <linux/lockdep.h>
#include <linux/mod_devicetable.h>
#include <linux/mutex.h>
#include <linux/of.h>
#include <linux/reset-controller.h>
#include <linux/slab.h>
#include <linux/types.h>

/*
 * A reset ID, as returned by eqr_of_xlate_*(), is a (domain, offset) pair.
 * Low byte is domain, rest is offset.
 */
#define ID_DOMAIN_MASK	GENMASK(7, 0)
#define ID_OFFSET_MASK	GENMASK(31, 8)

enum eqr_domain_type {
	EQR_EYEQ5_SARCR,
	EQR_EYEQ5_ACRP,
	EQR_EYEQ5_PCIE,
	EQR_EYEQ6H_SARCR,
};

/*
 * Domain type EQR_EYEQ5_SARCR register offsets.
 */
#define EQR_EYEQ5_SARCR_REQUEST		(0x000)
#define EQR_EYEQ5_SARCR_STATUS		(0x004)

/*
 * Domain type EQR_EYEQ5_ACRP register masks.
 * Registers are: base + 4 * offset.
 */
#define EQR_EYEQ5_ACRP_PD_REQ		BIT(0)
#define EQR_EYEQ5_ACRP_ST_POWER_DOWN	BIT(27)
#define EQR_EYEQ5_ACRP_ST_ACTIVE	BIT(29)

/*
 * Domain type EQR_EYEQ6H_SARCR register offsets.
 */
#define EQR_EYEQ6H_SARCR_RST_REQUEST	(0x000)
#define EQR_EYEQ6H_SARCR_CLK_STATUS	(0x004)
#define EQR_EYEQ6H_SARCR_RST_STATUS	(0x008)
#define EQR_EYEQ6H_SARCR_CLK_REQUEST	(0x00C)

struct eqr_busy_wait_timings {
	unsigned long sleep_us;
	unsigned long timeout_us;
};

static const struct eqr_busy_wait_timings eqr_timings[] = {
	[EQR_EYEQ5_SARCR]	= {1, 10},
	[EQR_EYEQ5_ACRP]	= {1, 40 * USEC_PER_MSEC}, /* LBIST implies long timeout. */
	/* EQR_EYEQ5_PCIE does no busy waiting. */
	[EQR_EYEQ6H_SARCR]	= {1, 400},
};

#define EQR_MAX_DOMAIN_COUNT 3

struct eqr_domain_descriptor {
	enum eqr_domain_type	type;
	u32			valid_mask;
	unsigned int		offset;
};

struct eqr_match_data {
	unsigned int				domain_count;
	const struct eqr_domain_descriptor	*domains;
};

struct eqr_private {
	/*
	 * One mutex per domain for read-modify-write operations on registers.
	 * Some domains can be involved in LBIST which implies long critical
	 * sections; we wouldn't want other domains to be impacted by that.
	 */
	struct mutex			mutexes[EQR_MAX_DOMAIN_COUNT];
	void __iomem			*base;
	const struct eqr_match_data	*data;
	struct reset_controller_dev	rcdev;
};

static inline struct eqr_private *eqr_rcdev_to_priv(struct reset_controller_dev *x)
{
	return container_of(x, struct eqr_private, rcdev);
}

static u32 eqr_double_readl(void __iomem *addr_a, void __iomem *addr_b,
			    u32 *dest_a, u32 *dest_b)
{
	*dest_a = readl(addr_a);
	*dest_b = readl(addr_b);
	return 0; /* read_poll_timeout() op argument must return something. */
}

static int eqr_busy_wait_locked(struct eqr_private *priv, struct device *dev,
				u32 domain, u32 offset, bool assert)
{
	void __iomem *base = priv->base + priv->data->domains[domain].offset;
	enum eqr_domain_type domain_type = priv->data->domains[domain].type;
	unsigned long timeout_us = eqr_timings[domain_type].timeout_us;
	unsigned long sleep_us = eqr_timings[domain_type].sleep_us;
	u32 val, mask, rst_status, clk_status;
	void __iomem *reg;
	int ret;

	lockdep_assert_held(&priv->mutexes[domain]);

	switch (domain_type) {
	case EQR_EYEQ5_SARCR:
		reg = base + EQR_EYEQ5_SARCR_STATUS;
		mask = BIT(offset);

		ret = readl_poll_timeout(reg, val, !(val & mask) == assert,
					 sleep_us, timeout_us);
		break;

	case EQR_EYEQ5_ACRP:
		reg = base + 4 * offset;
		if (assert)
			mask = EQR_EYEQ5_ACRP_ST_POWER_DOWN;
		else
			mask = EQR_EYEQ5_ACRP_ST_ACTIVE;

		ret = readl_poll_timeout(reg, val, !!(val & mask),
					 sleep_us, timeout_us);
		break;

	case EQR_EYEQ5_PCIE:
		ret = 0; /* No busy waiting. */
		break;

	case EQR_EYEQ6H_SARCR:
		/*
		 * Wait until both bits change:
		 *	readl(base + EQR_EYEQ6H_SARCR_RST_STATUS) & BIT(offset)
		 *	readl(base + EQR_EYEQ6H_SARCR_CLK_STATUS) & BIT(offset)
		 */
		mask = BIT(offset);
		ret = read_poll_timeout(eqr_double_readl, val,
					(!(rst_status & mask) == assert) &&
					(!(clk_status & mask) == assert),
					sleep_us, timeout_us, false,
					base + EQR_EYEQ6H_SARCR_RST_STATUS,
					base + EQR_EYEQ6H_SARCR_CLK_STATUS,
					&rst_status, &clk_status);
		break;

	default:
		WARN_ON(1);
		ret = -EINVAL;
		break;
	}

	if (ret == -ETIMEDOUT)
		dev_dbg(dev, "%u-%u: timeout\n", domain, offset);
	return ret;
}

static void eqr_assert_locked(struct eqr_private *priv, u32 domain, u32 offset)
{
	enum eqr_domain_type domain_type = priv->data->domains[domain].type;
	void __iomem *base, *reg;
	u32 val;

	lockdep_assert_held(&priv->mutexes[domain]);

	base = priv->base + priv->data->domains[domain].offset;

	switch (domain_type) {
	case EQR_EYEQ5_SARCR:
		reg = base + EQR_EYEQ5_SARCR_REQUEST;
		writel(readl(reg) & ~BIT(offset), reg);
		break;

	case EQR_EYEQ5_ACRP:
		reg = base + 4 * offset;
		writel(readl(reg) | EQR_EYEQ5_ACRP_PD_REQ, reg);
		break;

	case EQR_EYEQ5_PCIE:
		writel(readl(base) & ~BIT(offset), base);
		break;

	case EQR_EYEQ6H_SARCR:
		/* RST_REQUEST and CLK_REQUEST must be kept in sync. */
		val = readl(base + EQR_EYEQ6H_SARCR_RST_REQUEST);
		val &= ~BIT(offset);
		writel(val, base + EQR_EYEQ6H_SARCR_RST_REQUEST);
		writel(val, base + EQR_EYEQ6H_SARCR_CLK_REQUEST);
		break;

	default:
		WARN_ON(1);
		break;
	}
}

static int eqr_assert(struct reset_controller_dev *rcdev, unsigned long id)
{
	struct eqr_private *priv = eqr_rcdev_to_priv(rcdev);
	u32 domain = FIELD_GET(ID_DOMAIN_MASK, id);
	u32 offset = FIELD_GET(ID_OFFSET_MASK, id);

	dev_dbg(rcdev->dev, "%u-%u: assert request\n", domain, offset);

	guard(mutex)(&priv->mutexes[domain]);

	eqr_assert_locked(priv, domain, offset);
	return eqr_busy_wait_locked(priv, rcdev->dev, domain, offset, true);
}

static void eqr_deassert_locked(struct eqr_private *priv, u32 domain,
				u32 offset)
{
	enum eqr_domain_type domain_type = priv->data->domains[domain].type;
	void __iomem *base, *reg;
	u32 val;

	lockdep_assert_held(&priv->mutexes[domain]);

	base = priv->base + priv->data->domains[domain].offset;

	switch (domain_type) {
	case EQR_EYEQ5_SARCR:
		reg = base + EQR_EYEQ5_SARCR_REQUEST;
		writel(readl(reg) | BIT(offset), reg);
		break;

	case EQR_EYEQ5_ACRP:
		reg = base + 4 * offset;
		writel(readl(reg) & ~EQR_EYEQ5_ACRP_PD_REQ, reg);
		break;

	case EQR_EYEQ5_PCIE:
		writel(readl(base) | BIT(offset), base);
		break;

	case EQR_EYEQ6H_SARCR:
		/* RST_REQUEST and CLK_REQUEST must be kept in sync. */
		val = readl(base + EQR_EYEQ6H_SARCR_RST_REQUEST);
		val |= BIT(offset);
		writel(val, base + EQR_EYEQ6H_SARCR_RST_REQUEST);
		writel(val, base + EQR_EYEQ6H_SARCR_CLK_REQUEST);
		break;

	default:
		WARN_ON(1);
		break;
	}
}

static int eqr_deassert(struct reset_controller_dev *rcdev, unsigned long id)
{
	struct eqr_private *priv = eqr_rcdev_to_priv(rcdev);
	u32 domain = FIELD_GET(ID_DOMAIN_MASK, id);
	u32 offset = FIELD_GET(ID_OFFSET_MASK, id);

	dev_dbg(rcdev->dev, "%u-%u: deassert request\n", domain, offset);

	guard(mutex)(&priv->mutexes[domain]);

	eqr_deassert_locked(priv, domain, offset);
	return eqr_busy_wait_locked(priv, rcdev->dev, domain, offset, false);
}

static int eqr_status(struct reset_controller_dev *rcdev, unsigned long id)
{
	u32 domain = FIELD_GET(ID_DOMAIN_MASK, id);
	u32 offset = FIELD_GET(ID_OFFSET_MASK, id);
	struct eqr_private *priv = eqr_rcdev_to_priv(rcdev);
	enum eqr_domain_type domain_type = priv->data->domains[domain].type;
	void __iomem *base, *reg;

	dev_dbg(rcdev->dev, "%u-%u: status request\n", domain, offset);

	guard(mutex)(&priv->mutexes[domain]);

	base = priv->base + priv->data->domains[domain].offset;

	switch (domain_type) {
	case EQR_EYEQ5_SARCR:
		reg = base + EQR_EYEQ5_SARCR_STATUS;
		return !(readl(reg) & BIT(offset));
	case EQR_EYEQ5_ACRP:
		reg = base + 4 * offset;
		return !(readl(reg) & EQR_EYEQ5_ACRP_ST_ACTIVE);
	case EQR_EYEQ5_PCIE:
		return !(readl(base) & BIT(offset));
	case EQR_EYEQ6H_SARCR:
		reg = base + EQR_EYEQ6H_SARCR_RST_STATUS;
		return !(readl(reg) & BIT(offset));
	default:
		return -EINVAL;
	}
}

static const struct reset_control_ops eqr_ops = {
	.assert	  = eqr_assert,
	.deassert = eqr_deassert,
	.status	  = eqr_status,
};

static int eqr_of_xlate_internal(struct reset_controller_dev *rcdev,
				 u32 domain, u32 offset)
{
	struct eqr_private *priv = eqr_rcdev_to_priv(rcdev);

	if (domain >= priv->data->domain_count || offset > 31 ||
	    !(priv->data->domains[domain].valid_mask & BIT(offset))) {
		dev_err(rcdev->dev, "%u-%u: invalid reset\n", domain, offset);
		return -EINVAL;
	}

	return FIELD_PREP(ID_DOMAIN_MASK, domain) | FIELD_PREP(ID_OFFSET_MASK, offset);
}

static int eqr_of_xlate_onecell(struct reset_controller_dev *rcdev,
				const struct of_phandle_args *reset_spec)
{
	return eqr_of_xlate_internal(rcdev, 0, reset_spec->args[0]);
}

static int eqr_of_xlate_twocells(struct reset_controller_dev *rcdev,
				 const struct of_phandle_args *reset_spec)
{
	return eqr_of_xlate_internal(rcdev, reset_spec->args[0], reset_spec->args[1]);
}

static void eqr_of_node_put(void *_dev)
{
	struct device *dev = _dev;

	of_node_put(dev->of_node);
}

static int eqr_probe(struct auxiliary_device *adev,
		     const struct auxiliary_device_id *id)
{
	const struct of_device_id *match;
	struct device *dev = &adev->dev;
	struct eqr_private *priv;
	unsigned int i;
	int ret;

	/*
	 * We are an auxiliary device of clk-eyeq. We do not have an OF node by
	 * default; let's reuse our parent's OF node.
	 */
	WARN_ON(dev->of_node);
	device_set_of_node_from_dev(dev, dev->parent);
	if (!dev->of_node)
		return -ENODEV;

	ret = devm_add_action_or_reset(dev, eqr_of_node_put, dev);
	if (ret)
		return ret;

	/*
	 * Using our newfound OF node, we can get match data. We cannot use
	 * device_get_match_data() because it does not match reused OF nodes.
	 */
	match = of_match_node(dev->driver->of_match_table, dev->of_node);
	if (!match || !match->data)
		return -ENODEV;

	priv = devm_kzalloc(dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	priv->data = match->data;
	priv->base = (void __iomem *)dev_get_platdata(dev);
	priv->rcdev.ops = &eqr_ops;
	priv->rcdev.owner = THIS_MODULE;
	priv->rcdev.dev = dev;
	priv->rcdev.of_node = dev->of_node;

	if (priv->data->domain_count == 1) {
		priv->rcdev.of_reset_n_cells = 1;
		priv->rcdev.of_xlate = eqr_of_xlate_onecell;
	} else {
		priv->rcdev.of_reset_n_cells = 2;
		priv->rcdev.of_xlate = eqr_of_xlate_twocells;
	}

	for (i = 0; i < priv->data->domain_count; i++)
		mutex_init(&priv->mutexes[i]);

	priv->rcdev.nr_resets = 0;
	for (i = 0; i < priv->data->domain_count; i++)
		priv->rcdev.nr_resets += hweight32(priv->data->domains[i].valid_mask);

	ret = devm_reset_controller_register(dev, &priv->rcdev);
	if (ret)
		return dev_err_probe(dev, ret, "failed registering reset controller\n");

	return 0;
}

static const struct eqr_domain_descriptor eqr_eyeq5_domains[] = {
	{
		.type = EQR_EYEQ5_SARCR,
		.valid_mask = 0xFFFFFF8,
		.offset = 0x004,
	},
	{
		.type = EQR_EYEQ5_ACRP,
		.valid_mask = 0x0001FFF,
		.offset = 0x200,
	},
	{
		.type = EQR_EYEQ5_PCIE,
		.valid_mask = 0x007BFFF,
		.offset = 0x120,
	},
};

static const struct eqr_match_data eqr_eyeq5_data = {
	.domain_count	= ARRAY_SIZE(eqr_eyeq5_domains),
	.domains	= eqr_eyeq5_domains,
};

static const struct eqr_domain_descriptor eqr_eyeq6l_domains[] = {
	{
		.type = EQR_EYEQ5_SARCR,
		.valid_mask = 0x3FFF,
		.offset = 0x004,
	},
	{
		.type = EQR_EYEQ5_ACRP,
		.valid_mask = 0x00FF,
		.offset = 0x200,
	},
};

static const struct eqr_match_data eqr_eyeq6l_data = {
	.domain_count	= ARRAY_SIZE(eqr_eyeq6l_domains),
	.domains	= eqr_eyeq6l_domains,
};

/* West and east OLBs each have an instance. */
static const struct eqr_domain_descriptor eqr_eyeq6h_we_domains[] = {
	{
		.type = EQR_EYEQ6H_SARCR,
		.valid_mask = 0x1F7F,
		.offset = 0x004,
	},
};

static const struct eqr_match_data eqr_eyeq6h_we_data = {
	.domain_count	= ARRAY_SIZE(eqr_eyeq6h_we_domains),
	.domains	= eqr_eyeq6h_we_domains,
};

static const struct eqr_domain_descriptor eqr_eyeq6h_acc_domains[] = {
	{
		.type = EQR_EYEQ5_ACRP,
		.valid_mask = 0x7FFF,
		.offset = 0x000,
	},
};

static const struct eqr_match_data eqr_eyeq6h_acc_data = {
	.domain_count	= ARRAY_SIZE(eqr_eyeq6h_acc_domains),
	.domains	= eqr_eyeq6h_acc_domains,
};

/*
 * Table describes OLB system-controller compatibles.
 * It does not get used to match against devicetree node.
 */
static const struct of_device_id eqr_match_table[] = {
	{ .compatible = "mobileye,eyeq5-olb", .data = &eqr_eyeq5_data },
	{ .compatible = "mobileye,eyeq6l-olb", .data = &eqr_eyeq6l_data },
	{ .compatible = "mobileye,eyeq6h-west-olb", .data = &eqr_eyeq6h_we_data },
	{ .compatible = "mobileye,eyeq6h-east-olb", .data = &eqr_eyeq6h_we_data },
	{ .compatible = "mobileye,eyeq6h-acc-olb", .data = &eqr_eyeq6h_acc_data },
	{}
};
MODULE_DEVICE_TABLE(of, eqr_match_table);

static const struct auxiliary_device_id eqr_id_table[] = {
	{ .name = "clk_eyeq.reset" },
	{ .name = "clk_eyeq.reset_west" },
	{ .name = "clk_eyeq.reset_east" },
	{ .name = "clk_eyeq.reset_acc" },
	{}
};
MODULE_DEVICE_TABLE(auxiliary, eqr_id_table);

static struct auxiliary_driver eqr_driver = {
	.probe = eqr_probe,
	.id_table = eqr_id_table,
	.driver = {
		.of_match_table = eqr_match_table,
	}
};
module_auxiliary_driver(eqr_driver);
