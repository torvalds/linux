// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2021 StarFive Technology Co., Ltd.
 * Author: Xingyu Wu <xingyu.wu@starfivetech.com>
 *         Samin Guo <samin.guo@starfivetech.com>
 */

#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/err.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/mfd/syscon.h>
#include <linux/io.h>
#include <linux/iopoll.h>
#include <linux/interrupt.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/types.h>
#include <linux/timer.h>
#include <linux/uaccess.h>
#include <linux/watchdog.h>
#include <linux/reset.h>
#include <linux/reset-controller.h>
#include <linux/pm_runtime.h>

#define WDT_INT_DIS	BIT(0)
#define DELAY_US	0
#define TIMEOUT_US	10000

/* JH7100 WatchDog register define */
#define JH7100_WDGINTSTAUS	0x000
#define JH7100_WDOGCONTROL	0x104	/* Watchdog Control Register R/W */
#define JH7100_WDOGLOAD		0x108	/* The initial value to be loaded */
				/* into the counter and is also used */
				/* as the reload value. R/W */
#define JH7100_WDOGEN		0x110	/* Watchdog enable Register */
#define JH7100_WDOGRELOAD	0x114	/* Write this register to reload preset */
				/* value to counter. (Write 0 or 1 are both ok) */
#define JH7100_WDOGVALUE	0x118	/* Watchdog Value Register RO */
#define JH7100_WDOGINTCLR	0x120	/* Watchdog Clear Interrupt Register WO */
#define JH7100_WDOGINTMSK	0x124	/* Watchdog Interrupt Mask Register */
#define JH7100_WDOGLOCK		0x13c	/* Watchdog Lock Register  R/W */

#define JH7100_UNLOCK_KEY	0x378f0765
#define JH7100_RESEN_SHIFT	0
#define JH7100_EN_SHIFT		0
#define JH7100_INTCLR_AVA_SHIFT	1	/* Watchdog can clear interrupt when this bit is 0 */

/* JH7110 WatchDog register define */
#define JH7110_WDOGLOAD		0x000	/* RW: Watchdog load register */
#define JH7110_WDOGVALUE	0x004	/* RO: The current value for the watchdog counter */
#define JH7110_WDOGCONTROL	0x008	/* RW: [0]: reset enable;  [1]: int enable/wdt enable/reload counter; [31:2]: res */
#define JH7110_WDOGINTCLR	0x00c	/* WO: clear intterupt && reload the counter */
#define JH7110_WDOGRIS		0x010	/* RO: Raw interrupt status from the counter */
#define JH7110_WDOGIMS		0x014	/* RO: Enabled interrupt status from the counter */
#define JH7110_WDOGLOCK		0xc00	/* RO: Enable write access to all other registers by writing 0x1ACCE551 */
#define JH7110_WDOGITCR		0xf00	/* RW: When set HIGH, places the Watchdog into integraeion test mode */
#define JH7110_WDOGITOP		0xf04	/* WO:	[0] Integration Test WDOGRES value Integration Test Mode
					 * Value output on WDOGRES when in Integration Test Mode
					 * [1] Integration Test WDOGINT value
					 * Value output on WDOGINT when in Integration Test Mode
					 */

#define JH7110_UNLOCK_KEY	0x1acce551
#define JH7110_RESEN_SHIFT	1
#define JH7110_EN_SHIFT		0
#define JH7110_INT_EN_SHIFT	JH7110_EN_SHIFT

/* WDOGCONTROL */
#define WDOG_INT_EN	0x0
#define WDOG_RESET_EN	0x1

/* WDOGLOCK */
#define WDOG_LOCKED		BIT(0)

#define STARFIVE_WATCHDOG_INTCLR	0x1
#define STARFIVE_WATCHDOG_ENABLE	0x1
#define STARFIVE_WATCHDOG_ATBOOT	0x0
#define STARFIVE_WATCHDOG_MAXCNT	0xffffffff

#define STARFIVE_WATCHDOG_DEFAULT_TIME	(15)

static bool nowayout = WATCHDOG_NOWAYOUT;
static int tmr_margin;
static int tmr_atboot = STARFIVE_WATCHDOG_ATBOOT;
static int soft_noboot;

module_param(tmr_margin, int, 0);
module_param(tmr_atboot, int, 0);
module_param(nowayout, bool, 0);
module_param(soft_noboot, int, 0);

