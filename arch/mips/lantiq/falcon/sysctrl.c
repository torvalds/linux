/*
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published
 * by the Free Software Foundation.
 *
 * Copyright (C) 2011 Thomas Langer <thomas.langer@lantiq.com>
 * Copyright (C) 2011 John Crispin <blogic@openwrt.org>
 */

#include <linux/ioport.h>
#include <linux/export.h>
#include <linux/clkdev.h>
#include <linux/of_address.h>
#include <asm/delay.h>

#include <lantiq_soc.h>

#include "../clk.h"

/* infrastructure control register */
#define SYS1_INFRAC		0x00bc
/* Configuration fuses for drivers and pll */
#define STATUS_CONFIG		0x0040

/* GPE frequency selection */
#define GPPC_OFFSET		24
#define GPEFREQ_MASK		0x00000C0
#define GPEFREQ_OFFSET		10
/* Clock status register */
#define SYSCTL_CLKS		0x0000
/* Clock enable register */
#define SYSCTL_CLKEN		0x0004
/* Clock clear register */
#define SYSCTL_CLKCLR		0x0008
/* Activation Status Register */
#define SYSCTL_ACTS		0x0020
/* Activation Register */
#define SYSCTL_ACT		0x0024
/* Deactivation Register */
#define SYSCTL_DEACT		0x0028
/* reboot Register */
#define SYSCTL_RBT		0x002c
/* CPU0 Clock Control Register */
#define SYS1_CPU0CC		0x0040
/* HRST_OUT_N Control Register */
#define SYS1_HRSTOUTC		0x00c0
/* clock divider bit */
#define CPU0CC_CPUDIV		0x0001

/* Activation Status Register */
#define ACTS_ASC0_ACT	0x00001000
#define ACTS_ASC1_ACT	0x00000800
#define ACTS_I2C_ACT	0x00004000
#define ACTS_P0		0x00010000
#define ACTS_P1		0x00010000
#define ACTS_P2		0x00020000
#define ACTS_P3		0x00020000
#define ACTS_P4		0x00040000
#define ACTS_PADCTRL0	0x00100000
#define ACTS_PADCTRL1	0x00100000
#define ACTS_PADCTRL2	0x00200000
#define ACTS_PADCTRL3	0x00200000
#define ACTS_PADCTRL4	0x00400000

#define sysctl_w32(m, x, y)	ltq_w32((x), sysctl_membase[m] + (y))
#define sysctl_r32(m, x)	ltq_r32(sysctl_membase[m] + (x))
#define sysctl_w32_mask(m, clear, set, reg)	\
		sysctl_w32(m, (sysctl_r32(m, reg) & ~(clear)) | (set), reg)

#define status_w32(x, y)	ltq_w32((x), status_membase + (y))
#define status_r32(x)		ltq_r32(status_membase + (x))

static void __iomem *sysctl_membase[3], *status_membase;
void __iomem *ltq_sys1_membase, *ltq_ebu_membase;

void falcon_trigger_hrst(int level)
{
	sysctl_w32(SYSCTL_SYS1, level & 1, SYS1_HRSTOUTC);
}

static inline void sysctl_wait(struct clk *clk,
		unsigned int test, unsigned int reg)
{
	int err = 1000000;

	do {} while (--err && ((sysctl_r32(clk->module, reg)
					& clk->bits) != test));
	if (!err)
		pr_err("module de/activation failed %d %08X %08X %08X\n",
			clk->module, clk->bits, test,
			sysctl_r32(clk->module, reg) & clk->bits);
}

static int sysctl_activate(struct clk *clk)
{
	sysctl_w32(clk->module, clk->bits, SYSCTL_CLKEN);
	sysctl_w32(clk->module, clk->bits, SYSCTL_ACT);
	sysctl_wait(clk, clk->bits, SYSCTL_ACTS);
	return 0;
}

static void sysctl_deactivate(struct clk *clk)
{
	sysctl_w32(clk->module, clk->bits, SYSCTL_CLKCLR);
	sysctl_w32(clk->module, clk->bits, SYSCTL_DEACT);
	sysctl_wait(clk, 0, SYSCTL_ACTS);
}

static int sysctl_clken(struct clk *clk)
{
	sysctl_w32(clk->module, clk->bits, SYSCTL_CLKEN);
	sysctl_w32(clk->module, clk->bits, SYSCTL_ACT);
	sysctl_wait(clk, clk->bits, SYSCTL_CLKS);
	return 0;
}

static void sysctl_clkdis(struct clk *clk)
{
	sysctl_w32(clk->module, clk->bits, SYSCTL_CLKCLR);
	sysctl_wait(clk, 0, SYSCTL_CLKS);
}

static void sysctl_reboot(struct clk *clk)
{
	unsigned int act;
	unsigned int bits;

	act = sysctl_r32(clk->module, SYSCTL_ACT);
	bits = ~act & clk->bits;
	if (bits != 0) {
		sysctl_w32(clk->module, bits, SYSCTL_CLKEN);
		sysctl_w32(clk->module, bits, SYSCTL_ACT);
		sysctl_wait(clk, bits, SYSCTL_ACTS);
	}
	sysctl_w32(clk->module, act & clk->bits, SYSCTL_RBT);
	sysctl_wait(clk, clk->bits, SYSCTL_ACTS);
}

