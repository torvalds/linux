/* linux/drivers/mfd/sm501.c
 *
 * Copyright (C) 2006 Simtec Electronics
 *	Ben Dooks <ben@simtec.co.uk>
 *	Vincent Sanders <vince@simtec.co.uk>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * SM501 MFD driver
*/

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/delay.h>
#include <linux/init.h>
#include <linux/list.h>
#include <linux/device.h>
#include <linux/platform_device.h>
#include <linux/pci.h>
#include <linux/i2c-gpio.h>
#include <linux/slab.h>

#include <linux/sm501.h>
#include <linux/sm501-regs.h>
#include <linux/serial_8250.h>

#include <linux/io.h>

struct sm501_device {
	struct list_head		list;
	struct platform_device		pdev;
};

struct sm501_gpio;

#ifdef CONFIG_MFD_SM501_GPIO
#include <linux/gpio.h>

struct sm501_gpio_chip {
	struct gpio_chip	gpio;
	struct sm501_gpio	*ourgpio;	/* to get back to parent. */
	void __iomem		*regbase;
	void __iomem		*control;	/* address of control reg. */
};

struct sm501_gpio {
	struct sm501_gpio_chip	low;
	struct sm501_gpio_chip	high;
	spinlock_t		lock;

	unsigned int		 registered : 1;
	void __iomem		*regs;
	struct resource		*regs_res;
};
#else
struct sm501_gpio {
	/* no gpio support, empty definition for sm501_devdata. */
};
#endif

struct sm501_devdata {
	spinlock_t			 reg_lock;
	struct mutex			 clock_lock;
	struct list_head		 devices;
	struct sm501_gpio		 gpio;

	struct device			*dev;
	struct resource			*io_res;
	struct resource			*mem_res;
	struct resource			*regs_claim;
	struct sm501_platdata		*platdata;


	unsigned int			 in_suspend;
	unsigned long			 pm_misc;

	int				 unit_power[20];
	unsigned int			 pdev_id;
	unsigned int			 irq;
	void __iomem			*regs;
	unsigned int			 rev;
};


#define MHZ (1000 * 1000)

#ifdef DEBUG
static const unsigned int div_tab[] = {
	[0]		= 1,
	[1]		= 2,
	[2]		= 4,
	[3]		= 8,
	[4]		= 16,
	[5]		= 32,
	[6]		= 64,
	[7]		= 128,
	[8]		= 3,
	[9]		= 6,
	[10]	        = 12,
	[11]		= 24,
	[12]		= 48,
	[13]		= 96,
	[14]		= 192,
	[15]		= 384,
	[16]		= 5,
	[17]		= 10,
	[18]		= 20,
	[19]		= 40,
	[20]		= 80,
	[21]		= 160,
	[22]		= 320,
	[23]		= 604,
};

static unsigned long decode_div(unsigned long pll2, unsigned long val,
				unsigned int lshft, unsigned int selbit,
				unsigned long mask)
{
	if (val & selbit)
		pll2 = 288 * MHZ;

	return pll2 / div_tab[(val >> lshft) & mask];
}

#define fmt_freq(x) ((x) / MHZ), ((x) % MHZ), (x)

/* sm501_dump_clk
 *
 * Print out the current clock configuration for the device
*/

static void sm501_dump_clk(struct sm501_devdata *sm)
{
	unsigned long misct = smc501_readl(sm->regs + SM501_MISC_TIMING);
	unsigned long pm0 = smc501_readl(sm->regs + SM501_POWER_MODE_0_CLOCK);
	unsigned long pm1 = smc501_readl(sm->regs + SM501_POWER_MODE_1_CLOCK);
	unsigned long pmc = smc501_readl(sm->regs + SM501_POWER_MODE_CONTROL);
	unsigned long sdclk0, sdclk1;
	unsigned long pll2 = 0;

	switch (misct & 0x30) {
	case 0x00:
		pll2 = 336 * MHZ;
		break;
	case 0x10:
		pll2 = 288 * MHZ;
		break;
	case 0x20:
		pll2 = 240 * MHZ;
		break;
	case 0x30:
		pll2 = 192 * MHZ;
		break;
	}

	sdclk0 = (misct & (1<<12)) ? pll2 : 288 * MHZ;
	sdclk0 /= div_tab[((misct >> 8) & 0xf)];

	sdclk1 = (misct & (1<<20)) ? pll2 : 288 * MHZ;
	sdclk1 /= div_tab[((misct >> 16) & 0xf)];

	dev_dbg(sm->dev, "MISCT=%08lx, PM0=%08lx, PM1=%08lx\n",
		misct, pm0, pm1);

	dev_dbg(sm->dev, "PLL2 = %ld.%ld MHz (%ld), SDCLK0=%08lx, SDCLK1=%08lx\n",
		fmt_freq(pll2), sdclk0, sdclk1);

	dev_dbg(sm->dev, "SDRAM: PM0=%ld, PM1=%ld\n", sdclk0, sdclk1);

	dev_dbg(sm->dev, "PM0[%c]: "
		 "P2 %ld.%ld MHz (%ld), V2 %ld.%ld (%ld), "
		 "M %ld.%ld (%ld), MX1 %ld.%ld (%ld)\n",
		 (pmc & 3 ) == 0 ? '*' : '-',
		 fmt_freq(decode_div(pll2, pm0, 24, 1<<29, 31)),
		 fmt_freq(decode_div(pll2, pm0, 16, 1<<20, 15)),
		 fmt_freq(decode_div(pll2, pm0, 8,  1<<12, 15)),
		 fmt_freq(decode_div(pll2, pm0, 0,  1<<4,  15)));

	dev_dbg(sm->dev, "PM1[%c]: "
		"P2 %ld.%ld MHz (%ld), V2 %ld.%ld (%ld), "
		"M %ld.%ld (%ld), MX1 %ld.%ld (%ld)\n",
		(pmc & 3 ) == 1 ? '*' : '-',
		fmt_freq(decode_div(pll2, pm1, 24, 1<<29, 31)),
		fmt_freq(decode_div(pll2, pm1, 16, 1<<20, 15)),
		fmt_freq(decode_div(pll2, pm1, 8,  1<<12, 15)),
		fmt_freq(decode_div(pll2, pm1, 0,  1<<4,  15)));
}

static void sm501_dump_regs(struct sm501_devdata *sm)
{
	void __iomem *regs = sm->regs;

	dev_info(sm->dev, "System Control   %08x\n",
			smc501_readl(regs + SM501_SYSTEM_CONTROL));
	dev_info(sm->dev, "Misc Control     %08x\n",
			smc501_readl(regs + SM501_MISC_CONTROL));
	dev_info(sm->dev, "GPIO Control Low %08x\n",
			smc501_readl(regs + SM501_GPIO31_0_CONTROL));
	dev_info(sm->dev, "GPIO Control Hi  %08x\n",
			smc501_readl(regs + SM501_GPIO63_32_CONTROL));
	dev_info(sm->dev, "DRAM Control     %08x\n",
			smc501_readl(regs + SM501_DRAM_CONTROL));
	dev_info(sm->dev, "Arbitration Ctrl %08x\n",
			smc501_readl(regs + SM501_ARBTRTN_CONTROL));
	dev_info(sm->dev, "Misc Timing      %08x\n",
			smc501_readl(regs + SM501_MISC_TIMING));
}

