/*
 * Copyright (C) 2007,2008 Freescale Semiconductor, Inc. All rights reserved.
 *
 * Author: John Rigby <jrigby@freescale.com>
 *
 * Implements the clk api defined in include/linux/clk.h
 *
 *    Original based on linux/arch/arm/mach-integrator/clock.c
 *
 *    Copyright (C) 2004 ARM Limited.
 *    Written by Deep Blue Solutions Limited.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#include <linux/kernel.h>
#include <linux/list.h>
#include <linux/errno.h>
#include <linux/err.h>
#include <linux/module.h>
#include <linux/string.h>
#include <linux/clk.h>
#include <linux/mutex.h>
#include <linux/io.h>

#include <linux/of_platform.h>
#include <asm/mpc5xxx.h>
#include <asm/clk_interface.h>

#undef CLK_DEBUG

static int clocks_initialized;

#define CLK_HAS_RATE	0x1	/* has rate in MHz */
#define CLK_HAS_CTRL	0x2	/* has control reg and bit */

struct clk {
	struct list_head node;
	char name[32];
	int flags;
	struct device *dev;
	unsigned long rate;
	struct module *owner;
	void (*calc) (struct clk *);
	struct clk *parent;
	int reg, bit;		/* CLK_HAS_CTRL */
	int div_shift;		/* only used by generic_div_clk_calc */
};

static LIST_HEAD(clocks);
static DEFINE_MUTEX(clocks_mutex);

static struct clk *mpc5121_clk_get(struct device *dev, const char *id)
{
	struct clk *p, *clk = ERR_PTR(-ENOENT);
	int dev_match;
	int id_match;

	if (dev == NULL || id == NULL)
		return clk;

	mutex_lock(&clocks_mutex);
	list_for_each_entry(p, &clocks, node) {
		dev_match = id_match = 0;

		if (dev == p->dev)
			dev_match++;
		if (strcmp(id, p->name) == 0)
			id_match++;
		if ((dev_match || id_match) && try_module_get(p->owner)) {
			clk = p;
			break;
		}
	}
	mutex_unlock(&clocks_mutex);

	return clk;
}

#ifdef CLK_DEBUG
static void dump_clocks(void)
{
	struct clk *p;

	mutex_lock(&clocks_mutex);
	printk(KERN_INFO "CLOCKS:\n");
	list_for_each_entry(p, &clocks, node) {
		pr_info("  %s=%ld", p->name, p->rate);
		if (p->parent)
			pr_cont(" %s=%ld", p->parent->name,
			       p->parent->rate);
		if (p->flags & CLK_HAS_CTRL)
			pr_cont(" reg/bit=%d/%d", p->reg, p->bit);
		pr_cont("\n");
	}
	mutex_unlock(&clocks_mutex);
}
#define	DEBUG_CLK_DUMP() dump_clocks()
#else
#define	DEBUG_CLK_DUMP()
#endif


static void mpc5121_clk_put(struct clk *clk)
{
	module_put(clk->owner);
}

#define NRPSC 12

struct mpc512x_clockctl {
	u32 spmr;		/* System PLL Mode Reg */
	u32 sccr[2];		/* System Clk Ctrl Reg 1 & 2 */
	u32 scfr1;		/* System Clk Freq Reg 1 */
	u32 scfr2;		/* System Clk Freq Reg 2 */
	u32 reserved;
	u32 bcr;		/* Bread Crumb Reg */
	u32 pccr[NRPSC];	/* PSC Clk Ctrl Reg 0-11 */
	u32 spccr;		/* SPDIF Clk Ctrl Reg */
	u32 cccr;		/* CFM Clk Ctrl Reg */
	u32 dccr;		/* DIU Clk Cnfg Reg */
};

struct mpc512x_clockctl __iomem *clockctl;

static int mpc5121_clk_enable(struct clk *clk)
{
	unsigned int mask;

	if (clk->flags & CLK_HAS_CTRL) {
		mask = in_be32(&clockctl->sccr[clk->reg]);
		mask |= 1 << clk->bit;
		out_be32(&clockctl->sccr[clk->reg], mask);
	}
	return 0;
}

