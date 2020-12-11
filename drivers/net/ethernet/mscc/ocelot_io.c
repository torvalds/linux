// SPDX-License-Identifier: (GPL-2.0 OR MIT)
/*
 * Microsemi Ocelot Switch driver
 *
 * Copyright (c) 2017 Microsemi Corporation
 */
#include <linux/io.h>
#include <linux/kernel.h>
#include <linux/platform_device.h>

#include "ocelot.h"

u32 __ocelot_read_ix(struct ocelot *ocelot, u32 reg, u32 offset)
{
	u16 target = reg >> TARGET_OFFSET;
	u32 val;

	WARN_ON(!target);

	regmap_read(ocelot->targets[target],
		    ocelot->map[target][reg & REG_MASK] + offset, &val);
	return val;
}
EXPORT_SYMBOL(__ocelot_read_ix);

void __ocelot_write_ix(struct ocelot *ocelot, u32 val, u32 reg, u32 offset)
{
	u16 target = reg >> TARGET_OFFSET;

	WARN_ON(!target);

	regmap_write(ocelot->targets[target],
		     ocelot->map[target][reg & REG_MASK] + offset, val);
}
EXPORT_SYMBOL(__ocelot_write_ix);

void __ocelot_rmw_ix(struct ocelot *ocelot, u32 val, u32 mask, u32 reg,
		     u32 offset)
{
	u16 target = reg >> TARGET_OFFSET;

	WARN_ON(!target);

	regmap_update_bits(ocelot->targets[target],
			   ocelot->map[target][reg & REG_MASK] + offset,
			   mask, val);
}
EXPORT_SYMBOL(__ocelot_rmw_ix);

u32 ocelot_port_readl(struct ocelot_port *port, u32 reg)
{
	struct ocelot *ocelot = port->ocelot;
	u16 target = reg >> TARGET_OFFSET;
	u32 val;

	WARN_ON(!target);

	regmap_read(port->target, ocelot->map[target][reg & REG_MASK], &val);
	return val;
}
EXPORT_SYMBOL(ocelot_port_readl);

void ocelot_port_writel(struct ocelot_port *port, u32 val, u32 reg)
{
	struct ocelot *ocelot = port->ocelot;
	u16 target = reg >> TARGET_OFFSET;

	WARN_ON(!target);

	regmap_write(port->target, ocelot->map[target][reg & REG_MASK], val);
}
EXPORT_SYMBOL(ocelot_port_writel);

u32 __ocelot_target_read_ix(struct ocelot *ocelot, enum ocelot_target target,
			    u32 reg, u32 offset)
{
	u32 val;

	regmap_read(ocelot->targets[target],
		    ocelot->map[target][reg] + offset, &val);
	return val;
}

void __ocelot_target_write_ix(struct ocelot *ocelot, enum ocelot_target target,
			      u32 val, u32 reg, u32 offset)
{
	regmap_write(ocelot->targets[target],
		     ocelot->map[target][reg] + offset, val);
}

int ocelot_regfields_init(struct ocelot *ocelot,
			  const struct reg_field *const regfields)
{
	unsigned int i;
	u16 target;

	for (i = 0; i < REGFIELD_MAX; i++) {
		struct reg_field regfield = {};
		u32 reg = regfields[i].reg;

		if (!reg)
			continue;

		target = regfields[i].reg >> TARGET_OFFSET;

		regfield.reg = ocelot->map[target][reg & REG_MASK];
		regfield.lsb = regfields[i].lsb;
		regfield.msb = regfields[i].msb;
		regfield.id_size = regfields[i].id_size;
		regfield.id_offset = regfields[i].id_offset;

		ocelot->regfields[i] =
		devm_regmap_field_alloc(ocelot->dev,
					ocelot->targets[target],
					regfield);

		if (IS_ERR(ocelot->regfields[i]))
			return PTR_ERR(ocelot->regfields[i]);
	}

	return 0;
}
EXPORT_SYMBOL(ocelot_regfields_init);

static struct regmap_config ocelot_regmap_config = {
	.reg_bits	= 32,
	.val_bits	= 32,
	.reg_stride	= 4,
};

struct regmap *ocelot_regmap_init(struct ocelot *ocelot, struct resource *res)
{
	void __iomem *regs;

	regs = devm_ioremap_resource(ocelot->dev, res);
	if (IS_ERR(regs))
		return ERR_CAST(regs);

	ocelot_regmap_config.name = res->name;

	return devm_regmap_init_mmio(ocelot->dev, regs, &ocelot_regmap_config);
}
EXPORT_SYMBOL(ocelot_regmap_init);
