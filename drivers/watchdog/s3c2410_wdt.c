// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (c) 2004 Simtec Electronics
 *	Ben Dooks <ben@simtec.co.uk>
 *
 * S3C2410 Watchdog Timer Support
 *
 * Based on, softdog.c by Alan Cox,
 *     (c) Copyright 1996 Alan Cox <alan@lxorguk.ukuu.org.uk>
 */

#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/types.h>
#include <linux/timer.h>
#include <linux/watchdog.h>
#include <linux/platform_device.h>
#include <linux/interrupt.h>
#include <linux/clk.h>
#include <linux/uaccess.h>
#include <linux/io.h>
#include <linux/cpufreq.h>
#include <linux/slab.h>
#include <linux/err.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/mfd/syscon.h>
#include <linux/regmap.h>
#include <linux/delay.h>

#define S3C2410_WTCON		0x00
#define S3C2410_WTDAT		0x04
#define S3C2410_WTCNT		0x08
#define S3C2410_WTCLRINT	0x0c

#define S3C2410_WTCNT_MAXCNT	0xffff

#define S3C2410_WTCON_RSTEN	(1 << 0)
#define S3C2410_WTCON_INTEN	(1 << 2)
#define S3C2410_WTCON_ENABLE	(1 << 5)

#define S3C2410_WTCON_DIV16	(0 << 3)
#define S3C2410_WTCON_DIV32	(1 << 3)
#define S3C2410_WTCON_DIV64	(2 << 3)
#define S3C2410_WTCON_DIV128	(3 << 3)

#define S3C2410_WTCON_MAXDIV	0x80

#define S3C2410_WTCON_PRESCALE(x)	((x) << 8)
#define S3C2410_WTCON_PRESCALE_MASK	(0xff << 8)
#define S3C2410_WTCON_PRESCALE_MAX	0xff

#define S3C2410_WATCHDOG_ATBOOT		(0)
#define S3C2410_WATCHDOG_DEFAULT_TIME	(15)

#define EXYNOS5_RST_STAT_REG_OFFSET		0x0404
#define EXYNOS5_WDT_DISABLE_REG_OFFSET		0x0408
#define EXYNOS5_WDT_MASK_RESET_REG_OFFSET	0x040c
#define EXYNOS850_CLUSTER0_NONCPU_OUT		0x1220
#define EXYNOS850_CLUSTER0_NONCPU_INT_EN	0x1244
#define EXYNOS850_CLUSTER1_NONCPU_OUT		0x1620
#define EXYNOS850_CLUSTER1_NONCPU_INT_EN	0x1644
#define EXYNOSAUTOV9_CLUSTER1_NONCPU_OUT	0x1520
#define EXYNOSAUTOV9_CLUSTER1_NONCPU_INT_EN	0x1544

#define EXYNOS850_CLUSTER0_WDTRESET_BIT		24
#define EXYNOS850_CLUSTER1_WDTRESET_BIT		23
#define EXYNOSAUTOV9_CLUSTER0_WDTRESET_BIT	25
#define EXYNOSAUTOV9_CLUSTER1_WDTRESET_BIT	24

/**
 * DOC: Quirk flags for different Samsung watchdog IP-cores
 *
 * This driver supports multiple Samsung SoCs, each of which might have
 * different set of registers and features supported. As watchdog block
 * sometimes requires modifying PMU registers for proper functioning, register
 * differences in both watchdog and PMU IP-cores should be accounted for. Quirk
 * flags described below serve the purpose of telling the driver about mentioned
 * SoC traits, and can be specified in driver data for each particular supported
 * device.
 *
 * %QUIRK_HAS_WTCLRINT_REG: Watchdog block has WTCLRINT register. It's used to
 * clear the interrupt once the interrupt service routine is complete. It's
 * write-only, writing any values to this register clears the interrupt, but
 * reading is not permitted.
 *
 * %QUIRK_HAS_PMU_MASK_RESET: PMU block has the register for disabling/enabling
 * WDT reset request. On old SoCs it's usually called MASK_WDT_RESET_REQUEST,
 * new SoCs have CLUSTERx_NONCPU_INT_EN register, which 'mask_bit' value is
 * inverted compared to the former one.
 *
 * %QUIRK_HAS_PMU_RST_STAT: PMU block has RST_STAT (reset status) register,
 * which contains bits indicating the reason for most recent CPU reset. If
 * present, driver will use this register to check if previous reboot was due to
 * watchdog timer reset.
 *
 * %QUIRK_HAS_PMU_AUTO_DISABLE: PMU block has AUTOMATIC_WDT_RESET_DISABLE
 * register. If 'mask_bit' bit is set, PMU will disable WDT reset when
 * corresponding processor is in reset state.
 *
 * %QUIRK_HAS_PMU_CNT_EN: PMU block has some register (e.g. CLUSTERx_NONCPU_OUT)
 * with "watchdog counter enable" bit. That bit should be set to make watchdog
 * counter running.
 */