static void mpc5121_clk_disable(struct clk *clk)
{
	unsigned int mask;

	if (clk->flags & CLK_HAS_CTRL) {
		mask = in_be32(&clockctl->sccr[clk->reg]);
		mask &= ~(1 << clk->bit);
		out_be32(&clockctl->sccr[clk->reg], mask);
	}
}

static unsigned long mpc5121_clk_get_rate(struct clk *clk)
{
	if (clk->flags & CLK_HAS_RATE)
		return clk->rate;
	else
		return 0;
}

static long mpc5121_clk_round_rate(struct clk *clk, unsigned long rate)
{
	return rate;
}

static int mpc5121_clk_set_rate(struct clk *clk, unsigned long rate)
{
	return 0;
}

static int clk_register(struct clk *clk)
{
	mutex_lock(&clocks_mutex);
	list_add(&clk->node, &clocks);
	mutex_unlock(&clocks_mutex);
	return 0;
}

static unsigned long spmf_mult(void)
{
	/*
	 * Convert spmf to multiplier
	 */
	static int spmf_to_mult[] = {
		68, 1, 12, 16,
		20, 24, 28, 32,
		36, 40, 44, 48,
		52, 56, 60, 64
	};
	int spmf = (in_be32(&clockctl->spmr) >> 24) & 0xf;
	return spmf_to_mult[spmf];
}

static unsigned long sysdiv_div_x_2(void)
{
	/*
	 * Convert sysdiv to divisor x 2
	 * Some divisors have fractional parts so
	 * multiply by 2 then divide by this value
	 */
	static int sysdiv_to_div_x_2[] = {
		4, 5, 6, 7,
		8, 9, 10, 14,
		12, 16, 18, 22,
		20, 24, 26, 30,
		28, 32, 34, 38,
		36, 40, 42, 46,
		44, 48, 50, 54,
		52, 56, 58, 62,
		60, 64, 66,
	};
	int sysdiv = (in_be32(&clockctl->scfr2) >> 26) & 0x3f;
	return sysdiv_to_div_x_2[sysdiv];
}

static unsigned long ref_to_sys(unsigned long rate)
{
	rate *= spmf_mult();
	rate *= 2;
	rate /= sysdiv_div_x_2();

	return rate;
}

static unsigned long sys_to_ref(unsigned long rate)
{
	rate *= sysdiv_div_x_2();
	rate /= 2;
	rate /= spmf_mult();

	return rate;
}

static long ips_to_ref(unsigned long rate)
{
	int ips_div = (in_be32(&clockctl->scfr1) >> 23) & 0x7;

	rate *= ips_div;	/* csb_clk = ips_clk * ips_div */
	rate *= 2;		/* sys_clk = csb_clk * 2 */
	return sys_to_ref(rate);
}

static unsigned long devtree_getfreq(char *clockname)
{
	struct device_node *np;
	const unsigned int *prop;
	unsigned int val = 0;

	np = of_find_compatible_node(NULL, NULL, "fsl,mpc5121-immr");
	if (np) {
		prop = of_get_property(np, clockname, NULL);
		if (prop)
			val = *prop;
	    of_node_put(np);
	}
	return val;
}

static void ref_clk_calc(struct clk *clk)
{
	unsigned long rate;

	rate = devtree_getfreq("bus-frequency");
	if (rate == 0) {
		printk(KERN_ERR "No bus-frequency in dev tree\n");
		clk->rate = 0;
		return;
	}
	clk->rate = ips_to_ref(rate);
}

static struct clk ref_clk = {
	.name = "ref_clk",
	.calc = ref_clk_calc,
};


static void sys_clk_calc(struct clk *clk)
{
	clk->rate = ref_to_sys(ref_clk.rate);
}

static struct clk sys_clk = {
	.name = "sys_clk",
	.calc = sys_clk_calc,
};

