/*
 *  Copyright (C) 2009 ST-Ericsson
 *  Copyright (C) 2009 STMicroelectronics
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/list.h>
#include <linux/errno.h>
#include <linux/err.h>
#include <linux/clk.h>
#include <linux/io.h>
#include <linux/clkdev.h>
#include <linux/cpufreq.h>

#include <plat/mtu.h>
#include <mach/hardware.h>
#include "clock.h"

#ifdef CONFIG_DEBUG_FS
#include <linux/debugfs.h>
#include <linux/uaccess.h>	/* for copy_from_user */
static LIST_HEAD(clk_list);
#endif

#define PRCC_PCKEN		0x00
#define PRCC_PCKDIS		0x04
#define PRCC_KCKEN		0x08
#define PRCC_KCKDIS		0x0C

#define PRCM_YYCLKEN0_MGT_SET	0x510
#define PRCM_YYCLKEN1_MGT_SET	0x514
#define PRCM_YYCLKEN0_MGT_CLR	0x518
#define PRCM_YYCLKEN1_MGT_CLR	0x51C
#define PRCM_YYCLKEN0_MGT_VAL	0x520
#define PRCM_YYCLKEN1_MGT_VAL	0x524

#define PRCM_SVAMMDSPCLK_MGT	0x008
#define PRCM_SIAMMDSPCLK_MGT	0x00C
#define PRCM_SGACLK_MGT		0x014
#define PRCM_UARTCLK_MGT	0x018
#define PRCM_MSP02CLK_MGT	0x01C
#define PRCM_MSP1CLK_MGT	0x288
#define PRCM_I2CCLK_MGT		0x020
#define PRCM_SDMMCCLK_MGT	0x024
#define PRCM_SLIMCLK_MGT	0x028
#define PRCM_PER1CLK_MGT	0x02C
#define PRCM_PER2CLK_MGT	0x030
#define PRCM_PER3CLK_MGT	0x034
#define PRCM_PER5CLK_MGT	0x038
#define PRCM_PER6CLK_MGT	0x03C
#define PRCM_PER7CLK_MGT	0x040
#define PRCM_LCDCLK_MGT		0x044
#define PRCM_BMLCLK_MGT		0x04C
#define PRCM_HSITXCLK_MGT	0x050
#define PRCM_HSIRXCLK_MGT	0x054
#define PRCM_HDMICLK_MGT	0x058
#define PRCM_APEATCLK_MGT	0x05C
#define PRCM_APETRACECLK_MGT	0x060
#define PRCM_MCDECLK_MGT	0x064
#define PRCM_IPI2CCLK_MGT	0x068
#define PRCM_DSIALTCLK_MGT	0x06C
#define PRCM_DMACLK_MGT		0x074
#define PRCM_B2R2CLK_MGT	0x078
#define PRCM_TVCLK_MGT		0x07C
#define PRCM_TCR		0x1C8
#define PRCM_TCR_STOPPED	(1 << 16)
#define PRCM_TCR_DOZE_MODE	(1 << 17)
#define PRCM_UNIPROCLK_MGT	0x278
#define PRCM_SSPCLK_MGT		0x280
#define PRCM_RNGCLK_MGT		0x284
#define PRCM_UICCCLK_MGT	0x27C

#define PRCM_MGT_ENABLE		(1 << 8)

static DEFINE_SPINLOCK(clocks_lock);

static void __clk_enable(struct clk *clk)
{
	if (clk->enabled++ == 0) {
		if (clk->parent_cluster)
			__clk_enable(clk->parent_cluster);

		if (clk->parent_periph)
			__clk_enable(clk->parent_periph);

		if (clk->ops && clk->ops->enable)
			clk->ops->enable(clk);
	}
}

int clk_enable(struct clk *clk)
{
	unsigned long flags;

	spin_lock_irqsave(&clocks_lock, flags);
	__clk_enable(clk);
	spin_unlock_irqrestore(&clocks_lock, flags);

	return 0;
}
EXPORT_SYMBOL(clk_enable);