MODULE_PARM_DESC(tmr_margin, "Watchdog tmr_margin in seconds. (default="
		__MODULE_STRING(STARFIVE_WATCHDOG_DEFAULT_TIME) ")");
MODULE_PARM_DESC(tmr_atboot,
		"Watchdog is started at boot time if set to 1, default="
			__MODULE_STRING(STARFIVE_WATCHDOG_ATBOOT));
MODULE_PARM_DESC(nowayout, "Watchdog cannot be stopped once started (default="
			__MODULE_STRING(WATCHDOG_NOWAYOUT) ")");
MODULE_PARM_DESC(soft_noboot,
	"Watchdog action, set to 1 to ignore reboots, 0 to reboot (default 0)");

struct starfive_wdt_variant_t {
	u32 unlock_key;
	u8 enrst_shift;
	u8 en_shift;
	u8 intclr_check;
	u8 intclr_ava_shift;
};

struct starfive_wdt_variant {
	u32 control;
	u32 load;
	u32 enable;
	u32 reload;
	u32 value;
	u32 int_clr;
	u32 int_mask;
	u32 unlock;
	u32 enter_irq_status;
	struct starfive_wdt_variant_t *variant;
};

struct starfive_wdt {
	u64 freq;
	struct device *dev;
	struct watchdog_device wdt_device;
	struct clk *core_clk;
	struct clk *apb_clk;
	struct reset_control *rsts;
	const struct starfive_wdt_variant *drv_data;
	u32 count;	/*count of timeout*/
	u32 reload;	/*restore the count*/
	void __iomem *base;
	spinlock_t lock;

	int irq;
};

#ifdef CONFIG_OF
static struct starfive_wdt_variant_t jh7100_variant = {
	.unlock_key = JH7100_UNLOCK_KEY,
	.enrst_shift = JH7100_RESEN_SHIFT,
	.en_shift = JH7100_EN_SHIFT,
	.intclr_check = 1,
	.intclr_ava_shift = JH7100_INTCLR_AVA_SHIFT,
};

static struct starfive_wdt_variant_t jh7110_variant = {
	.unlock_key = JH7110_UNLOCK_KEY,
	.enrst_shift = JH7110_RESEN_SHIFT,
	.en_shift = JH7110_EN_SHIFT,
};

static const struct starfive_wdt_variant drv_data_jh7100 = {
	.control = JH7100_WDOGCONTROL,
	.load = JH7100_WDOGLOAD,
	.enable = JH7100_WDOGEN,
	.reload = JH7100_WDOGRELOAD,
	.value = JH7100_WDOGVALUE,
	.int_clr = JH7100_WDOGINTCLR,
	.int_mask = JH7100_WDOGINTMSK,
	.unlock = JH7100_WDOGLOCK,
	.variant = &jh7100_variant,
};

static const struct starfive_wdt_variant drv_data_jh7110 = {
	.control = JH7110_WDOGCONTROL,
	.load = JH7110_WDOGLOAD,
	.enable = JH7110_WDOGCONTROL,
	.value = JH7110_WDOGVALUE,
	.int_clr = JH7110_WDOGINTCLR,
	.unlock = JH7110_WDOGLOCK,
	.enter_irq_status = JH7110_WDOGIMS,
	.variant = &jh7110_variant,
};

static const struct of_device_id starfive_wdt_match[] = {
	{ .compatible = "starfive,jh7100-wdt", .data = &drv_data_jh7100 },
	{ .compatible = "starfive,jh7110-wdt", .data = &drv_data_jh7110 },
	{},
};
MODULE_DEVICE_TABLE(of, starfive_wdt_match);
#endif

static const struct platform_device_id starfive_wdt_ids[] = {
	{
		.name = "starfive-wdt",
		.driver_data = (unsigned long)&drv_data_jh7100,
	},
	{
		.name = "starfive-dskit-wdt",
		.driver_data = (unsigned long)&drv_data_jh7110,
	},
	{}
};
MODULE_DEVICE_TABLE(platform, starfive_wdt_ids);