#define QUIRK_HAS_WTCLRINT_REG			(1 << 0)
#define QUIRK_HAS_PMU_MASK_RESET		(1 << 1)
#define QUIRK_HAS_PMU_RST_STAT			(1 << 2)
#define QUIRK_HAS_PMU_AUTO_DISABLE		(1 << 3)
#define QUIRK_HAS_PMU_CNT_EN			(1 << 4)

/* These quirks require that we have a PMU register map */
#define QUIRKS_HAVE_PMUREG \
	(QUIRK_HAS_PMU_MASK_RESET | QUIRK_HAS_PMU_RST_STAT | \
	 QUIRK_HAS_PMU_AUTO_DISABLE | QUIRK_HAS_PMU_CNT_EN)

static bool nowayout	= WATCHDOG_NOWAYOUT;
static int tmr_margin;
static int tmr_atboot	= S3C2410_WATCHDOG_ATBOOT;
static int soft_noboot;

module_param(tmr_margin,  int, 0);
module_param(tmr_atboot,  int, 0);
module_param(nowayout,   bool, 0);
module_param(soft_noboot, int, 0);

MODULE_PARM_DESC(tmr_margin, "Watchdog tmr_margin in seconds. (default="
		__MODULE_STRING(S3C2410_WATCHDOG_DEFAULT_TIME) ")");
MODULE_PARM_DESC(tmr_atboot,
		"Watchdog is started at boot time if set to 1, default="
			__MODULE_STRING(S3C2410_WATCHDOG_ATBOOT));
MODULE_PARM_DESC(nowayout, "Watchdog cannot be stopped once started (default="
			__MODULE_STRING(WATCHDOG_NOWAYOUT) ")");
MODULE_PARM_DESC(soft_noboot, "Watchdog action, set to 1 to ignore reboots, 0 to reboot (default 0)");

/**
 * struct s3c2410_wdt_variant - Per-variant config data
 *
 * @disable_reg: Offset in pmureg for the register that disables the watchdog
 * timer reset functionality.
 * @mask_reset_reg: Offset in pmureg for the register that masks the watchdog
 * timer reset functionality.
 * @mask_reset_inv: If set, mask_reset_reg value will have inverted meaning.
 * @mask_bit: Bit number for the watchdog timer in the disable register and the
 * mask reset register.
 * @rst_stat_reg: Offset in pmureg for the register that has the reset status.
 * @rst_stat_bit: Bit number in the rst_stat register indicating a watchdog
 * reset.
 * @cnt_en_reg: Offset in pmureg for the register that enables WDT counter.
 * @cnt_en_bit: Bit number for "watchdog counter enable" in cnt_en register.
 * @quirks: A bitfield of quirks.
 */

struct s3c2410_wdt_variant {
	int disable_reg;
	int mask_reset_reg;
	bool mask_reset_inv;
	int mask_bit;
	int rst_stat_reg;
	int rst_stat_bit;
	int cnt_en_reg;
	int cnt_en_bit;
	u32 quirks;
};

struct s3c2410_wdt {
	struct device		*dev;
	struct clk		*bus_clk; /* for register interface (PCLK) */
	struct clk		*src_clk; /* for WDT counter */
	void __iomem		*reg_base;
	unsigned int		count;
	spinlock_t		lock;
	unsigned long		wtcon_save;
	unsigned long		wtdat_save;
	struct watchdog_device	wdt_device;
	struct notifier_block	freq_transition;
	const struct s3c2410_wdt_variant *drv_data;
	struct regmap *pmureg;
};

static const struct s3c2410_wdt_variant drv_data_s3c2410 = {
	.quirks = 0
};

