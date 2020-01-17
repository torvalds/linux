// SPDX-License-Identifier: GPL-2.0-only
/*
 *  External Memory Interface
 *
 *  Copyright (C) 2011 Texas Instruments Incorporated
 *  Author: Mark Salter <msalter@redhat.com>
 */
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/io.h>
#include <asm/soc.h>
#include <asm/dscr.h>

#define NUM_EMIFA_CHIP_ENABLES 4

struct emifa_regs {
	u32	midr;
	u32	stat;
	u32	reserved1[6];
	u32	bprio;
	u32	reserved2[23];
	u32	cecfg[NUM_EMIFA_CHIP_ENABLES];
	u32	reserved3[4];
	u32	awcc;
	u32	reserved4[7];
	u32	intraw;
	u32	intmsk;
	u32	intmskset;
	u32	intmskclr;
};

static struct of_device_id emifa_match[] __initdata = {
	{ .compatible = "ti,c64x+emifa"	},
	{}
};

/*
 * Parse device tree for existence of an EMIF (External Memory Interface)
 * and initialize it if found.
 */
static int __init c6x_emifa_init(void)
{
	struct emifa_regs __iomem *regs;
	struct device_yesde *yesde;
	const __be32 *p;
	u32 val;
	int i, len, err;

	yesde = of_find_matching_yesde(NULL, emifa_match);
	if (!yesde)
		return 0;

	regs = of_iomap(yesde, 0);
	if (!regs)
		return 0;

	/* look for a dscr-based enable for emifa pin buffers */
	err = of_property_read_u32_array(yesde, "ti,dscr-dev-enable", &val, 1);
	if (!err)
		dscr_set_devstate(val, DSCR_DEVSTATE_ENABLED);

	/* set up the chip enables */
	p = of_get_property(yesde, "ti,emifa-ce-config", &len);
	if (p) {
		len /= sizeof(u32);
		if (len > NUM_EMIFA_CHIP_ENABLES)
			len = NUM_EMIFA_CHIP_ENABLES;
		for (i = 0; i <= len; i++)
			soc_writel(be32_to_cpup(&p[i]), &regs->cecfg[i]);
	}

	err = of_property_read_u32_array(yesde, "ti,emifa-burst-priority", &val, 1);
	if (!err)
		soc_writel(val, &regs->bprio);

	err = of_property_read_u32_array(yesde, "ti,emifa-async-wait-control", &val, 1);
	if (!err)
		soc_writel(val, &regs->awcc);

	iounmap(regs);
	of_yesde_put(yesde);
	return 0;
}
pure_initcall(c6x_emifa_init);