static int starfive_wdt_get_clock_rate(struct starfive_wdt *wdt)
{
	int ret;
	u32 freq;

	/* Next we try to get clock-frequency from dts.*/
	ret = of_property_read_u32(wdt->dev->of_node, "clock-frequency", &freq);
	if (!ret) {
		wdt->freq = (u64)freq;
		return 0;
	}
	dev_dbg(wdt->dev, "get rate failed, need clock-frequency define in dts.\n");

	if (!IS_ERR(wdt->core_clk)) {
		wdt->freq = clk_get_rate(wdt->core_clk);
		return 0;
	}
	dev_err(wdt->dev, "get clock-frequency failed\n");
	return -ENOENT;
}

static int starfive_wdt_enable_clock(struct starfive_wdt *wdt)
{
	int err = 0;

	wdt->apb_clk = devm_clk_get(wdt->dev, "apb_clk");
	if (!IS_ERR(wdt->apb_clk)) {
		err = clk_prepare_enable(wdt->apb_clk);
		if (err)
			dev_warn(wdt->dev, "enable core_clk error.\n");
	}

	wdt->core_clk = devm_clk_get(wdt->dev, "core_clk");
	if (!IS_ERR(wdt->core_clk)) {
		err = clk_prepare_enable(wdt->core_clk);
		if (err)
			dev_warn(wdt->dev, "enable apb_clk error.\n");
	}

	return err;
}

static int starfive_wdt_reset_init(struct starfive_wdt *wdt)
{
	int err = 0;

	wdt->rsts = devm_reset_control_array_get_exclusive(wdt->dev);
	if (!IS_ERR(wdt->rsts)) {
		err = reset_control_deassert(wdt->rsts);
		if (err)
			dev_err(wdt->dev, "deassert rsts error.\n");
	} else
		err = PTR_ERR(wdt->rsts);

	return err;
}

static __maybe_unused
u32 starfive_wdt_sec_to_ticks(struct starfive_wdt *wdt, u32 sec)
{
	return sec * wdt->freq;
}

static __maybe_unused
u32 starfive_wdt_ticks_to_sec(struct starfive_wdt *wdt, u32 ticks)
{
	return DIV_ROUND_CLOSEST(ticks, wdt->freq);
}

/*
 * Write unlock-key to unlock. Write other value to lock. When lock bit is 1,
 * external accesses to other watchdog registers are ignored.
 */
static int starfive_wdt_is_locked(struct starfive_wdt *wdt)
{
	u32 val;

	val = readl(wdt->base + wdt->drv_data->unlock);
	return !!(val & WDOG_LOCKED);
}

static void starfive_wdt_unlock(struct starfive_wdt *wdt)
{
	if (starfive_wdt_is_locked(wdt))
		writel(wdt->drv_data->variant->unlock_key,
			wdt->base + wdt->drv_data->unlock);
}

static void starfive_wdt_lock(struct starfive_wdt *wdt)
{
	if (!starfive_wdt_is_locked(wdt))
		writel(~wdt->drv_data->variant->unlock_key,
			wdt->base + wdt->drv_data->unlock);
}

static int __maybe_unused starfive_wdt_is_running(struct starfive_wdt *wdt)
{
	u32 val;

	starfive_wdt_unlock(wdt);
	val = readl(wdt->base + wdt->drv_data->enable);
	starfive_wdt_lock(wdt);

	return !!(val & STARFIVE_WATCHDOG_ENABLE <<
		wdt->drv_data->variant->en_shift);
}

static inline void starfive_wdt_int_enable(struct starfive_wdt *wdt)
{
	u32 val;

	if (wdt->drv_data->int_mask) {
		val = readl(wdt->base + wdt->drv_data->int_mask);
		val &= ~WDT_INT_DIS;
		writel(val, wdt->base + wdt->drv_data->int_mask);
	}
}

static inline void starfive_wdt_int_disable(struct starfive_wdt *wdt)
{
	u32 val;

	if (wdt->drv_data->int_mask) {
		val = readl(wdt->base + wdt->drv_data->int_mask);
		val |= WDT_INT_DIS;
		writel(val, wdt->base + wdt->drv_data->int_mask);
	}
}

static void starfive_wdt_enable_reset(struct starfive_wdt *wdt)
{
	u32 val;

	val = readl(wdt->base + wdt->drv_data->control);
	val |= WDOG_RESET_EN << wdt->drv_data->variant->enrst_shift;
	/* enable wdog interrupt to reset */
	writel(val, wdt->base + wdt->drv_data->control);
}

