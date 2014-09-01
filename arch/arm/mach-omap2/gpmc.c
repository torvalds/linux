/*
 * GPMC support functions
 *
 * Copyright (C) 2005-2006 Nokia Corporation
 *
 * Author: Juha Yrjola
 *
 * Copyright (C) 2009 Texas Instruments
 * Added OMAP4 support - Santosh Shilimkar <santosh.shilimkar@ti.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#undef DEBUG

#include <linux/irq.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/err.h>
#include <linux/clk.h>
#include <linux/ioport.h>
#include <linux/spinlock.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/interrupt.h>
#include <linux/platform_device.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/of_mtd.h>
#include <linux/of_device.h>
#include <linux/mtd/nand.h>
#include <linux/pm_runtime.h>

#include <linux/platform_data/mtd-nand-omap2.h>

#include <asm/mach-types.h>

#include "soc.h"
#include "common.h"
#include "omap_device.h"
#include "gpmc.h"
#include "gpmc-nand.h"
#include "gpmc-onenand.h"

#define	DEVICE_NAME		"omap-gpmc"

/* GPMC register offsets */
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
#define GPMC_PREFETCH_CONTROL	0x1ec
#define GPMC_PREFETCH_STATUS	0x1f0
#define GPMC_ECC_CONFIG		0x1f4
#define GPMC_ECC_CONTROL	0x1f8
#define GPMC_ECC_SIZE_CONFIG	0x1fc
#define GPMC_ECC1_RESULT        0x200
#define GPMC_ECC_BCH_RESULT_0   0x240   /* not available on OMAP2 */
#define	GPMC_ECC_BCH_RESULT_1	0x244	/* not available on OMAP2 */
#define	GPMC_ECC_BCH_RESULT_2	0x248	/* not available on OMAP2 */
#define	GPMC_ECC_BCH_RESULT_3	0x24c	/* not available on OMAP2 */
#define	GPMC_ECC_BCH_RESULT_4	0x300	/* not available on OMAP2 */
#define	GPMC_ECC_BCH_RESULT_5	0x304	/* not available on OMAP2 */
#define	GPMC_ECC_BCH_RESULT_6	0x308	/* not available on OMAP2 */

/* GPMC ECC control settings */
#define GPMC_ECC_CTRL_ECCCLEAR		0x100
#define GPMC_ECC_CTRL_ECCDISABLE	0x000
#define GPMC_ECC_CTRL_ECCREG1		0x001
#define GPMC_ECC_CTRL_ECCREG2		0x002
#define GPMC_ECC_CTRL_ECCREG3		0x003
#define GPMC_ECC_CTRL_ECCREG4		0x004
#define GPMC_ECC_CTRL_ECCREG5		0x005
#define GPMC_ECC_CTRL_ECCREG6		0x006
#define GPMC_ECC_CTRL_ECCREG7		0x007
#define GPMC_ECC_CTRL_ECCREG8		0x008
#define GPMC_ECC_CTRL_ECCREG9		0x009

#define	GPMC_CONFIG2_CSEXTRADELAY		BIT(7)
#define	GPMC_CONFIG3_ADVEXTRADELAY		BIT(7)
#define	GPMC_CONFIG4_OEEXTRADELAY		BIT(7)
#define	GPMC_CONFIG4_WEEXTRADELAY		BIT(23)
#define	GPMC_CONFIG6_CYCLE2CYCLEDIFFCSEN	BIT(6)
#define	GPMC_CONFIG6_CYCLE2CYCLESAMECSEN	BIT(7)

#define GPMC_CS0_OFFSET		0x60
#define GPMC_CS_SIZE		0x30
#define	GPMC_BCH_SIZE		0x10

#define GPMC_MEM_END		0x3FFFFFFF

#define GPMC_CHUNK_SHIFT	24		/* 16 MB */
#define GPMC_SECTION_SHIFT	28		/* 128 MB */

#define CS_NUM_SHIFT		24
#define ENABLE_PREFETCH		(0x1 << 7)
#define DMA_MPU_MODE		2

#define	GPMC_REVISION_MAJOR(l)		((l >> 4) & 0xf)
#define	GPMC_REVISION_MINOR(l)		(l & 0xf)

#define	GPMC_HAS_WR_ACCESS		0x1
#define	GPMC_HAS_WR_DATA_MUX_BUS	0x2
#define	GPMC_HAS_MUX_AAD		0x4

#define GPMC_NR_WAITPINS		4

/* XXX: Only NAND irq has been considered,currently these are the only ones used
 */
#define	GPMC_NR_IRQ		2

struct gpmc_client_irq	{
	unsigned		irq;
	u32			bitmask;
};

/* Structure to save gpmc cs context */
struct gpmc_cs_config {
	u32 config1;
	u32 config2;
	u32 config3;
	u32 config4;
	u32 config5;
	u32 config6;
	u32 config7;
	int is_valid;
};

/*
 * Structure to save/restore gpmc context
 * to support core off on OMAP3
 */
struct omap3_gpmc_regs {
	u32 sysconfig;
	u32 irqenable;
	u32 timeout_ctrl;
	u32 config;
	u32 prefetch_config1;
	u32 prefetch_config2;
	u32 prefetch_control;
	struct gpmc_cs_config cs_context[GPMC_CS_NUM];
};

static struct gpmc_client_irq gpmc_client_irq[GPMC_NR_IRQ];
static struct irq_chip gpmc_irq_chip;
static int gpmc_irq_start;

static struct resource	gpmc_mem_root;
static struct resource	gpmc_cs_mem[GPMC_CS_NUM];
static DEFINE_SPINLOCK(gpmc_mem_lock);
/* Define chip-selects as reserved by default until probe completes */
static unsigned int gpmc_cs_map = ((1 << GPMC_CS_NUM) - 1);
static unsigned int gpmc_cs_num = GPMC_CS_NUM;
static unsigned int gpmc_nr_waitpins;
static struct device *gpmc_dev;
static int gpmc_irq;
static resource_size_t phys_base, mem_size;
static unsigned gpmc_capability;
static void __iomem *gpmc_base;

static struct clk *gpmc_l3_clk;

static irqreturn_t gpmc_handle_irq(int irq, void *dev);

static void gpmc_write_reg(int idx, u32 val)
{
	writel_relaxed(val, gpmc_base + idx);
}

static u32 gpmc_read_reg(int idx)
{
	return readl_relaxed(gpmc_base + idx);
}

void gpmc_cs_write_reg(int cs, int idx, u32 val)
{
	void __iomem *reg_addr;

	reg_addr = gpmc_base + GPMC_CS0_OFFSET + (cs * GPMC_CS_SIZE) + idx;
	writel_relaxed(val, reg_addr);
}

static u32 gpmc_cs_read_reg(int cs, int idx)
{
	void __iomem *reg_addr;

	reg_addr = gpmc_base + GPMC_CS0_OFFSET + (cs * GPMC_CS_SIZE) + idx;
	return readl_relaxed(reg_addr);
}

/* TODO: Add support for gpmc_fck to clock framework and use it */
static unsigned long gpmc_get_fclk_period(void)
{
	unsigned long rate = clk_get_rate(gpmc_l3_clk);

	if (rate == 0) {
		printk(KERN_WARNING "gpmc_l3_clk not enabled\n");
		return 0;
	}

	rate /= 1000;
	rate = 1000000000 / rate;	/* In picoseconds */

	return rate;
}

static unsigned int gpmc_ns_to_ticks(unsigned int time_ns)
{
	unsigned long tick_ps;

	/* Calculate in picosecs to yield more exact results */
	tick_ps = gpmc_get_fclk_period();

	return (time_ns * 1000 + tick_ps - 1) / tick_ps;
}

static unsigned int gpmc_ps_to_ticks(unsigned int time_ps)
{
	unsigned long tick_ps;

	/* Calculate in picosecs to yield more exact results */
	tick_ps = gpmc_get_fclk_period();

	return (time_ps + tick_ps - 1) / tick_ps;
}

unsigned int gpmc_ticks_to_ns(unsigned int ticks)
{
	return ticks * gpmc_get_fclk_period() / 1000;
}

static unsigned int gpmc_ticks_to_ps(unsigned int ticks)
{
	return ticks * gpmc_get_fclk_period();
}

static unsigned int gpmc_round_ps_to_ticks(unsigned int time_ps)
{
	unsigned long ticks = gpmc_ps_to_ticks(time_ps);

	return ticks * gpmc_get_fclk_period();
}

static inline void gpmc_cs_modify_reg(int cs, int reg, u32 mask, bool value)
{
	u32 l;

	l = gpmc_cs_read_reg(cs, reg);
	if (value)
		l |= mask;
	else
		l &= ~mask;
	gpmc_cs_write_reg(cs, reg, l);
}

