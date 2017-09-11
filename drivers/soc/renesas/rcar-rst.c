/*
 * R-Car Gen1 RESET/WDT, R-Car Gen2, Gen3, and RZ/G RST Driver
 *
 * Copyright (C) 2016 Glider bvba
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 */

#include <linux/err.h>
#include <linux/io.h>
#include <linux/of_address.h>
#include <linux/soc/renesas/rcar-rst.h>

struct rst_config {
	unsigned int modemr;	/* Mode Monitoring Register Offset */
};

static const struct rst_config rcar_rst_gen1 __initconst = {
	.modemr = 0x20,
};

static const struct rst_config rcar_rst_gen2 __initconst = {
	.modemr = 0x60,
};

static const struct of_device_id rcar_rst_matches[] __initconst = {
	/* RZ/G is handled like R-Car Gen2 */
	{ .compatible = "renesas,r8a7743-rst", .data = &rcar_rst_gen2 },
	{ .compatible = "renesas,r8a7745-rst", .data = &rcar_rst_gen2 },
	/* R-Car Gen1 */
	{ .compatible = "renesas,r8a7778-reset-wdt", .data = &rcar_rst_gen1 },
	{ .compatible = "renesas,r8a7779-reset-wdt", .data = &rcar_rst_gen1 },
	/* R-Car Gen2 */
	{ .compatible = "renesas,r8a7790-rst", .data = &rcar_rst_gen2 },
	{ .compatible = "renesas,r8a7791-rst", .data = &rcar_rst_gen2 },
	{ .compatible = "renesas,r8a7792-rst", .data = &rcar_rst_gen2 },
	{ .compatible = "renesas,r8a7793-rst", .data = &rcar_rst_gen2 },
	{ .compatible = "renesas,r8a7794-rst", .data = &rcar_rst_gen2 },
	/* R-Car Gen3 is handled like R-Car Gen2 */
	{ .compatible = "renesas,r8a7795-rst", .data = &rcar_rst_gen2 },
	{ .compatible = "renesas,r8a7796-rst", .data = &rcar_rst_gen2 },
	{ .compatible = "renesas,r8a77995-rst", .data = &rcar_rst_gen2 },
	{ /* sentinel */ }
};

static void __iomem *rcar_rst_base __initdata;
static u32 saved_mode __initdata;

static int __init rcar_rst_init(void)
{
	const struct of_device_id *match;
	const struct rst_config *cfg;
	struct device_node *np;
	void __iomem *base;
	int error = 0;

	np = of_find_matching_node_and_match(NULL, rcar_rst_matches, &match);
	if (!np)
		return -ENODEV;

	base = of_iomap(np, 0);
	if (!base) {
		pr_warn("%pOF: Cannot map regs\n", np);
		error = -ENOMEM;
		goto out_put;
	}

	rcar_rst_base = base;
	cfg = match->data;
	saved_mode = ioread32(base + cfg->modemr);

	pr_debug("%pOF: MODE = 0x%08x\n", np, saved_mode);

out_put:
	of_node_put(np);
	return error;
}

int __init rcar_rst_read_mode_pins(u32 *mode)
{
	int error;

	if (!rcar_rst_base) {
		error = rcar_rst_init();
		if (error)
			return error;
	}

	*mode = saved_mode;
	return 0;
}