static void __clk_disable(struct clk *clk)
{
	if (--clk->enabled == 0) {
		if (clk->ops && clk->ops->disable)
			clk->ops->disable(clk);

		if (clk->parent_periph)
			__clk_disable(clk->parent_periph);

		if (clk->parent_cluster)
			__clk_disable(clk->parent_cluster);
	}
}

void clk_disable(struct clk *clk)
{
	unsigned long flags;

	WARN_ON(!clk->enabled);

	spin_lock_irqsave(&clocks_lock, flags);
	__clk_disable(clk);
	spin_unlock_irqrestore(&clocks_lock, flags);
}
EXPORT_SYMBOL(clk_disable);

/*
 * The MTU has a separate, rather complex muxing setup
 * with alternative parents (peripheral cluster or
 * ULP or fixed 32768 Hz) depending on settings
 */
static unsigned long clk_mtu_get_rate(struct clk *clk)
{
	void __iomem *addr;
	u32 tcr;
	int mtu = (int) clk->data;
	/*
	 * One of these is selected eventually
	 * TODO: Replace the constant with a reference
	 * to the ULP source once this is modeled.
	 */
	unsigned long clk32k = 32768;
	unsigned long mturate;
	unsigned long retclk;

	if (cpu_is_u8500_family())
		addr = __io_address(U8500_PRCMU_BASE);
	else
		ux500_unknown_soc();

	/*
	 * On a startup, always conifgure the TCR to the doze mode;
	 * bootloaders do it for us. Do this in the kernel too.
	 */
	writel(PRCM_TCR_DOZE_MODE, addr + PRCM_TCR);

	tcr = readl(addr + PRCM_TCR);

	/* Get the rate from the parent as a default */
	if (clk->parent_periph)
		mturate = clk_get_rate(clk->parent_periph);
	else if (clk->parent_cluster)
		mturate = clk_get_rate(clk->parent_cluster);
	else
		/* We need to be connected SOMEWHERE */
		BUG();

	/* Return the clock selected for this MTU */
	if (tcr & (1 << mtu))
		retclk = clk32k;
	else
		retclk = mturate;

	pr_info("MTU%d clock rate: %lu Hz\n", mtu, retclk);
	return retclk;
}

unsigned long clk_get_rate(struct clk *clk)
{
	unsigned long rate;

	/*
	 * If there is a custom getrate callback for this clock,
	 * it will take precedence.
	 */
	if (clk->get_rate)
		return clk->get_rate(clk);

	if (clk->ops && clk->ops->get_rate)
		return clk->ops->get_rate(clk);

	rate = clk->rate;
	if (!rate) {
		if (clk->parent_periph)
			rate = clk_get_rate(clk->parent_periph);
		else if (clk->parent_cluster)
			rate = clk_get_rate(clk->parent_cluster);
	}

	return rate;
}
EXPORT_SYMBOL(clk_get_rate);

long clk_round_rate(struct clk *clk, unsigned long rate)
{
	/*TODO*/
	return rate;
}
EXPORT_SYMBOL(clk_round_rate);

int clk_set_rate(struct clk *clk, unsigned long rate)
{
	clk->rate = rate;
	return 0;
}
EXPORT_SYMBOL(clk_set_rate);

int clk_set_parent(struct clk *clk, struct clk *parent)
{
	/*TODO*/
	return -ENOSYS;
}
EXPORT_SYMBOL(clk_set_parent);

static void clk_prcmu_enable(struct clk *clk)
{
	void __iomem *cg_set_reg = __io_address(U8500_PRCMU_BASE)
				   + PRCM_YYCLKEN0_MGT_SET + clk->prcmu_cg_off;

	writel(1 << clk->prcmu_cg_bit, cg_set_reg);
}

static void clk_prcmu_disable(struct clk *clk)
{
	void __iomem *cg_clr_reg = __io_address(U8500_PRCMU_BASE)
				   + PRCM_YYCLKEN0_MGT_CLR + clk->prcmu_cg_off;

	writel(1 << clk->prcmu_cg_bit, cg_clr_reg);
}

