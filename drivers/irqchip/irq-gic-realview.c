/*
 * Special GIC quirks for the ARM RealView
 * Copyright (C) 2015 Linus Walleij
 */
#include <linux/of.h>
#include <linux/regmap.h>
#include <linux/mfd/syscon.h>
#include <linux/bitops.h>
#include <linux/irqchip.h>
#include <linux/irqchip/arm-gic.h>

#define REALVIEW_SYS_LOCK_OFFSET	0x20
#define REALVIEW_SYS_PLD_CTRL1		0x74
#define REALVIEW_EB_REVB_SYS_PLD_CTRL1	0xD8
#define VERSATILE_LOCK_VAL		0xA05F
#define PLD_INTMODE_MASK		BIT(22)|BIT(23)|BIT(24)
#define PLD_INTMODE_LEGACY		0x0
#define PLD_INTMODE_NEW_DCC		BIT(22)
#define PLD_INTMODE_NEW_NO_DCC		BIT(23)
#define PLD_INTMODE_FIQ_ENABLE		BIT(24)

/* For some reason RealView EB Rev B moved this register */
static const struct of_device_id syscon_pldset_of_match[] = {
	{
		.compatible = "arm,realview-eb11mp-revb-syscon",
		.data = (void *)REALVIEW_EB_REVB_SYS_PLD_CTRL1,
	},
	{
		.compatible = "arm,realview-eb11mp-revc-syscon",
		.data = (void *)REALVIEW_SYS_PLD_CTRL1,
	},
	{
		.compatible = "arm,realview-eb-syscon",
		.data = (void *)REALVIEW_SYS_PLD_CTRL1,
	},
	{
		.compatible = "arm,realview-pb11mp-syscon",
		.data = (void *)REALVIEW_SYS_PLD_CTRL1,
	},
	{},
};

static int __init
realview_gic_of_init(struct device_node *node, struct device_node *parent)
{
	static struct regmap *map;
	struct device_node *np;
	const struct of_device_id *gic_id;
	u32 pld1_ctrl;

	np = of_find_matching_node_and_match(NULL, syscon_pldset_of_match,
					     &gic_id);
	if (!np)
		return -ENODEV;
	pld1_ctrl = (u32)gic_id->data;

	/* The PB11MPCore GIC needs to be configured in the syscon */
	map = syscon_node_to_regmap(np);
	if (!IS_ERR(map)) {
		/* new irq mode with no DCC */
		regmap_write(map, REALVIEW_SYS_LOCK_OFFSET,
			     VERSATILE_LOCK_VAL);
		regmap_update_bits(map, pld1_ctrl,
				   PLD_INTMODE_NEW_NO_DCC,
				   PLD_INTMODE_MASK);
		regmap_write(map, REALVIEW_SYS_LOCK_OFFSET, 0x0000);
		pr_info("RealView GIC: set up interrupt controller to NEW mode, no DCC\n");
	} else {
		pr_err("RealView GIC setup: could not find syscon\n");
		return -ENODEV;
	}
	return gic_of_init(node, parent);
}
IRQCHIP_DECLARE(armtc11mp_gic, "arm,tc11mp-gic", realview_gic_of_init);
IRQCHIP_DECLARE(armeb11mp_gic, "arm,eb11mp-gic", realview_gic_of_init);