#ifdef CONFIG_OF
static const struct s3c2410_wdt_variant drv_data_s3c6410 = {
	.quirks = QUIRK_HAS_WTCLRINT_REG,
};

static const struct s3c2410_wdt_variant drv_data_exynos5250  = {
	.disable_reg = EXYNOS5_WDT_DISABLE_REG_OFFSET,
	.mask_reset_reg = EXYNOS5_WDT_MASK_RESET_REG_OFFSET,
	.mask_bit = 20,
	.rst_stat_reg = EXYNOS5_RST_STAT_REG_OFFSET,
	.rst_stat_bit = 20,
	.quirks = QUIRK_HAS_WTCLRINT_REG | QUIRK_HAS_PMU_MASK_RESET | \
		  QUIRK_HAS_PMU_RST_STAT | QUIRK_HAS_PMU_AUTO_DISABLE,
};

static const struct s3c2410_wdt_variant drv_data_exynos5420 = {
	.disable_reg = EXYNOS5_WDT_DISABLE_REG_OFFSET,
	.mask_reset_reg = EXYNOS5_WDT_MASK_RESET_REG_OFFSET,
	.mask_bit = 0,
	.rst_stat_reg = EXYNOS5_RST_STAT_REG_OFFSET,
	.rst_stat_bit = 9,
	.quirks = QUIRK_HAS_WTCLRINT_REG | QUIRK_HAS_PMU_MASK_RESET | \
		  QUIRK_HAS_PMU_RST_STAT | QUIRK_HAS_PMU_AUTO_DISABLE,
};

static const struct s3c2410_wdt_variant drv_data_exynos7 = {
	.disable_reg = EXYNOS5_WDT_DISABLE_REG_OFFSET,
	.mask_reset_reg = EXYNOS5_WDT_MASK_RESET_REG_OFFSET,
	.mask_bit = 23,
	.rst_stat_reg = EXYNOS5_RST_STAT_REG_OFFSET,
	.rst_stat_bit = 23,	/* A57 WDTRESET */
	.quirks = QUIRK_HAS_WTCLRINT_REG | QUIRK_HAS_PMU_MASK_RESET | \
		  QUIRK_HAS_PMU_RST_STAT | QUIRK_HAS_PMU_AUTO_DISABLE,
};

static const struct s3c2410_wdt_variant drv_data_exynos850_cl0 = {
	.mask_reset_reg = EXYNOS850_CLUSTER0_NONCPU_INT_EN,
	.mask_bit = 2,
	.mask_reset_inv = true,
	.rst_stat_reg = EXYNOS5_RST_STAT_REG_OFFSET,
	.rst_stat_bit = EXYNOS850_CLUSTER0_WDTRESET_BIT,
	.cnt_en_reg = EXYNOS850_CLUSTER0_NONCPU_OUT,
	.cnt_en_bit = 7,
	.quirks = QUIRK_HAS_WTCLRINT_REG | QUIRK_HAS_PMU_MASK_RESET | \
		  QUIRK_HAS_PMU_RST_STAT | QUIRK_HAS_PMU_CNT_EN,
};

static const struct s3c2410_wdt_variant drv_data_exynos850_cl1 = {
	.mask_reset_reg = EXYNOS850_CLUSTER1_NONCPU_INT_EN,
	.mask_bit = 2,
	.mask_reset_inv = true,
	.rst_stat_reg = EXYNOS5_RST_STAT_REG_OFFSET,
	.rst_stat_bit = EXYNOS850_CLUSTER1_WDTRESET_BIT,
	.cnt_en_reg = EXYNOS850_CLUSTER1_NONCPU_OUT,
	.cnt_en_bit = 7,
	.quirks = QUIRK_HAS_WTCLRINT_REG | QUIRK_HAS_PMU_MASK_RESET | \
		  QUIRK_HAS_PMU_RST_STAT | QUIRK_HAS_PMU_CNT_EN,
};