static void starfive_wdt_disable_reset(struct starfive_wdt *wdt)
{
	u32 val;

	val = readl(wdt->base + wdt->drv_data->control);
	val &= ~(WDOG_RESET_EN << wdt->drv_data->variant->enrst_shift);
	/*disable wdog interrupt to reset*/
	writel(val, wdt->base + wdt->drv_data->control);
}

static bool starfive_wdt_enter_irq_status(struct starfive_wdt *wdt)
{
	/* interrupt status whether has entered from the counter */
	return !!readl(wdt->base + wdt->drv_data->enter_irq_status);
}

static void starfive_wdt_int_clr(struct starfive_wdt *wdt)
{
	void __iomem *addr;
	u8 clr_check;
	u8 clr_ava_shift;
	u32 value;
	int ret = 0;

	addr = wdt->base + wdt->drv_data->int_clr;
	clr_ava_shift = wdt->drv_data->variant->intclr_ava_shift;
	clr_check = wdt->drv_data->variant->intclr_check;
	if (clr_check) {
		/* waiting interrupt can be to clearing */
		value = readl(addr);
		ret = readl_poll_timeout_atomic(addr, value,
				!(value & BIT(clr_ava_shift)), DELAY_US, TIMEOUT_US);
	}

	if (!ret)
		writel(STARFIVE_WATCHDOG_INTCLR, addr);

	if (starfive_wdt_enter_irq_status(wdt))
		enable_irq(wdt->irq);
}

static inline void starfive_wdt_set_count(struct starfive_wdt *wdt, u32 val)
{
	writel(val, wdt->base + wdt->drv_data->load);
}

static inline u32 starfive_wdt_get_count(struct starfive_wdt *wdt)
{
	return readl(wdt->base + wdt->drv_data->value);
}

static inline void starfive_wdt_enable(struct starfive_wdt *wdt)
{
	u32 val;

	val = readl(wdt->base + wdt->drv_data->enable);
	val |= STARFIVE_WATCHDOG_ENABLE << wdt->drv_data->variant->en_shift;
	writel(val, wdt->base + wdt->drv_data->enable);
}

static inline void starfive_wdt_disable(struct starfive_wdt *wdt)
{
	u32 val;

	val = readl(wdt->base + wdt->drv_data->enable);
	val &= ~(STARFIVE_WATCHDOG_ENABLE << wdt->drv_data->variant->en_shift);
	writel(val, wdt->base + wdt->drv_data->enable);
}

static inline void
starfive_wdt_set_relod_count(struct starfive_wdt *wdt, u32 count)
{
	writel(count, wdt->base + wdt->drv_data->load);
	if (wdt->drv_data->reload)
		writel(0x1, wdt->base + wdt->drv_data->reload);
	else
		/* jh7110 need enable controller to reload counter */
		starfive_wdt_enable(wdt);
}

static int starfive_wdt_mask_and_disable_reset(struct starfive_wdt *wdt, bool mask)
{
	starfive_wdt_unlock(wdt);

	if (mask)
		starfive_wdt_disable_reset(wdt);
	else
		starfive_wdt_enable_reset(wdt);

	starfive_wdt_lock(wdt);

	return 0;
}

static unsigned int starfive_wdt_max_timeout(struct starfive_wdt *wdt)
{
	return DIV_ROUND_UP(STARFIVE_WATCHDOG_MAXCNT, wdt->freq) - 1;
}

static unsigned int starfive_wdt_get_timeleft(struct watchdog_device *wdd)
{
	struct starfive_wdt *wdt = watchdog_get_drvdata(wdd);
	u32 count;
	u32 irq_status;

	starfive_wdt_unlock(wdt);
	/*
	 * Because set half count value,
	 * timeleft value should add the count value before first timeout.
	 */
	irq_status = starfive_wdt_enter_irq_status(wdt) ? 1 : 0;
	count = starfive_wdt_get_count(wdt) + (1 - irq_status) * wdt->count;
	starfive_wdt_lock(wdt);

	return starfive_wdt_ticks_to_sec(wdt, count);
}

static int starfive_wdt_keepalive(struct watchdog_device *wdd)
{
	struct starfive_wdt *wdt = watchdog_get_drvdata(wdd);

	spin_lock(&wdt->lock);

	starfive_wdt_unlock(wdt);
	starfive_wdt_int_clr(wdt);
	starfive_wdt_set_relod_count(wdt, wdt->count);
	starfive_wdt_lock(wdt);

	spin_unlock(&wdt->lock);

	return 0;
}