static void gpmc_cs_bool_timings(int cs, const struct gpmc_bool_timings *p)
{
	gpmc_cs_modify_reg(cs, GPMC_CS_CONFIG1,
			   GPMC_CONFIG1_TIME_PARA_GRAN,
			   p->time_para_granularity);
	gpmc_cs_modify_reg(cs, GPMC_CS_CONFIG2,
			   GPMC_CONFIG2_CSEXTRADELAY, p->cs_extra_delay);
	gpmc_cs_modify_reg(cs, GPMC_CS_CONFIG3,
			   GPMC_CONFIG3_ADVEXTRADELAY, p->adv_extra_delay);
	gpmc_cs_modify_reg(cs, GPMC_CS_CONFIG4,
			   GPMC_CONFIG4_OEEXTRADELAY, p->oe_extra_delay);
	gpmc_cs_modify_reg(cs, GPMC_CS_CONFIG4,
			   GPMC_CONFIG4_OEEXTRADELAY, p->we_extra_delay);
	gpmc_cs_modify_reg(cs, GPMC_CS_CONFIG6,
			   GPMC_CONFIG6_CYCLE2CYCLESAMECSEN,
			   p->cycle2cyclesamecsen);
	gpmc_cs_modify_reg(cs, GPMC_CS_CONFIG6,
			   GPMC_CONFIG6_CYCLE2CYCLEDIFFCSEN,
			   p->cycle2cyclediffcsen);
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
	if (ticks >= 1 << nr_bits) {
#ifdef DEBUG
		printk(KERN_INFO "GPMC CS%d: %-10s* %3d ns, %3d ticks >= %d\n",
				cs, name, time, ticks, 1 << nr_bits);
#endif
		return -1;
	}

	mask = (1 << nr_bits) - 1;
	l = gpmc_cs_read_reg(cs, reg);
#ifdef DEBUG
	printk(KERN_INFO
		"GPMC CS%d: %-10s: %3d ticks, %3lu ns (was %3i ticks) %3d ns\n",
	       cs, name, ticks, gpmc_get_fclk_period() * ticks / 1000,
			(l >> st_bit) & mask, time);
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

int gpmc_calc_divider(unsigned int sync_clk)
{
	int div;
	u32 l;

	l = sync_clk + (gpmc_get_fclk_period() - 1);
	div = l / gpmc_get_fclk_period();
	if (div > 4)
		return -1;
	if (div <= 0)
		div = 1;

	return div;
}

int gpmc_cs_set_timings(int cs, const struct gpmc_timings *t)
{
	int div;
	u32 l;

	div = gpmc_calc_divider(t->sync_clk);
	if (div < 0)
		return div;

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

	GPMC_SET_ONE(GPMC_CS_CONFIG6, 0, 3, bus_turnaround);
	GPMC_SET_ONE(GPMC_CS_CONFIG6, 8, 11, cycle2cycle_delay);

	GPMC_SET_ONE(GPMC_CS_CONFIG1, 18, 19, wait_monitoring);
	GPMC_SET_ONE(GPMC_CS_CONFIG1, 25, 26, clk_activation);

	if (gpmc_capability & GPMC_HAS_WR_DATA_MUX_BUS)
		GPMC_SET_ONE(GPMC_CS_CONFIG6, 16, 19, wr_data_mux_bus);
	if (gpmc_capability & GPMC_HAS_WR_ACCESS)
		GPMC_SET_ONE(GPMC_CS_CONFIG6, 24, 28, wr_access);

	/* caller is expected to have initialized CONFIG1 to cover
	 * at least sync vs async
	 */
	l = gpmc_cs_read_reg(cs, GPMC_CS_CONFIG1);
	if (l & (GPMC_CONFIG1_READTYPE_SYNC | GPMC_CONFIG1_WRITETYPE_SYNC)) {
#ifdef DEBUG
		printk(KERN_INFO "GPMC CS%d CLK period is %lu ns (div %d)\n",
				cs, (div * gpmc_get_fclk_period()) / 1000, div);
#endif
		l &= ~0x03;
		l |= (div - 1);
		gpmc_cs_write_reg(cs, GPMC_CS_CONFIG1, l);
	}

	gpmc_cs_bool_timings(cs, &t->bool_timings);

	return 0;
}

static int gpmc_cs_enable_mem(int cs, u32 base, u32 size)
{
	u32 l;
	u32 mask;

	/*
	 * Ensure that base address is aligned on a
	 * boundary equal to or greater than size.
	 */
	if (base & (size - 1))
		return -EINVAL;

	mask = (1 << GPMC_SECTION_SHIFT) - size;
	l = gpmc_cs_read_reg(cs, GPMC_CS_CONFIG7);
	l &= ~0x3f;
	l = (base >> GPMC_CHUNK_SHIFT) & 0x3f;
	l &= ~(0x0f << 8);
	l |= ((mask >> GPMC_CHUNK_SHIFT) & 0x0f) << 8;
	l |= GPMC_CONFIG7_CSVALID;
	gpmc_cs_write_reg(cs, GPMC_CS_CONFIG7, l);

	return 0;
}

static void gpmc_cs_disable_mem(int cs)
{
	u32 l;

	l = gpmc_cs_read_reg(cs, GPMC_CS_CONFIG7);
	l &= ~GPMC_CONFIG7_CSVALID;
	gpmc_cs_write_reg(cs, GPMC_CS_CONFIG7, l);
}

static void gpmc_cs_get_memconf(int cs, u32 *base, u32 *size)
{
	u32 l;
	u32 mask;

	l = gpmc_cs_read_reg(cs, GPMC_CS_CONFIG7);
	*base = (l & 0x3f) << GPMC_CHUNK_SHIFT;
	mask = (l >> 8) & 0x0f;
	*size = (1 << GPMC_SECTION_SHIFT) - (mask << GPMC_CHUNK_SHIFT);
}

static int gpmc_cs_mem_enabled(int cs)
{
	u32 l;

	l = gpmc_cs_read_reg(cs, GPMC_CS_CONFIG7);
	return l & GPMC_CONFIG7_CSVALID;
}

static void gpmc_cs_set_reserved(int cs, int reserved)
{
	gpmc_cs_map &= ~(1 << cs);
	gpmc_cs_map |= (reserved ? 1 : 0) << cs;
}

static bool gpmc_cs_reserved(int cs)
{
	return gpmc_cs_map & (1 << cs);
}

static unsigned long gpmc_mem_align(unsigned long size)
{
	int order;

	size = (size - 1) >> (GPMC_CHUNK_SHIFT - 1);
	order = GPMC_CHUNK_SHIFT - 1;
	do {
		size >>= 1;
		order++;
	} while (size);
	size = 1 << order;
	return size;
}

static int gpmc_cs_insert_mem(int cs, unsigned long base, unsigned long size)
{
	struct resource	*res = &gpmc_cs_mem[cs];
	int r;

	size = gpmc_mem_align(size);
	spin_lock(&gpmc_mem_lock);
	res->start = base;
	res->end = base + size - 1;
	r = request_resource(&gpmc_mem_root, res);
	spin_unlock(&gpmc_mem_lock);

	return r;
}

static int gpmc_cs_delete_mem(int cs)
{
	struct resource	*res = &gpmc_cs_mem[cs];
	int r;

	spin_lock(&gpmc_mem_lock);
	r = release_resource(res);
	res->start = 0;
	res->end = 0;
	spin_unlock(&gpmc_mem_lock);

	return r;
}

/**
 * gpmc_cs_remap - remaps a chip-select physical base address
 * @cs:		chip-select to remap
 * @base:	physical base address to re-map chip-select to
 *
 * Re-maps a chip-select to a new physical base address specified by
 * "base". Returns 0 on success and appropriate negative error code
 * on failure.
 */
static int gpmc_cs_remap(int cs, u32 base)
{
	int ret;
	u32 old_base, size;

	if (cs > gpmc_cs_num) {
		pr_err("%s: requested chip-select is disabled\n", __func__);
		return -ENODEV;
	}

	/*
	 * Make sure we ignore any device offsets from the GPMC partition
	 * allocated for the chip select and that the new base confirms
	 * to the GPMC 16MB minimum granularity.
	 */ 
	base &= ~(SZ_16M - 1);

	gpmc_cs_get_memconf(cs, &old_base, &size);
	if (base == old_base)
		return 0;
	gpmc_cs_disable_mem(cs);
	ret = gpmc_cs_delete_mem(cs);
	if (ret < 0)
		return ret;
	ret = gpmc_cs_insert_mem(cs, base, size);
	if (ret < 0)
		return ret;
	ret = gpmc_cs_enable_mem(cs, base, size);
	if (ret < 0)
		return ret;

	return 0;
}

int gpmc_cs_request(int cs, unsigned long size, unsigned long *base)
{
	struct resource *res = &gpmc_cs_mem[cs];
	int r = -1;

	if (cs > gpmc_cs_num) {
		pr_err("%s: requested chip-select is disabled\n", __func__);
		return -ENODEV;
	}
	size = gpmc_mem_align(size);
	if (size > (1 << GPMC_SECTION_SHIFT))
		return -ENOMEM;

	spin_lock(&gpmc_mem_lock);
	if (gpmc_cs_reserved(cs)) {
		r = -EBUSY;
		goto out;
	}
	if (gpmc_cs_mem_enabled(cs))
		r = adjust_resource(res, res->start & ~(size - 1), size);
	if (r < 0)
		r = allocate_resource(&gpmc_mem_root, res, size, 0, ~0,
				      size, NULL, NULL);
	if (r < 0)
		goto out;

	r = gpmc_cs_enable_mem(cs, res->start, resource_size(res));
	if (r < 0) {
		release_resource(res);
		goto out;
	}

	*base = res->start;
	gpmc_cs_set_reserved(cs, 1);
out:
	spin_unlock(&gpmc_mem_lock);
	return r;
}
EXPORT_SYMBOL(gpmc_cs_request);

void gpmc_cs_free(int cs)
{
	struct resource	*res = &gpmc_cs_mem[cs];

	spin_lock(&gpmc_mem_lock);
	if (cs >= gpmc_cs_num || cs < 0 || !gpmc_cs_reserved(cs)) {
		printk(KERN_ERR "Trying to free non-reserved GPMC CS%d\n", cs);
		BUG();
		spin_unlock(&gpmc_mem_lock);
		return;
	}
	gpmc_cs_disable_mem(cs);
	if (res->flags)
		release_resource(res);
	gpmc_cs_set_reserved(cs, 0);
	spin_unlock(&gpmc_mem_lock);
}
EXPORT_SYMBOL(gpmc_cs_free);

/**
 * gpmc_configure - write request to configure gpmc
 * @cmd: command type
 * @wval: value to write
 * @return status of the operation
 */
int gpmc_configure(int cmd, int wval)
{
	u32 regval;

	switch (cmd) {
	case GPMC_ENABLE_IRQ:
		gpmc_write_reg(GPMC_IRQENABLE, wval);
		break;

	case GPMC_SET_IRQ_STATUS:
		gpmc_write_reg(GPMC_IRQSTATUS, wval);
		break;

	case GPMC_CONFIG_WP:
		regval = gpmc_read_reg(GPMC_CONFIG);
		if (wval)
			regval &= ~GPMC_CONFIG_WRITEPROTECT; /* WP is ON */
		else
			regval |= GPMC_CONFIG_WRITEPROTECT;  /* WP is OFF */
		gpmc_write_reg(GPMC_CONFIG, regval);
		break;

	default:
		pr_err("%s: command not supported\n", __func__);
		return -EINVAL;
	}

	return 0;
}
EXPORT_SYMBOL(gpmc_configure);

void gpmc_update_nand_reg(struct gpmc_nand_regs *reg, int cs)
{
	int i;

	reg->gpmc_status = gpmc_base + GPMC_STATUS;
	reg->gpmc_nand_command = gpmc_base + GPMC_CS0_OFFSET +
				GPMC_CS_NAND_COMMAND + GPMC_CS_SIZE * cs;
	reg->gpmc_nand_address = gpmc_base + GPMC_CS0_OFFSET +
				GPMC_CS_NAND_ADDRESS + GPMC_CS_SIZE * cs;
	reg->gpmc_nand_data = gpmc_base + GPMC_CS0_OFFSET +
				GPMC_CS_NAND_DATA + GPMC_CS_SIZE * cs;
	reg->gpmc_prefetch_config1 = gpmc_base + GPMC_PREFETCH_CONFIG1;
	reg->gpmc_prefetch_config2 = gpmc_base + GPMC_PREFETCH_CONFIG2;
	reg->gpmc_prefetch_control = gpmc_base + GPMC_PREFETCH_CONTROL;
	reg->gpmc_prefetch_status = gpmc_base + GPMC_PREFETCH_STATUS;
	reg->gpmc_ecc_config = gpmc_base + GPMC_ECC_CONFIG;
	reg->gpmc_ecc_control = gpmc_base + GPMC_ECC_CONTROL;
	reg->gpmc_ecc_size_config = gpmc_base + GPMC_ECC_SIZE_CONFIG;
	reg->gpmc_ecc1_result = gpmc_base + GPMC_ECC1_RESULT;

	for (i = 0; i < GPMC_BCH_NUM_REMAINDER; i++) {
		reg->gpmc_bch_result0[i] = gpmc_base + GPMC_ECC_BCH_RESULT_0 +
					   GPMC_BCH_SIZE * i;
		reg->gpmc_bch_result1[i] = gpmc_base + GPMC_ECC_BCH_RESULT_1 +
					   GPMC_BCH_SIZE * i;
		reg->gpmc_bch_result2[i] = gpmc_base + GPMC_ECC_BCH_RESULT_2 +
					   GPMC_BCH_SIZE * i;
		reg->gpmc_bch_result3[i] = gpmc_base + GPMC_ECC_BCH_RESULT_3 +
					   GPMC_BCH_SIZE * i;
		reg->gpmc_bch_result4[i] = gpmc_base + GPMC_ECC_BCH_RESULT_4 +
					   i * GPMC_BCH_SIZE;
		reg->gpmc_bch_result5[i] = gpmc_base + GPMC_ECC_BCH_RESULT_5 +
					   i * GPMC_BCH_SIZE;
		reg->gpmc_bch_result6[i] = gpmc_base + GPMC_ECC_BCH_RESULT_6 +
					   i * GPMC_BCH_SIZE;
	}
}

int gpmc_get_client_irq(unsigned irq_config)
{
	int i;

	if (hweight32(irq_config) > 1)
		return 0;

	for (i = 0; i < GPMC_NR_IRQ; i++)
		if (gpmc_client_irq[i].bitmask & irq_config)
			return gpmc_client_irq[i].irq;

	return 0;
}

static int gpmc_irq_endis(unsigned irq, bool endis)
{
	int i;
	u32 regval;

	for (i = 0; i < GPMC_NR_IRQ; i++)
		if (irq == gpmc_client_irq[i].irq) {
			regval = gpmc_read_reg(GPMC_IRQENABLE);
			if (endis)
				regval |= gpmc_client_irq[i].bitmask;
			else
				regval &= ~gpmc_client_irq[i].bitmask;
			gpmc_write_reg(GPMC_IRQENABLE, regval);
			break;
		}

	return 0;
}

static void gpmc_irq_disable(struct irq_data *p)
{
	gpmc_irq_endis(p->irq, false);
}

static void gpmc_irq_enable(struct irq_data *p)
{
	gpmc_irq_endis(p->irq, true);
}

static void gpmc_irq_noop(struct irq_data *data) { }

static unsigned int gpmc_irq_noop_ret(struct irq_data *data) { return 0; }

static int gpmc_setup_irq(void)
{
	int i;
	u32 regval;

	if (!gpmc_irq)
		return -EINVAL;

	gpmc_irq_start = irq_alloc_descs(-1, 0, GPMC_NR_IRQ, 0);
	if (gpmc_irq_start < 0) {
		pr_err("irq_alloc_descs failed\n");
		return gpmc_irq_start;
	}

	gpmc_irq_chip.name = "gpmc";
	gpmc_irq_chip.irq_startup = gpmc_irq_noop_ret;
	gpmc_irq_chip.irq_enable = gpmc_irq_enable;
	gpmc_irq_chip.irq_disable = gpmc_irq_disable;
	gpmc_irq_chip.irq_shutdown = gpmc_irq_noop;
	gpmc_irq_chip.irq_ack = gpmc_irq_noop;
	gpmc_irq_chip.irq_mask = gpmc_irq_noop;
	gpmc_irq_chip.irq_unmask = gpmc_irq_noop;

	gpmc_client_irq[0].bitmask = GPMC_IRQ_FIFOEVENTENABLE;
	gpmc_client_irq[1].bitmask = GPMC_IRQ_COUNT_EVENT;

	for (i = 0; i < GPMC_NR_IRQ; i++) {
		gpmc_client_irq[i].irq = gpmc_irq_start + i;
		irq_set_chip_and_handler(gpmc_client_irq[i].irq,
					&gpmc_irq_chip, handle_simple_irq);
		set_irq_flags(gpmc_client_irq[i].irq,
				IRQF_VALID | IRQF_NOAUTOEN);
	}

	/* Disable interrupts */
	gpmc_write_reg(GPMC_IRQENABLE, 0);

	/* clear interrupts */
	regval = gpmc_read_reg(GPMC_IRQSTATUS);
	gpmc_write_reg(GPMC_IRQSTATUS, regval);

	return request_irq(gpmc_irq, gpmc_handle_irq, 0, "gpmc", NULL);
}

static int gpmc_free_irq(void)
{
	int i;

	if (gpmc_irq)
		free_irq(gpmc_irq, NULL);

	for (i = 0; i < GPMC_NR_IRQ; i++) {
		irq_set_handler(gpmc_client_irq[i].irq, NULL);
		irq_set_chip(gpmc_client_irq[i].irq, &no_irq_chip);
		irq_modify_status(gpmc_client_irq[i].irq, 0, 0);
	}

	irq_free_descs(gpmc_irq_start, GPMC_NR_IRQ);

	return 0;
}

static void gpmc_mem_exit(void)
{
	int cs;

	for (cs = 0; cs < gpmc_cs_num; cs++) {
		if (!gpmc_cs_mem_enabled(cs))
			continue;
		gpmc_cs_delete_mem(cs);
	}

}

static void gpmc_mem_init(void)
{
	int cs;

	/*
	 * The first 1MB of GPMC address space is typically mapped to
	 * the internal ROM. Never allocate the first page, to
	 * facilitate bug detection; even if we didn't boot from ROM.
	 */
	gpmc_mem_root.start = SZ_1M;
	gpmc_mem_root.end = GPMC_MEM_END;

	/* Reserve all regions that has been set up by bootloader */
	for (cs = 0; cs < gpmc_cs_num; cs++) {
		u32 base, size;

		if (!gpmc_cs_mem_enabled(cs))
			continue;
		gpmc_cs_get_memconf(cs, &base, &size);
		if (gpmc_cs_insert_mem(cs, base, size)) {
			pr_warn("%s: disabling cs %d mapped at 0x%x-0x%x\n",
				__func__, cs, base, base + size);
			gpmc_cs_disable_mem(cs);
		}
	}
}

static u32 gpmc_round_ps_to_sync_clk(u32 time_ps, u32 sync_clk)
{
	u32 temp;
	int div;

	div = gpmc_calc_divider(sync_clk);
	temp = gpmc_ps_to_ticks(time_ps);
	temp = (temp + div - 1) / div;
	return gpmc_ticks_to_ps(temp * div);
}

/* XXX: can the cycles be avoided ? */
static int gpmc_calc_sync_read_timings(struct gpmc_timings *gpmc_t,
				       struct gpmc_device_timings *dev_t,
				       bool mux)
{
	u32 temp;

	/* adv_rd_off */
	temp = dev_t->t_avdp_r;
	/* XXX: mux check required ? */
	if (mux) {
		/* XXX: t_avdp not to be required for sync, only added for tusb
		 * this indirectly necessitates requirement of t_avdp_r and
		 * t_avdp_w instead of having a single t_avdp
		 */
		temp = max_t(u32, temp,	gpmc_t->clk_activation + dev_t->t_avdh);
		temp = max_t(u32, gpmc_t->adv_on + gpmc_ticks_to_ps(1), temp);
	}
	gpmc_t->adv_rd_off = gpmc_round_ps_to_ticks(temp);

	/* oe_on */
	temp = dev_t->t_oeasu; /* XXX: remove this ? */
	if (mux) {
		temp = max_t(u32, temp,	gpmc_t->clk_activation + dev_t->t_ach);
		temp = max_t(u32, temp, gpmc_t->adv_rd_off +
				gpmc_ticks_to_ps(dev_t->cyc_aavdh_oe));
	}
	gpmc_t->oe_on = gpmc_round_ps_to_ticks(temp);

	/* access */
	/* XXX: any scope for improvement ?, by combining oe_on
	 * and clk_activation, need to check whether
	 * access = clk_activation + round to sync clk ?
	 */
	temp = max_t(u32, dev_t->t_iaa,	dev_t->cyc_iaa * gpmc_t->sync_clk);
	temp += gpmc_t->clk_activation;
	if (dev_t->cyc_oe)
		temp = max_t(u32, temp, gpmc_t->oe_on +
				gpmc_ticks_to_ps(dev_t->cyc_oe));
	gpmc_t->access = gpmc_round_ps_to_ticks(temp);

	gpmc_t->oe_off = gpmc_t->access + gpmc_ticks_to_ps(1);
	gpmc_t->cs_rd_off = gpmc_t->oe_off;

	/* rd_cycle */
	temp = max_t(u32, dev_t->t_cez_r, dev_t->t_oez);
	temp = gpmc_round_ps_to_sync_clk(temp, gpmc_t->sync_clk) +
							gpmc_t->access;
	/* XXX: barter t_ce_rdyz with t_cez_r ? */
	if (dev_t->t_ce_rdyz)
		temp = max_t(u32, temp,	gpmc_t->cs_rd_off + dev_t->t_ce_rdyz);
	gpmc_t->rd_cycle = gpmc_round_ps_to_ticks(temp);

	return 0;
}

static int gpmc_calc_sync_write_timings(struct gpmc_timings *gpmc_t,
					struct gpmc_device_timings *dev_t,
					bool mux)
{
	u32 temp;

	/* adv_wr_off */
	temp = dev_t->t_avdp_w;
	if (mux) {
		temp = max_t(u32, temp,
			gpmc_t->clk_activation + dev_t->t_avdh);
		temp = max_t(u32, gpmc_t->adv_on + gpmc_ticks_to_ps(1), temp);
	}
	gpmc_t->adv_wr_off = gpmc_round_ps_to_ticks(temp);

	/* wr_data_mux_bus */
	temp = max_t(u32, dev_t->t_weasu,
			gpmc_t->clk_activation + dev_t->t_rdyo);
	/* XXX: shouldn't mux be kept as a whole for wr_data_mux_bus ?,
	 * and in that case remember to handle we_on properly
	 */
	if (mux) {
		temp = max_t(u32, temp,
			gpmc_t->adv_wr_off + dev_t->t_aavdh);
		temp = max_t(u32, temp, gpmc_t->adv_wr_off +
				gpmc_ticks_to_ps(dev_t->cyc_aavdh_we));
	}
	gpmc_t->wr_data_mux_bus = gpmc_round_ps_to_ticks(temp);

	/* we_on */
	if (gpmc_capability & GPMC_HAS_WR_DATA_MUX_BUS)
		gpmc_t->we_on = gpmc_round_ps_to_ticks(dev_t->t_weasu);
	else
		gpmc_t->we_on = gpmc_t->wr_data_mux_bus;

	/* wr_access */
	/* XXX: gpmc_capability check reqd ? , even if not, will not harm */
	gpmc_t->wr_access = gpmc_t->access;

	/* we_off */
	temp = gpmc_t->we_on + dev_t->t_wpl;
	temp = max_t(u32, temp,
			gpmc_t->wr_access + gpmc_ticks_to_ps(1));
	temp = max_t(u32, temp,
		gpmc_t->we_on + gpmc_ticks_to_ps(dev_t->cyc_wpl));
	gpmc_t->we_off = gpmc_round_ps_to_ticks(temp);

	gpmc_t->cs_wr_off = gpmc_round_ps_to_ticks(gpmc_t->we_off +
							dev_t->t_wph);

	/* wr_cycle */
	temp = gpmc_round_ps_to_sync_clk(dev_t->t_cez_w, gpmc_t->sync_clk);
	temp += gpmc_t->wr_access;
	/* XXX: barter t_ce_rdyz with t_cez_w ? */
	if (dev_t->t_ce_rdyz)
		temp = max_t(u32, temp,
				 gpmc_t->cs_wr_off + dev_t->t_ce_rdyz);
	gpmc_t->wr_cycle = gpmc_round_ps_to_ticks(temp);

	return 0;
}

static int gpmc_calc_async_read_timings(struct gpmc_timings *gpmc_t,
					struct gpmc_device_timings *dev_t,
					bool mux)
{
	u32 temp;

	/* adv_rd_off */
	temp = dev_t->t_avdp_r;
	if (mux)
		temp = max_t(u32, gpmc_t->adv_on + gpmc_ticks_to_ps(1), temp);
	gpmc_t->adv_rd_off = gpmc_round_ps_to_ticks(temp);

	/* oe_on */
	temp = dev_t->t_oeasu;
	if (mux)
		temp = max_t(u32, temp,
			gpmc_t->adv_rd_off + dev_t->t_aavdh);
	gpmc_t->oe_on = gpmc_round_ps_to_ticks(temp);

	/* access */
	temp = max_t(u32, dev_t->t_iaa, /* XXX: remove t_iaa in async ? */
				gpmc_t->oe_on + dev_t->t_oe);
	temp = max_t(u32, temp,
				gpmc_t->cs_on + dev_t->t_ce);
	temp = max_t(u32, temp,
				gpmc_t->adv_on + dev_t->t_aa);
	gpmc_t->access = gpmc_round_ps_to_ticks(temp);

	gpmc_t->oe_off = gpmc_t->access + gpmc_ticks_to_ps(1);
	gpmc_t->cs_rd_off = gpmc_t->oe_off;

	/* rd_cycle */
	temp = max_t(u32, dev_t->t_rd_cycle,
			gpmc_t->cs_rd_off + dev_t->t_cez_r);
	temp = max_t(u32, temp, gpmc_t->oe_off + dev_t->t_oez);
	gpmc_t->rd_cycle = gpmc_round_ps_to_ticks(temp);

	return 0;
}

static int gpmc_calc_async_write_timings(struct gpmc_timings *gpmc_t,
					 struct gpmc_device_timings *dev_t,
					 bool mux)
{
	u32 temp;

	/* adv_wr_off */
	temp = dev_t->t_avdp_w;
	if (mux)
		temp = max_t(u32, gpmc_t->adv_on + gpmc_ticks_to_ps(1), temp);
	gpmc_t->adv_wr_off = gpmc_round_ps_to_ticks(temp);

	/* wr_data_mux_bus */
	temp = dev_t->t_weasu;
	if (mux) {
		temp = max_t(u32, temp,	gpmc_t->adv_wr_off + dev_t->t_aavdh);
		temp = max_t(u32, temp, gpmc_t->adv_wr_off +
				gpmc_ticks_to_ps(dev_t->cyc_aavdh_we));
	}
	gpmc_t->wr_data_mux_bus = gpmc_round_ps_to_ticks(temp);

	/* we_on */
	if (gpmc_capability & GPMC_HAS_WR_DATA_MUX_BUS)
		gpmc_t->we_on = gpmc_round_ps_to_ticks(dev_t->t_weasu);
	else
		gpmc_t->we_on = gpmc_t->wr_data_mux_bus;

	/* we_off */
	temp = gpmc_t->we_on + dev_t->t_wpl;
	gpmc_t->we_off = gpmc_round_ps_to_ticks(temp);

	gpmc_t->cs_wr_off = gpmc_round_ps_to_ticks(gpmc_t->we_off +
							dev_t->t_wph);

	/* wr_cycle */
	temp = max_t(u32, dev_t->t_wr_cycle,
				gpmc_t->cs_wr_off + dev_t->t_cez_w);
	gpmc_t->wr_cycle = gpmc_round_ps_to_ticks(temp);

	return 0;
}

static int gpmc_calc_sync_common_timings(struct gpmc_timings *gpmc_t,
			struct gpmc_device_timings *dev_t)
{
	u32 temp;

	gpmc_t->sync_clk = gpmc_calc_divider(dev_t->clk) *
						gpmc_get_fclk_period();

	gpmc_t->page_burst_access = gpmc_round_ps_to_sync_clk(
					dev_t->t_bacc,
					gpmc_t->sync_clk);

	temp = max_t(u32, dev_t->t_ces, dev_t->t_avds);
	gpmc_t->clk_activation = gpmc_round_ps_to_ticks(temp);

	if (gpmc_calc_divider(gpmc_t->sync_clk) != 1)
		return 0;

	if (dev_t->ce_xdelay)
		gpmc_t->bool_timings.cs_extra_delay = true;
	if (dev_t->avd_xdelay)
		gpmc_t->bool_timings.adv_extra_delay = true;
	if (dev_t->oe_xdelay)
		gpmc_t->bool_timings.oe_extra_delay = true;
	if (dev_t->we_xdelay)
		gpmc_t->bool_timings.we_extra_delay = true;

	return 0;
}

static int gpmc_calc_common_timings(struct gpmc_timings *gpmc_t,
				    struct gpmc_device_timings *dev_t,
				    bool sync)
{
	u32 temp;

	/* cs_on */
	gpmc_t->cs_on = gpmc_round_ps_to_ticks(dev_t->t_ceasu);

	/* adv_on */
	temp = dev_t->t_avdasu;
	if (dev_t->t_ce_avd)
		temp = max_t(u32, temp,
				gpmc_t->cs_on + dev_t->t_ce_avd);
	gpmc_t->adv_on = gpmc_round_ps_to_ticks(temp);

	if (sync)
		gpmc_calc_sync_common_timings(gpmc_t, dev_t);

	return 0;
}

/* TODO: remove this function once all peripherals are confirmed to
 * work with generic timing. Simultaneously gpmc_cs_set_timings()
 * has to be modified to handle timings in ps instead of ns
*/
static void gpmc_convert_ps_to_ns(struct gpmc_timings *t)
{
	t->cs_on /= 1000;
	t->cs_rd_off /= 1000;
	t->cs_wr_off /= 1000;
	t->adv_on /= 1000;
	t->adv_rd_off /= 1000;
	t->adv_wr_off /= 1000;
	t->we_on /= 1000;
	t->we_off /= 1000;
	t->oe_on /= 1000;
	t->oe_off /= 1000;
	t->page_burst_access /= 1000;
	t->access /= 1000;
	t->rd_cycle /= 1000;
	t->wr_cycle /= 1000;
	t->bus_turnaround /= 1000;
	t->cycle2cycle_delay /= 1000;
	t->wait_monitoring /= 1000;
	t->clk_activation /= 1000;
	t->wr_access /= 1000;
	t->wr_data_mux_bus /= 1000;
}

int gpmc_calc_timings(struct gpmc_timings *gpmc_t,
		      struct gpmc_settings *gpmc_s,
		      struct gpmc_device_timings *dev_t)
{
	bool mux = false, sync = false;

	if (gpmc_s) {
		mux = gpmc_s->mux_add_data ? true : false;
		sync = (gpmc_s->sync_read || gpmc_s->sync_write);
	}

	memset(gpmc_t, 0, sizeof(*gpmc_t));

	gpmc_calc_common_timings(gpmc_t, dev_t, sync);

	if (gpmc_s && gpmc_s->sync_read)
		gpmc_calc_sync_read_timings(gpmc_t, dev_t, mux);
	else
		gpmc_calc_async_read_timings(gpmc_t, dev_t, mux);

	if (gpmc_s && gpmc_s->sync_write)
		gpmc_calc_sync_write_timings(gpmc_t, dev_t, mux);
	else
		gpmc_calc_async_write_timings(gpmc_t, dev_t, mux);

	/* TODO: remove, see function definition */
	gpmc_convert_ps_to_ns(gpmc_t);

	return 0;
}

/**
 * gpmc_cs_program_settings - programs non-timing related settings
 * @cs:		GPMC chip-select to program
 * @p:		pointer to GPMC settings structure
 *
 * Programs non-timing related settings for a GPMC chip-select, such as
 * bus-width, burst configuration, etc. Function should be called once
 * for each chip-select that is being used and must be called before
 * calling gpmc_cs_set_timings() as timing parameters in the CONFIG1
 * register will be initialised to zero by this function. Returns 0 on
 * success and appropriate negative error code on failure.
 */
int gpmc_cs_program_settings(int cs, struct gpmc_settings *p)
{
	u32 config1;

	if ((!p->device_width) || (p->device_width > GPMC_DEVWIDTH_16BIT)) {
		pr_err("%s: invalid width %d!", __func__, p->device_width);
		return -EINVAL;
	}

	/* Address-data multiplexing not supported for NAND devices */
	if (p->device_nand && p->mux_add_data) {
		pr_err("%s: invalid configuration!\n", __func__);
		return -EINVAL;
	}

	if ((p->mux_add_data > GPMC_MUX_AD) ||
	    ((p->mux_add_data == GPMC_MUX_AAD) &&
	     !(gpmc_capability & GPMC_HAS_MUX_AAD))) {
		pr_err("%s: invalid multiplex configuration!\n", __func__);
		return -EINVAL;
	}

	/* Page/burst mode supports lengths of 4, 8 and 16 bytes */
	if (p->burst_read || p->burst_write) {
		switch (p->burst_len) {
		case GPMC_BURST_4:
		case GPMC_BURST_8:
		case GPMC_BURST_16:
			break;
		default:
			pr_err("%s: invalid page/burst-length (%d)\n",
			       __func__, p->burst_len);
			return -EINVAL;
		}
	}

	if ((p->wait_on_read || p->wait_on_write) &&
	    (p->wait_pin > gpmc_nr_waitpins)) {
		pr_err("%s: invalid wait-pin (%d)\n", __func__, p->wait_pin);
		return -EINVAL;
	}

	config1 = GPMC_CONFIG1_DEVICESIZE((p->device_width - 1));

	if (p->sync_read)
		config1 |= GPMC_CONFIG1_READTYPE_SYNC;
	if (p->sync_write)
		config1 |= GPMC_CONFIG1_WRITETYPE_SYNC;
	if (p->wait_on_read)
		config1 |= GPMC_CONFIG1_WAIT_READ_MON;
	if (p->wait_on_write)
		config1 |= GPMC_CONFIG1_WAIT_WRITE_MON;
	if (p->wait_on_read || p->wait_on_write)
		config1 |= GPMC_CONFIG1_WAIT_PIN_SEL(p->wait_pin);
	if (p->device_nand)
		config1	|= GPMC_CONFIG1_DEVICETYPE(GPMC_DEVICETYPE_NAND);
	if (p->mux_add_data)
		config1	|= GPMC_CONFIG1_MUXTYPE(p->mux_add_data);
	if (p->burst_read)
		config1 |= GPMC_CONFIG1_READMULTIPLE_SUPP;
	if (p->burst_write)
		config1 |= GPMC_CONFIG1_WRITEMULTIPLE_SUPP;
	if (p->burst_read || p->burst_write) {
		config1 |= GPMC_CONFIG1_PAGE_LEN(p->burst_len >> 3);
		config1 |= p->burst_wrap ? GPMC_CONFIG1_WRAPBURST_SUPP : 0;
	}

	gpmc_cs_write_reg(cs, GPMC_CS_CONFIG1, config1);

	return 0;
}

#ifdef CONFIG_OF
static struct of_device_id gpmc_dt_ids[] = {
	{ .compatible = "ti,omap2420-gpmc" },
	{ .compatible = "ti,omap2430-gpmc" },
	{ .compatible = "ti,omap3430-gpmc" },	/* omap3430 & omap3630 */
	{ .compatible = "ti,omap4430-gpmc" },	/* omap4430 & omap4460 & omap543x */
	{ .compatible = "ti,am3352-gpmc" },	/* am335x devices */
	{ }
};
MODULE_DEVICE_TABLE(of, gpmc_dt_ids);

/**
 * gpmc_read_settings_dt - read gpmc settings from device-tree
 * @np:		pointer to device-tree node for a gpmc child device
 * @p:		pointer to gpmc settings structure
 *
 * Reads the GPMC settings for a GPMC child device from device-tree and
 * stores them in the GPMC settings structure passed. The GPMC settings
 * structure is initialised to zero by this function and so any
 * previously stored settings will be cleared.
 */
void gpmc_read_settings_dt(struct device_node *np, struct gpmc_settings *p)
{
	memset(p, 0, sizeof(struct gpmc_settings));

	p->sync_read = of_property_read_bool(np, "gpmc,sync-read");
	p->sync_write = of_property_read_bool(np, "gpmc,sync-write");
	of_property_read_u32(np, "gpmc,device-width", &p->device_width);
	of_property_read_u32(np, "gpmc,mux-add-data", &p->mux_add_data);

	if (!of_property_read_u32(np, "gpmc,burst-length", &p->burst_len)) {
		p->burst_wrap = of_property_read_bool(np, "gpmc,burst-wrap");
		p->burst_read = of_property_read_bool(np, "gpmc,burst-read");
		p->burst_write = of_property_read_bool(np, "gpmc,burst-write");
		if (!p->burst_read && !p->burst_write)
			pr_warn("%s: page/burst-length set but not used!\n",
				__func__);
	}

	if (!of_property_read_u32(np, "gpmc,wait-pin", &p->wait_pin)) {
		p->wait_on_read = of_property_read_bool(np,
							"gpmc,wait-on-read");
		p->wait_on_write = of_property_read_bool(np,
							 "gpmc,wait-on-write");
		if (!p->wait_on_read && !p->wait_on_write)
			pr_warn("%s: read/write wait monitoring not enabled!\n",
				__func__);
	}
}

static void __maybe_unused gpmc_read_timings_dt(struct device_node *np,
						struct gpmc_timings *gpmc_t)
{
	struct gpmc_bool_timings *p;

	if (!np || !gpmc_t)
		return;

	memset(gpmc_t, 0, sizeof(*gpmc_t));

	/* minimum clock period for syncronous mode */
	of_property_read_u32(np, "gpmc,sync-clk-ps", &gpmc_t->sync_clk);

	/* chip select timtings */
	of_property_read_u32(np, "gpmc,cs-on-ns", &gpmc_t->cs_on);
	of_property_read_u32(np, "gpmc,cs-rd-off-ns", &gpmc_t->cs_rd_off);
	of_property_read_u32(np, "gpmc,cs-wr-off-ns", &gpmc_t->cs_wr_off);

	/* ADV signal timings */
	of_property_read_u32(np, "gpmc,adv-on-ns", &gpmc_t->adv_on);
	of_property_read_u32(np, "gpmc,adv-rd-off-ns", &gpmc_t->adv_rd_off);
	of_property_read_u32(np, "gpmc,adv-wr-off-ns", &gpmc_t->adv_wr_off);

	/* WE signal timings */
	of_property_read_u32(np, "gpmc,we-on-ns", &gpmc_t->we_on);
	of_property_read_u32(np, "gpmc,we-off-ns", &gpmc_t->we_off);

	/* OE signal timings */
	of_property_read_u32(np, "gpmc,oe-on-ns", &gpmc_t->oe_on);
	of_property_read_u32(np, "gpmc,oe-off-ns", &gpmc_t->oe_off);

	/* access and cycle timings */
	of_property_read_u32(np, "gpmc,page-burst-access-ns",
			     &gpmc_t->page_burst_access);
	of_property_read_u32(np, "gpmc,access-ns", &gpmc_t->access);
	of_property_read_u32(np, "gpmc,rd-cycle-ns", &gpmc_t->rd_cycle);
	of_property_read_u32(np, "gpmc,wr-cycle-ns", &gpmc_t->wr_cycle);
	of_property_read_u32(np, "gpmc,bus-turnaround-ns",
			     &gpmc_t->bus_turnaround);
	of_property_read_u32(np, "gpmc,cycle2cycle-delay-ns",
			     &gpmc_t->cycle2cycle_delay);
	of_property_read_u32(np, "gpmc,wait-monitoring-ns",
			     &gpmc_t->wait_monitoring);
	of_property_read_u32(np, "gpmc,clk-activation-ns",
			     &gpmc_t->clk_activation);

	/* only applicable to OMAP3+ */
	of_property_read_u32(np, "gpmc,wr-access-ns", &gpmc_t->wr_access);
	of_property_read_u32(np, "gpmc,wr-data-mux-bus-ns",
			     &gpmc_t->wr_data_mux_bus);

	/* bool timing parameters */
	p = &gpmc_t->bool_timings;

	p->cycle2cyclediffcsen =
		of_property_read_bool(np, "gpmc,cycle2cycle-diffcsen");
	p->cycle2cyclesamecsen =
		of_property_read_bool(np, "gpmc,cycle2cycle-samecsen");
	p->we_extra_delay = of_property_read_bool(np, "gpmc,we-extra-delay");
	p->oe_extra_delay = of_property_read_bool(np, "gpmc,oe-extra-delay");
	p->adv_extra_delay = of_property_read_bool(np, "gpmc,adv-extra-delay");
	p->cs_extra_delay = of_property_read_bool(np, "gpmc,cs-extra-delay");
	p->time_para_granularity =
		of_property_read_bool(np, "gpmc,time-para-granularity");
}

#if IS_ENABLED(CONFIG_MTD_NAND)

static const char * const nand_xfer_types[] = {
	[NAND_OMAP_PREFETCH_POLLED]		= "prefetch-polled",
	[NAND_OMAP_POLLED]			= "polled",
	[NAND_OMAP_PREFETCH_DMA]		= "prefetch-dma",
	[NAND_OMAP_PREFETCH_IRQ]		= "prefetch-irq",
};

static int gpmc_probe_nand_child(struct platform_device *pdev,
				 struct device_node *child)
{
	u32 val;
	const char *s;
	struct gpmc_timings gpmc_t;
	struct omap_nand_platform_data *gpmc_nand_data;

	if (of_property_read_u32(child, "reg", &val) < 0) {
		dev_err(&pdev->dev, "%s has no 'reg' property\n",
			child->full_name);
		return -ENODEV;
	}

	gpmc_nand_data = devm_kzalloc(&pdev->dev, sizeof(*gpmc_nand_data),
				      GFP_KERNEL);
	if (!gpmc_nand_data)
		return -ENOMEM;

	gpmc_nand_data->cs = val;
	gpmc_nand_data->of_node = child;

	/* Detect availability of ELM module */
	gpmc_nand_data->elm_of_node = of_parse_phandle(child, "ti,elm-id", 0);
	if (gpmc_nand_data->elm_of_node == NULL)
		gpmc_nand_data->elm_of_node =
					of_parse_phandle(child, "elm_id", 0);
	if (gpmc_nand_data->elm_of_node == NULL)
		pr_warn("%s: ti,elm-id property not found\n", __func__);

	/* select ecc-scheme for NAND */
	if (of_property_read_string(child, "ti,nand-ecc-opt", &s)) {
		pr_err("%s: ti,nand-ecc-opt not found\n", __func__);
		return -ENODEV;
	}

	if (!strcmp(s, "sw"))
		gpmc_nand_data->ecc_opt = OMAP_ECC_HAM1_CODE_SW;
	else if (!strcmp(s, "ham1") ||
		 !strcmp(s, "hw") || !strcmp(s, "hw-romcode"))
		gpmc_nand_data->ecc_opt =
				OMAP_ECC_HAM1_CODE_HW;
	else if (!strcmp(s, "bch4"))
		if (gpmc_nand_data->elm_of_node)
			gpmc_nand_data->ecc_opt =
				OMAP_ECC_BCH4_CODE_HW;
		else
			gpmc_nand_data->ecc_opt =
				OMAP_ECC_BCH4_CODE_HW_DETECTION_SW;
	else if (!strcmp(s, "bch8"))
		if (gpmc_nand_data->elm_of_node)
			gpmc_nand_data->ecc_opt =
				OMAP_ECC_BCH8_CODE_HW;
		else
			gpmc_nand_data->ecc_opt =
				OMAP_ECC_BCH8_CODE_HW_DETECTION_SW;
	else if (!strcmp(s, "bch16"))
		if (gpmc_nand_data->elm_of_node)
			gpmc_nand_data->ecc_opt =
				OMAP_ECC_BCH16_CODE_HW;
		else
			pr_err("%s: BCH16 requires ELM support\n", __func__);
	else
		pr_err("%s: ti,nand-ecc-opt invalid value\n", __func__);

	/* select data transfer mode for NAND controller */
	if (!of_property_read_string(child, "ti,nand-xfer-type", &s))
		for (val = 0; val < ARRAY_SIZE(nand_xfer_types); val++)
			if (!strcasecmp(s, nand_xfer_types[val])) {
				gpmc_nand_data->xfer_type = val;
				break;
			}

	val = of_get_nand_bus_width(child);
	if (val == 16)
		gpmc_nand_data->devsize = NAND_BUSWIDTH_16;

	gpmc_read_timings_dt(child, &gpmc_t);
	gpmc_nand_init(gpmc_nand_data, &gpmc_t);

	return 0;
}
#else
static int gpmc_probe_nand_child(struct platform_device *pdev,
				 struct device_node *child)
{
	return 0;
}
#endif

#if IS_ENABLED(CONFIG_MTD_ONENAND)
static int gpmc_probe_onenand_child(struct platform_device *pdev,
				 struct device_node *child)
{
	u32 val;
	struct omap_onenand_platform_data *gpmc_onenand_data;

	if (of_property_read_u32(child, "reg", &val) < 0) {
		dev_err(&pdev->dev, "%s has no 'reg' property\n",
			child->full_name);
		return -ENODEV;
	}

	gpmc_onenand_data = devm_kzalloc(&pdev->dev, sizeof(*gpmc_onenand_data),
					 GFP_KERNEL);
	if (!gpmc_onenand_data)
		return -ENOMEM;

	gpmc_onenand_data->cs = val;
	gpmc_onenand_data->of_node = child;
	gpmc_onenand_data->dma_channel = -1;

	if (!of_property_read_u32(child, "dma-channel", &val))
		gpmc_onenand_data->dma_channel = val;

	gpmc_onenand_init(gpmc_onenand_data);

	return 0;
}
#else
static int gpmc_probe_onenand_child(struct platform_device *pdev,
				    struct device_node *child)
{
	return 0;
}
#endif

/**
 * gpmc_probe_generic_child - configures the gpmc for a child device
 * @pdev:	pointer to gpmc platform device
 * @child:	pointer to device-tree node for child device
 *
 * Allocates and configures a GPMC chip-select for a child device.
 * Returns 0 on success and appropriate negative error code on failure.
 */
static int gpmc_probe_generic_child(struct platform_device *pdev,
				struct device_node *child)
{
	struct gpmc_settings gpmc_s;
	struct gpmc_timings gpmc_t;
	struct resource res;
	unsigned long base;
	int ret, cs;

	if (of_property_read_u32(child, "reg", &cs) < 0) {
		dev_err(&pdev->dev, "%s has no 'reg' property\n",
			child->full_name);
		return -ENODEV;
	}

	if (of_address_to_resource(child, 0, &res) < 0) {
		dev_err(&pdev->dev, "%s has malformed 'reg' property\n",
			child->full_name);
		return -ENODEV;
	}

	ret = gpmc_cs_request(cs, resource_size(&res), &base);
	if (ret < 0) {
		dev_err(&pdev->dev, "cannot request GPMC CS %d\n", cs);
		return ret;
	}

	/*
	 * For some GPMC devices we still need to rely on the bootloader
	 * timings because the devices can be connected via FPGA. So far
	 * the list is smc91x on the omap2 SDP boards, and 8250 on zooms.
	 * REVISIT: Add timing support from slls644g.pdf and from the
	 * lan91c96 manual.
	 */
	if (of_device_is_compatible(child, "ns16550a") ||
	    of_device_is_compatible(child, "smsc,lan91c94") ||
	    of_device_is_compatible(child, "smsc,lan91c111")) {
		dev_warn(&pdev->dev,
			 "%s using bootloader timings on CS%d\n",
			 child->name, cs);
		goto no_timings;
	}

	/*
	 * FIXME: gpmc_cs_request() will map the CS to an arbitary
	 * location in the gpmc address space. When booting with
	 * device-tree we want the NOR flash to be mapped to the
	 * location specified in the device-tree blob. So remap the
	 * CS to this location. Once DT migration is complete should
	 * just make gpmc_cs_request() map a specific address.
	 */
	ret = gpmc_cs_remap(cs, res.start);
	if (ret < 0) {
		dev_err(&pdev->dev, "cannot remap GPMC CS %d to %pa\n",
			cs, &res.start);
		goto err;
	}

	gpmc_read_settings_dt(child, &gpmc_s);

	ret = of_property_read_u32(child, "bank-width", &gpmc_s.device_width);
	if (ret < 0)
		goto err;

	ret = gpmc_cs_program_settings(cs, &gpmc_s);
	if (ret < 0)
		goto err;

	gpmc_read_timings_dt(child, &gpmc_t);
	gpmc_cs_set_timings(cs, &gpmc_t);

no_timings:
	if (of_platform_device_create(child, NULL, &pdev->dev))
		return 0;

	dev_err(&pdev->dev, "failed to create gpmc child %s\n", child->name);
	ret = -ENODEV;

err:
	gpmc_cs_free(cs);

	return ret;
}

static int gpmc_probe_dt(struct platform_device *pdev)
{
	int ret;
	struct device_node *child;
	const struct of_device_id *of_id =
		of_match_device(gpmc_dt_ids, &pdev->dev);

	if (!of_id)
		return 0;

	ret = of_property_read_u32(pdev->dev.of_node, "gpmc,num-cs",
				   &gpmc_cs_num);
	if (ret < 0) {
		pr_err("%s: number of chip-selects not defined\n", __func__);
		return ret;
	} else if (gpmc_cs_num < 1) {
		pr_err("%s: all chip-selects are disabled\n", __func__);
		return -EINVAL;
	} else if (gpmc_cs_num > GPMC_CS_NUM) {
		pr_err("%s: number of supported chip-selects cannot be > %d\n",
					 __func__, GPMC_CS_NUM);
		return -EINVAL;
	}

	ret = of_property_read_u32(pdev->dev.of_node, "gpmc,num-waitpins",
				   &gpmc_nr_waitpins);
	if (ret < 0) {
		pr_err("%s: number of wait pins not found!\n", __func__);
		return ret;
	}

	for_each_available_child_of_node(pdev->dev.of_node, child) {

		if (!child->name)
			continue;

		if (of_node_cmp(child->name, "nand") == 0)
			ret = gpmc_probe_nand_child(pdev, child);
		else if (of_node_cmp(child->name, "onenand") == 0)
			ret = gpmc_probe_onenand_child(pdev, child);
		else if (of_node_cmp(child->name, "ethernet") == 0 ||
			 of_node_cmp(child->name, "nor") == 0 ||
			 of_node_cmp(child->name, "uart") == 0)
			ret = gpmc_probe_generic_child(pdev, child);

		if (WARN(ret < 0, "%s: probing gpmc child %s failed\n",
			 __func__, child->full_name))
			of_node_put(child);
	}

	return 0;
}
#else
static int gpmc_probe_dt(struct platform_device *pdev)
{
	return 0;
}
#endif

static int gpmc_probe(struct platform_device *pdev)
{
	int rc;
	u32 l;
	struct resource *res;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (res == NULL)
		return -ENOENT;

	phys_base = res->start;
	mem_size = resource_size(res);

	gpmc_base = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(gpmc_base))
		return PTR_ERR(gpmc_base);

	res = platform_get_resource(pdev, IORESOURCE_IRQ, 0);
	if (res == NULL)
		dev_warn(&pdev->dev, "Failed to get resource: irq\n");
	else
		gpmc_irq = res->start;

	gpmc_l3_clk = clk_get(&pdev->dev, "fck");
	if (IS_ERR(gpmc_l3_clk)) {
		dev_err(&pdev->dev, "error: clk_get\n");
		gpmc_irq = 0;
		return PTR_ERR(gpmc_l3_clk);
	}

	pm_runtime_enable(&pdev->dev);
	pm_runtime_get_sync(&pdev->dev);

	gpmc_dev = &pdev->dev;

	l = gpmc_read_reg(GPMC_REVISION);

	/*
	 * FIXME: Once device-tree migration is complete the below flags
	 * should be populated based upon the device-tree compatible
	 * string. For now just use the IP revision. OMAP3+ devices have
	 * the wr_access and wr_data_mux_bus register fields. OMAP4+
	 * devices support the addr-addr-data multiplex protocol.
	 *
	 * GPMC IP revisions:
	 * - OMAP24xx			= 2.0
	 * - OMAP3xxx			= 5.0
	 * - OMAP44xx/54xx/AM335x	= 6.0
	 */
	if (GPMC_REVISION_MAJOR(l) > 0x4)
		gpmc_capability = GPMC_HAS_WR_ACCESS | GPMC_HAS_WR_DATA_MUX_BUS;
	if (GPMC_REVISION_MAJOR(l) > 0x5)
		gpmc_capability |= GPMC_HAS_MUX_AAD;
	dev_info(gpmc_dev, "GPMC revision %d.%d\n", GPMC_REVISION_MAJOR(l),
		 GPMC_REVISION_MINOR(l));

	gpmc_mem_init();

	if (gpmc_setup_irq() < 0)
		dev_warn(gpmc_dev, "gpmc_setup_irq failed\n");

	/* Now the GPMC is initialised, unreserve the chip-selects */
	gpmc_cs_map = 0;

	if (!pdev->dev.of_node) {
		gpmc_cs_num	 = GPMC_CS_NUM;
		gpmc_nr_waitpins = GPMC_NR_WAITPINS;
	}

	rc = gpmc_probe_dt(pdev);
	if (rc < 0) {
		pm_runtime_put_sync(&pdev->dev);
		clk_put(gpmc_l3_clk);
		dev_err(gpmc_dev, "failed to probe DT parameters\n");
		return rc;
	}

	return 0;
}