static const struct s3c2410_wdt_variant drv_data_exynosautov9_cl0 = {
	.mask_reset_reg = EXYNOS850_CLUSTER0_NONCPU_INT_EN,
	.mask_bit = 2,
	.mask_reset_inv = true,
	.rst_stat_reg = EXYNOS5_RST_STAT_REG_OFFSET,
	.rst_stat_bit = EXYNOSAUTOV9_CLUSTER0_WDTRESET_BIT,
	.cnt_en_reg = EXYNOS850_CLUSTER0_NONCPU_OUT,
	.cnt_en_bit = 7,
	.quirks = QUIRK_HAS_WTCLRINT_REG | QUIRK_HAS_PMU_MASK_RESET |
		  QUIRK_HAS_PMU_RST_STAT | QUIRK_HAS_PMU_CNT_EN,
};

static const struct s3c2410_wdt_variant drv_data_exynosautov9_cl1 = {
	.mask_reset_reg = EXYNOSAUTOV9_CLUSTER1_NONCPU_INT_EN,
	.mask_bit = 2,
	.mask_reset_inv = true,
	.rst_stat_reg = EXYNOS5_RST_STAT_REG_OFFSET,
	.rst_stat_bit = EXYNOSAUTOV9_CLUSTER1_WDTRESET_BIT,
	.cnt_en_reg = EXYNOSAUTOV9_CLUSTER1_NONCPU_OUT,
	.cnt_en_bit = 7,
	.quirks = QUIRK_HAS_WTCLRINT_REG | QUIRK_HAS_PMU_MASK_RESET |
		  QUIRK_HAS_PMU_RST_STAT | QUIRK_HAS_PMU_CNT_EN,
};

static const struct of_device_id s3c2410_wdt_match[] = {
	{ .compatible = "samsung,s3c2410-wdt",
	  .data = &drv_data_s3c2410 },
	{ .compatible = "samsung,s3c6410-wdt",
	  .data = &drv_data_s3c6410 },
	{ .compatible = "samsung,exynos5250-wdt",
	  .data = &drv_data_exynos5250 },
	{ .compatible = "samsung,exynos5420-wdt",
	  .data = &drv_data_exynos5420 },
	{ .compatible = "samsung,exynos7-wdt",
	  .data = &drv_data_exynos7 },
	{ .compatible = "samsung,exynos850-wdt",
	  .data = &drv_data_exynos850_cl0 },
	{ .compatible = "samsung,exynosautov9-wdt",
	  .data = &drv_data_exynosautov9_cl0 },
	{},
};
MODULE_DEVICE_TABLE(of, s3c2410_wdt_match);
#endif

static const struct platform_device_id s3c2410_wdt_ids[] = {
	{
		.name = "s3c2410-wdt",
		.driver_data = (unsigned long)&drv_data_s3c2410,
	},
	{}
};
MODULE_DEVICE_TABLE(platform, s3c2410_wdt_ids);

/* functions */

static inline unsigned long s3c2410wdt_get_freq(struct s3c2410_wdt *wdt)
{
	return clk_get_rate(wdt->src_clk ? wdt->src_clk : wdt->bus_clk);
}

static inline unsigned int s3c2410wdt_max_timeout(struct s3c2410_wdt *wdt)
{
	const unsigned long freq = s3c2410wdt_get_freq(wdt);

	return S3C2410_WTCNT_MAXCNT / (freq / (S3C2410_WTCON_PRESCALE_MAX + 1)
				       / S3C2410_WTCON_MAXDIV);
}

static int s3c2410wdt_disable_wdt_reset(struct s3c2410_wdt *wdt, bool mask)
{
	const u32 mask_val = BIT(wdt->drv_data->mask_bit);
	const u32 val = mask ? mask_val : 0;
	int ret;

	ret = regmap_update_bits(wdt->pmureg, wdt->drv_data->disable_reg,
				 mask_val, val);
	if (ret < 0)
		dev_err(wdt->dev, "failed to update reg(%d)\n", ret);

	return ret;
}

static int s3c2410wdt_mask_wdt_reset(struct s3c2410_wdt *wdt, bool mask)
{
	const u32 mask_val = BIT(wdt->drv_data->mask_bit);
	const bool val_inv = wdt->drv_data->mask_reset_inv;
	const u32 val = (mask ^ val_inv) ? mask_val : 0;
	int ret;

	ret = regmap_update_bits(wdt->pmureg, wdt->drv_data->mask_reset_reg,
				 mask_val, val);
	if (ret < 0)
		dev_err(wdt->dev, "failed to update reg(%d)\n", ret);

	return ret;
}