static struct clkops clk_prcmu_ops = {
	.enable = clk_prcmu_enable,
	.disable = clk_prcmu_disable,
};

static unsigned int clkrst_base[] = {
	[1] = U8500_CLKRST1_BASE,
	[2] = U8500_CLKRST2_BASE,
	[3] = U8500_CLKRST3_BASE,
	[5] = U8500_CLKRST5_BASE,
	[6] = U8500_CLKRST6_BASE,
};

static void clk_prcc_enable(struct clk *clk)
{
	void __iomem *addr = __io_address(clkrst_base[clk->cluster]);

	if (clk->prcc_kernel != -1)
		writel(1 << clk->prcc_kernel, addr + PRCC_KCKEN);

	if (clk->prcc_bus != -1)
		writel(1 << clk->prcc_bus, addr + PRCC_PCKEN);
}

static void clk_prcc_disable(struct clk *clk)
{
	void __iomem *addr = __io_address(clkrst_base[clk->cluster]);

	if (clk->prcc_bus != -1)
		writel(1 << clk->prcc_bus, addr + PRCC_PCKDIS);

	if (clk->prcc_kernel != -1)
		writel(1 << clk->prcc_kernel, addr + PRCC_KCKDIS);
}

static struct clkops clk_prcc_ops = {
	.enable = clk_prcc_enable,
	.disable = clk_prcc_disable,
};

static struct clk clk_32khz = {
	.name =  "clk_32khz",
	.rate = 32000,
};

/*
 * PRCMU level clock gating
 */

/* Bank 0 */
static DEFINE_PRCMU_CLK(svaclk,		0x0, 2, SVAMMDSPCLK);
static DEFINE_PRCMU_CLK(siaclk,		0x0, 3, SIAMMDSPCLK);
static DEFINE_PRCMU_CLK(sgaclk,		0x0, 4, SGACLK);
static DEFINE_PRCMU_CLK_RATE(uartclk,	0x0, 5, UARTCLK, 38400000);
static DEFINE_PRCMU_CLK(msp02clk,	0x0, 6, MSP02CLK);
static DEFINE_PRCMU_CLK(msp1clk,	0x0, 7, MSP1CLK); /* v1 */
static DEFINE_PRCMU_CLK_RATE(i2cclk,	0x0, 8, I2CCLK, 48000000);
static DEFINE_PRCMU_CLK_RATE(sdmmcclk,	0x0, 9, SDMMCCLK, 100000000);
static DEFINE_PRCMU_CLK(slimclk,	0x0, 10, SLIMCLK);
static DEFINE_PRCMU_CLK(per1clk,	0x0, 11, PER1CLK);
static DEFINE_PRCMU_CLK(per2clk,	0x0, 12, PER2CLK);
static DEFINE_PRCMU_CLK(per3clk,	0x0, 13, PER3CLK);
static DEFINE_PRCMU_CLK(per5clk,	0x0, 14, PER5CLK);
static DEFINE_PRCMU_CLK_RATE(per6clk,	0x0, 15, PER6CLK, 133330000);
static DEFINE_PRCMU_CLK(lcdclk,		0x0, 17, LCDCLK);
static DEFINE_PRCMU_CLK(bmlclk,		0x0, 18, BMLCLK);
static DEFINE_PRCMU_CLK(hsitxclk,	0x0, 19, HSITXCLK);
static DEFINE_PRCMU_CLK(hsirxclk,	0x0, 20, HSIRXCLK);
static DEFINE_PRCMU_CLK(hdmiclk,	0x0, 21, HDMICLK);
static DEFINE_PRCMU_CLK(apeatclk,	0x0, 22, APEATCLK);
static DEFINE_PRCMU_CLK(apetraceclk,	0x0, 23, APETRACECLK);
static DEFINE_PRCMU_CLK(mcdeclk,	0x0, 24, MCDECLK);
static DEFINE_PRCMU_CLK(ipi2clk,	0x0, 25, IPI2CCLK);
static DEFINE_PRCMU_CLK(dsialtclk,	0x0, 26, DSIALTCLK); /* v1 */
static DEFINE_PRCMU_CLK(dmaclk,		0x0, 27, DMACLK);
static DEFINE_PRCMU_CLK(b2r2clk,	0x0, 28, B2R2CLK);
static DEFINE_PRCMU_CLK(tvclk,		0x0, 29, TVCLK);
static DEFINE_PRCMU_CLK(uniproclk,	0x0, 30, UNIPROCLK); /* v1 */
static DEFINE_PRCMU_CLK_RATE(sspclk,	0x0, 31, SSPCLK, 48000000); /* v1 */