static int gpmc_remove(struct platform_device *pdev)
{
	gpmc_free_irq();
	gpmc_mem_exit();
	pm_runtime_put_sync(&pdev->dev);
	pm_runtime_disable(&pdev->dev);
	gpmc_dev = NULL;
	return 0;
}

#ifdef CONFIG_PM_SLEEP
static int gpmc_suspend(struct device *dev)
{
	omap3_gpmc_save_context();
	pm_runtime_put_sync(dev);
	return 0;
}

static int gpmc_resume(struct device *dev)
{
	pm_runtime_get_sync(dev);
	omap3_gpmc_restore_context();
	return 0;
}
#endif

static SIMPLE_DEV_PM_OPS(gpmc_pm_ops, gpmc_suspend, gpmc_resume);

static struct platform_driver gpmc_driver = {
	.probe		= gpmc_probe,
	.remove		= gpmc_remove,
	.driver		= {
		.name	= DEVICE_NAME,
		.owner	= THIS_MODULE,
		.of_match_table = of_match_ptr(gpmc_dt_ids),
		.pm	= &gpmc_pm_ops,
	},
};

static __init int gpmc_init(void)
{
	return platform_driver_register(&gpmc_driver);
}

static __exit void gpmc_exit(void)
{
	platform_driver_unregister(&gpmc_driver);

}