static irqreturn_t starfive_wdt_interrupt_handler(int irq, void *data)
{
	/*
	 * We don't clear the IRQ status. It's supposed to be done by the
	 * following ping operations.
	 */
	struct platform_device *pdev = data;
	struct starfive_wdt *wdt = platform_get_drvdata(pdev);

	/* Disable the IRQ and avoid re-entry interrupt. */
	disable_irq_nosync(wdt->irq);

	return IRQ_HANDLED;
}

static int starfive_wdt_stop(struct watchdog_device *wdd)
{
	struct starfive_wdt *wdt = watchdog_get_drvdata(wdd);

	spin_lock(&wdt->lock);

	starfive_wdt_unlock(wdt);
	starfive_wdt_int_disable(wdt);
	starfive_wdt_int_clr(wdt);
	starfive_wdt_disable(wdt);
	starfive_wdt_lock(wdt);

	spin_unlock(&wdt->lock);

	return 0;
}

static int starfive_wdt_pm_stop(struct watchdog_device *wdd)
{
	struct starfive_wdt *wdt = watchdog_get_drvdata(wdd);

	starfive_wdt_stop(wdd);
	pm_runtime_put(wdt->dev);

	return 0;
}

static int starfive_wdt_start(struct watchdog_device *wdd)
{
	struct starfive_wdt *wdt = watchdog_get_drvdata(wdd);

	pm_runtime_get_sync(wdt->dev);

	spin_lock(&wdt->lock);

	starfive_wdt_unlock(wdt);

	if (soft_noboot)
		starfive_wdt_disable_reset(wdt);
	else
		starfive_wdt_enable_reset(wdt);

	starfive_wdt_int_clr(wdt);
	starfive_wdt_set_count(wdt, wdt->count);
	starfive_wdt_int_enable(wdt);
	starfive_wdt_enable(wdt);

	starfive_wdt_lock(wdt);

	spin_unlock(&wdt->lock);

	return 0;
}

static int starfive_wdt_restart(struct watchdog_device *wdd, unsigned long action,
				void *data)
{
	struct starfive_wdt *wdt = watchdog_get_drvdata(wdd);

	starfive_wdt_unlock(wdt);
	/* disable watchdog, to be safe */
	starfive_wdt_disable(wdt);

	if (soft_noboot)
		starfive_wdt_disable_reset(wdt);
	else
		starfive_wdt_enable_reset(wdt);

	/* put initial values into count and data */
	starfive_wdt_set_count(wdt, wdt->count);

	/* set the watchdog to go and reset... */
	starfive_wdt_int_clr(wdt);
	starfive_wdt_int_enable(wdt);
	starfive_wdt_enable(wdt);

	/* wait for reset to assert... */
	mdelay(500);

	starfive_wdt_lock(wdt);

	return 0;
}

static int starfive_wdt_set_timeout(struct watchdog_device *wdd,
				    unsigned int timeout)
{
	struct starfive_wdt *wdt = watchdog_get_drvdata(wdd);

	unsigned long freq = wdt->freq;
	unsigned int count;

	if (timeout < 1)
		return -EINVAL;

	/*
	 * This watchdog takes twice timeouts to reset.
	 * In order to reduce time to reset, should set half count value.
	 */
	count = timeout * freq / 2;

	if (count > STARFIVE_WATCHDOG_MAXCNT) {
		dev_warn(wdt->dev, "timeout %d too big,use the MAX-timeout set.\n",
				timeout);
		timeout = starfive_wdt_max_timeout(wdt);
		count = timeout * freq;
	}

	dev_info(wdt->dev, "Heartbeat: timeout=%d, count/2=%d (%08x)\n",
		timeout, count, count);

	starfive_wdt_unlock(wdt);
	starfive_wdt_disable(wdt);
	starfive_wdt_set_relod_count(wdt, count);
	starfive_wdt_enable(wdt);
	starfive_wdt_lock(wdt);

	wdt->count = count;
	wdd->timeout = timeout;

	return 0;
}

#define OPTIONS (WDIOF_SETTIMEOUT | WDIOF_KEEPALIVEPING | WDIOF_MAGICCLOSE)