static void sm501_dump_gate(struct sm501_devdata *sm)
{
	dev_info(sm->dev, "CurrentGate      %08x\n",
			smc501_readl(sm->regs + SM501_CURRENT_GATE));
	dev_info(sm->dev, "CurrentClock     %08x\n",
			smc501_readl(sm->regs + SM501_CURRENT_CLOCK));
	dev_info(sm->dev, "PowerModeControl %08x\n",
			smc501_readl(sm->regs + SM501_POWER_MODE_CONTROL));
}

#else
static inline void sm501_dump_gate(struct sm501_devdata *sm) { }
static inline void sm501_dump_regs(struct sm501_devdata *sm) { }
static inline void sm501_dump_clk(struct sm501_devdata *sm) { }
#endif

/* sm501_sync_regs
 *
 * ensure the
*/

static void sm501_sync_regs(struct sm501_devdata *sm)
{
	smc501_readl(sm->regs);
}

static inline void sm501_mdelay(struct sm501_devdata *sm, unsigned int delay)
{
	/* during suspend/resume, we are currently not allowed to sleep,
	 * so change to using mdelay() instead of msleep() if we
	 * are in one of these paths */

	if (sm->in_suspend)
		mdelay(delay);
	else
		msleep(delay);
}

/* sm501_misc_control
 *
 * alters the miscellaneous control parameters
*/

int sm501_misc_control(struct device *dev,
		       unsigned long set, unsigned long clear)
{
	struct sm501_devdata *sm = dev_get_drvdata(dev);
	unsigned long misc;
	unsigned long save;
	unsigned long to;

	spin_lock_irqsave(&sm->reg_lock, save);

	misc = smc501_readl(sm->regs + SM501_MISC_CONTROL);
	to = (misc & ~clear) | set;

	if (to != misc) {
		smc501_writel(to, sm->regs + SM501_MISC_CONTROL);
		sm501_sync_regs(sm);

		dev_dbg(sm->dev, "MISC_CONTROL %08lx\n", misc);
	}

	spin_unlock_irqrestore(&sm->reg_lock, save);
	return to;
}

EXPORT_SYMBOL_GPL(sm501_misc_control);

/* sm501_modify_reg
 *
 * Modify a register in the SM501 which may be shared with other
 * drivers.
*/

unsigned long sm501_modify_reg(struct device *dev,
			       unsigned long reg,
			       unsigned long set,
			       unsigned long clear)
{
	struct sm501_devdata *sm = dev_get_drvdata(dev);
	unsigned long data;
	unsigned long save;

	spin_lock_irqsave(&sm->reg_lock, save);

	data = smc501_readl(sm->regs + reg);
	data |= set;
	data &= ~clear;

	smc501_writel(data, sm->regs + reg);
	sm501_sync_regs(sm);

	spin_unlock_irqrestore(&sm->reg_lock, save);

	return data;
}

EXPORT_SYMBOL_GPL(sm501_modify_reg);

/* sm501_unit_power
 *
 * alters the power active gate to set specific units on or off
 */

int sm501_unit_power(struct device *dev, unsigned int unit, unsigned int to)
{
	struct sm501_devdata *sm = dev_get_drvdata(dev);
	unsigned long mode;
	unsigned long gate;
	unsigned long clock;

	mutex_lock(&sm->clock_lock);

	mode = smc501_readl(sm->regs + SM501_POWER_MODE_CONTROL);
	gate = smc501_readl(sm->regs + SM501_CURRENT_GATE);
	clock = smc501_readl(sm->regs + SM501_CURRENT_CLOCK);

	mode &= 3;		/* get current power mode */

	if (unit >= ARRAY_SIZE(sm->unit_power)) {
		dev_err(dev, "%s: bad unit %d\n", __func__, unit);
		goto already;
	}

	dev_dbg(sm->dev, "%s: unit %d, cur %d, to %d\n", __func__, unit,
		sm->unit_power[unit], to);

	if (to == 0 && sm->unit_power[unit] == 0) {
		dev_err(sm->dev, "unit %d is already shutdown\n", unit);
		goto already;
	}

	sm->unit_power[unit] += to ? 1 : -1;
	to = sm->unit_power[unit] ? 1 : 0;

	if (to) {
		if (gate & (1 << unit))
			goto already;
		gate |= (1 << unit);
	} else {
		if (!(gate & (1 << unit)))
			goto already;
		gate &= ~(1 << unit);
	}

	switch (mode) {
	case 1:
		smc501_writel(gate, sm->regs + SM501_POWER_MODE_0_GATE);
		smc501_writel(clock, sm->regs + SM501_POWER_MODE_0_CLOCK);
		mode = 0;
		break;
	case 2:
	case 0:
		smc501_writel(gate, sm->regs + SM501_POWER_MODE_1_GATE);
		smc501_writel(clock, sm->regs + SM501_POWER_MODE_1_CLOCK);
		mode = 1;
		break;

	default:
		gate = -1;
		goto already;
	}

	smc501_writel(mode, sm->regs + SM501_POWER_MODE_CONTROL);
	sm501_sync_regs(sm);

	dev_dbg(sm->dev, "gate %08lx, clock %08lx, mode %08lx\n",
		gate, clock, mode);

	sm501_mdelay(sm, 16);

 already:
	mutex_unlock(&sm->clock_lock);
	return gate;
}

EXPORT_SYMBOL_GPL(sm501_unit_power);

/* clock value structure. */
struct sm501_clock {
	unsigned long mclk;
	int divider;
	int shift;
	unsigned int m, n, k;
};

/* sm501_calc_clock
 *
 * Calculates the nearest discrete clock frequency that
 * can be achieved with the specified input clock.
 *   the maximum divisor is 3 or 5
 */

static int sm501_calc_clock(unsigned long freq,
			    struct sm501_clock *clock,
			    int max_div,
			    unsigned long mclk,
			    long *best_diff)
{
	int ret = 0;
	int divider;
	int shift;
	long diff;

	/* try dividers 1 and 3 for CRT and for panel,
	   try divider 5 for panel only.*/

	for (divider = 1; divider <= max_div; divider += 2) {
		/* try all 8 shift values.*/
		for (shift = 0; shift < 8; shift++) {
			/* Calculate difference to requested clock */
			diff = DIV_ROUND_CLOSEST(mclk, divider << shift) - freq;
			if (diff < 0)
				diff = -diff;

			/* If it is less than the current, use it */
			if (diff < *best_diff) {
				*best_diff = diff;

				clock->mclk = mclk;
				clock->divider = divider;
				clock->shift = shift;
				ret = 1;
			}
		}
	}

	return ret;
}