static int s3c2410wdt_enable_counter(struct s3c2410_wdt *wdt, bool en)
{
	const u32 mask_val = BIT(wdt->drv_data->cnt_en_bit);
	const u32 val = en ? mask_val : 0;
	int ret;

	ret = regmap_update_bits(wdt->pmureg, wdt->drv_data->cnt_en_reg,
				 mask_val, val);
	if (ret < 0)
		dev_err(wdt->dev, "failed to update reg(%d)\n", ret);

	return ret;
}

static int s3c2410wdt_enable(struct s3c2410_wdt *wdt, bool en)
{
	int ret;

	if (wdt->drv_data->quirks & QUIRK_HAS_PMU_AUTO_DISABLE) {
		ret = s3c2410wdt_disable_wdt_reset(wdt, !en);
		if (ret < 0)
			return ret;
	}

	if (wdt->drv_data->quirks & QUIRK_HAS_PMU_MASK_RESET) {
		ret = s3c2410wdt_mask_wdt_reset(wdt, !en);
		if (ret < 0)
			return ret;
	}

	if (wdt->drv_data->quirks & QUIRK_HAS_PMU_CNT_EN) {
		ret = s3c2410wdt_enable_counter(wdt, en);
		if (ret < 0)
			return ret;
	}

	return 0;
}

static int s3c2410wdt_keepalive(struct watchdog_device *wdd)
{
	struct s3c2410_wdt *wdt = watchdog_get_drvdata(wdd);

	spin_lock(&wdt->lock);
	writel(wdt->count, wdt->reg_base + S3C2410_WTCNT);
	spin_unlock(&wdt->lock);

	return 0;
}

static void __s3c2410wdt_stop(struct s3c2410_wdt *wdt)
{
	unsigned long wtcon;

	wtcon = readl(wdt->reg_base + S3C2410_WTCON);
	wtcon &= ~(S3C2410_WTCON_ENABLE | S3C2410_WTCON_RSTEN);
	writel(wtcon, wdt->reg_base + S3C2410_WTCON);
}

static int s3c2410wdt_stop(struct watchdog_device *wdd)
{
	struct s3c2410_wdt *wdt = watchdog_get_drvdata(wdd);

	spin_lock(&wdt->lock);
	__s3c2410wdt_stop(wdt);
	spin_unlock(&wdt->lock);

	return 0;
}

static int s3c2410wdt_start(struct watchdog_device *wdd)
{
	unsigned long wtcon;
	struct s3c2410_wdt *wdt = watchdog_get_drvdata(wdd);

	spin_lock(&wdt->lock);

	__s3c2410wdt_stop(wdt);

	wtcon = readl(wdt->reg_base + S3C2410_WTCON);
	wtcon |= S3C2410_WTCON_ENABLE | S3C2410_WTCON_DIV128;

	if (soft_noboot) {
		wtcon |= S3C2410_WTCON_INTEN;
		wtcon &= ~S3C2410_WTCON_RSTEN;
	} else {
		wtcon &= ~S3C2410_WTCON_INTEN;
		wtcon |= S3C2410_WTCON_RSTEN;
	}

	dev_dbg(wdt->dev, "Starting watchdog: count=0x%08x, wtcon=%08lx\n",
		wdt->count, wtcon);

	writel(wdt->count, wdt->reg_base + S3C2410_WTDAT);
	writel(wdt->count, wdt->reg_base + S3C2410_WTCNT);
	writel(wtcon, wdt->reg_base + S3C2410_WTCON);
	spin_unlock(&wdt->lock);

	return 0;
}

