/* linux/drivers/char/watchdog/s3c2410_wdt.c
 *
 * Copyright (c) 2004 Simtec Electronics
 *	Ben Dooks <ben@simtec.co.uk>
 *
 * S3C2410 Watchdog Timer Support
 *
 * Based on, softdog.c by Alan Cox,
 *     (c) Copyright 1996 Alan Cox <alan@lxorguk.ukuu.org.uk>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
*/

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

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
#include <linux/mfd/syscon.h>
#include <linux/regmap.h>
#include <linux/delay.h>

#define S3C2410_WTCON		0x00
#define S3C2410_WTDAT		0x04
#define S3C2410_WTCNT		0x08

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

#define CONFIG_S3C2410_WATCHDOG_ATBOOT		(0)
#define CONFIG_S3C2410_WATCHDOG_DEFAULT_TIME	(15)

#define EXYNOS5_RST_STAT_REG_OFFSET		0x0404
#define EXYNOS5_WDT_DISABLE_REG_OFFSET		0x0408
#define EXYNOS5_WDT_MASK_RESET_REG_OFFSET	0x040c
#define QUIRK_HAS_PMU_CONFIG			(1 << 0)
#define QUIRK_HAS_RST_STAT			(1 << 1)

/* These quirks require that we have a PMU register map */
#define QUIRKS_HAVE_PMUREG			(QUIRK_HAS_PMU_CONFIG | \
						 QUIRK_HAS_RST_STAT)

static bool nowayout	= WATCHDOG_NOWAYOUT;
static int tmr_margin;
static int tmr_atboot	= CONFIG_S3C2410_WATCHDOG_ATBOOT;
static int soft_noboot;
static int debug;

module_param(tmr_margin,  int, 0);
module_param(tmr_atboot,  int, 0);
module_param(nowayout,   bool, 0);
module_param(soft_noboot, int, 0);
module_param(debug,	  int, 0);

MODULE_PARM_DESC(tmr_margin, "Watchdog tmr_margin in seconds. (default="
		__MODULE_STRING(CONFIG_S3C2410_WATCHDOG_DEFAULT_TIME) ")");
MODULE_PARM_DESC(tmr_atboot,
		"Watchdog is started at boot time if set to 1, default="
			__MODULE_STRING(CONFIG_S3C2410_WATCHDOG_ATBOOT));
MODULE_PARM_DESC(nowayout, "Watchdog cannot be stopped once started (default="
			__MODULE_STRING(WATCHDOG_NOWAYOUT) ")");
MODULE_PARM_DESC(soft_noboot, "Watchdog action, set to 1 to ignore reboots, "
			"0 to reboot (default 0)");
MODULE_PARM_DESC(debug, "Watchdog debug, set to >1 for debug (default 0)");

/**
 * struct s3c2410_wdt_variant - Per-variant config data
 *
 * @disable_reg: Offset in pmureg for the register that disables the watchdog
 * timer reset functionality.
 * @mask_reset_reg: Offset in pmureg for the register that masks the watchdog
 * timer reset functionality.
 * @mask_bit: Bit number for the watchdog timer in the disable register and the
 * mask reset register.
 * @rst_stat_reg: Offset in pmureg for the register that has the reset status.
 * @rst_stat_bit: Bit number in the rst_stat register indicating a watchdog
 * reset.
 * @quirks: A bitfield of quirks.
 */

struct s3c2410_wdt_variant {
	int disable_reg;
	int mask_reset_reg;
	int mask_bit;
	int rst_stat_reg;
	int rst_stat_bit;
	u32 quirks;
};

struct s3c2410_wdt {
	struct device		*dev;
	struct clk		*clock;
	void __iomem		*reg_base;
	unsigned int		count;
	spinlock_t		lock;
	unsigned long		wtcon_save;
	unsigned long		wtdat_save;
	struct watchdog_device	wdt_device;
	struct notifier_block	freq_transition;
	struct s3c2410_wdt_variant *drv_data;
	struct regmap *pmureg;
};

static const struct s3c2410_wdt_variant drv_data_s3c2410 = {
	.quirks = 0
};