/* sm501_calc_pll
 *
 * Calculates the nearest discrete clock frequency that can be
 * achieved using the programmable PLL.
 *   the maximum divisor is 3 or 5
 */

static unsigned long sm501_calc_pll(unsigned long freq,
					struct sm501_clock *clock,
					int max_div)
{
	unsigned long mclk;
	unsigned int m, n, k;
	long best_diff = 999999999;

	/*
	 * The SM502 datasheet doesn't specify the min/max values for M and N.
	 * N = 1 at least doesn't work in practice.
	 */
	for (m = 2; m <= 255; m++) {
		for (n = 2; n <= 127; n++) {
			for (k = 0; k <= 1; k++) {
				mclk = (24000000UL * m / n) >> k;

				if (sm501_calc_clock(freq, clock, max_div,
						     mclk, &best_diff)) {
					clock->m = m;
					clock->n = n;
					clock->k = k;
				}
			}
		}
	}

	/* Return best clock. */
	return clock->mclk / (clock->divider << clock->shift);
}

/* sm501_select_clock
 *
 * Calculates the nearest discrete clock frequency that can be
 * achieved using the 288MHz and 336MHz PLLs.
 *   the maximum divisor is 3 or 5
 */

static unsigned long sm501_select_clock(unsigned long freq,
					struct sm501_clock *clock,
					int max_div)
{
	unsigned long mclk;
	long best_diff = 999999999;

	/* Try 288MHz and 336MHz clocks. */
	for (mclk = 288000000; mclk <= 336000000; mclk += 48000000) {
		sm501_calc_clock(freq, clock, max_div, mclk, &best_diff);
	}

	/* Return best clock. */
	return clock->mclk / (clock->divider << clock->shift);
}

/* sm501_set_clock
 *
 * set one of the four clock sources to the closest available frequency to
 *  the one specified
*/

unsigned long sm501_set_clock(struct device *dev,
			      int clksrc,
			      unsigned long req_freq)
{
	struct sm501_devdata *sm = dev_get_drvdata(dev);
	unsigned long mode = smc501_readl(sm->regs + SM501_POWER_MODE_CONTROL);
	unsigned long gate = smc501_readl(sm->regs + SM501_CURRENT_GATE);
	unsigned long clock = smc501_readl(sm->regs + SM501_CURRENT_CLOCK);
	unsigned int pll_reg = 0;
	unsigned long sm501_freq; /* the actual frequency achieved */
	u64 reg;

	struct sm501_clock to;

	/* find achivable discrete frequency and setup register value
	 * accordingly, V2XCLK, MCLK and M1XCLK are the same P2XCLK
	 * has an extra bit for the divider */

	switch (clksrc) {
	case SM501_CLOCK_P2XCLK:
		/* This clock is divided in half so to achieve the
		 * requested frequency the value must be multiplied by
		 * 2. This clock also has an additional pre divisor */

		if (sm->rev >= 0xC0) {
			/* SM502 -> use the programmable PLL */
			sm501_freq = (sm501_calc_pll(2 * req_freq,
						     &to, 5) / 2);
			reg = to.shift & 0x07;/* bottom 3 bits are shift */
			if (to.divider == 3)
				reg |= 0x08; /* /3 divider required */
			else if (to.divider == 5)
				reg |= 0x10; /* /5 divider required */
			reg |= 0x40; /* select the programmable PLL */
			pll_reg = 0x20000 | (to.k << 15) | (to.n << 8) | to.m;
		} else {
			sm501_freq = (sm501_select_clock(2 * req_freq,
							 &to, 5) / 2);
			reg = to.shift & 0x07;/* bottom 3 bits are shift */
			if (to.divider == 3)
				reg |= 0x08; /* /3 divider required */
			else if (to.divider == 5)
				reg |= 0x10; /* /5 divider required */
			if (to.mclk != 288000000)
				reg |= 0x20; /* which mclk pll is source */
		}
		break;

	case SM501_CLOCK_V2XCLK:
		/* This clock is divided in half so to achieve the
		 * requested frequency the value must be multiplied by 2. */

		sm501_freq = (sm501_select_clock(2 * req_freq, &to, 3) / 2);
		reg=to.shift & 0x07;	/* bottom 3 bits are shift */
		if (to.divider == 3)
			reg |= 0x08;	/* /3 divider required */
		if (to.mclk != 288000000)
			reg |= 0x10;	/* which mclk pll is source */
		break;

	case SM501_CLOCK_MCLK:
	case SM501_CLOCK_M1XCLK:
		/* These clocks are the same and not further divided */

		sm501_freq = sm501_select_clock( req_freq, &to, 3);
		reg=to.shift & 0x07;	/* bottom 3 bits are shift */
		if (to.divider == 3)
			reg |= 0x08;	/* /3 divider required */
		if (to.mclk != 288000000)
			reg |= 0x10;	/* which mclk pll is source */
		break;

	default:
		return 0; /* this is bad */
	}

	mutex_lock(&sm->clock_lock);

	mode = smc501_readl(sm->regs + SM501_POWER_MODE_CONTROL);
	gate = smc501_readl(sm->regs + SM501_CURRENT_GATE);
	clock = smc501_readl(sm->regs + SM501_CURRENT_CLOCK);

	clock = clock & ~(0xFF << clksrc);
	clock |= reg<<clksrc;

	mode &= 3;	/* find current mode */

	switch (mode) {
	case 1:
		smc501_writel(gate, sm->regs + SM501_POWER_MODE_0_GATE);
		smc501_writel(clock, sm->regs + SM501_POWER_MODE_0_CLOCK);
		mode = 0;
		break;
	case 2:
	case 0:
		smc501_writel(gate, sm->regs + SM501_POWER_MODE_1_GATE);
		smc501_writel(clock, sm->regs + SM501_POWER_MODE_1_CLOCK);
		mode = 1;
		break;

	default:
		mutex_unlock(&sm->clock_lock);
		return -1;
	}

	smc501_writel(mode, sm->regs + SM501_POWER_MODE_CONTROL);

	if (pll_reg)
		smc501_writel(pll_reg,
				sm->regs + SM501_PROGRAMMABLE_PLL_CONTROL);

	sm501_sync_regs(sm);

	dev_dbg(sm->dev, "gate %08lx, clock %08lx, mode %08lx\n",
		gate, clock, mode);

	sm501_mdelay(sm, 16);
	mutex_unlock(&sm->clock_lock);

	sm501_dump_clk(sm);

	return sm501_freq;
}

EXPORT_SYMBOL_GPL(sm501_set_clock);

/* sm501_find_clock
 *
 * finds the closest available frequency for a given clock
*/