omap_postcore_initcall(gpmc_init);
module_exit(gpmc_exit);

static int __init omap_gpmc_init(void)
{
	struct omap_hwmod *oh;
	struct platform_device *pdev;
	char *oh_name = "gpmc";

	/*
	 * if the board boots up with a populated DT, do not
	 * manually add the device from this initcall
	 */
	if (of_have_populated_dt())
		return -ENODEV;

	oh = omap_hwmod_lookup(oh_name);
	if (!oh) {
		pr_err("Could not look up %s\n", oh_name);
		return -ENODEV;
	}

	pdev = omap_device_build(DEVICE_NAME, -1, oh, NULL, 0);
	WARN(IS_ERR(pdev), "could not build omap_device for %s\n", oh_name);

	return PTR_RET(pdev);
}
omap_postcore_initcall(omap_gpmc_init);

static irqreturn_t gpmc_handle_irq(int irq, void *dev)
{
	int i;
	u32 regval;

	regval = gpmc_read_reg(GPMC_IRQSTATUS);

	if (!regval)
		return IRQ_NONE;

	for (i = 0; i < GPMC_NR_IRQ; i++)
		if (regval & gpmc_client_irq[i].bitmask)
			generic_handle_irq(gpmc_client_irq[i].irq);

	gpmc_write_reg(GPMC_IRQSTATUS, regval);

	return IRQ_HANDLED;
}