static const struct watchdog_info starfive_wdt_ident = {
	.options	= OPTIONS,
	.firmware_version = 0,
	.identity	= "StarFive Watchdog",
};

static const struct watchdog_ops starfive_wdt_ops = {
	.owner = THIS_MODULE,
	.start = starfive_wdt_start,
	.stop = starfive_wdt_pm_stop,
	.ping = starfive_wdt_keepalive,
	.set_timeout = starfive_wdt_set_timeout,
	.restart = starfive_wdt_restart,
	.get_timeleft = starfive_wdt_get_timeleft,
};

static const struct watchdog_device starfive_wdd = {
	.info = &starfive_wdt_ident,
	.ops = &starfive_wdt_ops,
	.timeout = STARFIVE_WATCHDOG_DEFAULT_TIME,
};

static inline const struct starfive_wdt_variant *
starfive_get_wdt_drv_data(struct platform_device *pdev)
{
	const struct starfive_wdt_variant *variant;

	variant = of_device_get_match_data(&pdev->dev);
	if (!variant) {
		/* Device matched by platform_device_id */
		variant = (struct starfive_wdt_variant *)
			platform_get_device_id(pdev)->driver_data;
	}

	return variant;
}

static int starfive_wdt_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct starfive_wdt *wdt;
	int started = 0;
	int ret;

	pm_runtime_enable(dev);

	wdt = devm_kzalloc(dev, sizeof(*wdt), GFP_KERNEL);
	if (!wdt)
		return -ENOMEM;

	wdt->dev = dev;
	spin_lock_init(&wdt->lock);
	wdt->wdt_device = starfive_wdd;

	wdt->drv_data = starfive_get_wdt_drv_data(pdev);

	wdt->irq = platform_get_irq(pdev, 0);
	if (wdt->irq < 0) {
		dev_err(dev, "can not find irq.\n");
		return wdt->irq;
	}

	/* get the memory region for the watchdog timer */
	wdt->base = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(wdt->base)) {
		ret = PTR_ERR(wdt->base);
		goto err;
	}

	ret = starfive_wdt_enable_clock(wdt);
	if (ret)
		dev_warn(wdt->dev, "get & enable clk err\n");

	starfive_wdt_get_clock_rate(wdt);

	ret = starfive_wdt_reset_init(wdt);
	if (ret)
		dev_warn(wdt->dev, "get & deassert rst err\n");

	wdt->wdt_device.min_timeout = 1;
	wdt->wdt_device.max_timeout = starfive_wdt_max_timeout(wdt);

	watchdog_set_drvdata(&wdt->wdt_device, wdt);

	/*
	 * see if we can actually set the requested timer margin,
	 * and if not, try the default value.
	 */
	watchdog_init_timeout(&wdt->wdt_device, tmr_margin, dev);

	ret = starfive_wdt_set_timeout(&wdt->wdt_device,
				       wdt->wdt_device.timeout);
	if (ret) {
		dev_info(dev, "tmr_margin value out of range, default %d used\n",
				 STARFIVE_WATCHDOG_DEFAULT_TIME);
		starfive_wdt_set_timeout(&wdt->wdt_device,
					 STARFIVE_WATCHDOG_DEFAULT_TIME);
	}

	ret = devm_request_irq(dev, wdt->irq, starfive_wdt_interrupt_handler, 0,
			       pdev->name, pdev);
	if (ret != 0) {
		dev_err(dev, "failed to install irq (%d)\n", ret);
		goto err;
	}

	watchdog_set_nowayout(&wdt->wdt_device, nowayout);
	watchdog_set_restart_priority(&wdt->wdt_device, 128);

	wdt->wdt_device.parent = dev;

	ret = watchdog_register_device(&wdt->wdt_device);
	if (ret)
		goto err;

	ret = starfive_wdt_mask_and_disable_reset(wdt, false);
	if (ret < 0)
		goto err_unregister;

	if (tmr_atboot && started == 0) {
		dev_info(dev, "starting watchdog timer\n");
		starfive_wdt_start(&wdt->wdt_device);
	} else if (!tmr_atboot) {

		/*
		 *if we're not enabling the watchdog, then ensure it is
		 * disabled if it has been left running from the bootloader
		 * or other source.
		 */
		starfive_wdt_stop(&wdt->wdt_device);
	}
	clk_disable_unprepare(wdt->core_clk);
	clk_disable_unprepare(wdt->apb_clk);

	platform_set_drvdata(pdev, wdt);

	return 0;

