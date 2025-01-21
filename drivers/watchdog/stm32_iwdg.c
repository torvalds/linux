// SPDX-License-Identifier: GPL-2.0
/*
 * Driver for STM32 Independent Watchdog
 *
 * Copyright (C) STMicroelectronics 2017
 * Author: Yannick Fertre <yannick.fertre@st.com> for STMicroelectronics.
 *
 * This driver is based on tegra_wdt.c
 *
 */

#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/iopoll.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/pm_wakeirq.h>
#include <linux/watchdog.h>

#define DEFAULT_TIMEOUT 10

/* IWDG registers */
#define IWDG_KR		0x00 /* Key register */
#define IWDG_PR		0x04 /* Prescaler Register */
#define IWDG_RLR	0x08 /* ReLoad Register */
#define IWDG_SR		0x0C /* Status Register */
#define IWDG_WINR	0x10 /* Windows Register */
#define IWDG_EWCR	0x14 /* Early Wake-up Register */

/* IWDG_KR register bit mask */
#define KR_KEY_RELOAD	0xAAAA /* reload counter enable */
#define KR_KEY_ENABLE	0xCCCC /* peripheral enable */
#define KR_KEY_EWA	0x5555 /* write access enable */
#define KR_KEY_DWA	0x0000 /* write access disable */

/* IWDG_PR register */
#define PR_SHIFT	2
#define PR_MIN		BIT(PR_SHIFT)

/* IWDG_RLR register values */
#define RLR_MIN		0x2		/* min value recommended */
#define RLR_MAX		GENMASK(11, 0)	/* max value of reload register */

/* IWDG_SR register bit mask */
#define SR_PVU	BIT(0) /* Watchdog prescaler value update */
#define SR_RVU	BIT(1) /* Watchdog counter reload value update */

#define EWCR_EWIT	GENMASK(11, 0) /* Watchdog counter window value */
#define EWCR_EWIC	BIT(14) /* Watchdog early interrupt acknowledge */
#define EWCR_EWIE	BIT(15) /* Watchdog early interrupt enable */

/* set timeout to 100000 us */
#define TIMEOUT_US	100000
#define SLEEP_US	1000

struct stm32_iwdg_data {
	bool has_pclk;
	bool has_early_wakeup;
	u32 max_prescaler;
};

static const struct stm32_iwdg_data stm32_iwdg_data = {
	.has_pclk = false,
	.has_early_wakeup = false,
	.max_prescaler = 256,
};

static const struct stm32_iwdg_data stm32mp1_iwdg_data = {
	.has_pclk = true,
	.has_early_wakeup = true,
	.max_prescaler = 1024,
};

struct stm32_iwdg {
	struct watchdog_device	wdd;
	const struct stm32_iwdg_data *data;
	void __iomem		*regs;
	struct clk		*clk_lsi;
	struct clk		*clk_pclk;
	unsigned int		rate;
};

static inline u32 reg_read(void __iomem *base, u32 reg)
{
	return readl_relaxed(base + reg);
}

static inline void reg_write(void __iomem *base, u32 reg, u32 val)
{
	writel_relaxed(val, base + reg);
}

static int stm32_iwdg_start(struct watchdog_device *wdd)
{
	struct stm32_iwdg *wdt = watchdog_get_drvdata(wdd);
	u32 tout, ptot, presc, iwdg_rlr, iwdg_ewcr, iwdg_pr, iwdg_sr;
	int ret;

	dev_dbg(wdd->parent, "%s\n", __func__);

	if (!wdd->pretimeout)
		wdd->pretimeout = 3 * wdd->timeout / 4;

	tout = clamp_t(unsigned int, wdd->timeout,
		       wdd->min_timeout, wdd->max_hw_heartbeat_ms / 1000);
	ptot = clamp_t(unsigned int, tout - wdd->pretimeout,
		       wdd->min_timeout, tout);

	presc = DIV_ROUND_UP(tout * wdt->rate, RLR_MAX + 1);

	/* The prescaler is align on power of 2 and start at 2 ^ PR_SHIFT. */
	presc = roundup_pow_of_two(presc);
	iwdg_pr = presc <= 1 << PR_SHIFT ? 0 : ilog2(presc) - PR_SHIFT;
	iwdg_rlr = ((tout * wdt->rate) / presc) - 1;
	iwdg_ewcr = ((ptot * wdt->rate) / presc) - 1;

	/* enable write access */
	reg_write(wdt->regs, IWDG_KR, KR_KEY_EWA);

	/* set prescaler & reload registers */
	reg_write(wdt->regs, IWDG_PR, iwdg_pr);
	reg_write(wdt->regs, IWDG_RLR, iwdg_rlr);
	if (wdt->data->has_early_wakeup)
		reg_write(wdt->regs, IWDG_EWCR, iwdg_ewcr | EWCR_EWIE);
	reg_write(wdt->regs, IWDG_KR, KR_KEY_ENABLE);

	/* wait for the registers to be updated (max 100ms) */
	ret = readl_relaxed_poll_timeout(wdt->regs + IWDG_SR, iwdg_sr,
					 !(iwdg_sr & (SR_PVU | SR_RVU)),
					 SLEEP_US, TIMEOUT_US);
	if (ret) {
		dev_err(wdd->parent, "Fail to set prescaler, reload regs\n");
		return ret;
	}

	/* reload watchdog */
	reg_write(wdt->regs, IWDG_KR, KR_KEY_RELOAD);

	return 0;
}