static int s3c2410wdt_set_heartbeat(struct watchdog_device *wdd,
				    unsigned int timeout)
{
	struct s3c2410_wdt *wdt = watchdog_get_drvdata(wdd);
	unsigned long freq = s3c2410wdt_get_freq(wdt);
	unsigned int count;
	unsigned int divisor = 1;
	unsigned long wtcon;

	if (timeout < 1)
		return -EINVAL;

	freq = DIV_ROUND_UP(freq, 128);
	count = timeout * freq;

	dev_dbg(wdt->dev, "Heartbeat: count=%d, timeout=%d, freq=%lu\n",
		count, timeout, freq);

	/* if the count is bigger than the watchdog register,
	   then work out what we need to do (and if) we can
	   actually make this value
	*/

	if (count >= 0x10000) {
		divisor = DIV_ROUND_UP(count, 0xffff);

		if (divisor > 0x100) {
			dev_err(wdt->dev, "timeout %d too big\n", timeout);
			return -EINVAL;
		}
	}

	dev_dbg(wdt->dev, "Heartbeat: timeout=%d, divisor=%d, count=%d (%08x)\n",
		timeout, divisor, count, DIV_ROUND_UP(count, divisor));

	count = DIV_ROUND_UP(count, divisor);
	wdt->count = count;

	/* update the pre-scaler */
	wtcon = readl(wdt->reg_base + S3C2410_WTCON);
	wtcon &= ~S3C2410_WTCON_PRESCALE_MASK;
	wtcon |= S3C2410_WTCON_PRESCALE(divisor-1);

	writel(count, wdt->reg_base + S3C2410_WTDAT);
	writel(wtcon, wdt->reg_base + S3C2410_WTCON);

	wdd->timeout = (count * divisor) / freq;

	return 0;
}

static int s3c2410wdt_restart(struct watchdog_device *wdd, unsigned long action,
			      void *data)
{
	struct s3c2410_wdt *wdt = watchdog_get_drvdata(wdd);
	void __iomem *wdt_base = wdt->reg_base;

	/* disable watchdog, to be safe  */
	writel(0, wdt_base + S3C2410_WTCON);

	/* put initial values into count and data */
	writel(0x80, wdt_base + S3C2410_WTCNT);
	writel(0x80, wdt_base + S3C2410_WTDAT);

	/* set the watchdog to go and reset... */
	writel(S3C2410_WTCON_ENABLE | S3C2410_WTCON_DIV16 |
		S3C2410_WTCON_RSTEN | S3C2410_WTCON_PRESCALE(0x20),
		wdt_base + S3C2410_WTCON);

	/* wait for reset to assert... */
	mdelay(500);

	return 0;
}

#define OPTIONS (WDIOF_SETTIMEOUT | WDIOF_KEEPALIVEPING | WDIOF_MAGICCLOSE)

static const struct watchdog_info s3c2410_wdt_ident = {
	.options          =     OPTIONS,
	.firmware_version =	0,
	.identity         =	"S3C2410 Watchdog",
};

static const struct watchdog_ops s3c2410wdt_ops = {
	.owner = THIS_MODULE,
	.start = s3c2410wdt_start,
	.stop = s3c2410wdt_stop,
	.ping = s3c2410wdt_keepalive,
	.set_timeout = s3c2410wdt_set_heartbeat,
	.restart = s3c2410wdt_restart,
};

static const struct watchdog_device s3c2410_wdd = {
	.info = &s3c2410_wdt_ident,
	.ops = &s3c2410wdt_ops,
	.timeout = S3C2410_WATCHDOG_DEFAULT_TIME,
};

/* interrupt handler code */

static irqreturn_t s3c2410wdt_irq(int irqno, void *param)
{
	struct s3c2410_wdt *wdt = platform_get_drvdata(param);

	dev_info(wdt->dev, "watchdog timer expired (irq)\n");

	s3c2410wdt_keepalive(&wdt->wdt_device);

	if (wdt->drv_data->quirks & QUIRK_HAS_WTCLRINT_REG)
		writel(0x1, wdt->reg_base + S3C2410_WTCLRINT);

	return IRQ_HANDLED;
}

static inline unsigned int s3c2410wdt_get_bootstatus(struct s3c2410_wdt *wdt)
{
	unsigned int rst_stat;
	int ret;

	if (!(wdt->drv_data->quirks & QUIRK_HAS_PMU_RST_STAT))
		return 0;

	ret = regmap_read(wdt->pmureg, wdt->drv_data->rst_stat_reg, &rst_stat);
	if (ret)
		dev_warn(wdt->dev, "Couldn't get RST_STAT register\n");
	else if (rst_stat & BIT(wdt->drv_data->rst_stat_bit))
		return WDIOF_CARDRESET;

	return 0;
}