#ifdef CONFIG_OF
static const struct s3c2410_wdt_variant drv_data_exynos5250  = {
	.disable_reg = EXYNOS5_WDT_DISABLE_REG_OFFSET,
	.mask_reset_reg = EXYNOS5_WDT_MASK_RESET_REG_OFFSET,
	.mask_bit = 20,
	.rst_stat_reg = EXYNOS5_RST_STAT_REG_OFFSET,
	.rst_stat_bit = 20,
	.quirks = QUIRK_HAS_PMU_CONFIG | QUIRK_HAS_RST_STAT,
};

static const struct s3c2410_wdt_variant drv_data_exynos5420 = {
	.disable_reg = EXYNOS5_WDT_DISABLE_REG_OFFSET,
	.mask_reset_reg = EXYNOS5_WDT_MASK_RESET_REG_OFFSET,
	.mask_bit = 0,
	.rst_stat_reg = EXYNOS5_RST_STAT_REG_OFFSET,
	.rst_stat_bit = 9,
	.quirks = QUIRK_HAS_PMU_CONFIG | QUIRK_HAS_RST_STAT,
};

static const struct s3c2410_wdt_variant drv_data_exynos7 = {
	.disable_reg = EXYNOS5_WDT_DISABLE_REG_OFFSET,
	.mask_reset_reg = EXYNOS5_WDT_MASK_RESET_REG_OFFSET,
	.mask_bit = 23,
	.rst_stat_reg = EXYNOS5_RST_STAT_REG_OFFSET,
	.rst_stat_bit = 23,	/* A57 WDTRESET */
	.quirks = QUIRK_HAS_PMU_CONFIG | QUIRK_HAS_RST_STAT,
};