unsigned long sm501_find_clock(struct device *dev,
			       int clksrc,
			       unsigned long req_freq)
{
	struct sm501_devdata *sm = dev_get_drvdata(dev);
	unsigned long sm501_freq; /* the frequency achieveable by the 501 */
	struct sm501_clock to;

	switch (clksrc) {
	case SM501_CLOCK_P2XCLK:
		if (sm->rev >= 0xC0) {
			/* SM502 -> use the programmable PLL */
			sm501_freq = (sm501_calc_pll(2 * req_freq,
						     &to, 5) / 2);
		} else {
			sm501_freq = (sm501_select_clock(2 * req_freq,
							 &to, 5) / 2);
		}
		break;

	case SM501_CLOCK_V2XCLK:
		sm501_freq = (sm501_select_clock(2 * req_freq, &to, 3) / 2);
		break;

	case SM501_CLOCK_MCLK:
	case SM501_CLOCK_M1XCLK:
		sm501_freq = sm501_select_clock(req_freq, &to, 3);
		break;

	default:
		sm501_freq = 0;		/* error */
	}

	return sm501_freq;
}

EXPORT_SYMBOL_GPL(sm501_find_clock);

static struct sm501_device *to_sm_device(struct platform_device *pdev)
{
	return container_of(pdev, struct sm501_device, pdev);
}

/* sm501_device_release
 *
 * A release function for the platform devices we create to allow us to
 * free any items we allocated
*/

static void sm501_device_release(struct device *dev)
{
	kfree(to_sm_device(to_platform_device(dev)));
}

/* sm501_create_subdev
 *
 * Create a skeleton platform device with resources for passing to a
 * sub-driver
*/

static struct platform_device *
sm501_create_subdev(struct sm501_devdata *sm, char *name,
		    unsigned int res_count, unsigned int platform_data_size)
{
	struct sm501_device *smdev;

	smdev = kzalloc(sizeof(struct sm501_device) +
			(sizeof(struct resource) * res_count) +
			platform_data_size, GFP_KERNEL);
	if (!smdev)
		return NULL;

	smdev->pdev.dev.release = sm501_device_release;

	smdev->pdev.name = name;
	smdev->pdev.id = sm->pdev_id;
	smdev->pdev.dev.parent = sm->dev;

	if (res_count) {
		smdev->pdev.resource = (struct resource *)(smdev+1);
		smdev->pdev.num_resources = res_count;
	}
	if (platform_data_size)
		smdev->pdev.dev.platform_data = (void *)(smdev+1);

	return &smdev->pdev;
}

/* sm501_register_device
 *
 * Register a platform device created with sm501_create_subdev()
*/

static int sm501_register_device(struct sm501_devdata *sm,
				 struct platform_device *pdev)
{
	struct sm501_device *smdev = to_sm_device(pdev);
	int ptr;
	int ret;

	for (ptr = 0; ptr < pdev->num_resources; ptr++) {
		printk(KERN_DEBUG "%s[%d] %pR\n",
		       pdev->name, ptr, &pdev->resource[ptr]);
	}

	ret = platform_device_register(pdev);

	if (ret >= 0) {
		dev_dbg(sm->dev, "registered %s\n", pdev->name);
		list_add_tail(&smdev->list, &sm->devices);
	} else
		dev_err(sm->dev, "error registering %s (%d)\n",
			pdev->name, ret);

	return ret;
}

/* sm501_create_subio
 *
 * Fill in an IO resource for a sub device
*/

static void sm501_create_subio(struct sm501_devdata *sm,
			       struct resource *res,
			       resource_size_t offs,
			       resource_size_t size)
{
	res->flags = IORESOURCE_MEM;
	res->parent = sm->io_res;
	res->start = sm->io_res->start + offs;
	res->end = res->start + size - 1;
}

/* sm501_create_mem
 *
 * Fill in an MEM resource for a sub device
*/

static void sm501_create_mem(struct sm501_devdata *sm,
			     struct resource *res,
			     resource_size_t *offs,
			     resource_size_t size)
{
	*offs -= size;		/* adjust memory size */

	res->flags = IORESOURCE_MEM;
	res->parent = sm->mem_res;
	res->start = sm->mem_res->start + *offs;
	res->end = res->start + size - 1;
}

/* sm501_create_irq
 *
 * Fill in an IRQ resource for a sub device
*/

static void sm501_create_irq(struct sm501_devdata *sm,
			     struct resource *res)
{
	res->flags = IORESOURCE_IRQ;
	res->parent = NULL;
	res->start = res->end = sm->irq;
}

static int sm501_register_usbhost(struct sm501_devdata *sm,
				  resource_size_t *mem_avail)
{
	struct platform_device *pdev;

	pdev = sm501_create_subdev(sm, "sm501-usb", 3, 0);
	if (!pdev)
		return -ENOMEM;

	sm501_create_subio(sm, &pdev->resource[0], 0x40000, 0x20000);
	sm501_create_mem(sm, &pdev->resource[1], mem_avail, 256*1024);
	sm501_create_irq(sm, &pdev->resource[2]);

	return sm501_register_device(sm, pdev);
}

static void sm501_setup_uart_data(struct sm501_devdata *sm,
				  struct plat_serial8250_port *uart_data,
				  unsigned int offset)
{
	uart_data->membase = sm->regs + offset;
	uart_data->mapbase = sm->io_res->start + offset;
	uart_data->iotype = UPIO_MEM;
	uart_data->irq = sm->irq;
	uart_data->flags = UPF_BOOT_AUTOCONF | UPF_SKIP_TEST | UPF_SHARE_IRQ;
	uart_data->regshift = 2;
	uart_data->uartclk = (9600 * 16);
}

static int sm501_register_uart(struct sm501_devdata *sm, int devices)
{
	struct platform_device *pdev;
	struct plat_serial8250_port *uart_data;

	pdev = sm501_create_subdev(sm, "serial8250", 0,
				   sizeof(struct plat_serial8250_port) * 3);
	if (!pdev)
		return -ENOMEM;

	uart_data = dev_get_platdata(&pdev->dev);

	if (devices & SM501_USE_UART0) {
		sm501_setup_uart_data(sm, uart_data++, 0x30000);
		sm501_unit_power(sm->dev, SM501_GATE_UART0, 1);
		sm501_modify_reg(sm->dev, SM501_IRQ_MASK, 1 << 12, 0);
		sm501_modify_reg(sm->dev, SM501_GPIO63_32_CONTROL, 0x01e0, 0);
	}
	if (devices & SM501_USE_UART1) {
		sm501_setup_uart_data(sm, uart_data++, 0x30020);
		sm501_unit_power(sm->dev, SM501_GATE_UART1, 1);
		sm501_modify_reg(sm->dev, SM501_IRQ_MASK, 1 << 13, 0);
		sm501_modify_reg(sm->dev, SM501_GPIO63_32_CONTROL, 0x1e00, 0);
	}

	pdev->id = PLAT8250_DEV_SM501;

	return sm501_register_device(sm, pdev);
}