static void diu_clk_calc(struct clk *clk)
{
	int diudiv_x_2 = in_be32(&clockctl->scfr1) & 0xff;
	unsigned long rate;

	rate = sys_clk.rate;

	rate *= 2;
	rate /= diudiv_x_2;

	clk->rate = rate;
}

static void viu_clk_calc(struct clk *clk)
{
	unsigned long rate;

	rate = sys_clk.rate;
	rate /= 2;
	clk->rate = rate;
}

static void half_clk_calc(struct clk *clk)
{
	clk->rate = clk->parent->rate / 2;
}

static void generic_div_clk_calc(struct clk *clk)
{
	int div = (in_be32(&clockctl->scfr1) >> clk->div_shift) & 0x7;

	clk->rate = clk->parent->rate / div;
}

static void unity_clk_calc(struct clk *clk)
{
	clk->rate = clk->parent->rate;
}

static struct clk csb_clk = {
	.name = "csb_clk",
	.calc = half_clk_calc,
	.parent = &sys_clk,
};

static void e300_clk_calc(struct clk *clk)
{
	int spmf = (in_be32(&clockctl->spmr) >> 16) & 0xf;
	int ratex2 = clk->parent->rate * spmf;

	clk->rate = ratex2 / 2;
}

static struct clk e300_clk = {
	.name = "e300_clk",
	.calc = e300_clk_calc,
	.parent = &csb_clk,
};

static struct clk ips_clk = {
	.name = "ips_clk",
	.calc = generic_div_clk_calc,
	.parent = &csb_clk,
	.div_shift = 23,
};

/*
 * Clocks controlled by SCCR1 (.reg = 0)
 */
static struct clk lpc_clk = {
	.name = "lpc_clk",
	.flags = CLK_HAS_CTRL,
	.reg = 0,
	.bit = 30,
	.calc = generic_div_clk_calc,
	.parent = &ips_clk,
	.div_shift = 11,
};

static struct clk nfc_clk = {
	.name = "nfc_clk",
	.flags = CLK_HAS_CTRL,
	.reg = 0,
	.bit = 29,
	.calc = generic_div_clk_calc,
	.parent = &ips_clk,
	.div_shift = 8,
};

static struct clk pata_clk = {
	.name = "pata_clk",
	.flags = CLK_HAS_CTRL,
	.reg = 0,
	.bit = 28,
	.calc = unity_clk_calc,
	.parent = &ips_clk,
};

/*
 * PSC clocks (bits 27 - 16)
 * are setup elsewhere
 */

static struct clk sata_clk = {
	.name = "sata_clk",
	.flags = CLK_HAS_CTRL,
	.reg = 0,
	.bit = 14,
	.calc = unity_clk_calc,
	.parent = &ips_clk,
};

static struct clk fec_clk = {
	.name = "fec_clk",
	.flags = CLK_HAS_CTRL,
	.reg = 0,
	.bit = 13,
	.calc = unity_clk_calc,
	.parent = &ips_clk,
};

static struct clk pci_clk = {
	.name = "pci_clk",
	.flags = CLK_HAS_CTRL,
	.reg = 0,
	.bit = 11,
	.calc = generic_div_clk_calc,
	.parent = &csb_clk,
	.div_shift = 20,
};

/*
 * Clocks controlled by SCCR2 (.reg = 1)
 */
static struct clk diu_clk = {
	.name = "diu_clk",
	.flags = CLK_HAS_CTRL,
	.reg = 1,
	.bit = 31,
	.calc = diu_clk_calc,
};

static struct clk viu_clk = {
	.name = "viu_clk",
	.flags = CLK_HAS_CTRL,
	.reg = 1,
	.bit = 18,
	.calc = viu_clk_calc,
};

static struct clk axe_clk = {
	.name = "axe_clk",
	.flags = CLK_HAS_CTRL,
	.reg = 1,
	.bit = 30,
	.calc = unity_clk_calc,
	.parent = &csb_clk,
};

static struct clk usb1_clk = {
	.name = "usb1_clk",
	.flags = CLK_HAS_CTRL,
	.reg = 1,
	.bit = 28,
	.calc = unity_clk_calc,
	.parent = &csb_clk,
};

