/*
 * Emma Mobile EV2 clock framework support
 *
 * Copyright (C) 2012  Magnus Damm
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; version 2 of the License.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 */
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/io.h>
#include <linux/sh_clk.h>
#include <linux/clkdev.h>
#include <mach/common.h>

#define EMEV2_SMU_BASE 0xe0110000

/* EMEV2 SMU registers */
#define USIAU0_RSTCTRL 0x094
#define USIBU1_RSTCTRL 0x0ac
#define USIBU2_RSTCTRL 0x0b0
#define USIBU3_RSTCTRL 0x0b4
#define STI_RSTCTRL 0x124
#define USIAU0GCLKCTRL 0x4a0
#define USIBU1GCLKCTRL 0x4b8
#define USIBU2GCLKCTRL 0x4bc
#define USIBU3GCLKCTRL 0x04c0
#define STIGCLKCTRL 0x528
#define USIAU0SCLKDIV 0x61c
#define USIB2SCLKDIV 0x65c
#define USIB3SCLKDIV 0x660
#define STI_CLKSEL 0x688
#define SMU_GENERAL_REG0 0x7c0

/* not pretty, but hey */
static void __iomem *smu_base;

static void emev2_smu_write(unsigned long value, int offs)
{
	BUG_ON(!smu_base || (offs >= PAGE_SIZE));
	iowrite32(value, smu_base + offs);
}

void emev2_set_boot_vector(unsigned long value)
{
	emev2_smu_write(value, SMU_GENERAL_REG0);
}

static struct clk_mapping smu_mapping = {
	.phys	= EMEV2_SMU_BASE,
	.len	= PAGE_SIZE,
};

/* Fixed 32 KHz root clock from C32K pin */
static struct clk c32k_clk = {
	.rate           = 32768,
	.mapping	= &smu_mapping,
};

/* PLL3 multiplies C32K with 7000 */
static unsigned long pll3_recalc(struct clk *clk)
{
	return clk->parent->rate * 7000;
}

static struct sh_clk_ops pll3_clk_ops = {
	.recalc		= pll3_recalc,
};

static struct clk pll3_clk = {
	.ops		= &pll3_clk_ops,
	.parent		= &c32k_clk,
};

static struct clk *main_clks[] = {
	&c32k_clk,
	&pll3_clk,
};

enum { SCLKDIV_USIAU0, SCLKDIV_USIBU2, SCLKDIV_USIBU1, SCLKDIV_USIBU3,
	SCLKDIV_NR };

#define SCLKDIV(_reg, _shift)			\
{								\
	.parent		= &pll3_clk,				\
	.enable_reg	= IOMEM(EMEV2_SMU_BASE + (_reg)),	\
	.enable_bit	= _shift,				\
}

static struct clk sclkdiv_clks[SCLKDIV_NR] = {
	[SCLKDIV_USIAU0] = SCLKDIV(USIAU0SCLKDIV, 0),
	[SCLKDIV_USIBU2] = SCLKDIV(USIB2SCLKDIV, 16),
	[SCLKDIV_USIBU1] = SCLKDIV(USIB2SCLKDIV, 0),
	[SCLKDIV_USIBU3] = SCLKDIV(USIB3SCLKDIV, 0),
};

enum { GCLK_USIAU0_SCLK, GCLK_USIBU1_SCLK, GCLK_USIBU2_SCLK, GCLK_USIBU3_SCLK,
	GCLK_STI_SCLK,
	GCLK_NR };

#define GCLK_SCLK(_parent, _reg) \
{								\
	.parent		= _parent,				\
	.enable_reg	= IOMEM(EMEV2_SMU_BASE + (_reg)),	\
	.enable_bit	= 1, /* SCLK_GCC */			\
}

static struct clk gclk_clks[GCLK_NR] = {
	[GCLK_USIAU0_SCLK] = GCLK_SCLK(&sclkdiv_clks[SCLKDIV_USIAU0],
				       USIAU0GCLKCTRL),
	[GCLK_USIBU1_SCLK] = GCLK_SCLK(&sclkdiv_clks[SCLKDIV_USIBU1],
				       USIBU1GCLKCTRL),
	[GCLK_USIBU2_SCLK] = GCLK_SCLK(&sclkdiv_clks[SCLKDIV_USIBU2],
				       USIBU2GCLKCTRL),
	[GCLK_USIBU3_SCLK] = GCLK_SCLK(&sclkdiv_clks[SCLKDIV_USIBU3],
				       USIBU3GCLKCTRL),
	[GCLK_STI_SCLK] = GCLK_SCLK(&c32k_clk, STIGCLKCTRL),
};