static int sm501_register_display(struct sm501_devdata *sm,
				  resource_size_t *mem_avail)
{
	struct platform_device *pdev;

	pdev = sm501_create_subdev(sm, "sm501-fb", 4, 0);
	if (!pdev)
		return -ENOMEM;

	sm501_create_subio(sm, &pdev->resource[0], 0x80000, 0x10000);
	sm501_create_subio(sm, &pdev->resource[1], 0x100000, 0x50000);
	sm501_create_mem(sm, &pdev->resource[2], mem_avail, *mem_avail);
	sm501_create_irq(sm, &pdev->resource[3]);

	return sm501_register_device(sm, pdev);
}

#ifdef CONFIG_MFD_SM501_GPIO

static inline struct sm501_gpio_chip *to_sm501_gpio(struct gpio_chip *gc)
{
	return container_of(gc, struct sm501_gpio_chip, gpio);
}

static inline struct sm501_devdata *sm501_gpio_to_dev(struct sm501_gpio *gpio)
{
	return container_of(gpio, struct sm501_devdata, gpio);
}

static int sm501_gpio_get(struct gpio_chip *chip, unsigned offset)

{
	struct sm501_gpio_chip *smgpio = to_sm501_gpio(chip);
	unsigned long result;

	result = smc501_readl(smgpio->regbase + SM501_GPIO_DATA_LOW);
	result >>= offset;

	return result & 1UL;
}

static void sm501_gpio_ensure_gpio(struct sm501_gpio_chip *smchip,
				   unsigned long bit)
{
	unsigned long ctrl;

	/* check and modify if this pin is not set as gpio. */

	if (smc501_readl(smchip->control) & bit) {
		dev_info(sm501_gpio_to_dev(smchip->ourgpio)->dev,
			 "changing mode of gpio, bit %08lx\n", bit);

		ctrl = smc501_readl(smchip->control);
		ctrl &= ~bit;
		smc501_writel(ctrl, smchip->control);

		sm501_sync_regs(sm501_gpio_to_dev(smchip->ourgpio));
	}
}

static void sm501_gpio_set(struct gpio_chip *chip, unsigned offset, int value)

{
	struct sm501_gpio_chip *smchip = to_sm501_gpio(chip);
	struct sm501_gpio *smgpio = smchip->ourgpio;
	unsigned long bit = 1 << offset;
	void __iomem *regs = smchip->regbase;
	unsigned long save;
	unsigned long val;

	dev_dbg(sm501_gpio_to_dev(smgpio)->dev, "%s(%p,%d)\n",
		__func__, chip, offset);

	spin_lock_irqsave(&smgpio->lock, save);

	val = smc501_readl(regs + SM501_GPIO_DATA_LOW) & ~bit;
	if (value)
		val |= bit;
	smc501_writel(val, regs);

	sm501_sync_regs(sm501_gpio_to_dev(smgpio));
	sm501_gpio_ensure_gpio(smchip, bit);

	spin_unlock_irqrestore(&smgpio->lock, save);
}

static int sm501_gpio_input(struct gpio_chip *chip, unsigned offset)
{
	struct sm501_gpio_chip *smchip = to_sm501_gpio(chip);
	struct sm501_gpio *smgpio = smchip->ourgpio;
	void __iomem *regs = smchip->regbase;
	unsigned long bit = 1 << offset;
	unsigned long save;
	unsigned long ddr;

	dev_dbg(sm501_gpio_to_dev(smgpio)->dev, "%s(%p,%d)\n",
		__func__, chip, offset);

	spin_lock_irqsave(&smgpio->lock, save);

	ddr = smc501_readl(regs + SM501_GPIO_DDR_LOW);
	smc501_writel(ddr & ~bit, regs + SM501_GPIO_DDR_LOW);

	sm501_sync_regs(sm501_gpio_to_dev(smgpio));
	sm501_gpio_ensure_gpio(smchip, bit);

	spin_unlock_irqrestore(&smgpio->lock, save);

	return 0;
}

static int sm501_gpio_output(struct gpio_chip *chip,
			     unsigned offset, int value)
{
	struct sm501_gpio_chip *smchip = to_sm501_gpio(chip);
	struct sm501_gpio *smgpio = smchip->ourgpio;
	unsigned long bit = 1 << offset;
	void __iomem *regs = smchip->regbase;
	unsigned long save;
	unsigned long val;
	unsigned long ddr;

	dev_dbg(sm501_gpio_to_dev(smgpio)->dev, "%s(%p,%d,%d)\n",
		__func__, chip, offset, value);

	spin_lock_irqsave(&smgpio->lock, save);

	val = smc501_readl(regs + SM501_GPIO_DATA_LOW);
	if (value)
		val |= bit;
	else
		val &= ~bit;
	smc501_writel(val, regs);

	ddr = smc501_readl(regs + SM501_GPIO_DDR_LOW);
	smc501_writel(ddr | bit, regs + SM501_GPIO_DDR_LOW);

	sm501_sync_regs(sm501_gpio_to_dev(smgpio));
	smc501_writel(val, regs + SM501_GPIO_DATA_LOW);

	sm501_sync_regs(sm501_gpio_to_dev(smgpio));
	spin_unlock_irqrestore(&smgpio->lock, save);

	return 0;
}

static struct gpio_chip gpio_chip_template = {
	.ngpio			= 32,
	.direction_input	= sm501_gpio_input,
	.direction_output	= sm501_gpio_output,
	.set			= sm501_gpio_set,
	.get			= sm501_gpio_get,
};

static int sm501_gpio_register_chip(struct sm501_devdata *sm,
					      struct sm501_gpio *gpio,
					      struct sm501_gpio_chip *chip)
{
	struct sm501_platdata *pdata = sm->platdata;
	struct gpio_chip *gchip = &chip->gpio;
	int base = pdata->gpio_base;

	chip->gpio = gpio_chip_template;

	if (chip == &gpio->high) {
		if (base > 0)
			base += 32;
		chip->regbase = gpio->regs + SM501_GPIO_DATA_HIGH;
		chip->control = sm->regs + SM501_GPIO63_32_CONTROL;
		gchip->label  = "SM501-HIGH";
	} else {
		chip->regbase = gpio->regs + SM501_GPIO_DATA_LOW;
		chip->control = sm->regs + SM501_GPIO31_0_CONTROL;
		gchip->label  = "SM501-LOW";
	}

	gchip->base   = base;
	chip->ourgpio = gpio;

	return gpiochip_add(gchip);
}