static struct clk usb2_clk = {
	.name = "usb2_clk",
	.flags = CLK_HAS_CTRL,
	.reg = 1,
	.bit = 27,
	.calc = unity_clk_calc,
	.parent = &csb_clk,
};

static struct clk i2c_clk = {
	.name = "i2c_clk",
	.flags = CLK_HAS_CTRL,
	.reg = 1,
	.bit = 26,
	.calc = unity_clk_calc,
	.parent = &ips_clk,
};

static struct clk mscan_clk = {
	.name = "mscan_clk",
	.flags = CLK_HAS_CTRL,
	.reg = 1,
	.bit = 25,
	.calc = unity_clk_calc,
	.parent = &ips_clk,
};

static struct clk sdhc_clk = {
	.name = "sdhc_clk",
	.flags = CLK_HAS_CTRL,
	.reg = 1,
	.bit = 24,
	.calc = unity_clk_calc,
	.parent = &ips_clk,
};

static struct clk mbx_bus_clk = {
	.name = "mbx_bus_clk",
	.flags = CLK_HAS_CTRL,
	.reg = 1,
	.bit = 22,
	.calc = half_clk_calc,
	.parent = &csb_clk,
};

static struct clk mbx_clk = {
	.name = "mbx_clk",
	.flags = CLK_HAS_CTRL,
	.reg = 1,
	.bit = 21,
	.calc = unity_clk_calc,
	.parent = &csb_clk,
};

static struct clk mbx_3d_clk = {
	.name = "mbx_3d_clk",
	.flags = CLK_HAS_CTRL,
	.reg = 1,
	.bit = 20,
	.calc = generic_div_clk_calc,
	.parent = &mbx_bus_clk,
	.div_shift = 14,
};

static void psc_mclk_in_calc(struct clk *clk)
{
	clk->rate = devtree_getfreq("psc_mclk_in");
	if (!clk->rate)
		clk->rate = 25000000;
}

static struct clk psc_mclk_in = {
	.name = "psc_mclk_in",
	.calc = psc_mclk_in_calc,
};

static struct clk spdif_txclk = {
	.name = "spdif_txclk",
	.flags = CLK_HAS_CTRL,
	.reg = 1,
	.bit = 23,
};

static struct clk spdif_rxclk = {
	.name = "spdif_rxclk",
	.flags = CLK_HAS_CTRL,
	.reg = 1,
	.bit = 23,
};

static void ac97_clk_calc(struct clk *clk)
{
	/* ac97 bit clock is always 24.567 MHz */
	clk->rate = 24567000;
}

static struct clk ac97_clk = {
	.name = "ac97_clk_in",
	.calc = ac97_clk_calc,
};

struct clk *rate_clks[] = {
	&ref_clk,
	&sys_clk,
	&diu_clk,
	&viu_clk,
	&csb_clk,
	&e300_clk,
	&ips_clk,
	&fec_clk,
	&sata_clk,
	&pata_clk,
	&nfc_clk,
	&lpc_clk,
	&mbx_bus_clk,
	&mbx_clk,
	&mbx_3d_clk,
	&axe_clk,
	&usb1_clk,
	&usb2_clk,
	&i2c_clk,
	&mscan_clk,
	&sdhc_clk,
	&pci_clk,
	&psc_mclk_in,
	&spdif_txclk,
	&spdif_rxclk,
	&ac97_clk,
	NULL
};

static void rate_clk_init(struct clk *clk)
{
	if (clk->calc) {
		clk->calc(clk);
		clk->flags |= CLK_HAS_RATE;
		clk_register(clk);
	} else {
		printk(KERN_WARNING
		       "Could not initialize clk %s without a calc routine\n",
		       clk->name);
	}
}

static void rate_clks_init(void)
{
	struct clk **cpp, *clk;

	cpp = rate_clks;
	while ((clk = *cpp++))
		rate_clk_init(clk);
}

/*
 * There are two clk enable registers with 32 enable bits each
 * psc clocks and device clocks are all stored in dev_clks
 */
struct clk dev_clks[2][32];