/* Bank 1 */
static DEFINE_PRCMU_CLK(rngclk,		0x4, 0, RNGCLK); /* v1 */
static DEFINE_PRCMU_CLK(uiccclk,	0x4, 1, UICCCLK); /* v1 */

/*
 * PRCC level clock gating
 * Format: per#, clk, PCKEN bit, KCKEN bit, parent
 */

/* Peripheral Cluster #1 */
static DEFINE_PRCC_CLK(1, i2c4,		10, 9, &clk_i2cclk);
static DEFINE_PRCC_CLK(1, gpio0,	9, -1, NULL);
static DEFINE_PRCC_CLK(1, slimbus0,	8,  8, &clk_slimclk);
static DEFINE_PRCC_CLK(1, spi3,		7, -1, NULL);
static DEFINE_PRCC_CLK(1, i2c2,		6,  6, &clk_i2cclk);
static DEFINE_PRCC_CLK(1, sdi0,		5,  5, &clk_sdmmcclk);
static DEFINE_PRCC_CLK(1, msp1,		4,  4, &clk_msp1clk);
static DEFINE_PRCC_CLK(1, msp0,		3,  3, &clk_msp02clk);
static DEFINE_PRCC_CLK(1, i2c1,		2,  2, &clk_i2cclk);
static DEFINE_PRCC_CLK(1, uart1,	1,  1, &clk_uartclk);
static DEFINE_PRCC_CLK(1, uart0,	0,  0, &clk_uartclk);

/* Peripheral Cluster #2 */
static DEFINE_PRCC_CLK(2, gpio1,	11, -1, NULL);
static DEFINE_PRCC_CLK(2, ssitx,	10,  7, NULL);
static DEFINE_PRCC_CLK(2, ssirx,	 9,  6, NULL);
static DEFINE_PRCC_CLK(2, spi0,		8, -1, NULL);
static DEFINE_PRCC_CLK(2, sdi3,		7,  5, &clk_sdmmcclk);
static DEFINE_PRCC_CLK(2, sdi1,		6,  4, &clk_sdmmcclk);
static DEFINE_PRCC_CLK(2, msp2,		5,  3, &clk_msp02clk);
static DEFINE_PRCC_CLK(2, sdi4,		4,  2, &clk_sdmmcclk);
static DEFINE_PRCC_CLK(2, pwl,		3,  1, NULL);
static DEFINE_PRCC_CLK(2, spi1,		2, -1, NULL);
static DEFINE_PRCC_CLK(2, spi2,		1, -1, NULL);
static DEFINE_PRCC_CLK(2, i2c3,		0,  0, &clk_i2cclk);

/* Peripheral Cluster #3 */
static DEFINE_PRCC_CLK(3, gpio2,	8, -1, NULL);
static DEFINE_PRCC_CLK(3, sdi5,		7,  7, &clk_sdmmcclk);
static DEFINE_PRCC_CLK(3, uart2,	6,  6, &clk_uartclk);
static DEFINE_PRCC_CLK(3, ske,		5,  5, &clk_32khz);
static DEFINE_PRCC_CLK(3, sdi2,		4,  4, &clk_sdmmcclk);
static DEFINE_PRCC_CLK(3, i2c0,		3,  3, &clk_i2cclk);
static DEFINE_PRCC_CLK(3, ssp1,		2,  2, &clk_sspclk);
static DEFINE_PRCC_CLK(3, ssp0,		1,  1, &clk_sspclk);
static DEFINE_PRCC_CLK(3, fsmc,		0, -1, NULL);