static int sm501_register_gpio(struct sm501_devdata *sm)
{
	struct sm501_gpio *gpio = &sm->gpio;
	resource_size_t iobase = sm->io_res->start + SM501_GPIO;
	int ret;

	dev_dbg(sm->dev, "registering gpio block %08llx\n",
		(unsigned long long)iobase);

	spin_lock_init(&gpio->lock);

	gpio->regs_res = request_mem_region(iobase, 0x20, "sm501-gpio");
	if (gpio->regs_res == NULL) {
		dev_err(sm->dev, "gpio: failed to request region\n");
		return -ENXIO;
	}

	gpio->regs = ioremap(iobase, 0x20);
	if (gpio->regs == NULL) {
		dev_err(sm->dev, "gpio: failed to remap registers\n");
		ret = -ENXIO;
		goto err_claimed;
	}

	/* Register both our chips. */

	ret = sm501_gpio_register_chip(sm, gpio, &gpio->low);
	if (ret) {
		dev_err(sm->dev, "failed to add low chip\n");
		goto err_mapped;
	}

	ret = sm501_gpio_register_chip(sm, gpio, &gpio->high);
	if (ret) {
		dev_err(sm->dev, "failed to add high chip\n");
		goto err_low_chip;
	}

	gpio->registered = 1;

	return 0;

 err_low_chip:
	gpiochip_remove(&gpio->low.gpio);

 err_mapped:
	iounmap(gpio->regs);

 err_claimed:
	release_resource(gpio->regs_res);
	kfree(gpio->regs_res);

	return ret;
}

static void sm501_gpio_remove(struct sm501_devdata *sm)
{
	struct sm501_gpio *gpio = &sm->gpio;

	if (!sm->gpio.registered)
		return;

	gpiochip_remove(&gpio->low.gpio);
	gpiochip_remove(&gpio->high.gpio);

	iounmap(gpio->regs);
	release_resource(gpio->regs_res);
	kfree(gpio->regs_res);
}

static inline int sm501_gpio_pin2nr(struct sm501_devdata *sm, unsigned int pin)
{
	struct sm501_gpio *gpio = &sm->gpio;
	int base = (pin < 32) ? gpio->low.gpio.base : gpio->high.gpio.base;

	return (pin % 32) + base;
}

static inline int sm501_gpio_isregistered(struct sm501_devdata *sm)
{
	return sm->gpio.registered;
}
#else
static inline int sm501_register_gpio(struct sm501_devdata *sm)
{
	return 0;
}

static inline void sm501_gpio_remove(struct sm501_devdata *sm)
{
}

static inline int sm501_gpio_pin2nr(struct sm501_devdata *sm, unsigned int pin)
{
	return -1;
}

static inline int sm501_gpio_isregistered(struct sm501_devdata *sm)
{
	return 0;
}
#endif

static int sm501_register_gpio_i2c_instance(struct sm501_devdata *sm,
					    struct sm501_platdata_gpio_i2c *iic)
{
	struct i2c_gpio_platform_data *icd;
	struct platform_device *pdev;

	pdev = sm501_create_subdev(sm, "i2c-gpio", 0,
				   sizeof(struct i2c_gpio_platform_data));
	if (!pdev)
		return -ENOMEM;

	icd = dev_get_platdata(&pdev->dev);

	/* We keep the pin_sda and pin_scl fields relative in case the
	 * same platform data is passed to >1 SM501.
	 */

	icd->sda_pin = sm501_gpio_pin2nr(sm, iic->pin_sda);
	icd->scl_pin = sm501_gpio_pin2nr(sm, iic->pin_scl);
	icd->timeout = iic->timeout;
	icd->udelay = iic->udelay;

	/* note, we can't use either of the pin numbers, as the i2c-gpio
	 * driver uses the platform.id field to generate the bus number
	 * to register with the i2c core; The i2c core doesn't have enough
	 * entries to deal with anything we currently use.
	*/

	pdev->id = iic->bus_num;

	dev_info(sm->dev, "registering i2c-%d: sda=%d (%d), scl=%d (%d)\n",
		 iic->bus_num,
		 icd->sda_pin, iic->pin_sda, icd->scl_pin, iic->pin_scl);

	return sm501_register_device(sm, pdev);
}

static int sm501_register_gpio_i2c(struct sm501_devdata *sm,
				   struct sm501_platdata *pdata)
{
	struct sm501_platdata_gpio_i2c *iic = pdata->gpio_i2c;
	int index;
	int ret;

	for (index = 0; index < pdata->gpio_i2c_nr; index++, iic++) {
		ret = sm501_register_gpio_i2c_instance(sm, iic);
		if (ret < 0)
			return ret;
	}

	return 0;
}

/* sm501_dbg_regs
 *
 * Debug attribute to attach to parent device to show core registers
*/

static ssize_t sm501_dbg_regs(struct device *dev,
			      struct device_attribute *attr, char *buff)
{
	struct sm501_devdata *sm = dev_get_drvdata(dev)	;
	unsigned int reg;
	char *ptr = buff;
	int ret;

	for (reg = 0x00; reg < 0x70; reg += 4) {
		ret = sprintf(ptr, "%08x = %08x\n",
			      reg, smc501_readl(sm->regs + reg));
		ptr += ret;
	}

	return ptr - buff;
}


static DEVICE_ATTR(dbg_regs, 0444, sm501_dbg_regs, NULL);

/* sm501_init_reg
 *
 * Helper function for the init code to setup a register
 *
 * clear the bits which are set in r->mask, and then set
 * the bits set in r->set.
*/

static inline void sm501_init_reg(struct sm501_devdata *sm,
				  unsigned long reg,
				  struct sm501_reg_init *r)
{
	unsigned long tmp;

	tmp = smc501_readl(sm->regs + reg);
	tmp &= ~r->mask;
	tmp |= r->set;
	smc501_writel(tmp, sm->regs + reg);
}

/* sm501_init_regs
 *
 * Setup core register values
*/

static void sm501_init_regs(struct sm501_devdata *sm,
			    struct sm501_initdata *init)
{
	sm501_misc_control(sm->dev,
			   init->misc_control.set,
			   init->misc_control.mask);

	sm501_init_reg(sm, SM501_MISC_TIMING, &init->misc_timing);
	sm501_init_reg(sm, SM501_GPIO31_0_CONTROL, &init->gpio_low);
	sm501_init_reg(sm, SM501_GPIO63_32_CONTROL, &init->gpio_high);

	if (init->m1xclk) {
		dev_info(sm->dev, "setting M1XCLK to %ld\n", init->m1xclk);
		sm501_set_clock(sm->dev, SM501_CLOCK_M1XCLK, init->m1xclk);
	}

	if (init->mclk) {
		dev_info(sm->dev, "setting MCLK to %ld\n", init->mclk);
		sm501_set_clock(sm->dev, SM501_CLOCK_MCLK, init->mclk);
	}

}

/* Check the PLL sources for the M1CLK and M1XCLK
 *
 * If the M1CLK and M1XCLKs are not sourced from the same PLL, then
 * there is a risk (see errata AB-5) that the SM501 will cease proper
 * function. If this happens, then it is likely the SM501 will
 * hang the system.
*/

static int sm501_check_clocks(struct sm501_devdata *sm)
{
	unsigned long pwrmode = smc501_readl(sm->regs + SM501_CURRENT_CLOCK);
	unsigned long msrc = (pwrmode & SM501_POWERMODE_M_SRC);
	unsigned long m1src = (pwrmode & SM501_POWERMODE_M1_SRC);

	return ((msrc == 0 && m1src != 0) || (msrc != 0 && m1src == 0));
}