static int stm32_iwdg_ping(struct watchdog_device *wdd)
{
	struct stm32_iwdg *wdt = watchdog_get_drvdata(wdd);

	dev_dbg(wdd->parent, "%s\n", __func__);

	/* reload watchdog */
	reg_write(wdt->regs, IWDG_KR, KR_KEY_RELOAD);

	return 0;
}

static int stm32_iwdg_set_timeout(struct watchdog_device *wdd,
				  unsigned int timeout)
{
	dev_dbg(wdd->parent, "%s timeout: %d sec\n", __func__, timeout);

	wdd->timeout = timeout;

	if (watchdog_active(wdd))
		return stm32_iwdg_start(wdd);

	return 0;
}

static int stm32_iwdg_set_pretimeout(struct watchdog_device *wdd,
				     unsigned int pretimeout)
{
	dev_dbg(wdd->parent, "%s pretimeout: %d sec\n", __func__, pretimeout);

	wdd->pretimeout = pretimeout;

	if (watchdog_active(wdd))
		return stm32_iwdg_start(wdd);

	return 0;
}

static irqreturn_t stm32_iwdg_isr(int irq, void *wdog_arg)
{
	struct watchdog_device *wdd = wdog_arg;
	struct stm32_iwdg *wdt = watchdog_get_drvdata(wdd);
	u32 reg;

	reg = reg_read(wdt->regs, IWDG_EWCR);
	reg |= EWCR_EWIC;
	reg_write(wdt->regs, IWDG_EWCR, reg);

	watchdog_notify_pretimeout(wdd);

	return IRQ_HANDLED;
}

static void stm32_clk_disable_unprepare(void *data)
{
	clk_disable_unprepare(data);
}

static int stm32_iwdg_clk_init(struct platform_device *pdev,
			       struct stm32_iwdg *wdt)
{
	struct device *dev = &pdev->dev;
	u32 ret;

	wdt->clk_lsi = devm_clk_get(dev, "lsi");
	if (IS_ERR(wdt->clk_lsi))
		return dev_err_probe(dev, PTR_ERR(wdt->clk_lsi), "Unable to get lsi clock\n");

	/* optional peripheral clock */
	if (wdt->data->has_pclk) {
		wdt->clk_pclk = devm_clk_get(dev, "pclk");
		if (IS_ERR(wdt->clk_pclk))
			return dev_err_probe(dev, PTR_ERR(wdt->clk_pclk),
					     "Unable to get pclk clock\n");

		ret = clk_prepare_enable(wdt->clk_pclk);
		if (ret) {
			dev_err(dev, "Unable to prepare pclk clock\n");
			return ret;
		}
		ret = devm_add_action_or_reset(dev,
					       stm32_clk_disable_unprepare,
					       wdt->clk_pclk);
		if (ret)
			return ret;
	}

	ret = clk_prepare_enable(wdt->clk_lsi);
	if (ret) {
		dev_err(dev, "Unable to prepare lsi clock\n");
		return ret;
	}
	ret = devm_add_action_or_reset(dev, stm32_clk_disable_unprepare,
				       wdt->clk_lsi);
	if (ret)
		return ret;

	wdt->rate = clk_get_rate(wdt->clk_lsi);

	return 0;
}

static const struct watchdog_info stm32_iwdg_info = {
	.options	= WDIOF_SETTIMEOUT |
			  WDIOF_MAGICCLOSE |
			  WDIOF_KEEPALIVEPING,
	.identity	= "STM32 Independent Watchdog",
};

static const struct watchdog_info stm32_iwdg_preinfo = {
	.options	= WDIOF_SETTIMEOUT |
			  WDIOF_MAGICCLOSE |
			  WDIOF_KEEPALIVEPING |
			  WDIOF_PRETIMEOUT,
	.identity	= "STM32 Independent Watchdog",
};