static struct omap3_gpmc_regs gpmc_context;

void omap3_gpmc_save_context(void)
{
	int i;

	gpmc_context.sysconfig = gpmc_read_reg(GPMC_SYSCONFIG);
	gpmc_context.irqenable = gpmc_read_reg(GPMC_IRQENABLE);
	gpmc_context.timeout_ctrl = gpmc_read_reg(GPMC_TIMEOUT_CONTROL);
	gpmc_context.config = gpmc_read_reg(GPMC_CONFIG);
	gpmc_context.prefetch_config1 = gpmc_read_reg(GPMC_PREFETCH_CONFIG1);
	gpmc_context.prefetch_config2 = gpmc_read_reg(GPMC_PREFETCH_CONFIG2);
	gpmc_context.prefetch_control = gpmc_read_reg(GPMC_PREFETCH_CONTROL);
	for (i = 0; i < gpmc_cs_num; i++) {
		gpmc_context.cs_context[i].is_valid = gpmc_cs_mem_enabled(i);
		if (gpmc_context.cs_context[i].is_valid) {
			gpmc_context.cs_context[i].config1 =
				gpmc_cs_read_reg(i, GPMC_CS_CONFIG1);
			gpmc_context.cs_context[i].config2 =
				gpmc_cs_read_reg(i, GPMC_CS_CONFIG2);
			gpmc_context.cs_context[i].config3 =
				gpmc_cs_read_reg(i, GPMC_CS_CONFIG3);
			gpmc_context.cs_context[i].config4 =
				gpmc_cs_read_reg(i, GPMC_CS_CONFIG4);
			gpmc_context.cs_context[i].config5 =
				gpmc_cs_read_reg(i, GPMC_CS_CONFIG5);
			gpmc_context.cs_context[i].config6 =
				gpmc_cs_read_reg(i, GPMC_CS_CONFIG6);
			gpmc_context.cs_context[i].config7 =
				gpmc_cs_read_reg(i, GPMC_CS_CONFIG7);
		}
	}
}

