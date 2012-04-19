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

#include <asm/mach-types.h>
#include <plat/gpmc.h>

#include <plat/sdrc.h>

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

#define GPMC_CS0_OFFSET		0x60
#define GPMC_CS_SIZE		0x30

#define GPMC_MEM_START		0x00000000
#define GPMC_MEM_END		0x3FFFFFFF
#define BOOT_ROM_SPACE		0x100000	/* 1MB */

#define GPMC_CHUNK_SHIFT	24		/* 16 MB */
#define GPMC_SECTION_SHIFT	28		/* 128 MB */

#define CS_NUM_SHIFT		24
#define ENABLE_PREFETCH		(0x1 << 7)
#define DMA_MPU_MODE		2

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

static struct resource	gpmc_mem_root;
static struct resource	gpmc_cs_mem[GPMC_CS_NUM];
static DEFINE_SPINLOCK(gpmc_mem_lock);
static unsigned int gpmc_cs_map;	/* flag for cs which are initialized */
static int gpmc_ecc_used = -EINVAL;	/* cs using ecc engine */

static void __iomem *gpmc_base;

static struct clk *gpmc_l3_clk;

static irqreturn_t gpmc_handle_irq(int irq, void *dev);

static void gpmc_write_reg(int idx, u32 val)
{
	__raw_writel(val, gpmc_base + idx);
}

static u32 gpmc_read_reg(int idx)
{
	return __raw_readl(gpmc_base + idx);
}

static void gpmc_cs_write_byte(int cs, int idx, u8 val)
{
	void __iomem *reg_addr;

	reg_addr = gpmc_base + GPMC_CS0_OFFSET + (cs * GPMC_CS_SIZE) + idx;
	__raw_writeb(val, reg_addr);
}

static u8 gpmc_cs_read_byte(int cs, int idx)
{
	void __iomem *reg_addr;

	reg_addr = gpmc_base + GPMC_CS0_OFFSET + (cs * GPMC_CS_SIZE) + idx;
	return __raw_readb(reg_addr);
}

void gpmc_cs_write_reg(int cs, int idx, u32 val)
{
	void __iomem *reg_addr;

	reg_addr = gpmc_base + GPMC_CS0_OFFSET + (cs * GPMC_CS_SIZE) + idx;
	__raw_writel(val, reg_addr);
}

u32 gpmc_cs_read_reg(int cs, int idx)
{
	void __iomem *reg_addr;

	reg_addr = gpmc_base + GPMC_CS0_OFFSET + (cs * GPMC_CS_SIZE) + idx;
	return __raw_readl(reg_addr);
}

/* TODO: Add support for gpmc_fck to clock framework and use it */
unsigned long gpmc_get_fclk_period(void)
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

unsigned int gpmc_ns_to_ticks(unsigned int time_ns)
{
	unsigned long tick_ps;

	/* Calculate in picosecs to yield more exact results */
	tick_ps = gpmc_get_fclk_period();

	return (time_ns * 1000 + tick_ps - 1) / tick_ps;
}

unsigned int gpmc_ps_to_ticks(unsigned int time_ps)
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

unsigned int gpmc_round_ns_to_ticks(unsigned int time_ns)
{
	unsigned long ticks = gpmc_ns_to_ticks(time_ns);

	return ticks * gpmc_get_fclk_period() / 1000;
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

int gpmc_cs_calc_divider(int cs, unsigned int sync_clk)
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

	if (cpu_is_omap34xx()) {
		GPMC_SET_ONE(GPMC_CS_CONFIG6, 16, 19, wr_data_mux_bus);
		GPMC_SET_ONE(GPMC_CS_CONFIG6, 24, 28, wr_access);
	}

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

	return 0;
}