static int emev2_gclk_enable(struct clk *clk)
{
	iowrite32(ioread32(clk->mapped_reg) | (1 << clk->enable_bit),
		  clk->mapped_reg);
	return 0;
}

static void emev2_gclk_disable(struct clk *clk)
{
	iowrite32(ioread32(clk->mapped_reg) & ~(1 << clk->enable_bit),
		  clk->mapped_reg);
}

static struct sh_clk_ops emev2_gclk_clk_ops = {
	.enable		= emev2_gclk_enable,
	.disable	= emev2_gclk_disable,
	.recalc		= followparent_recalc,
};

static int __init emev2_gclk_register(struct clk *clks, int nr)
{
	struct clk *clkp;
	int ret = 0;
	int k;

	for (k = 0; !ret && (k < nr); k++) {
		clkp = clks + k;
		clkp->ops = &emev2_gclk_clk_ops;
		ret |= clk_register(clkp);
	}

	return ret;
}

static unsigned long emev2_sclkdiv_recalc(struct clk *clk)
{
	unsigned int sclk_div;

	sclk_div = (ioread32(clk->mapped_reg) >> clk->enable_bit) & 0xff;

	return clk->parent->rate / (sclk_div + 1);
}

static struct sh_clk_ops emev2_sclkdiv_clk_ops = {
	.recalc		= emev2_sclkdiv_recalc,
};

static int __init emev2_sclkdiv_register(struct clk *clks, int nr)
{
	struct clk *clkp;
	int ret = 0;
	int k;

	for (k = 0; !ret && (k < nr); k++) {
		clkp = clks + k;
		clkp->ops = &emev2_sclkdiv_clk_ops;
		ret |= clk_register(clkp);
	}

	return ret;
}

static struct clk_lookup lookups[] = {
	CLKDEV_DEV_ID("serial8250-em.0", &gclk_clks[GCLK_USIAU0_SCLK]),
	CLKDEV_DEV_ID("e1020000.uart", &gclk_clks[GCLK_USIAU0_SCLK]),
	CLKDEV_DEV_ID("serial8250-em.1", &gclk_clks[GCLK_USIBU1_SCLK]),
	CLKDEV_DEV_ID("e1030000.uart", &gclk_clks[GCLK_USIBU1_SCLK]),
	CLKDEV_DEV_ID("serial8250-em.2", &gclk_clks[GCLK_USIBU2_SCLK]),
	CLKDEV_DEV_ID("e1040000.uart", &gclk_clks[GCLK_USIBU2_SCLK]),
	CLKDEV_DEV_ID("serial8250-em.3", &gclk_clks[GCLK_USIBU3_SCLK]),
	CLKDEV_DEV_ID("e1050000.uart", &gclk_clks[GCLK_USIBU3_SCLK]),
	CLKDEV_DEV_ID("em_sti.0", &gclk_clks[GCLK_STI_SCLK]),
	CLKDEV_DEV_ID("e0180000.sti", &gclk_clks[GCLK_STI_SCLK]),
};

void __init emev2_clock_init(void)
{
	int k, ret = 0;
	static int is_setup;

	/* yuck, this is ugly as hell, but the non-smp case of clocks
	 * code is now designed to rely on ioremap() instead of static
	 * entity maps. in the case of smp we need access to the SMU
	 * register earlier than ioremap() is actually working without
	 * any static maps. to enable SMP in ugly but with dynamic
	 * mappings we have to call emev2_clock_init() from different
	 * places depending on UP and SMP...
	 */
	if (is_setup++)
		return;

	smu_base = ioremap(EMEV2_SMU_BASE, PAGE_SIZE);
	BUG_ON(!smu_base);

	/* setup STI timer to run on 32.768 kHz and deassert reset */
	emev2_smu_write(0, STI_CLKSEL);
	emev2_smu_write(1, STI_RSTCTRL);

	/* deassert reset for UART0->UART3 */
	emev2_smu_write(2, USIAU0_RSTCTRL);
	emev2_smu_write(2, USIBU1_RSTCTRL);
	emev2_smu_write(2, USIBU2_RSTCTRL);
	emev2_smu_write(2, USIBU3_RSTCTRL);

	for (k = 0; !ret && (k < ARRAY_SIZE(main_clks)); k++)
		ret = clk_register(main_clks[k]);

	if (!ret)
		ret = emev2_sclkdiv_register(sclkdiv_clks, SCLKDIV_NR);

	if (!ret)
		ret = emev2_gclk_register(gclk_clks, GCLK_NR);

	clkdev_add_table(lookups, ARRAY_SIZE(lookups));

	if (!ret)
		shmobile_clk_init();
	else
		panic("failed to setup emev2 clocks\n");
}
