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
#define REALVIEW_PB11MP_SYS_PLD_CTRL1	0x74
#define VERSATILE_LOCK_VAL		0xA05F
#define PLD_INTMODE_MASK		BIT(22)|BIT(23)|BIT(24)
#define PLD_INTMODE_LEGACY		0x0
#define PLD_INTMODE_NEW_DCC		BIT(22)
#define PLD_INTMODE_NEW_NO_DCC		BIT(23)
#define PLD_INTMODE_FIQ_ENABLE		BIT(24)

static int __init
realview_gic_of_init(struct device_node *node, struct device_node *parent)
{
	static struct regmap *map;

	/* The PB11MPCore GIC needs to be configured in the syscon */
	map = syscon_regmap_lookup_by_compatible("arm,realview-pb11mp-syscon");
	if (!IS_ERR(map)) {
		/* new irq mode with no DCC */
		regmap_write(map, REALVIEW_SYS_LOCK_OFFSET,
			     VERSATILE_LOCK_VAL);
		regmap_update_bits(map, REALVIEW_PB11MP_SYS_PLD_CTRL1,
				   PLD_INTMODE_NEW_NO_DCC,
				   PLD_INTMODE_MASK);
		regmap_write(map, REALVIEW_SYS_LOCK_OFFSET, 0x0000);
		pr_info("TC11MP GIC: set up interrupt controller to NEW mode, no DCC\n");
	} else {
		pr_err("TC11MP GIC setup: could not find syscon\n");
		return -ENXIO;
	}
	return gic_of_init(node, parent);
}
IRQCHIP_DECLARE(armtc11mp_gic, "arm,tc11mp-gic", realview_gic_of_init);