static const struct of_device_id s3c2410_wdt_match[] = {
	{ .compatible = "samsung,s3c2410-wdt",
	  .data = &drv_data_s3c2410 },
	{ .compatible = "samsung,exynos5250-wdt",
	  .data = &drv_data_exynos5250 },
	{ .compatible = "samsung,exynos5420-wdt",
	  .data = &drv_data_exynos5420 },
	{ .compatible = "samsung,exynos7-wdt",
	  .data = &drv_data_exynos7 },
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

/* watchdog control routines */

#define DBG(fmt, ...)					\
do {							\
	if (debug)					\
		pr_info(fmt, ##__VA_ARGS__);		\
} while (0)

/* functions */

static inline unsigned int s3c2410wdt_max_timeout(struct clk *clock)
{
	unsigned long freq = clk_get_rate(clock);

	return S3C2410_WTCNT_MAXCNT / (freq / (S3C2410_WTCON_PRESCALE_MAX + 1)
				       / S3C2410_WTCON_MAXDIV);
}

static inline struct s3c2410_wdt *freq_to_wdt(struct notifier_block *nb)
{
	return container_of(nb, struct s3c2410_wdt, freq_transition);
}

static int s3c2410wdt_mask_and_disable_reset(struct s3c2410_wdt *wdt, bool mask)
{
	int ret;
	u32 mask_val = 1 << wdt->drv_data->mask_bit;
	u32 val = 0;

	/* No need to do anything if no PMU CONFIG needed */
	if (!(wdt->drv_data->quirks & QUIRK_HAS_PMU_CONFIG))
		return 0;

	if (mask)
		val = mask_val;

	ret = regmap_update_bits(wdt->pmureg,
			wdt->drv_data->disable_reg,
			mask_val, val);
	if (ret < 0)
		goto error;

	ret = regmap_update_bits(wdt->pmureg,
			wdt->drv_data->mask_reset_reg,
			mask_val, val);
 error:
	if (ret < 0)
		dev_err(wdt->dev, "failed to update reg(%d)\n", ret);

	return ret;
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

	DBG("%s: count=0x%08x, wtcon=%08lx\n",
	    __func__, wdt->count, wtcon);

	writel(wdt->count, wdt->reg_base + S3C2410_WTDAT);
	writel(wdt->count, wdt->reg_base + S3C2410_WTCNT);
	writel(wtcon, wdt->reg_base + S3C2410_WTCON);
	spin_unlock(&wdt->lock);

	return 0;
}

static inline int s3c2410wdt_is_running(struct s3c2410_wdt *wdt)
{
	return readl(wdt->reg_base + S3C2410_WTCON) & S3C2410_WTCON_ENABLE;
}

static int s3c2410wdt_set_heartbeat(struct watchdog_device *wdd, unsigned timeout)
{
	struct s3c2410_wdt *wdt = watchdog_get_drvdata(wdd);
	unsigned long freq = clk_get_rate(wdt->clock);
	unsigned int count;
	unsigned int divisor = 1;
	unsigned long wtcon;

	if (timeout < 1)
		return -EINVAL;

	freq = DIV_ROUND_UP(freq, 128);
	count = timeout * freq;

	DBG("%s: count=%d, timeout=%d, freq=%lu\n",
	    __func__, count, timeout, freq);

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

	DBG("%s: timeout=%d, divisor=%d, count=%d (%08x)\n",
	    __func__, timeout, divisor, count, DIV_ROUND_UP(count, divisor));

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

static struct watchdog_ops s3c2410wdt_ops = {
	.owner = THIS_MODULE,
	.start = s3c2410wdt_start,
	.stop = s3c2410wdt_stop,
	.ping = s3c2410wdt_keepalive,
	.set_timeout = s3c2410wdt_set_heartbeat,
	.restart = s3c2410wdt_restart,
};

static struct watchdog_device s3c2410_wdd = {
	.info = &s3c2410_wdt_ident,
	.ops = &s3c2410wdt_ops,
	.timeout = CONFIG_S3C2410_WATCHDOG_DEFAULT_TIME,
};

/* interrupt handler code */

static irqreturn_t s3c2410wdt_irq(int irqno, void *param)
{
	struct s3c2410_wdt *wdt = platform_get_drvdata(param);

	dev_info(wdt->dev, "watchdog timer expired (irq)\n");

	s3c2410wdt_keepalive(&wdt->wdt_device);
	return IRQ_HANDLED;
}

#ifdef CONFIG_ARM_S3C24XX_CPUFREQ

static int s3c2410wdt_cpufreq_transition(struct notifier_block *nb,
					  unsigned long val, void *data)
{
	int ret;
	struct s3c2410_wdt *wdt = freq_to_wdt(nb);

	if (!s3c2410wdt_is_running(wdt))
		goto done;

	if (val == CPUFREQ_PRECHANGE) {
		/* To ensure that over the change we don't cause the
		 * watchdog to trigger, we perform an keep-alive if
		 * the watchdog is running.
		 */

		s3c2410wdt_keepalive(&wdt->wdt_device);
	} else if (val == CPUFREQ_POSTCHANGE) {
		s3c2410wdt_stop(&wdt->wdt_device);

		ret = s3c2410wdt_set_heartbeat(&wdt->wdt_device,
						wdt->wdt_device.timeout);

		if (ret >= 0)
			s3c2410wdt_start(&wdt->wdt_device);
		else
			goto err;
	}

done:
	return 0;

 err:
	dev_err(wdt->dev, "cannot set new value for timeout %d\n",
				wdt->wdt_device.timeout);
	return ret;
}

static inline int s3c2410wdt_cpufreq_register(struct s3c2410_wdt *wdt)
{
	wdt->freq_transition.notifier_call = s3c2410wdt_cpufreq_transition;

	return cpufreq_register_notifier(&wdt->freq_transition,
					 CPUFREQ_TRANSITION_NOTIFIER);
}

static inline void s3c2410wdt_cpufreq_deregister(struct s3c2410_wdt *wdt)
{
	wdt->freq_transition.notifier_call = s3c2410wdt_cpufreq_transition;

	cpufreq_unregister_notifier(&wdt->freq_transition,
				    CPUFREQ_TRANSITION_NOTIFIER);
}

#else

static inline int s3c2410wdt_cpufreq_register(struct s3c2410_wdt *wdt)
{
	return 0;
}

static inline void s3c2410wdt_cpufreq_deregister(struct s3c2410_wdt *wdt)
{
}
#endif

static inline unsigned int s3c2410wdt_get_bootstatus(struct s3c2410_wdt *wdt)
{
	unsigned int rst_stat;
	int ret;

	if (!(wdt->drv_data->quirks & QUIRK_HAS_RST_STAT))
		return 0;

	ret = regmap_read(wdt->pmureg, wdt->drv_data->rst_stat_reg, &rst_stat);
	if (ret)
		dev_warn(wdt->dev, "Couldn't get RST_STAT register\n");
	else if (rst_stat & BIT(wdt->drv_data->rst_stat_bit))
		return WDIOF_CARDRESET;

	return 0;
}

/* s3c2410_get_wdt_driver_data */
static inline struct s3c2410_wdt_variant *
get_wdt_drv_data(struct platform_device *pdev)
{
	if (pdev->dev.of_node) {
		const struct of_device_id *match;
		match = of_match_node(s3c2410_wdt_match, pdev->dev.of_node);
		return (struct s3c2410_wdt_variant *)match->data;
	} else {
		return (struct s3c2410_wdt_variant *)
			platform_get_device_id(pdev)->driver_data;
	}
}

static int s3c2410wdt_probe(struct platform_device *pdev)
{
	struct device *dev;
	struct s3c2410_wdt *wdt;
	struct resource *wdt_mem;
	struct resource *wdt_irq;
	unsigned int wtcon;
	int started = 0;
	int ret;

	DBG("%s: probe=%p\n", __func__, pdev);

	dev = &pdev->dev;

	wdt = devm_kzalloc(dev, sizeof(*wdt), GFP_KERNEL);
	if (!wdt)
		return -ENOMEM;

	wdt->dev = &pdev->dev;
	spin_lock_init(&wdt->lock);
	wdt->wdt_device = s3c2410_wdd;

	wdt->drv_data = get_wdt_drv_data(pdev);
	if (wdt->drv_data->quirks & QUIRKS_HAVE_PMUREG) {
		wdt->pmureg = syscon_regmap_lookup_by_phandle(dev->of_node,
						"samsung,syscon-phandle");
		if (IS_ERR(wdt->pmureg)) {
			dev_err(dev, "syscon regmap lookup failed.\n");
			return PTR_ERR(wdt->pmureg);
		}
	}

	wdt_irq = platform_get_resource(pdev, IORESOURCE_IRQ, 0);
	if (wdt_irq == NULL) {
		dev_err(dev, "no irq resource specified\n");
		ret = -ENOENT;
		goto err;
	}

	/* get the memory region for the watchdog timer */
	wdt_mem = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	wdt->reg_base = devm_ioremap_resource(dev, wdt_mem);
	if (IS_ERR(wdt->reg_base)) {
		ret = PTR_ERR(wdt->reg_base);
		goto err;
	}

	DBG("probe: mapped reg_base=%p\n", wdt->reg_base);

	wdt->clock = devm_clk_get(dev, "watchdog");
	if (IS_ERR(wdt->clock)) {
		dev_err(dev, "failed to find watchdog clock source\n");
		ret = PTR_ERR(wdt->clock);
		goto err;
	}

	ret = clk_prepare_enable(wdt->clock);
	if (ret < 0) {
		dev_err(dev, "failed to enable clock\n");
		return ret;
	}

	wdt->wdt_device.min_timeout = 1;
	wdt->wdt_device.max_timeout = s3c2410wdt_max_timeout(wdt->clock);

	ret = s3c2410wdt_cpufreq_register(wdt);
	if (ret < 0) {
		dev_err(dev, "failed to register cpufreq\n");
		goto err_clk;
	}

	watchdog_set_drvdata(&wdt->wdt_device, wdt);

	/* see if we can actually set the requested timer margin, and if
	 * not, try the default value */

	watchdog_init_timeout(&wdt->wdt_device, tmr_margin, &pdev->dev);
	ret = s3c2410wdt_set_heartbeat(&wdt->wdt_device,
					wdt->wdt_device.timeout);
	if (ret) {
		started = s3c2410wdt_set_heartbeat(&wdt->wdt_device,
					CONFIG_S3C2410_WATCHDOG_DEFAULT_TIME);

		if (started == 0)
			dev_info(dev,
			   "tmr_margin value out of range, default %d used\n",
			       CONFIG_S3C2410_WATCHDOG_DEFAULT_TIME);
		else
			dev_info(dev, "default timer value is out of range, "
							"cannot start\n");
	}

	ret = devm_request_irq(dev, wdt_irq->start, s3c2410wdt_irq, 0,
				pdev->name, pdev);
	if (ret != 0) {
		dev_err(dev, "failed to install irq (%d)\n", ret);
		goto err_cpufreq;
	}

	watchdog_set_nowayout(&wdt->wdt_device, nowayout);
	watchdog_set_restart_priority(&wdt->wdt_device, 128);

	wdt->wdt_device.bootstatus = s3c2410wdt_get_bootstatus(wdt);
	wdt->wdt_device.parent = &pdev->dev;

	ret = watchdog_register_device(&wdt->wdt_device);
	if (ret) {
		dev_err(dev, "cannot register watchdog (%d)\n", ret);
		goto err_cpufreq;
	}

	ret = s3c2410wdt_mask_and_disable_reset(wdt, false);
	if (ret < 0)
		goto err_unregister;

	if (tmr_atboot && started == 0) {
		dev_info(dev, "starting watchdog timer\n");
		s3c2410wdt_start(&wdt->wdt_device);
	} else if (!tmr_atboot) {
		/* if we're not enabling the watchdog, then ensure it is
		 * disabled if it has been left running from the bootloader
		 * or other source */

		s3c2410wdt_stop(&wdt->wdt_device);
	}

	platform_set_drvdata(pdev, wdt);

	/* print out a statement of readiness */

	wtcon = readl(wdt->reg_base + S3C2410_WTCON);

	dev_info(dev, "watchdog %sactive, reset %sabled, irq %sabled\n",
		 (wtcon & S3C2410_WTCON_ENABLE) ?  "" : "in",
		 (wtcon & S3C2410_WTCON_RSTEN) ? "en" : "dis",
		 (wtcon & S3C2410_WTCON_INTEN) ? "en" : "dis");

	return 0;

 err_unregister:
	watchdog_unregister_device(&wdt->wdt_device);

 err_cpufreq:
	s3c2410wdt_cpufreq_deregister(wdt);

 err_clk:
	clk_disable_unprepare(wdt->clock);

 err:
	return ret;
}

static int s3c2410wdt_remove(struct platform_device *dev)
{
	int ret;
	struct s3c2410_wdt *wdt = platform_get_drvdata(dev);

	ret = s3c2410wdt_mask_and_disable_reset(wdt, true);
	if (ret < 0)
		return ret;

	watchdog_unregister_device(&wdt->wdt_device);

	s3c2410wdt_cpufreq_deregister(wdt);

	clk_disable_unprepare(wdt->clock);

	return 0;
}

static void s3c2410wdt_shutdown(struct platform_device *dev)
{
	struct s3c2410_wdt *wdt = platform_get_drvdata(dev);

	s3c2410wdt_mask_and_disable_reset(wdt, true);

	s3c2410wdt_stop(&wdt->wdt_device);
}

#ifdef CONFIG_PM_SLEEP

static int s3c2410wdt_suspend(struct device *dev)
{
	int ret;
	struct s3c2410_wdt *wdt = dev_get_drvdata(dev);

	/* Save watchdog state, and turn it off. */
	wdt->wtcon_save = readl(wdt->reg_base + S3C2410_WTCON);
	wdt->wtdat_save = readl(wdt->reg_base + S3C2410_WTDAT);

	ret = s3c2410wdt_mask_and_disable_reset(wdt, true);
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

	ret = s3c2410wdt_mask_and_disable_reset(wdt, false);
	if (ret < 0)
		return ret;

	dev_info(dev, "watchdog %sabled\n",
		(wdt->wtcon_save & S3C2410_WTCON_ENABLE) ? "en" : "dis");

	return 0;
}
#endif

static SIMPLE_DEV_PM_OPS(s3c2410wdt_pm_ops, s3c2410wdt_suspend,
			s3c2410wdt_resume);

static struct platform_driver s3c2410wdt_driver = {
	.probe		= s3c2410wdt_probe,
	.remove		= s3c2410wdt_remove,
	.shutdown	= s3c2410wdt_shutdown,
	.id_table	= s3c2410_wdt_ids,
	.driver		= {
		.name	= "s3c2410-wdt",
		.pm	= &s3c2410wdt_pm_ops,
		.of_match_table	= of_match_ptr(s3c2410_wdt_match),
	},
};

module_platform_driver(s3c2410wdt_driver);

MODULE_AUTHOR("Ben Dooks <ben@simtec.co.uk>, "
	      "Dimitry Andric <dimitry.andric@tomtom.com>");
MODULE_DESCRIPTION("S3C2410 Watchdog Device Driver");
MODULE_LICENSE("GPL");