/*
 * Given a psc number return the dev_clk
 * associated with it
 */
static struct clk *psc_dev_clk(int pscnum)
{
	int reg, bit;
	struct clk *clk;

	reg = 0;
	bit = 27 - pscnum;

	clk = &dev_clks[reg][bit];
	clk->reg = 0;
	clk->bit = bit;
	return clk;
}

/*
 * PSC clock rate calculation
 */
static void psc_calc_rate(struct clk *clk, int pscnum, struct device_node *np)
{
	unsigned long mclk_src = sys_clk.rate;
	unsigned long mclk_div;

	/*
	 * Can only change value of mclk divider
	 * when the divider is disabled.
	 *
	 * Zero is not a valid divider so minimum
	 * divider is 1
	 *
	 * disable/set divider/enable
	 */
	out_be32(&clockctl->pccr[pscnum], 0);
	out_be32(&clockctl->pccr[pscnum], 0x00020000);
	out_be32(&clockctl->pccr[pscnum], 0x00030000);

	if (in_be32(&clockctl->pccr[pscnum]) & 0x80) {
		clk->rate = spdif_rxclk.rate;
		return;
	}

	switch ((in_be32(&clockctl->pccr[pscnum]) >> 14) & 0x3) {
	case 0:
		mclk_src = sys_clk.rate;
		break;
	case 1:
		mclk_src = ref_clk.rate;
		break;
	case 2:
		mclk_src = psc_mclk_in.rate;
		break;
	case 3:
		mclk_src = spdif_txclk.rate;
		break;
	}

	mclk_div = ((in_be32(&clockctl->pccr[pscnum]) >> 17) & 0x7fff) + 1;
	clk->rate = mclk_src / mclk_div;
}

/*
 * Find all psc nodes in device tree and assign a clock
 * with name "psc%d_mclk" and dev pointing at the device
 * returned from of_find_device_by_node
 */
static void psc_clks_init(void)
{
	struct device_node *np;
	struct platform_device *ofdev;
	u32 reg;

	for_each_compatible_node(np, NULL, "fsl,mpc5121-psc") {
		if (!of_property_read_u32(np, "reg", &reg)) {
			int pscnum = (reg & 0xf00) >> 8;
			struct clk *clk = psc_dev_clk(pscnum);

			clk->flags = CLK_HAS_RATE | CLK_HAS_CTRL;
			ofdev = of_find_device_by_node(np);
			clk->dev = &ofdev->dev;
			/*
			 * AC97 is special rate clock does
			 * not go through normal path
			 */
			if (of_device_is_compatible(np, "fsl,mpc5121-psc-ac97"))
				clk->rate = ac97_clk.rate;
			else
				psc_calc_rate(clk, pscnum, np);
			sprintf(clk->name, "psc%d_mclk", pscnum);
			clk_register(clk);
			clk_enable(clk);
		}
	}
}

static struct clk_interface mpc5121_clk_functions = {
	.clk_get		= mpc5121_clk_get,
	.clk_enable		= mpc5121_clk_enable,
	.clk_disable		= mpc5121_clk_disable,
	.clk_get_rate		= mpc5121_clk_get_rate,
	.clk_put		= mpc5121_clk_put,
	.clk_round_rate		= mpc5121_clk_round_rate,
	.clk_set_rate		= mpc5121_clk_set_rate,
	.clk_set_parent		= NULL,
	.clk_get_parent		= NULL,
};

int __init mpc5121_clk_init(void)
{
	struct device_node *np;

	np = of_find_compatible_node(NULL, NULL, "fsl,mpc5121-clock");
	if (np) {
		clockctl = of_iomap(np, 0);
		of_node_put(np);
	}

	if (!clockctl) {
		printk(KERN_ERR "Could not map clock control registers\n");
		return 0;
	}

	rate_clks_init();
	psc_clks_init();

	/* leave clockctl mapped forever */
	/*iounmap(clockctl); */
	DEBUG_CLK_DUMP();
	clocks_initialized++;
	clk_functions = mpc5121_clk_functions;
	return 0;
}