/* enable the ONU core */
static void falcon_gpe_enable(void)
{
	unsigned int freq;
	unsigned int status;

	/* if if the clock is already enabled */
	status = sysctl_r32(SYSCTL_SYS1, SYS1_INFRAC);
	if (status & (1 << (GPPC_OFFSET + 1)))
		return;

	if (status_r32(STATUS_CONFIG) == 0)
		freq = 1; /* use 625MHz on unfused chip */
	else
		freq = (status_r32(STATUS_CONFIG) &
			GPEFREQ_MASK) >>
			GPEFREQ_OFFSET;

	/* apply new frequency */
	sysctl_w32_mask(SYSCTL_SYS1, 7 << (GPPC_OFFSET + 1),
		freq << (GPPC_OFFSET + 2) , SYS1_INFRAC);
	udelay(1);

	/* enable new frequency */
	sysctl_w32_mask(SYSCTL_SYS1, 0, 1 << (GPPC_OFFSET + 1), SYS1_INFRAC);
	udelay(1);
}

static inline void clkdev_add_sys(const char *dev, unsigned int module,
					unsigned int bits)
{
	struct clk *clk = kzalloc(sizeof(struct clk), GFP_KERNEL);

	clk->cl.dev_id = dev;
	clk->cl.con_id = NULL;
	clk->cl.clk = clk;
	clk->module = module;
	clk->bits = bits;
	clk->activate = sysctl_activate;
	clk->deactivate = sysctl_deactivate;
	clk->enable = sysctl_clken;
	clk->disable = sysctl_clkdis;
	clk->reboot = sysctl_reboot;
	clkdev_add(&clk->cl);
}

void __init ltq_soc_init(void)
{
	struct device_node *np_status =
		of_find_compatible_node(NULL, NULL, "lantiq,status-falcon");
	struct device_node *np_ebu =
		of_find_compatible_node(NULL, NULL, "lantiq,ebu-falcon");
	struct device_node *np_sys1 =
		of_find_compatible_node(NULL, NULL, "lantiq,sys1-falcon");
	struct device_node *np_syseth =
		of_find_compatible_node(NULL, NULL, "lantiq,syseth-falcon");
	struct device_node *np_sysgpe =
		of_find_compatible_node(NULL, NULL, "lantiq,sysgpe-falcon");
	struct resource res_status, res_ebu, res_sys[3];
	int i;

	/* check if all the core register ranges are available */
	if (!np_status || !np_ebu || !np_sys1 || !np_syseth || !np_sysgpe)
		panic("Failed to load core nodes from devicetree");

	if (of_address_to_resource(np_status, 0, &res_status) ||
			of_address_to_resource(np_ebu, 0, &res_ebu) ||
			of_address_to_resource(np_sys1, 0, &res_sys[0]) ||
			of_address_to_resource(np_syseth, 0, &res_sys[1]) ||
			of_address_to_resource(np_sysgpe, 0, &res_sys[2]))
		panic("Failed to get core resources");

	if ((request_mem_region(res_status.start, resource_size(&res_status),
				res_status.name) < 0) ||
		(request_mem_region(res_ebu.start, resource_size(&res_ebu),
				res_ebu.name) < 0) ||
		(request_mem_region(res_sys[0].start,
				resource_size(&res_sys[0]),
				res_sys[0].name) < 0) ||
		(request_mem_region(res_sys[1].start,
				resource_size(&res_sys[1]),
				res_sys[1].name) < 0) ||
		(request_mem_region(res_sys[2].start,
				resource_size(&res_sys[2]),
				res_sys[2].name) < 0))
		pr_err("Failed to request core resources");

	status_membase = ioremap_nocache(res_status.start,
					resource_size(&res_status));
	ltq_ebu_membase = ioremap_nocache(res_ebu.start,
					resource_size(&res_ebu));

	if (!status_membase || !ltq_ebu_membase)
		panic("Failed to remap core resources");

	for (i = 0; i < 3; i++) {
		sysctl_membase[i] = ioremap_nocache(res_sys[i].start,
						resource_size(&res_sys[i]));
		if (!sysctl_membase[i])
			panic("Failed to remap sysctrl resources");
	}
	ltq_sys1_membase = sysctl_membase[0];

	falcon_gpe_enable();

	/* get our 3 static rates for cpu, fpi and io clocks */
	if (ltq_sys1_r32(SYS1_CPU0CC) & CPU0CC_CPUDIV)
		clkdev_add_static(CLOCK_200M, CLOCK_100M, CLOCK_200M, 0);
	else
		clkdev_add_static(CLOCK_400M, CLOCK_100M, CLOCK_200M, 0);

	/* add our clock domains */
	clkdev_add_sys("1d810000.gpio", SYSCTL_SYSETH, ACTS_P0);
	clkdev_add_sys("1d810100.gpio", SYSCTL_SYSETH, ACTS_P2);
	clkdev_add_sys("1e800100.gpio", SYSCTL_SYS1, ACTS_P1);
	clkdev_add_sys("1e800200.gpio", SYSCTL_SYS1, ACTS_P3);
	clkdev_add_sys("1e800300.gpio", SYSCTL_SYS1, ACTS_P4);
	clkdev_add_sys("1db01000.pad", SYSCTL_SYSETH, ACTS_PADCTRL0);
	clkdev_add_sys("1db02000.pad", SYSCTL_SYSETH, ACTS_PADCTRL2);
	clkdev_add_sys("1e800400.pad", SYSCTL_SYS1, ACTS_PADCTRL1);
	clkdev_add_sys("1e800500.pad", SYSCTL_SYS1, ACTS_PADCTRL3);
	clkdev_add_sys("1e800600.pad", SYSCTL_SYS1, ACTS_PADCTRL4);
	clkdev_add_sys("1e100b00.serial", SYSCTL_SYS1, ACTS_ASC1_ACT);
	clkdev_add_sys("1e100c00.serial", SYSCTL_SYS1, ACTS_ASC0_ACT);
	clkdev_add_sys("1e200000.i2c", SYSCTL_SYS1, ACTS_I2C_ACT);
}