static inline int
s3c2410_get_wdt_drv_data(struct platform_device *pdev, struct s3c2410_wdt *wdt)
{
	const struct s3c2410_wdt_variant *variant;
	struct device *dev = &pdev->dev;

	variant = of_device_get_match_data(dev);
	if (!variant) {
		/* Device matched by platform_device_id */
		variant = (struct s3c2410_wdt_variant *)
			   platform_get_device_id(pdev)->driver_data;
	}

#ifdef CONFIG_OF
	/* Choose Exynos850/ExynosAutov9 driver data w.r.t. cluster index */
	if (variant == &drv_data_exynos850_cl0 ||
	    variant == &drv_data_exynosautov9_cl0) {
		u32 index;
		int err;

		err = of_property_read_u32(dev->of_node,
					   "samsung,cluster-index", &index);
		if (err)
			return dev_err_probe(dev, -EINVAL, "failed to get cluster index\n");

		switch (index) {
		case 0:
			break;
		case 1:
			variant = (variant == &drv_data_exynos850_cl0) ?
				&drv_data_exynos850_cl1 :
				&drv_data_exynosautov9_cl1;
			break;
		default:
			return dev_err_probe(dev, -EINVAL, "wrong cluster index: %u\n", index);
		}
	}
#endif

	wdt->drv_data = variant;
	return 0;
}

static void s3c2410wdt_wdt_disable_action(void *data)
{
	s3c2410wdt_enable(data, false);
}

static int s3c2410wdt_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct s3c2410_wdt *wdt;
	unsigned int wtcon;
	int wdt_irq;
	int ret;

	wdt = devm_kzalloc(dev, sizeof(*wdt), GFP_KERNEL);
	if (!wdt)
		return -ENOMEM;

	wdt->dev = dev;
	spin_lock_init(&wdt->lock);
	wdt->wdt_device = s3c2410_wdd;

	ret = s3c2410_get_wdt_drv_data(pdev, wdt);
	if (ret)
		return ret;

	if (wdt->drv_data->quirks & QUIRKS_HAVE_PMUREG) {
		wdt->pmureg = syscon_regmap_lookup_by_phandle(dev->of_node,
						"samsung,syscon-phandle");
		if (IS_ERR(wdt->pmureg))
			return dev_err_probe(dev, PTR_ERR(wdt->pmureg),
					     "syscon regmap lookup failed.\n");
	}

	wdt_irq = platform_get_irq(pdev, 0);
	if (wdt_irq < 0)
		return wdt_irq;

	/* get the memory region for the watchdog timer */
	wdt->reg_base = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(wdt->reg_base))
		return PTR_ERR(wdt->reg_base);

	wdt->bus_clk = devm_clk_get_enabled(dev, "watchdog");
	if (IS_ERR(wdt->bus_clk))
		return dev_err_probe(dev, PTR_ERR(wdt->bus_clk), "failed to get bus clock\n");

	/*
	 * "watchdog_src" clock is optional; if it's not present -- just skip it
	 * and use "watchdog" clock as both bus and source clock.
	 */
	wdt->src_clk = devm_clk_get_optional_enabled(dev, "watchdog_src");
	if (IS_ERR(wdt->src_clk))
		return dev_err_probe(dev, PTR_ERR(wdt->src_clk), "failed to get source clock\n");

	wdt->wdt_device.min_timeout = 1;
	wdt->wdt_device.max_timeout = s3c2410wdt_max_timeout(wdt);

	watchdog_set_drvdata(&wdt->wdt_device, wdt);

	/* see if we can actually set the requested timer margin, and if
	 * not, try the default value */

	watchdog_init_timeout(&wdt->wdt_device, tmr_margin, dev);
	ret = s3c2410wdt_set_heartbeat(&wdt->wdt_device,
					wdt->wdt_device.timeout);
	if (ret) {
		ret = s3c2410wdt_set_heartbeat(&wdt->wdt_device,
					       S3C2410_WATCHDOG_DEFAULT_TIME);
		if (ret == 0)
			dev_warn(dev, "tmr_margin value out of range, default %d used\n",
				 S3C2410_WATCHDOG_DEFAULT_TIME);
		else
			return dev_err_probe(dev, ret, "failed to use default timeout\n");
	}

	ret = devm_request_irq(dev, wdt_irq, s3c2410wdt_irq, 0,
			       pdev->name, pdev);
	if (ret != 0)
		return dev_err_probe(dev, ret, "failed to install irq (%d)\n", ret);

	watchdog_set_nowayout(&wdt->wdt_device, nowayout);
	watchdog_set_restart_priority(&wdt->wdt_device, 128);

	wdt->wdt_device.bootstatus = s3c2410wdt_get_bootstatus(wdt);
	wdt->wdt_device.parent = dev;

	/*
	 * If "tmr_atboot" param is non-zero, start the watchdog right now. Also
	 * set WDOG_HW_RUNNING bit, so that watchdog core can kick the watchdog.
	 *
	 * If we're not enabling the watchdog, then ensure it is disabled if it
	 * has been left running from the bootloader or other source.
	 */
	if (tmr_atboot) {
		dev_info(dev, "starting watchdog timer\n");
		s3c2410wdt_start(&wdt->wdt_device);
		set_bit(WDOG_HW_RUNNING, &wdt->wdt_device.status);
	} else {
		s3c2410wdt_stop(&wdt->wdt_device);
	}

	ret = devm_watchdog_register_device(dev, &wdt->wdt_device);
	if (ret)
		return ret;

	ret = s3c2410wdt_enable(wdt, true);
	if (ret < 0)
		return ret;

	ret = devm_add_action_or_reset(dev, s3c2410wdt_wdt_disable_action, wdt);
	if (ret)
		return ret;

	platform_set_drvdata(pdev, wdt);

	/* print out a statement of readiness */

	wtcon = readl(wdt->reg_base + S3C2410_WTCON);

	dev_info(dev, "watchdog %sactive, reset %sabled, irq %sabled\n",
		 (wtcon & S3C2410_WTCON_ENABLE) ?  "" : "in",
		 (wtcon & S3C2410_WTCON_RSTEN) ? "en" : "dis",
		 (wtcon & S3C2410_WTCON_INTEN) ? "en" : "dis");

	return 0;
}

