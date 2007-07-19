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

#include <linux/sm501.h>
#include <linux/sm501-regs.h>

#include <asm/io.h>

struct sm501_device {
	struct list_head		list;
	struct platform_device		pdev;
};

struct sm501_devdata {
	spinlock_t			 reg_lock;
	struct mutex			 clock_lock;
	struct list_head		 devices;

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
};

#define MHZ (1000 * 1000)

#ifdef DEBUG
static const unsigned int misc_div[] = {
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
	[10]		= 12,
	[11]		= 24,
	[12]		= 48,
	[13]		= 96,
	[14]		= 192,
	[15]		= 384,
};

static const unsigned int px_div[] = {
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
				unsigned long mask, const unsigned int *dtab)
{
	if (val & selbit)
		pll2 = 288 * MHZ;

	return pll2 / dtab[(val >> lshft) & mask];
}

#define fmt_freq(x) ((x) / MHZ), ((x) % MHZ), (x)

/* sm501_dump_clk
 *
 * Print out the current clock configuration for the device
*/

static void sm501_dump_clk(struct sm501_devdata *sm)
{
	unsigned long misct = readl(sm->regs + SM501_MISC_TIMING);
	unsigned long pm0 = readl(sm->regs + SM501_POWER_MODE_0_CLOCK);
	unsigned long pm1 = readl(sm->regs + SM501_POWER_MODE_1_CLOCK);
	unsigned long pmc = readl(sm->regs + SM501_POWER_MODE_CONTROL);
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
	sdclk0 /= misc_div[((misct >> 8) & 0xf)];

	sdclk1 = (misct & (1<<20)) ? pll2 : 288 * MHZ;
	sdclk1 /= misc_div[((misct >> 16) & 0xf)];

	dev_dbg(sm->dev, "MISCT=%08lx, PM0=%08lx, PM1=%08lx\n",
		misct, pm0, pm1);

	dev_dbg(sm->dev, "PLL2 = %ld.%ld MHz (%ld), SDCLK0=%08lx, SDCLK1=%08lx\n",
		fmt_freq(pll2), sdclk0, sdclk1);

	dev_dbg(sm->dev, "SDRAM: PM0=%ld, PM1=%ld\n", sdclk0, sdclk1);

	dev_dbg(sm->dev, "PM0[%c]: "
		 "P2 %ld.%ld MHz (%ld), V2 %ld.%ld (%ld), "
x		 "M %ld.%ld (%ld), MX1 %ld.%ld (%ld)\n",
		 (pmc & 3 ) == 0 ? '*' : '-',
		 fmt_freq(decode_div(pll2, pm0, 24, 1<<29, 31, px_div)),
		 fmt_freq(decode_div(pll2, pm0, 16, 1<<20, 15, misc_div)),
		 fmt_freq(decode_div(pll2, pm0, 8,  1<<12, 15, misc_div)),
		 fmt_freq(decode_div(pll2, pm0, 0,  1<<4,  15, misc_div)));

	dev_dbg(sm->dev, "PM1[%c]: "
		"P2 %ld.%ld MHz (%ld), V2 %ld.%ld (%ld), "
		"M %ld.%ld (%ld), MX1 %ld.%ld (%ld)\n",
		(pmc & 3 ) == 1 ? '*' : '-',
		fmt_freq(decode_div(pll2, pm1, 24, 1<<29, 31, px_div)),
		fmt_freq(decode_div(pll2, pm1, 16, 1<<20, 15, misc_div)),
		fmt_freq(decode_div(pll2, pm1, 8,  1<<12, 15, misc_div)),
		fmt_freq(decode_div(pll2, pm1, 0,  1<<4,  15, misc_div)));
}

static void sm501_dump_regs(struct sm501_devdata *sm)
{
	void __iomem *regs = sm->regs;

	dev_info(sm->dev, "System Control   %08x\n",
			readl(regs + SM501_SYSTEM_CONTROL));
	dev_info(sm->dev, "Misc Control     %08x\n",
			readl(regs + SM501_MISC_CONTROL));
	dev_info(sm->dev, "GPIO Control Low %08x\n",
			readl(regs + SM501_GPIO31_0_CONTROL));
	dev_info(sm->dev, "GPIO Control Hi  %08x\n",
			readl(regs + SM501_GPIO63_32_CONTROL));
	dev_info(sm->dev, "DRAM Control     %08x\n",
			readl(regs + SM501_DRAM_CONTROL));
	dev_info(sm->dev, "Arbitration Ctrl %08x\n",
			readl(regs + SM501_ARBTRTN_CONTROL));
	dev_info(sm->dev, "Misc Timing      %08x\n",
			readl(regs + SM501_MISC_TIMING));
}

