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
#define GEN4_WDTRSTCR		0x0010

#define CR7BAR			0x0070
#define CR7BAREN		BIT(4)
#define CR7BAR_MASK		0xFFFC0000

static void __iomem *rcar_rst_base;
static u32 saved_mode __initdata;
static int (*rcar_rst_set_rproc_boot_addr_func)(u64 boot_addr);

static int rcar_rst_enable_wdt_reset(void __iomem *base)
{
	iowrite32(WDTRSTCR_RESET, base + WDTRSTCR);
	return 0;
}

static int rcar_rst_v3u_enable_wdt_reset(void __iomem *base)
{
	iowrite32(WDTRSTCR_RESET, base + GEN4_WDTRSTCR);
	return 0;
}

/*
 * Most of the R-Car Gen3 SoCs have an ARM Realtime Core.
 * Firmware boot address has to be set in CR7BAR before
 * starting the realtime core.
 * Boot address must be aligned on a 256k boundary.
 */
static int rcar_rst_set_gen3_rproc_boot_addr(u64 boot_addr)
{
	if (boot_addr & ~(u64)CR7BAR_MASK) {
		pr_err("Invalid boot address got %llx\n", boot_addr);
		return -EINVAL;
	}

	iowrite32(boot_addr, rcar_rst_base + CR7BAR);
	iowrite32(boot_addr | CR7BAREN, rcar_rst_base + CR7BAR);

	return 0;
}

struct rst_config {
	unsigned int modemr;		/* Mode Monitoring Register Offset */
	int (*configure)(void __iomem *base);	/* Platform specific config */
	int (*set_rproc_boot_addr)(u64 boot_addr);
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
	.set_rproc_boot_addr = rcar_rst_set_gen3_rproc_boot_addr,
};

/* V3U firmware doesn't enable WDT reset and there won't be updates anymore */
static const struct rst_config rcar_rst_v3u __initconst = {
	.modemr = 0x00,		/* MODEMR0 and it has CPG related bits */
	.configure = rcar_rst_v3u_enable_wdt_reset,
};

static const struct rst_config rcar_rst_gen4 __initconst = {
	.modemr = 0x00,		/* MODEMR0 and it has CPG related bits */
};

static const struct of_device_id rcar_rst_matches[] __initconst = {
	/* RZ/G1 is handled like R-Car Gen2 */
	{ .compatible = "renesas,r8a7742-rst", .data = &rcar_rst_gen2 },
	{ .compatible = "renesas,r8a7743-rst", .data = &rcar_rst_gen2 },
	{ .compatible = "renesas,r8a7744-rst", .data = &rcar_rst_gen2 },
	{ .compatible = "renesas,r8a7745-rst", .data = &rcar_rst_gen2 },
	{ .compatible = "renesas,r8a77470-rst", .data = &rcar_rst_gen2 },
	/* RZ/G2 is handled like R-Car Gen3 */
	{ .compatible = "renesas,r8a774a1-rst", .data = &rcar_rst_gen3 },
	{ .compatible = "renesas,r8a774b1-rst", .data = &rcar_rst_gen3 },
	{ .compatible = "renesas,r8a774c0-rst", .data = &rcar_rst_gen3 },
	{ .compatible = "renesas,r8a774e1-rst", .data = &rcar_rst_gen3 },
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
	/* R-Car Gen4 */
	{ .compatible = "renesas,r8a779a0-rst", .data = &rcar_rst_v3u },
	{ .compatible = "renesas,r8a779f0-rst", .data = &rcar_rst_gen4 },
	{ .compatible = "renesas,r8a779g0-rst", .data = &rcar_rst_gen4 },
	{ /* sentinel */ }
};

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
	rcar_rst_set_rproc_boot_addr_func = cfg->set_rproc_boot_addr;

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

int rcar_rst_set_rproc_boot_addr(u64 boot_addr)
{
	if (!rcar_rst_set_rproc_boot_addr_func)
		return -EIO;

	return rcar_rst_set_rproc_boot_addr_func(boot_addr);
}
EXPORT_SYMBOL_GPL(rcar_rst_set_rproc_boot_addr);