/* Peripheral Cluster #4 is in the always on domain */

/* Peripheral Cluster #5 */
static DEFINE_PRCC_CLK(5, gpio3,	1, -1, NULL);
static DEFINE_PRCC_CLK(5, usb,		0,  0, NULL);

/* Peripheral Cluster #6 */

/* MTU ID in data */
static DEFINE_PRCC_CLK_CUSTOM(6, mtu1, 8, -1, NULL, clk_mtu_get_rate, 1);
static DEFINE_PRCC_CLK_CUSTOM(6, mtu0, 7, -1, NULL, clk_mtu_get_rate, 0);
static DEFINE_PRCC_CLK(6, cfgreg,	6,  6, NULL);
static DEFINE_PRCC_CLK(6, hash1,	5, -1, NULL);
static DEFINE_PRCC_CLK(6, unipro,	4,  1, &clk_uniproclk);
static DEFINE_PRCC_CLK(6, pka,		3, -1, NULL);
static DEFINE_PRCC_CLK(6, hash0,	2, -1, NULL);
static DEFINE_PRCC_CLK(6, cryp0,	1, -1, NULL);
static DEFINE_PRCC_CLK(6, rng,	0,  0, &clk_rngclk);

static struct clk clk_dummy_apb_pclk = {
	.name = "apb_pclk",
};

static struct clk_lookup u8500_clks[] = {
	CLK(dummy_apb_pclk, NULL,	"apb_pclk"),

	/* Peripheral Cluster #1 */
	CLK(gpio0,	"gpio.0",	NULL),
	CLK(gpio0,	"gpio.1",	NULL),
	CLK(slimbus0,	"slimbus0",	NULL),
	CLK(i2c2,	"nmk-i2c.2",	NULL),
	CLK(sdi0,	"sdi0",		NULL),
	CLK(msp0,	"msp0",		NULL),
	CLK(i2c1,	"nmk-i2c.1",	NULL),
	CLK(uart1,	"uart1",	NULL),
	CLK(uart0,	"uart0",	NULL),

	/* Peripheral Cluster #3 */
	CLK(gpio2,	"gpio.2",	NULL),
	CLK(gpio2,	"gpio.3",	NULL),
	CLK(gpio2,	"gpio.4",	NULL),
	CLK(gpio2,	"gpio.5",	NULL),
	CLK(sdi5,	"sdi5",		NULL),
	CLK(uart2,	"uart2",	NULL),
	CLK(ske,	"ske",		NULL),
	CLK(ske,	"nmk-ske-keypad",	NULL),
	CLK(sdi2,	"sdi2",		NULL),
	CLK(i2c0,	"nmk-i2c.0",	NULL),
	CLK(fsmc,	"fsmc",		NULL),

	/* Peripheral Cluster #5 */
	CLK(gpio3,	"gpio.8",	NULL),

	/* Peripheral Cluster #6 */
	CLK(hash1,	"hash1",	NULL),
	CLK(pka,	"pka",		NULL),
	CLK(hash0,	"hash0",	NULL),
	CLK(cryp0,	"cryp0",	NULL),

	/* PRCMU level clock gating */

	/* Bank 0 */
	CLK(svaclk,	"sva",		NULL),
	CLK(siaclk,	"sia",		NULL),
	CLK(sgaclk,	"sga",		NULL),
	CLK(slimclk,	"slim",		NULL),
	CLK(lcdclk,	"lcd",		NULL),
	CLK(bmlclk,	"bml",		NULL),
	CLK(hsitxclk,	"stm-hsi.0",	NULL),
	CLK(hsirxclk,	"stm-hsi.1",	NULL),
	CLK(hdmiclk,	"hdmi",		NULL),
	CLK(apeatclk,	"apeat",	NULL),
	CLK(apetraceclk,	"apetrace",	NULL),
	CLK(mcdeclk,	"mcde",		NULL),
	CLK(ipi2clk,	"ipi2",		NULL),
	CLK(dmaclk,	"dma40.0",	NULL),
	CLK(b2r2clk,	"b2r2",		NULL),
	CLK(tvclk,	"tv",		NULL),