static unsigned int sm501_mem_local[] = {
	[0]	= 4*1024*1024,
	[1]	= 8*1024*1024,
	[2]	= 16*1024*1024,
	[3]	= 32*1024*1024,
	[4]	= 64*1024*1024,
	[5]	= 2*1024*1024,
};

/* sm501_init_dev
 *
 * Common init code for an SM501
*/

static int sm501_init_dev(struct sm501_devdata *sm)
{
	struct sm501_initdata *idata;
	struct sm501_platdata *pdata;
	resource_size_t mem_avail;
	unsigned long dramctrl;
	unsigned long devid;
	int ret;

	mutex_init(&sm->clock_lock);
	spin_lock_init(&sm->reg_lock);

	INIT_LIST_HEAD(&sm->devices);

	devid = smc501_readl(sm->regs + SM501_DEVICEID);

	if ((devid & SM501_DEVICEID_IDMASK) != SM501_DEVICEID_SM501) {
		dev_err(sm->dev, "incorrect device id %08lx\n", devid);
		return -EINVAL;
	}

	/* disable irqs */
	smc501_writel(0, sm->regs + SM501_IRQ_MASK);

	dramctrl = smc501_readl(sm->regs + SM501_DRAM_CONTROL);
	mem_avail = sm501_mem_local[(dramctrl >> 13) & 0x7];

	dev_info(sm->dev, "SM501 At %p: Version %08lx, %ld Mb, IRQ %d\n",
		 sm->regs, devid, (unsigned long)mem_avail >> 20, sm->irq);

	sm->rev = devid & SM501_DEVICEID_REVMASK;

	sm501_dump_gate(sm);

	ret = device_create_file(sm->dev, &dev_attr_dbg_regs);
	if (ret)
		dev_err(sm->dev, "failed to create debug regs file\n");

	sm501_dump_clk(sm);

	/* check to see if we have some device initialisation */

	pdata = sm->platdata;
	idata = pdata ? pdata->init : NULL;

	if (idata) {
		sm501_init_regs(sm, idata);

		if (idata->devices & SM501_USE_USB_HOST)
			sm501_register_usbhost(sm, &mem_avail);
		if (idata->devices & (SM501_USE_UART0 | SM501_USE_UART1))
			sm501_register_uart(sm, idata->devices);
		if (idata->devices & SM501_USE_GPIO)
			sm501_register_gpio(sm);
	}

	if (pdata && pdata->gpio_i2c != NULL && pdata->gpio_i2c_nr > 0) {
		if (!sm501_gpio_isregistered(sm))
			dev_err(sm->dev, "no gpio available for i2c gpio.\n");
		else
			sm501_register_gpio_i2c(sm, pdata);
	}

	ret = sm501_check_clocks(sm);
	if (ret) {
		dev_err(sm->dev, "M1X and M clocks sourced from different "
					"PLLs\n");
		return -EINVAL;
	}

	/* always create a framebuffer */
	sm501_register_display(sm, &mem_avail);

	return 0;
}

static int sm501_plat_probe(struct platform_device *dev)
{
	struct sm501_devdata *sm;
	int ret;

	sm = kzalloc(sizeof(struct sm501_devdata), GFP_KERNEL);
	if (sm == NULL) {
		dev_err(&dev->dev, "no memory for device data\n");
		ret = -ENOMEM;
		goto err1;
	}

	sm->dev = &dev->dev;
	sm->pdev_id = dev->id;
	sm->platdata = dev_get_platdata(&dev->dev);

	ret = platform_get_irq(dev, 0);
	if (ret < 0) {
		dev_err(&dev->dev, "failed to get irq resource\n");
		goto err_res;
	}
	sm->irq = ret;

	sm->io_res = platform_get_resource(dev, IORESOURCE_MEM, 1);
	sm->mem_res = platform_get_resource(dev, IORESOURCE_MEM, 0);

	if (sm->io_res == NULL || sm->mem_res == NULL) {
		dev_err(&dev->dev, "failed to get IO resource\n");
		ret = -ENOENT;
		goto err_res;
	}

	sm->regs_claim = request_mem_region(sm->io_res->start,
					    0x100, "sm501");

	if (sm->regs_claim == NULL) {
		dev_err(&dev->dev, "cannot claim registers\n");
		ret = -EBUSY;
		goto err_res;
	}

	platform_set_drvdata(dev, sm);

	sm->regs = ioremap(sm->io_res->start, resource_size(sm->io_res));

	if (sm->regs == NULL) {
		dev_err(&dev->dev, "cannot remap registers\n");
		ret = -EIO;
		goto err_claim;
	}

	return sm501_init_dev(sm);

 err_claim:
	release_resource(sm->regs_claim);
	kfree(sm->regs_claim);
 err_res:
	kfree(sm);
 err1:
	return ret;

}

#ifdef CONFIG_PM

/* power management support */

static void sm501_set_power(struct sm501_devdata *sm, int on)
{
	struct sm501_platdata *pd = sm->platdata;

	if (pd == NULL)
		return;

	if (pd->get_power) {
		if (pd->get_power(sm->dev) == on) {
			dev_dbg(sm->dev, "is already %d\n", on);
			return;
		}
	}

	if (pd->set_power) {
		dev_dbg(sm->dev, "setting power to %d\n", on);

		pd->set_power(sm->dev, on);
		sm501_mdelay(sm, 10);
	}
}

static int sm501_plat_suspend(struct platform_device *pdev, pm_message_t state)
{
	struct sm501_devdata *sm = platform_get_drvdata(pdev);

	sm->in_suspend = 1;
	sm->pm_misc = smc501_readl(sm->regs + SM501_MISC_CONTROL);

	sm501_dump_regs(sm);

	if (sm->platdata) {
		if (sm->platdata->flags & SM501_FLAG_SUSPEND_OFF)
			sm501_set_power(sm, 0);
	}

	return 0;
}

static int sm501_plat_resume(struct platform_device *pdev)
{
	struct sm501_devdata *sm = platform_get_drvdata(pdev);

	sm501_set_power(sm, 1);

	sm501_dump_regs(sm);
	sm501_dump_gate(sm);
	sm501_dump_clk(sm);

	/* check to see if we are in the same state as when suspended */

	if (smc501_readl(sm->regs + SM501_MISC_CONTROL) != sm->pm_misc) {
		dev_info(sm->dev, "SM501_MISC_CONTROL changed over sleep\n");
		smc501_writel(sm->pm_misc, sm->regs + SM501_MISC_CONTROL);

		/* our suspend causes the controller state to change,
		 * either by something attempting setup, power loss,
		 * or an external reset event on power change */

		if (sm->platdata && sm->platdata->init) {
			sm501_init_regs(sm, sm->platdata->init);
		}
	}

	/* dump our state from resume */

	sm501_dump_regs(sm);
	sm501_dump_clk(sm);

	sm->in_suspend = 0;

	return 0;
}
#else
#define sm501_plat_suspend NULL
#define sm501_plat_resume NULL
#endif

