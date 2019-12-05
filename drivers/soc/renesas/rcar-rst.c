// SPDX-License-Identifier: GPL-2.0
/*
 * R-Car Gen1 RESET/WDT, R-Car Gen2, Gen3, and RZ/G RST Driver
 *
 * Copyright (C) 2016 Glider bvba
 */

#include <linux/err.h>
#include <linux/io.h>
#include <linux/of_address.h>
#include <linux/soc/renesas/rcar-rst.h>

#define WDTRSTCR_RESET		0xA55A0002
#define WDTRSTCR		0x0054

static int rcar_rst_enable_wdt_reset(void __iomem *base)
{
	iowrite32(WDTRSTCR_RESET, base + WDTRSTCR);
	return 0;
}

struct rst_config {
	unsigned int modemr;		/* Mode Monitoring Register Offset */
	int (*configure)(void *base);	/* Platform specific configuration */
};

static const struct rst_config rcar_rst_gen1 __initconst = {
	.modemr = 0x20,
};

static const struct rst_config rcar_rst_gen2 __initconst = {
	.modemr = 0x60,
	.configure = rcar_rst_enable_wdt_reset,
};

static const struct rst_config rcar_rst_gen3 __initconst = {
	.modemr = 0x60,
};

static const struct of_device_id rcar_rst_matches[] __initconst = {
	/* RZ/G1 is handled like R-Car Gen2 */
	{ .compatible = "renesas,r8a7743-rst", .data = &rcar_rst_gen2 },
	{ .compatible = "renesas,r8a7744-rst", .data = &rcar_rst_gen2 },
	{ .compatible = "renesas,r8a7745-rst", .data = &rcar_rst_gen2 },
	{ .compatible = "renesas,r8a77470-rst", .data = &rcar_rst_gen2 },
	/* RZ/G2 is handled like R-Car Gen3 */
	{ .compatible = "renesas,r8a774a1-rst", .data = &rcar_rst_gen3 },
	{ .compatible = "renesas,r8a774b1-rst", .data = &rcar_rst_gen3 },
	{ .compatible = "renesas,r8a774c0-rst", .data = &rcar_rst_gen3 },
	/* R-Car Gen1 */
	{ .compatible = "renesas,r8a7778-reset-wdt", .data = &rcar_rst_gen1 },
	{ .compatible = "renesas,r8a7779-reset-wdt", .data = &rcar_rst_gen1 },
	/* R-Car Gen2 */
	{ .compatible = "renesas,r8a7790-rst", .data = &rcar_rst_gen2 },
	{ .compatible = "renesas,r8a7791-rst", .data = &rcar_rst_gen2 },
	{ .compatible = "renesas,r8a7792-rst", .data = &rcar_rst_gen2 },
	{ .compatible = "renesas,r8a7793-rst", .data = &rcar_rst_gen2 },
	{ .compatible = "renesas,r8a7794-rst", .data = &rcar_rst_gen2 },
	/* R-Car Gen3 */
	{ .compatible = "renesas,r8a7795-rst", .data = &rcar_rst_gen3 },
	{ .compatible = "renesas,r8a7796-rst", .data = &rcar_rst_gen3 },
	{ .compatible = "renesas,r8a77961-rst", .data = &rcar_rst_gen3 },
	{ .compatible = "renesas,r8a77965-rst", .data = &rcar_rst_gen3 },
	{ .compatible = "renesas,r8a77970-rst", .data = &rcar_rst_gen3 },
	{ .compatible = "renesas,r8a77980-rst", .data = &rcar_rst_gen3 },
	{ .compatible = "renesas,r8a77990-rst", .data = &rcar_rst_gen3 },
	{ .compatible = "renesas,r8a77995-rst", .data = &rcar_rst_gen3 },
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
	if (cfg->configure) {
		error = cfg->configure(base);
		if (error) {
			pr_warn("%pOF: Cannot run SoC specific configuration\n",
				np);
			goto out_put;
		}
	}

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