static void sm501_dump_gate(struct sm501_devdata *sm)
{
	dev_info(sm->dev, "CurrentGate      %08x\n",
			readl(sm->regs + SM501_CURRENT_GATE));
	dev_info(sm->dev, "CurrentClock     %08x\n",
			readl(sm->regs + SM501_CURRENT_CLOCK));
	dev_info(sm->dev, "PowerModeControl %08x\n",
			readl(sm->regs + SM501_POWER_MODE_CONTROL));
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
	readl(sm->regs);
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

	misc = readl(sm->regs + SM501_MISC_CONTROL);
	to = (misc & ~clear) | set;

	if (to != misc) {
		writel(to, sm->regs + SM501_MISC_CONTROL);
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

	data = readl(sm->regs + reg);
	data |= set;
	data &= ~clear;

	writel(data, sm->regs + reg);
	sm501_sync_regs(sm);

	spin_unlock_irqrestore(&sm->reg_lock, save);

	return data;
}

EXPORT_SYMBOL_GPL(sm501_modify_reg);

unsigned long sm501_gpio_get(struct device *dev,
			     unsigned long gpio)
{
	struct sm501_devdata *sm = dev_get_drvdata(dev);
	unsigned long result;
	unsigned long reg;

	reg = (gpio > 32) ? SM501_GPIO_DATA_HIGH : SM501_GPIO_DATA_LOW;
	result = readl(sm->regs + reg);

	result >>= (gpio & 31);
	return result & 1UL;
}

EXPORT_SYMBOL_GPL(sm501_gpio_get);

void sm501_gpio_set(struct device *dev,
		    unsigned long gpio,
		    unsigned int to,
		    unsigned int dir)
{
	struct sm501_devdata *sm = dev_get_drvdata(dev);

	unsigned long bit = 1 << (gpio & 31);
	unsigned long base;
	unsigned long save;
	unsigned long val;

	base = (gpio > 32) ? SM501_GPIO_DATA_HIGH : SM501_GPIO_DATA_LOW;
	base += SM501_GPIO;

	spin_lock_irqsave(&sm->reg_lock, save);

	val = readl(sm->regs + base) & ~bit;
	if (to)
		val |= bit;
	writel(val, sm->regs + base);

	val = readl(sm->regs + SM501_GPIO_DDR_LOW) & ~bit;
	if (dir)
		val |= bit;

	writel(val, sm->regs + SM501_GPIO_DDR_LOW);
	sm501_sync_regs(sm);

	spin_unlock_irqrestore(&sm->reg_lock, save);

}

EXPORT_SYMBOL_GPL(sm501_gpio_set);


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

	mode = readl(sm->regs + SM501_POWER_MODE_CONTROL);
	gate = readl(sm->regs + SM501_CURRENT_GATE);
	clock = readl(sm->regs + SM501_CURRENT_CLOCK);

	mode &= 3;		/* get current power mode */

	if (unit >= ARRAY_SIZE(sm->unit_power)) {
		dev_err(dev, "%s: bad unit %d\n", __FUNCTION__, unit);
		goto already;
	}

	dev_dbg(sm->dev, "%s: unit %d, cur %d, to %d\n", __FUNCTION__, unit,
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
		writel(gate, sm->regs + SM501_POWER_MODE_0_GATE);
		writel(clock, sm->regs + SM501_POWER_MODE_0_CLOCK);
		mode = 0;
		break;
	case 2:
	case 0:
		writel(gate, sm->regs + SM501_POWER_MODE_1_GATE);
		writel(clock, sm->regs + SM501_POWER_MODE_1_CLOCK);
		mode = 1;
		break;

	default:
		return -1;
	}

	writel(mode, sm->regs + SM501_POWER_MODE_CONTROL);
	sm501_sync_regs(sm);

	dev_dbg(sm->dev, "gate %08lx, clock %08lx, mode %08lx\n",
		gate, clock, mode);

	sm501_mdelay(sm, 16);

 already:
	mutex_unlock(&sm->clock_lock);
	return gate;
}