/* Initialisation data for PCI devices */

static struct sm501_initdata sm501_pci_initdata = {
	.gpio_high	= {
		.set	= 0x3F000000,		/* 24bit panel */
		.mask	= 0x0,
	},
	.misc_timing	= {
		.set	= 0x010100,		/* SDRAM timing */
		.mask	= 0x1F1F00,
	},
	.misc_control	= {
		.set	= SM501_MISC_PNL_24BIT,
		.mask	= 0,
	},

	.devices	= SM501_USE_ALL,

	/* Errata AB-3 says that 72MHz is the fastest available
	 * for 33MHZ PCI with proper bus-mastering operation */

	.mclk		= 72 * MHZ,
	.m1xclk		= 144 * MHZ,
};

static struct sm501_platdata_fbsub sm501_pdata_fbsub = {
	.flags		= (SM501FB_FLAG_USE_INIT_MODE |
			   SM501FB_FLAG_USE_HWCURSOR |
			   SM501FB_FLAG_USE_HWACCEL |
			   SM501FB_FLAG_DISABLE_AT_EXIT),
};

static struct sm501_platdata_fb sm501_fb_pdata = {
	.fb_route	= SM501_FB_OWN,
	.fb_crt		= &sm501_pdata_fbsub,
	.fb_pnl		= &sm501_pdata_fbsub,
};

static struct sm501_platdata sm501_pci_platdata = {
	.init		= &sm501_pci_initdata,
	.fb		= &sm501_fb_pdata,
	.gpio_base	= -1,
};

static int sm501_pci_probe(struct pci_dev *dev,
				     const struct pci_device_id *id)
{
	struct sm501_devdata *sm;
	int err;

	sm = kzalloc(sizeof(struct sm501_devdata), GFP_KERNEL);
	if (sm == NULL) {
		dev_err(&dev->dev, "no memory for device data\n");
		err = -ENOMEM;
		goto err1;
	}

	/* set a default set of platform data */
	dev->dev.platform_data = sm->platdata = &sm501_pci_platdata;

	/* set a hopefully unique id for our child platform devices */
	sm->pdev_id = 32 + dev->devfn;

	pci_set_drvdata(dev, sm);

	err = pci_enable_device(dev);
	if (err) {
		dev_err(&dev->dev, "cannot enable device\n");
		goto err2;
	}

	sm->dev = &dev->dev;
	sm->irq = dev->irq;

#ifdef __BIG_ENDIAN
	/* if the system is big-endian, we most probably have a
	 * translation in the IO layer making the PCI bus little endian
	 * so make the framebuffer swapped pixels */

	sm501_fb_pdata.flags |= SM501_FBPD_SWAP_FB_ENDIAN;
#endif

	/* check our resources */

	if (!(pci_resource_flags(dev, 0) & IORESOURCE_MEM)) {
		dev_err(&dev->dev, "region #0 is not memory?\n");
		err = -EINVAL;
		goto err3;
	}

	if (!(pci_resource_flags(dev, 1) & IORESOURCE_MEM)) {
		dev_err(&dev->dev, "region #1 is not memory?\n");
		err = -EINVAL;
		goto err3;
	}

	/* make our resources ready for sharing */

	sm->io_res = &dev->resource[1];
	sm->mem_res = &dev->resource[0];

	sm->regs_claim = request_mem_region(sm->io_res->start,
					    0x100, "sm501");
	if (sm->regs_claim == NULL) {
		dev_err(&dev->dev, "cannot claim registers\n");
		err= -EBUSY;
		goto err3;
	}

	sm->regs = pci_ioremap_bar(dev, 1);

	if (sm->regs == NULL) {
		dev_err(&dev->dev, "cannot remap registers\n");
		err = -EIO;
		goto err4;
	}

	sm501_init_dev(sm);
	return 0;

 err4:
	release_resource(sm->regs_claim);
	kfree(sm->regs_claim);
 err3:
	pci_disable_device(dev);
 err2:
	kfree(sm);
 err1:
	return err;
}

static void sm501_remove_sub(struct sm501_devdata *sm,
			     struct sm501_device *smdev)
{
	list_del(&smdev->list);
	platform_device_unregister(&smdev->pdev);
}

static void sm501_dev_remove(struct sm501_devdata *sm)
{
	struct sm501_device *smdev, *tmp;

	list_for_each_entry_safe(smdev, tmp, &sm->devices, list)
		sm501_remove_sub(sm, smdev);

	device_remove_file(sm->dev, &dev_attr_dbg_regs);

	sm501_gpio_remove(sm);
}

static void sm501_pci_remove(struct pci_dev *dev)
{
	struct sm501_devdata *sm = pci_get_drvdata(dev);

	sm501_dev_remove(sm);
	iounmap(sm->regs);

	release_resource(sm->regs_claim);
	kfree(sm->regs_claim);

	pci_disable_device(dev);
}

static int sm501_plat_remove(struct platform_device *dev)
{
	struct sm501_devdata *sm = platform_get_drvdata(dev);

	sm501_dev_remove(sm);
	iounmap(sm->regs);

	release_resource(sm->regs_claim);
	kfree(sm->regs_claim);

	return 0;
}

static const struct pci_device_id sm501_pci_tbl[] = {
	{ 0x126f, 0x0501, PCI_ANY_ID, PCI_ANY_ID, 0, 0, 0 },
	{ 0, },
};

MODULE_DEVICE_TABLE(pci, sm501_pci_tbl);

static struct pci_driver sm501_pci_driver = {
	.name		= "sm501",
	.id_table	= sm501_pci_tbl,
	.probe		= sm501_pci_probe,
	.remove		= sm501_pci_remove,
};

MODULE_ALIAS("platform:sm501");

static const struct of_device_id of_sm501_match_tbl[] = {
	{ .compatible = "smi,sm501", },
	{ /* end */ }
};

static struct platform_driver sm501_plat_driver = {
	.driver		= {
		.name	= "sm501",
		.owner	= THIS_MODULE,
		.of_match_table = of_sm501_match_tbl,
	},
	.probe		= sm501_plat_probe,
	.remove		= sm501_plat_remove,
	.suspend	= sm501_plat_suspend,
	.resume		= sm501_plat_resume,
};

static int __init sm501_base_init(void)
{
	platform_driver_register(&sm501_plat_driver);
	return pci_register_driver(&sm501_pci_driver);
}

static void __exit sm501_base_exit(void)
{
	platform_driver_unregister(&sm501_plat_driver);
	pci_unregister_driver(&sm501_pci_driver);
}

module_init(sm501_base_init);
module_exit(sm501_base_exit);

MODULE_DESCRIPTION("SM501 Core Driver");
MODULE_AUTHOR("Ben Dooks <ben@simtec.co.uk>, Vincent Sanders");
MODULE_LICENSE("GPL v2");