	/* Peripheral Cluster #1 */
	CLK(i2c4,	"nmk-i2c.4",	NULL),
	CLK(spi3,	"spi3",		NULL),
	CLK(msp1,	"msp1",		NULL),

	/* Peripheral Cluster #2 */
	CLK(gpio1,	"gpio.6",	NULL),
	CLK(gpio1,	"gpio.7",	NULL),
	CLK(ssitx,	"ssitx",	NULL),
	CLK(ssirx,	"ssirx",	NULL),
	CLK(spi0,	"spi0",		NULL),
	CLK(sdi3,	"sdi3",		NULL),
	CLK(sdi1,	"sdi1",		NULL),
	CLK(msp2,	"msp2",		NULL),
	CLK(sdi4,	"sdi4",		NULL),
	CLK(pwl,	"pwl",		NULL),
	CLK(spi1,	"spi1",		NULL),
	CLK(spi2,	"spi2",		NULL),
	CLK(i2c3,	"nmk-i2c.3",	NULL),

	/* Peripheral Cluster #3 */
	CLK(ssp1,	"ssp1",		NULL),
	CLK(ssp0,	"ssp0",		NULL),

	/* Peripheral Cluster #5 */
	CLK(usb,	"musb-ux500.0",	"usb"),

	/* Peripheral Cluster #6 */
	CLK(mtu1,	"mtu1",		NULL),
	CLK(mtu0,	"mtu0",		NULL),
	CLK(cfgreg,	"cfgreg",	NULL),
	CLK(hash1,	"hash1",	NULL),
	CLK(unipro,	"unipro",	NULL),
	CLK(rng,	"rng",		NULL),

	/* PRCMU level clock gating */

	/* Bank 0 */
	CLK(uniproclk,	"uniproclk",	NULL),
	CLK(dsialtclk,	"dsialt",	NULL),

	/* Bank 1 */
	CLK(rngclk,	"rng",		NULL),
	CLK(uiccclk,	"uicc",		NULL),
};

#ifdef CONFIG_DEBUG_FS
/*
 *	debugfs support to trace clock tree hierarchy and attributes with
 *	powerdebug
 */
static struct dentry *clk_debugfs_root;

void __init clk_debugfs_add_table(struct clk_lookup *cl, size_t num)
{
	while (num--) {
		/* Check that the clock has not been already registered */
		if (!(cl->clk->list.prev != cl->clk->list.next))
			list_add_tail(&cl->clk->list, &clk_list);

		cl++;
	}
}

static ssize_t usecount_dbg_read(struct file *file, char __user *buf,
						  size_t size, loff_t *off)
{
	struct clk *clk = file->f_dentry->d_inode->i_private;
	char cusecount[128];
	unsigned int len;

	len = sprintf(cusecount, "%u\n", clk->enabled);
	return simple_read_from_buffer(buf, size, off, cusecount, len);
}

static ssize_t rate_dbg_read(struct file *file, char __user *buf,
					  size_t size, loff_t *off)
{
	struct clk *clk = file->f_dentry->d_inode->i_private;
	char crate[128];
	unsigned int rate;
	unsigned int len;

	rate = clk_get_rate(clk);
	len = sprintf(crate, "%u\n", rate);
	return simple_read_from_buffer(buf, size, off, crate, len);
}

static const struct file_operations usecount_fops = {
	.read = usecount_dbg_read,
};

static const struct file_operations set_rate_fops = {
	.read = rate_dbg_read,
};

static struct dentry *clk_debugfs_register_dir(struct clk *c,
						struct dentry *p_dentry)
{
	struct dentry *d, *clk_d;
	const char *p = c->name;

	if (!p)
		p = "BUG";

	clk_d = debugfs_create_dir(p, p_dentry);
	if (!clk_d)
		return NULL;