err_unregister:
	watchdog_unregister_device(&wdt->wdt_device);
err:
	return ret;
}

static int starfive_wdt_remove(struct platform_device *dev)
{
	int ret;
	struct starfive_wdt *wdt = platform_get_drvdata(dev);

	ret = starfive_wdt_mask_and_disable_reset(wdt, true);
	if (ret < 0)
		return ret;

	watchdog_unregister_device(&wdt->wdt_device);

	clk_disable_unprepare(wdt->core_clk);
	clk_disable_unprepare(wdt->apb_clk);
	pm_runtime_disable(wdt->dev);

	return 0;
}

static void starfive_wdt_shutdown(struct platform_device *dev)
{
	struct starfive_wdt *wdt = platform_get_drvdata(dev);

	starfive_wdt_mask_and_disable_reset(wdt, true);

	starfive_wdt_pm_stop(&wdt->wdt_device);
}

#ifdef CONFIG_PM_SLEEP

static int starfive_wdt_suspend(struct device *dev)
{
	int ret;
	struct starfive_wdt *wdt = dev_get_drvdata(dev);

	starfive_wdt_unlock(wdt);

	/* Save watchdog state, and turn it off. */
	wdt->reload = starfive_wdt_get_count(wdt);

	ret = starfive_wdt_mask_and_disable_reset(wdt, true);
	if (ret < 0)
		return ret;

	/* Note that WTCNT doesn't need to be saved. */
	starfive_wdt_stop(&wdt->wdt_device);
	pm_runtime_force_suspend(dev);

	starfive_wdt_lock(wdt);

	return 0;
}

static int starfive_wdt_resume(struct device *dev)
{
	int ret;
	struct starfive_wdt *wdt = dev_get_drvdata(dev);

	starfive_wdt_unlock(wdt);
	pm_runtime_force_resume(dev);

	ret = reset_control_deassert(wdt->rsts);
	if (ret)
		dev_err(wdt->dev, "deassert rsts error.\n");

	/* Restore watchdog state. */
	starfive_wdt_set_relod_count(wdt, wdt->reload);

	ret = starfive_wdt_mask_and_disable_reset(wdt, false);
	if (ret < 0)
		return ret;

	starfive_wdt_restart(&wdt->wdt_device, 0, NULL);

	starfive_wdt_lock(wdt);

	return 0;
}
#endif /* CONFIG_PM_SLEEP */

#ifdef CONFIG_PM

static int starfive_wdt_runtime_suspend(struct device *dev)
{
	struct starfive_wdt *wdt = dev_get_drvdata(dev);

	clk_disable_unprepare(wdt->apb_clk);
	clk_disable_unprepare(wdt->core_clk);

	return 0;
}

static int starfive_wdt_runtime_resume(struct device *dev)
{
	struct starfive_wdt *wdt = dev_get_drvdata(dev);

	clk_prepare_enable(wdt->apb_clk);
	clk_prepare_enable(wdt->core_clk);

	return 0;
}

#endif /*CONFIG_PM*/

static const struct dev_pm_ops starfive_wdt_pm_ops = {
	SET_RUNTIME_PM_OPS(starfive_wdt_runtime_suspend, starfive_wdt_runtime_resume, NULL)
	SET_SYSTEM_SLEEP_PM_OPS(starfive_wdt_suspend, starfive_wdt_resume)
};

static struct platform_driver starfive_starfive_wdt_driver = {
	.probe		= starfive_wdt_probe,
	.remove		= starfive_wdt_remove,
	.shutdown	= starfive_wdt_shutdown,
	.id_table	= starfive_wdt_ids,
	.driver		= {
		.name	= "starfive-wdt",
		.pm	= &starfive_wdt_pm_ops,
		.of_match_table = of_match_ptr(starfive_wdt_match),
	},
};

module_platform_driver(starfive_starfive_wdt_driver);

MODULE_AUTHOR("xingyu.wu <xingyu.wu@starfivetech.com>");
MODULE_AUTHOR("samin.guo <samin.guo@starfivetech.com>");
MODULE_DESCRIPTION("StarFive Watchdog Device Driver");
MODULE_LICENSE("GPL v2");