EXPORT_SYMBOL_GPL(sm501_unit_power);


/* Perform a rounded division. */
static long sm501fb_round_div(long num, long denom)
{
        /* n / d + 1 / 2 = (2n + d) / 2d */
        return (2 * num + denom) / (2 * denom);
}

/* clock value structure. */
struct sm501_clock {
	unsigned long mclk;
	int divider;
	int shift;
};

/* sm501_select_clock
 *
 * selects nearest discrete clock frequency the SM501 can achive
 *   the maximum divisor is 3 or 5
 */
static unsigned long sm501_select_clock(unsigned long freq,
					struct sm501_clock *clock,
					int max_div)
{
	unsigned long mclk;
	int divider;
	int shift;
	long diff;
	long best_diff = 999999999;

	/* Try 288MHz and 336MHz clocks. */
	for (mclk = 288000000; mclk <= 336000000; mclk += 48000000) {
		/* try dividers 1 and 3 for CRT and for panel,
		   try divider 5 for panel only.*/

		for (divider = 1; divider <= max_div; divider += 2) {
			/* try all 8 shift values.*/
			for (shift = 0; shift < 8; shift++) {
				/* Calculate difference to requested clock */
				diff = sm501fb_round_div(mclk, divider << shift) - freq;
				if (diff < 0)
					diff = -diff;

				/* If it is less than the current, use it */
				if (diff < best_diff) {
					best_diff = diff;

					clock->mclk = mclk;
					clock->divider = divider;
					clock->shift = shift;
				}
			}
		}
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
	unsigned long mode = readl(sm->regs + SM501_POWER_MODE_CONTROL);
	unsigned long gate = readl(sm->regs + SM501_CURRENT_GATE);
	unsigned long clock = readl(sm->regs + SM501_CURRENT_CLOCK);
	unsigned char reg;
	unsigned long sm501_freq; /* the actual frequency acheived */

	struct sm501_clock to;

	/* find achivable discrete frequency and setup register value
	 * accordingly, V2XCLK, MCLK and M1XCLK are the same P2XCLK
	 * has an extra bit for the divider */

	switch (clksrc) {
	case SM501_CLOCK_P2XCLK:
		/* This clock is divided in half so to achive the
		 * requested frequency the value must be multiplied by
		 * 2. This clock also has an additional pre divisor */

		sm501_freq = (sm501_select_clock(2 * req_freq, &to, 5) / 2);
		reg=to.shift & 0x07;/* bottom 3 bits are shift */
		if (to.divider == 3)
			reg |= 0x08; /* /3 divider required */
		else if (to.divider == 5)
			reg |= 0x10; /* /5 divider required */
		if (to.mclk != 288000000)
			reg |= 0x20; /* which mclk pll is source */
		break;

	case SM501_CLOCK_V2XCLK:
		/* This clock is divided in half so to achive the
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

	mode = readl(sm->regs + SM501_POWER_MODE_CONTROL);
	gate = readl(sm->regs + SM501_CURRENT_GATE);
	clock = readl(sm->regs + SM501_CURRENT_CLOCK);

	clock = clock & ~(0xFF << clksrc);
	clock |= reg<<clksrc;

	mode &= 3;	/* find current mode */

	switch (mode) {
	case 1:
		writel(gate, sm->regs + SM501_POWER_MODE_0_GATE);
		writel(clock, sm->regs + SM501_POWER_MODE_0_CLOCK);
		mode = 0;
		break;
	case 2:
	case 0:
		writel(gate, sm->regs + SM501_POWER_MODE_1_GATE);
		writel(clock, sm->regs + SM501_POWER_MODE_1_CLOCK);
		mode = 1;
		break;

	default:
		mutex_unlock(&sm->clock_lock);
		return -1;
	}

	writel(mode, sm->regs + SM501_POWER_MODE_CONTROL);
	sm501_sync_regs(sm);

	dev_info(sm->dev, "gate %08lx, clock %08lx, mode %08lx\n",
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

unsigned long sm501_find_clock(int clksrc,
			       unsigned long req_freq)
{
	unsigned long sm501_freq; /* the frequency achiveable by the 501 */
	struct sm501_clock to;

	switch (clksrc) {
	case SM501_CLOCK_P2XCLK:
		sm501_freq = (sm501_select_clock(2 * req_freq, &to, 5) / 2);
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
sm501_create_subdev(struct sm501_devdata *sm,
		    char *name, unsigned int res_count)
{
	struct sm501_device *smdev;

	smdev = kzalloc(sizeof(struct sm501_device) +
			sizeof(struct resource) * res_count, GFP_KERNEL);
	if (!smdev)
		return NULL;

	smdev->pdev.dev.release = sm501_device_release;

	smdev->pdev.name = name;
	smdev->pdev.id = sm->pdev_id;
	smdev->pdev.resource = (struct resource *)(smdev+1);
	smdev->pdev.num_resources = res_count;

	smdev->pdev.dev.parent = sm->dev;

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
		printk("%s[%d] flags %08lx: %08llx..%08llx\n",
		       pdev->name, ptr,
		       pdev->resource[ptr].flags,
		       (unsigned long long)pdev->resource[ptr].start,
		       (unsigned long long)pdev->resource[ptr].end);
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

	pdev = sm501_create_subdev(sm, "sm501-usb", 3);
	if (!pdev)
		return -ENOMEM;

	sm501_create_subio(sm, &pdev->resource[0], 0x40000, 0x20000);
	sm501_create_mem(sm, &pdev->resource[1], mem_avail, 256*1024);
	sm501_create_irq(sm, &pdev->resource[2]);

	return sm501_register_device(sm, pdev);
}

static int sm501_register_display(struct sm501_devdata *sm,
				  resource_size_t *mem_avail)
{
	struct platform_device *pdev;

	pdev = sm501_create_subdev(sm, "sm501-fb", 4);
	if (!pdev)
		return -ENOMEM;

	sm501_create_subio(sm, &pdev->resource[0], 0x80000, 0x10000);
	sm501_create_subio(sm, &pdev->resource[1], 0x100000, 0x50000);
	sm501_create_mem(sm, &pdev->resource[2], mem_avail, *mem_avail);
	sm501_create_irq(sm, &pdev->resource[3]);

	return sm501_register_device(sm, pdev);
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
			      reg, readl(sm->regs + reg));
		ptr += ret;
	}

	return ptr - buff;
}


static DEVICE_ATTR(dbg_regs, 0666, sm501_dbg_regs, NULL);

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

	tmp = readl(sm->regs + reg);
	tmp &= ~r->mask;
	tmp |= r->set;
	writel(tmp, sm->regs + reg);
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
	unsigned long pwrmode = readl(sm->regs + SM501_CURRENT_CLOCK);
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
	resource_size_t mem_avail;
	unsigned long dramctrl;
	unsigned long devid;
	int ret;

	mutex_init(&sm->clock_lock);
	spin_lock_init(&sm->reg_lock);

	INIT_LIST_HEAD(&sm->devices);

	devid = readl(sm->regs + SM501_DEVICEID);

	if ((devid & SM501_DEVICEID_IDMASK) != SM501_DEVICEID_SM501) {
		dev_err(sm->dev, "incorrect device id %08lx\n", devid);
		return -EINVAL;
	}

	dramctrl = readl(sm->regs + SM501_DRAM_CONTROL);
	mem_avail = sm501_mem_local[(dramctrl >> 13) & 0x7];

	dev_info(sm->dev, "SM501 At %p: Version %08lx, %ld Mb, IRQ %d\n",
		 sm->regs, devid, (unsigned long)mem_avail >> 20, sm->irq);

	sm501_dump_gate(sm);

	ret = device_create_file(sm->dev, &dev_attr_dbg_regs);
	if (ret)
		dev_err(sm->dev, "failed to create debug regs file\n");

	sm501_dump_clk(sm);

	/* check to see if we have some device initialisation */

	if (sm->platdata) {
		struct sm501_platdata *pdata = sm->platdata;

		if (pdata->init) {
			sm501_init_regs(sm, sm->platdata->init);

			if (pdata->init->devices & SM501_USE_USB_HOST)
				sm501_register_usbhost(sm, &mem_avail);
		}
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
	int err;

	sm = kzalloc(sizeof(struct sm501_devdata), GFP_KERNEL);
	if (sm == NULL) {
		dev_err(&dev->dev, "no memory for device data\n");
		err = -ENOMEM;
		goto err1;
	}

	sm->dev = &dev->dev;
	sm->pdev_id = dev->id;
	sm->irq = platform_get_irq(dev, 0);
	sm->io_res = platform_get_resource(dev, IORESOURCE_MEM, 1);
	sm->mem_res = platform_get_resource(dev, IORESOURCE_MEM, 0);
	sm->platdata = dev->dev.platform_data;

	if (sm->irq < 0) {
		dev_err(&dev->dev, "failed to get irq resource\n");
		err = sm->irq;
		goto err_res;
	}

	if (sm->io_res == NULL || sm->mem_res == NULL) {
		dev_err(&dev->dev, "failed to get IO resource\n");
		err = -ENOENT;
		goto err_res;
	}

	sm->regs_claim = request_mem_region(sm->io_res->start,
					    0x100, "sm501");

	if (sm->regs_claim == NULL) {
		dev_err(&dev->dev, "cannot claim registers\n");
		err= -EBUSY;
		goto err_res;
	}

	platform_set_drvdata(dev, sm);

	sm->regs = ioremap(sm->io_res->start,
			   (sm->io_res->end - sm->io_res->start) - 1);

	if (sm->regs == NULL) {
		dev_err(&dev->dev, "cannot remap registers\n");
		err = -EIO;
		goto err_claim;
	}

	return sm501_init_dev(sm);

 err_claim:
	release_resource(sm->regs_claim);
	kfree(sm->regs_claim);
 err_res:
	kfree(sm);
 err1:
	return err;

}

#ifdef CONFIG_PM
/* power management support */

static int sm501_plat_suspend(struct platform_device *pdev, pm_message_t state)
{
	struct sm501_devdata *sm = platform_get_drvdata(pdev);

	sm->in_suspend = 1;
	sm->pm_misc = readl(sm->regs + SM501_MISC_CONTROL);

	sm501_dump_regs(sm);
	return 0;
}

static int sm501_plat_resume(struct platform_device *pdev)
{
	struct sm501_devdata *sm = platform_get_drvdata(pdev);

	sm501_dump_regs(sm);
	sm501_dump_gate(sm);
	sm501_dump_clk(sm);

	/* check to see if we are in the same state as when suspended */

	if (readl(sm->regs + SM501_MISC_CONTROL) != sm->pm_misc) {
		dev_info(sm->dev, "SM501_MISC_CONTROL changed over sleep\n");
		writel(sm->pm_misc, sm->regs + SM501_MISC_CONTROL);

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

	sm->regs = ioremap(pci_resource_start(dev, 1),
			   pci_resource_len(dev, 1));

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
	pci_set_drvdata(dev, NULL);
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
}

static void sm501_pci_remove(struct pci_dev *dev)
{
	struct sm501_devdata *sm = pci_get_drvdata(dev);

	sm501_dev_remove(sm);
	iounmap(sm->regs);

	release_resource(sm->regs_claim);
	kfree(sm->regs_claim);

	pci_set_drvdata(dev, NULL);
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

static struct pci_device_id sm501_pci_tbl[] = {
	{ 0x126f, 0x0501, PCI_ANY_ID, PCI_ANY_ID, 0, 0, 0 },
	{ 0, },
};

MODULE_DEVICE_TABLE(pci, sm501_pci_tbl);

static struct pci_driver sm501_pci_drv = {
	.name		= "sm501",
	.id_table	= sm501_pci_tbl,
	.probe		= sm501_pci_probe,
	.remove		= sm501_pci_remove,
};

static struct platform_driver sm501_plat_drv = {
	.driver		= {
		.name	= "sm501",
		.owner	= THIS_MODULE,
	},
	.probe		= sm501_plat_probe,
	.remove		= sm501_plat_remove,
	.suspend	= sm501_plat_suspend,
	.resume		= sm501_plat_resume,
};

static int __init sm501_base_init(void)
{
	platform_driver_register(&sm501_plat_drv);
	return pci_register_driver(&sm501_pci_drv);
}

static void __exit sm501_base_exit(void)
{
	platform_driver_unregister(&sm501_plat_drv);
	pci_unregister_driver(&sm501_pci_drv);
}

module_init(sm501_base_init);
module_exit(sm501_base_exit);

MODULE_DESCRIPTION("SM501 Core Driver");
MODULE_AUTHOR("Ben Dooks <ben@simtec.co.uk>, Vincent Sanders");
MODULE_LICENSE("GPL v2");