	d = debugfs_create_file("usecount", S_IRUGO,
				clk_d, c, &usecount_fops);
	if (!d)
		goto err_out;
	d = debugfs_create_file("rate", S_IRUGO,
				clk_d, c, &set_rate_fops);
	if (!d)
		goto err_out;
	/*
	 * TODO : not currently available in ux500
	 * d = debugfs_create_x32("flags", S_IRUGO, clk_d, (u32 *)&c->flags);
	 * if (!d)
	 *	goto err_out;
	 */

	return clk_d;

err_out:
	debugfs_remove_recursive(clk_d);
	return NULL;
}

static int clk_debugfs_register_one(struct clk *c)
{
	struct clk *pa = c->parent_periph;
	struct clk *bpa = c->parent_cluster;

	if (!(bpa && !pa)) {
		c->dent = clk_debugfs_register_dir(c,
				pa ? pa->dent : clk_debugfs_root);
		if (!c->dent)
			return -ENOMEM;
	}

	if (bpa) {
		c->dent_bus = clk_debugfs_register_dir(c,
				bpa->dent_bus ? bpa->dent_bus : bpa->dent);
		if ((!c->dent_bus) &&  (c->dent)) {
			debugfs_remove_recursive(c->dent);
			c->dent = NULL;
			return -ENOMEM;
		}
	}
	return 0;
}

static int clk_debugfs_register(struct clk *c)
{
	int err;
	struct clk *pa = c->parent_periph;
	struct clk *bpa = c->parent_cluster;

	if (pa && (!pa->dent && !pa->dent_bus)) {
		err = clk_debugfs_register(pa);
		if (err)
			return err;
	}

	if (bpa && (!bpa->dent && !bpa->dent_bus)) {
		err = clk_debugfs_register(bpa);
		if (err)
			return err;
	}

	if ((!c->dent) && (!c->dent_bus)) {
		err = clk_debugfs_register_one(c);
		if (err)
			return err;
	}
	return 0;
}

static int __init clk_debugfs_init(void)
{
	struct clk *c;
	struct dentry *d;
	int err;

	d = debugfs_create_dir("clock", NULL);
	if (!d)
		return -ENOMEM;
	clk_debugfs_root = d;

	list_for_each_entry(c, &clk_list, list) {
		err = clk_debugfs_register(c);
		if (err)
			goto err_out;
	}
	return 0;
err_out:
	debugfs_remove_recursive(clk_debugfs_root);
	return err;
}

late_initcall(clk_debugfs_init);
#endif /* defined(CONFIG_DEBUG_FS) */

unsigned long clk_smp_twd_rate = 500000000;

unsigned long clk_smp_twd_get_rate(struct clk *clk)
{
	return clk_smp_twd_rate;
}

static struct clk clk_smp_twd = {
	.get_rate = clk_smp_twd_get_rate,
	.name =  "smp_twd",
};

static struct clk_lookup clk_smp_twd_lookup = {
	.dev_id = "smp_twd",
	.clk = &clk_smp_twd,
};

#ifdef CONFIG_CPU_FREQ

static int clk_twd_cpufreq_transition(struct notifier_block *nb,
				      unsigned long state, void *data)
{
	struct cpufreq_freqs *f = data;

	if (state == CPUFREQ_PRECHANGE) {
		/* Save frequency in simple Hz */
		clk_smp_twd_rate = (f->new * 1000) / 2;
	}

	return NOTIFY_OK;
}

static struct notifier_block clk_twd_cpufreq_nb = {
	.notifier_call = clk_twd_cpufreq_transition,
};

static int clk_init_smp_twd_cpufreq(void)
{
	return cpufreq_register_notifier(&clk_twd_cpufreq_nb,
				  CPUFREQ_TRANSITION_NOTIFIER);
}
late_initcall(clk_init_smp_twd_cpufreq);

#endif

int __init clk_init(void)
{
	clkdev_add_table(u8500_clks, ARRAY_SIZE(u8500_clks));
	clkdev_add(&clk_smp_twd_lookup);

#ifdef CONFIG_DEBUG_FS
	clk_debugfs_add_table(u8500_clks, ARRAY_SIZE(u8500_clks));
#endif
	return 0;
}