void omap3_gpmc_restore_context(void)
{
	int i;

	gpmc_write_reg(GPMC_SYSCONFIG, gpmc_context.sysconfig);
	gpmc_write_reg(GPMC_IRQENABLE, gpmc_context.irqenable);
	gpmc_write_reg(GPMC_TIMEOUT_CONTROL, gpmc_context.timeout_ctrl);
	gpmc_write_reg(GPMC_CONFIG, gpmc_context.config);
	gpmc_write_reg(GPMC_PREFETCH_CONFIG1, gpmc_context.prefetch_config1);
	gpmc_write_reg(GPMC_PREFETCH_CONFIG2, gpmc_context.prefetch_config2);
	gpmc_write_reg(GPMC_PREFETCH_CONTROL, gpmc_context.prefetch_control);
	for (i = 0; i < gpmc_cs_num; i++) {
		if (gpmc_context.cs_context[i].is_valid) {
			gpmc_cs_write_reg(i, GPMC_CS_CONFIG1,
				gpmc_context.cs_context[i].config1);
			gpmc_cs_write_reg(i, GPMC_CS_CONFIG2,
				gpmc_context.cs_context[i].config2);
			gpmc_cs_write_reg(i, GPMC_CS_CONFIG3,
				gpmc_context.cs_context[i].config3);
			gpmc_cs_write_reg(i, GPMC_CS_CONFIG4,
				gpmc_context.cs_context[i].config4);
			gpmc_cs_write_reg(i, GPMC_CS_CONFIG5,
				gpmc_context.cs_context[i].config5);
			gpmc_cs_write_reg(i, GPMC_CS_CONFIG6,
				gpmc_context.cs_context[i].config6);
			gpmc_cs_write_reg(i, GPMC_CS_CONFIG7,
				gpmc_context.cs_context[i].config7);
		}
	}
}
