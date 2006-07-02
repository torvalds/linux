/*
 * GPMC support functions
 *
 * Copyright (C) 2005-2006 Nokia Corporation
 *
 * Author: Juha Yrjola
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/err.h>
#include <linux/clk.h>

#include <asm/io.h>
#include <asm/arch/gpmc.h>

#undef DEBUG

#define GPMC_BASE		0x6800a000
#define GPMC_REVISION		0x00
#define GPMC_SYSCONFIG		0x10
#define GPMC_SYSSTATUS		0x14
#define GPMC_IRQSTATUS		0x18
#define GPMC_IRQENABLE		0x1c
#define GPMC_TIMEOUT_CONTROL	0x40
#define GPMC_ERR_ADDRESS	0x44
#define GPMC_ERR_TYPE		0x48
#define GPMC_CONFIG		0x50
#define GPMC_STATUS		0x54
#define GPMC_PREFETCH_CONFIG1	0x1e0
#define GPMC_PREFETCH_CONFIG2	0x1e4
#define GPMC_PREFETCH_CONTROL	0x1e8
#define GPMC_PREFETCH_STATUS	0x1f0
#define GPMC_ECC_CONFIG		0x1f4
#define GPMC_ECC_CONTROL	0x1f8
#define GPMC_ECC_SIZE_CONFIG	0x1fc

#define GPMC_CS0		0x60
#define GPMC_CS_SIZE		0x30

static void __iomem *gpmc_base =
	(void __iomem *) IO_ADDRESS(GPMC_BASE);
static void __iomem *gpmc_cs_base =
	(void __iomem *) IO_ADDRESS(GPMC_BASE) + GPMC_CS0;

static struct clk *gpmc_l3_clk;

static void gpmc_write_reg(int idx, u32 val)
{
	__raw_writel(val, gpmc_base + idx);
}

static u32 gpmc_read_reg(int idx)
{
	return __raw_readl(gpmc_base + idx);
}

void gpmc_cs_write_reg(int cs, int idx, u32 val)
{
	void __iomem *reg_addr;

	reg_addr = gpmc_cs_base + (cs * GPMC_CS_SIZE) + idx;
	__raw_writel(val, reg_addr);
}

u32 gpmc_cs_read_reg(int cs, int idx)
{
	return __raw_readl(gpmc_cs_base + (cs * GPMC_CS_SIZE) + idx);
}

/* TODO: Add support for gpmc_fck to clock framework and use it */
static unsigned long gpmc_get_fclk_period(void)
{
	/* In picoseconds */
	return 1000000000 / ((clk_get_rate(gpmc_l3_clk)) / 1000);
}

unsigned int gpmc_ns_to_ticks(unsigned int time_ns)
{
	unsigned long tick_ps;

	/* Calculate in picosecs to yield more exact results */
	tick_ps = gpmc_get_fclk_period();

	return (time_ns * 1000 + tick_ps - 1) / tick_ps;
}

#ifdef DEBUG
static int set_gpmc_timing_reg(int cs, int reg, int st_bit, int end_bit,
			       int time, const char *name)
#else
static int set_gpmc_timing_reg(int cs, int reg, int st_bit, int end_bit,
			       int time)
#endif
{
	u32 l;
	int ticks, mask, nr_bits;

	if (time == 0)
		ticks = 0;
	else
		ticks = gpmc_ns_to_ticks(time);
	nr_bits = end_bit - st_bit + 1;
	if (ticks >= 1 << nr_bits)
		return -1;

	mask = (1 << nr_bits) - 1;
	l = gpmc_cs_read_reg(cs, reg);
#ifdef DEBUG
	printk(KERN_INFO "GPMC CS%d: %-10s: %d ticks, %3lu ns (was %i ticks)\n",
	       cs, name, ticks, gpmc_get_fclk_period() * ticks / 1000,
	       (l >> st_bit) & mask);
#endif
	l &= ~(mask << st_bit);
	l |= ticks << st_bit;
	gpmc_cs_write_reg(cs, reg, l);

	return 0;
}

#ifdef DEBUG
#define GPMC_SET_ONE(reg, st, end, field) \
	if (set_gpmc_timing_reg(cs, (reg), (st), (end),		\
			t->field, #field) < 0)			\
		return -1
#else
#define GPMC_SET_ONE(reg, st, end, field) \
	if (set_gpmc_timing_reg(cs, (reg), (st), (end), t->field) < 0) \
		return -1
#endif

int gpmc_cs_calc_divider(int cs, unsigned int sync_clk)
{
	int div;
	u32 l;

	l = sync_clk * 1000 + (gpmc_get_fclk_period() - 1);
	div = l / gpmc_get_fclk_period();
	if (div > 4)
		return -1;
	if (div < 0)
		div = 1;

	return div;
}

int gpmc_cs_set_timings(int cs, const struct gpmc_timings *t)
{
	int div;
	u32 l;

	div = gpmc_cs_calc_divider(cs, t->sync_clk);
	if (div < 0)
		return -1;

	GPMC_SET_ONE(GPMC_CS_CONFIG2,  0,  3, cs_on);
	GPMC_SET_ONE(GPMC_CS_CONFIG2,  8, 12, cs_rd_off);
	GPMC_SET_ONE(GPMC_CS_CONFIG2, 16, 20, cs_wr_off);

	GPMC_SET_ONE(GPMC_CS_CONFIG3,  0,  3, adv_on);
	GPMC_SET_ONE(GPMC_CS_CONFIG3,  8, 12, adv_rd_off);
	GPMC_SET_ONE(GPMC_CS_CONFIG3, 16, 20, adv_wr_off);

	GPMC_SET_ONE(GPMC_CS_CONFIG4,  0,  3, oe_on);
	GPMC_SET_ONE(GPMC_CS_CONFIG4,  8, 12, oe_off);
	GPMC_SET_ONE(GPMC_CS_CONFIG4, 16, 19, we_on);
	GPMC_SET_ONE(GPMC_CS_CONFIG4, 24, 28, we_off);

	GPMC_SET_ONE(GPMC_CS_CONFIG5,  0,  4, rd_cycle);
	GPMC_SET_ONE(GPMC_CS_CONFIG5,  8, 12, wr_cycle);
	GPMC_SET_ONE(GPMC_CS_CONFIG5, 16, 20, access);

	GPMC_SET_ONE(GPMC_CS_CONFIG5, 24, 27, page_burst_access);

#ifdef DEBUG
	printk(KERN_INFO "GPMC CS%d CLK period is %lu (div %d)\n",
	       cs, gpmc_get_fclk_period(), div);
#endif

	l = gpmc_cs_read_reg(cs, GPMC_CS_CONFIG1);
	l &= ~0x03;
	l |= (div - 1);

	return 0;
}

unsigned long gpmc_cs_get_base_addr(int cs)
{
	return (gpmc_cs_read_reg(cs, GPMC_CS_CONFIG7) & 0x1f) << 24;
}

void __init gpmc_init(void)
{
	u32 l;

	gpmc_l3_clk = clk_get(NULL, "core_l3_ck");
	BUG_ON(IS_ERR(gpmc_l3_clk));

	l = gpmc_read_reg(GPMC_REVISION);
	printk(KERN_INFO "GPMC revision %d.%d\n", (l >> 4) & 0x0f, l & 0x0f);
	/* Set smart idle mode and automatic L3 clock gating */
	l = gpmc_read_reg(GPMC_SYSCONFIG);
	l &= 0x03 << 3;
	l |= (0x02 << 3) | (1 << 0);
	gpmc_write_reg(GPMC_SYSCONFIG, l);
}