static void s3c2410wdt_shutdown(struct platform_device *dev)
{
	struct s3c2410_wdt *wdt = platform_get_drvdata(dev);

	s3c2410wdt_enable(wdt, false);
	s3c2410wdt_stop(&wdt->wdt_device);
}

static int s3c2410wdt_suspend(struct device *dev)
{
	int ret;
	struct s3c2410_wdt *wdt = dev_get_drvdata(dev);

	/* Save watchdog state, and turn it off. */
	wdt->wtcon_save = readl(wdt->reg_base + S3C2410_WTCON);
	wdt->wtdat_save = readl(wdt->reg_base + S3C2410_WTDAT);

	ret = s3c2410wdt_enable(wdt, false);
	if (ret < 0)
		return ret;

	/* Note that WTCNT doesn't need to be saved. */
	s3c2410wdt_stop(&wdt->wdt_device);

	return 0;
}

static int s3c2410wdt_resume(struct device *dev)
{
	int ret;
	struct s3c2410_wdt *wdt = dev_get_drvdata(dev);

	/* Restore watchdog state. */
	writel(wdt->wtdat_save, wdt->reg_base + S3C2410_WTDAT);
	writel(wdt->wtdat_save, wdt->reg_base + S3C2410_WTCNT);/* Reset count */
	writel(wdt->wtcon_save, wdt->reg_base + S3C2410_WTCON);

	ret = s3c2410wdt_enable(wdt, true);
	if (ret < 0)
		return ret;

	dev_info(dev, "watchdog %sabled\n",
		(wdt->wtcon_save & S3C2410_WTCON_ENABLE) ? "en" : "dis");

	return 0;
}

static DEFINE_SIMPLE_DEV_PM_OPS(s3c2410wdt_pm_ops,
				s3c2410wdt_suspend, s3c2410wdt_resume);

static struct platform_driver s3c2410wdt_driver = {
	.probe		= s3c2410wdt_probe,
	.shutdown	= s3c2410wdt_shutdown,
	.id_table	= s3c2410_wdt_ids,
	.driver		= {
		.name	= "s3c2410-wdt",
		.pm	= pm_sleep_ptr(&s3c2410wdt_pm_ops),
		.of_match_table	= of_match_ptr(s3c2410_wdt_match),
	},
};

module_platform_driver(s3c2410wdt_driver);

MODULE_AUTHOR("Ben Dooks <ben@simtec.co.uk>, Dimitry Andric <dimitry.andric@tomtom.com>");
MODULE_DESCRIPTION("S3C2410 Watchdog Device Driver");
MODULE_LICENSE("GPL");