static void gpmc_cs_enable_mem(int cs, u32 base, u32 size)
{
	u32 l;
	u32 mask;

	mask = (1 << GPMC_SECTION_SHIFT) - size;
	l = gpmc_cs_read_reg(cs, GPMC_CS_CONFIG7);
	l &= ~0x3f;
	l = (base >> GPMC_CHUNK_SHIFT) & 0x3f;
	l &= ~(0x0f << 8);
	l |= ((mask >> GPMC_CHUNK_SHIFT) & 0x0f) << 8;
	l |= GPMC_CONFIG7_CSVALID;
	gpmc_cs_write_reg(cs, GPMC_CS_CONFIG7, l);
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

int gpmc_cs_set_reserved(int cs, int reserved)
{
	if (cs > GPMC_CS_NUM)
		return -ENODEV;

	gpmc_cs_map &= ~(1 << cs);
	gpmc_cs_map |= (reserved ? 1 : 0) << cs;

	return 0;
}

int gpmc_cs_reserved(int cs)
{
	if (cs > GPMC_CS_NUM)
		return -ENODEV;

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

int gpmc_cs_request(int cs, unsigned long size, unsigned long *base)
{
	struct resource *res = &gpmc_cs_mem[cs];
	int r = -1;

	if (cs > GPMC_CS_NUM)
		return -ENODEV;

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

	gpmc_cs_enable_mem(cs, res->start, resource_size(res));
	*base = res->start;
	gpmc_cs_set_reserved(cs, 1);
out:
	spin_unlock(&gpmc_mem_lock);
	return r;
}
EXPORT_SYMBOL(gpmc_cs_request);

void gpmc_cs_free(int cs)
{
	spin_lock(&gpmc_mem_lock);
	if (cs >= GPMC_CS_NUM || cs < 0 || !gpmc_cs_reserved(cs)) {
		printk(KERN_ERR "Trying to free non-reserved GPMC CS%d\n", cs);
		BUG();
		spin_unlock(&gpmc_mem_lock);
		return;
	}
	gpmc_cs_disable_mem(cs);
	release_resource(&gpmc_cs_mem[cs]);
	gpmc_cs_set_reserved(cs, 0);
	spin_unlock(&gpmc_mem_lock);
}
EXPORT_SYMBOL(gpmc_cs_free);

/**
 * gpmc_read_status - read access request to get the different gpmc status
 * @cmd: command type
 * @return status
 */
int gpmc_read_status(int cmd)
{
	int	status = -EINVAL;
	u32	regval = 0;

	switch (cmd) {
	case GPMC_GET_IRQ_STATUS:
		status = gpmc_read_reg(GPMC_IRQSTATUS);
		break;

	case GPMC_PREFETCH_FIFO_CNT:
		regval = gpmc_read_reg(GPMC_PREFETCH_STATUS);
		status = GPMC_PREFETCH_STATUS_FIFO_CNT(regval);
		break;

	case GPMC_PREFETCH_COUNT:
		regval = gpmc_read_reg(GPMC_PREFETCH_STATUS);
		status = GPMC_PREFETCH_STATUS_COUNT(regval);
		break;

	case GPMC_STATUS_BUFFER:
		regval = gpmc_read_reg(GPMC_STATUS);
		/* 1 : buffer is available to write */
		status = regval & GPMC_STATUS_BUFF_EMPTY;
		break;

	default:
		printk(KERN_ERR "gpmc_read_status: Not supported\n");
	}
	return status;
}
EXPORT_SYMBOL(gpmc_read_status);

/**
 * gpmc_cs_configure - write request to configure gpmc
 * @cs: chip select number
 * @cmd: command type
 * @wval: value to write
 * @return status of the operation
 */
int gpmc_cs_configure(int cs, int cmd, int wval)
{
	int err = 0;
	u32 regval = 0;

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

	case GPMC_CONFIG_RDY_BSY:
		regval  = gpmc_cs_read_reg(cs, GPMC_CS_CONFIG1);
		if (wval)
			regval |= WR_RD_PIN_MONITORING;
		else
			regval &= ~WR_RD_PIN_MONITORING;
		gpmc_cs_write_reg(cs, GPMC_CS_CONFIG1, regval);
		break;

	case GPMC_CONFIG_DEV_SIZE:
		regval  = gpmc_cs_read_reg(cs, GPMC_CS_CONFIG1);

		/* clear 2 target bits */
		regval &= ~GPMC_CONFIG1_DEVICESIZE(3);

		/* set the proper value */
		regval |= GPMC_CONFIG1_DEVICESIZE(wval);

		gpmc_cs_write_reg(cs, GPMC_CS_CONFIG1, regval);
		break;

	case GPMC_CONFIG_DEV_TYPE:
		regval  = gpmc_cs_read_reg(cs, GPMC_CS_CONFIG1);
		regval |= GPMC_CONFIG1_DEVICETYPE(wval);
		if (wval == GPMC_DEVICETYPE_NOR)
			regval |= GPMC_CONFIG1_MUXADDDATA;
		gpmc_cs_write_reg(cs, GPMC_CS_CONFIG1, regval);
		break;

	default:
		printk(KERN_ERR "gpmc_configure_cs: Not supported\n");
		err = -EINVAL;
	}

	return err;
}
EXPORT_SYMBOL(gpmc_cs_configure);

/**
 * gpmc_nand_read - nand specific read access request
 * @cs: chip select number
 * @cmd: command type
 */
int gpmc_nand_read(int cs, int cmd)
{
	int rval = -EINVAL;

	switch (cmd) {
	case GPMC_NAND_DATA:
		rval = gpmc_cs_read_byte(cs, GPMC_CS_NAND_DATA);
		break;

	default:
		printk(KERN_ERR "gpmc_read_nand_ctrl: Not supported\n");
	}
	return rval;
}
EXPORT_SYMBOL(gpmc_nand_read);

/**
 * gpmc_nand_write - nand specific write request
 * @cs: chip select number
 * @cmd: command type
 * @wval: value to write
 */
int gpmc_nand_write(int cs, int cmd, int wval)
{
	int err = 0;

	switch (cmd) {
	case GPMC_NAND_COMMAND:
		gpmc_cs_write_byte(cs, GPMC_CS_NAND_COMMAND, wval);
		break;

	case GPMC_NAND_ADDRESS:
		gpmc_cs_write_byte(cs, GPMC_CS_NAND_ADDRESS, wval);
		break;

	case GPMC_NAND_DATA:
		gpmc_cs_write_byte(cs, GPMC_CS_NAND_DATA, wval);

	default:
		printk(KERN_ERR "gpmc_write_nand_ctrl: Not supported\n");
		err = -EINVAL;
	}
	return err;
}
EXPORT_SYMBOL(gpmc_nand_write);



/**
 * gpmc_prefetch_enable - configures and starts prefetch transfer
 * @cs: cs (chip select) number
 * @fifo_th: fifo threshold to be used for read/ write
 * @dma_mode: dma mode enable (1) or disable (0)
 * @u32_count: number of bytes to be transferred
 * @is_write: prefetch read(0) or write post(1) mode
 */
int gpmc_prefetch_enable(int cs, int fifo_th, int dma_mode,
				unsigned int u32_count, int is_write)
{

	if (fifo_th > PREFETCH_FIFOTHRESHOLD_MAX) {
		pr_err("gpmc: fifo threshold is not supported\n");
		return -1;
	} else if (!(gpmc_read_reg(GPMC_PREFETCH_CONTROL))) {
		/* Set the amount of bytes to be prefetched */
		gpmc_write_reg(GPMC_PREFETCH_CONFIG2, u32_count);

		/* Set dma/mpu mode, the prefetch read / post write and
		 * enable the engine. Set which cs is has requested for.
		 */
		gpmc_write_reg(GPMC_PREFETCH_CONFIG1, ((cs << CS_NUM_SHIFT) |
					PREFETCH_FIFOTHRESHOLD(fifo_th) |
					ENABLE_PREFETCH |
					(dma_mode << DMA_MPU_MODE) |
					(0x1 & is_write)));

		/*  Start the prefetch engine */
		gpmc_write_reg(GPMC_PREFETCH_CONTROL, 0x1);
	} else {
		return -EBUSY;
	}

	return 0;
}
EXPORT_SYMBOL(gpmc_prefetch_enable);

/**
 * gpmc_prefetch_reset - disables and stops the prefetch engine
 */
int gpmc_prefetch_reset(int cs)
{
	u32 config1;

	/* check if the same module/cs is trying to reset */
	config1 = gpmc_read_reg(GPMC_PREFETCH_CONFIG1);
	if (((config1 >> CS_NUM_SHIFT) & 0x7) != cs)
		return -EINVAL;

	/* Stop the PFPW engine */
	gpmc_write_reg(GPMC_PREFETCH_CONTROL, 0x0);

	/* Reset/disable the PFPW engine */
	gpmc_write_reg(GPMC_PREFETCH_CONFIG1, 0x0);

	return 0;
}
EXPORT_SYMBOL(gpmc_prefetch_reset);

static void __init gpmc_mem_init(void)
{
	int cs;
	unsigned long boot_rom_space = 0;

	/* never allocate the first page, to facilitate bug detection;
	 * even if we didn't boot from ROM.
	 */
	boot_rom_space = BOOT_ROM_SPACE;
	/* In apollon the CS0 is mapped as 0x0000 0000 */
	if (machine_is_omap_apollon())
		boot_rom_space = 0;
	gpmc_mem_root.start = GPMC_MEM_START + boot_rom_space;
	gpmc_mem_root.end = GPMC_MEM_END;

	/* Reserve all regions that has been set up by bootloader */
	for (cs = 0; cs < GPMC_CS_NUM; cs++) {
		u32 base, size;

		if (!gpmc_cs_mem_enabled(cs))
			continue;
		gpmc_cs_get_memconf(cs, &base, &size);
		if (gpmc_cs_insert_mem(cs, base, size) < 0)
			BUG();
	}
}

static int __init gpmc_init(void)
{
	u32 l, irq;
	int cs, ret = -EINVAL;
	int gpmc_irq;
	char *ck = NULL;

	if (cpu_is_omap24xx()) {
		ck = "core_l3_ck";
		if (cpu_is_omap2420())
			l = OMAP2420_GPMC_BASE;
		else
			l = OMAP34XX_GPMC_BASE;
		gpmc_irq = INT_34XX_GPMC_IRQ;
	} else if (cpu_is_omap34xx()) {
		ck = "gpmc_fck";
		l = OMAP34XX_GPMC_BASE;
		gpmc_irq = INT_34XX_GPMC_IRQ;
	} else if (cpu_is_omap44xx()) {
		ck = "gpmc_ck";
		l = OMAP44XX_GPMC_BASE;
		gpmc_irq = OMAP44XX_IRQ_GPMC;
	}

	if (WARN_ON(!ck))
		return ret;

	gpmc_l3_clk = clk_get(NULL, ck);
	if (IS_ERR(gpmc_l3_clk)) {
		printk(KERN_ERR "Could not get GPMC clock %s\n", ck);
		BUG();
	}

	gpmc_base = ioremap(l, SZ_4K);
	if (!gpmc_base) {
		clk_put(gpmc_l3_clk);
		printk(KERN_ERR "Could not get GPMC register memory\n");
		BUG();
	}

	clk_enable(gpmc_l3_clk);

	l = gpmc_read_reg(GPMC_REVISION);
	printk(KERN_INFO "GPMC revision %d.%d\n", (l >> 4) & 0x0f, l & 0x0f);
	/* Set smart idle mode and automatic L3 clock gating */
	l = gpmc_read_reg(GPMC_SYSCONFIG);
	l &= 0x03 << 3;
	l |= (0x02 << 3) | (1 << 0);
	gpmc_write_reg(GPMC_SYSCONFIG, l);
	gpmc_mem_init();

	/* initalize the irq_chained */
	irq = OMAP_GPMC_IRQ_BASE;
	for (cs = 0; cs < GPMC_CS_NUM; cs++) {
		irq_set_chip_and_handler(irq, &dummy_irq_chip,
						handle_simple_irq);
		set_irq_flags(irq, IRQF_VALID);
		irq++;
	}

	ret = request_irq(gpmc_irq,
			gpmc_handle_irq, IRQF_SHARED, "gpmc", gpmc_base);
	if (ret)
		pr_err("gpmc: irq-%d could not claim: err %d\n",
						gpmc_irq, ret);
	return ret;
}
postcore_initcall(gpmc_init);

static irqreturn_t gpmc_handle_irq(int irq, void *dev)
{
	u8 cs;

	/* check cs to invoke the irq */
	cs = ((gpmc_read_reg(GPMC_PREFETCH_CONFIG1)) >> CS_NUM_SHIFT) & 0x7;
	if (OMAP_GPMC_IRQ_BASE+cs <= OMAP_GPMC_IRQ_END)
		generic_handle_irq(OMAP_GPMC_IRQ_BASE+cs);

	return IRQ_HANDLED;
}

#ifdef CONFIG_ARCH_OMAP3
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
	for (i = 0; i < GPMC_CS_NUM; i++) {
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
	for (i = 0; i < GPMC_CS_NUM; i++) {
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
#endif /* CONFIG_ARCH_OMAP3 */

/**
 * gpmc_enable_hwecc - enable hardware ecc functionality
 * @cs: chip select number
 * @mode: read/write mode
 * @dev_width: device bus width(1 for x16, 0 for x8)
 * @ecc_size: bytes for which ECC will be generated
 */
int gpmc_enable_hwecc(int cs, int mode, int dev_width, int ecc_size)
{
	unsigned int val;

	/* check if ecc module is in used */
	if (gpmc_ecc_used != -EINVAL)
		return -EINVAL;

	gpmc_ecc_used = cs;

	/* clear ecc and enable bits */
	val = ((0x00000001<<8) | 0x00000001);
	gpmc_write_reg(GPMC_ECC_CONTROL, val);

	/* program ecc and result sizes */
	val = ((((ecc_size >> 1) - 1) << 22) | (0x0000000F));
	gpmc_write_reg(GPMC_ECC_SIZE_CONFIG, val);

	switch (mode) {
	case GPMC_ECC_READ:
		gpmc_write_reg(GPMC_ECC_CONTROL, 0x101);
		break;
	case GPMC_ECC_READSYN:
		 gpmc_write_reg(GPMC_ECC_CONTROL, 0x100);
		break;
	case GPMC_ECC_WRITE:
		gpmc_write_reg(GPMC_ECC_CONTROL, 0x101);
		break;
	default:
		printk(KERN_INFO "Error: Unrecognized Mode[%d]!\n", mode);
		break;
	}

	/* (ECC 16 or 8 bit col) | ( CS  )  | ECC Enable */
	val = (dev_width << 7) | (cs << 1) | (0x1);
	gpmc_write_reg(GPMC_ECC_CONFIG, val);
	return 0;
}
EXPORT_SYMBOL_GPL(gpmc_enable_hwecc);

/**
 * gpmc_calculate_ecc - generate non-inverted ecc bytes
 * @cs: chip select number
 * @dat: data pointer over which ecc is computed
 * @ecc_code: ecc code buffer
 *
 * Using non-inverted ECC is considered ugly since writing a blank
 * page (padding) will clear the ECC bytes. This is not a problem as long
 * no one is trying to write data on the seemingly unused page. Reading
 * an erased page will produce an ECC mismatch between generated and read
 * ECC bytes that has to be dealt with separately.
 */
int gpmc_calculate_ecc(int cs, const u_char *dat, u_char *ecc_code)
{
	unsigned int val = 0x0;

	if (gpmc_ecc_used != cs)
		return -EINVAL;

	/* read ecc result */
	val = gpmc_read_reg(GPMC_ECC1_RESULT);
	*ecc_code++ = val;          /* P128e, ..., P1e */
	*ecc_code++ = val >> 16;    /* P128o, ..., P1o */
	/* P2048o, P1024o, P512o, P256o, P2048e, P1024e, P512e, P256e */
	*ecc_code++ = ((val >> 8) & 0x0f) | ((val >> 20) & 0xf0);

	gpmc_ecc_used = -EINVAL;
	return 0;
}
EXPORT_SYMBOL_GPL(gpmc_calculate_ecc);