static const struct watchdog_ops stm32_iwdg_ops = {
	.owner		= THIS_MODULE,
	.start		= stm32_iwdg_start,
	.ping		= stm32_iwdg_ping,
	.set_timeout	= stm32_iwdg_set_timeout,
	.set_pretimeout	= stm32_iwdg_set_pretimeout,
};

static const struct of_device_id stm32_iwdg_of_match[] = {
	{ .compatible = "st,stm32-iwdg", .data = &stm32_iwdg_data },
	{ .compatible = "st,stm32mp1-iwdg", .data = &stm32mp1_iwdg_data },
	{ /* end node */ }
};
MODULE_DEVICE_TABLE(of, stm32_iwdg_of_match);

static int stm32_iwdg_irq_init(struct platform_device *pdev,
			       struct stm32_iwdg *wdt)
{
	struct device_node *np = pdev->dev.of_node;
	struct watchdog_device *wdd = &wdt->wdd;
	struct device *dev = &pdev->dev;
	int irq, ret;

	if (!wdt->data->has_early_wakeup)
		return 0;

	irq = platform_get_irq(pdev, 0);
	if (irq <= 0)
		return 0;

	if (of_property_read_bool(np, "wakeup-source")) {
		ret = device_init_wakeup(dev, true);
		if (ret)
			return ret;

		ret = dev_pm_set_wake_irq(dev, irq);
		if (ret)
			return ret;
	}

	ret = devm_request_irq(dev, irq, stm32_iwdg_isr, 0,
			       dev_name(dev), wdd);
	if (ret)
		return ret;

	wdd->info = &stm32_iwdg_preinfo;
	return 0;
}

static int stm32_iwdg_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct watchdog_device *wdd;
	struct stm32_iwdg *wdt;
	int ret;

	wdt = devm_kzalloc(dev, sizeof(*wdt), GFP_KERNEL);
	if (!wdt)
		return -ENOMEM;

	wdt->data = of_device_get_match_data(&pdev->dev);
	if (!wdt->data)
		return -ENODEV;

	/* This is the timer base. */
	wdt->regs = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(wdt->regs))
		return PTR_ERR(wdt->regs);

	ret = stm32_iwdg_clk_init(pdev, wdt);
	if (ret)
		return ret;

	/* Initialize struct watchdog_device. */
	wdd = &wdt->wdd;
	wdd->parent = dev;
	wdd->info = &stm32_iwdg_info;
	wdd->ops = &stm32_iwdg_ops;
	wdd->timeout = DEFAULT_TIMEOUT;
	wdd->min_timeout = DIV_ROUND_UP((RLR_MIN + 1) * PR_MIN, wdt->rate);
	wdd->max_hw_heartbeat_ms = ((RLR_MAX + 1) * wdt->data->max_prescaler *
				    1000) / wdt->rate;

	/* Initialize IRQ, this might override wdd->info, hence it is here. */
	ret = stm32_iwdg_irq_init(pdev, wdt);
	if (ret)
		return ret;

	watchdog_set_drvdata(wdd, wdt);
	watchdog_set_nowayout(wdd, WATCHDOG_NOWAYOUT);
	watchdog_init_timeout(wdd, 0, dev);

	/*
	 * In case of CONFIG_WATCHDOG_HANDLE_BOOT_ENABLED is set
	 * (Means U-Boot/bootloaders leaves the watchdog running)
	 * When we get here we should make a decision to prevent
	 * any side effects before user space daemon will take care of it.
	 * The best option, taking into consideration that there is no
	 * way to read values back from hardware, is to enforce watchdog
	 * being run with deterministic values.
	 */
	if (IS_ENABLED(CONFIG_WATCHDOG_HANDLE_BOOT_ENABLED)) {
		ret = stm32_iwdg_start(wdd);
		if (ret)
			return ret;

		/* Make sure the watchdog is serviced */
		set_bit(WDOG_HW_RUNNING, &wdd->status);
	}

	ret = devm_watchdog_register_device(dev, wdd);
	if (ret)
		return ret;

	platform_set_drvdata(pdev, wdt);

	return 0;
}

static struct platform_driver stm32_iwdg_driver = {
	.probe		= stm32_iwdg_probe,
	.driver = {
		.name	= "iwdg",
		.of_match_table = stm32_iwdg_of_match,
	},
};
module_platform_driver(stm32_iwdg_driver);

MODULE_AUTHOR("Yannick Fertre <yannick.fertre@st.com>");
MODULE_DESCRIPTION("STMicroelectronics STM32 Independent Watchdog Driver");
MODULE_LICENSE("GPL v2");
