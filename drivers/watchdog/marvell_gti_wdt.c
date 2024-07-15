// SPDX-License-Identifier: GPL-2.0
/* Marvell GTI Watchdog driver
 *
 * Copyright (C) 2023 Marvell.
 */

#include <linux/clk.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/of.h>
#include <linux/watchdog.h>

/*
 * Hardware supports following mode of operation:
 * 1) Interrupt Only:
 *    This will generate the interrupt to arm core whenever timeout happens.
 *
 * 2) Interrupt + del3t (Interrupt to firmware (SCP processor)).
 *    This will generate interrupt to arm core on 1st timeout happens
 *    This will generate interrupt to SCP processor on 2nd timeout happens
 *
 * 3) Interrupt + Interrupt to SCP processor (called delt3t) + reboot.
 *    This will generate interrupt to arm core on 1st timeout happens
 *    Will generate interrupt to SCP processor on 2nd timeout happens,
 *    if interrupt is configured.
 *    Reboot on 3rd timeout.
 *
 * Driver will use hardware in mode-3 above so that system can reboot in case
 * a hardware hang. Also h/w is configured not to generate SCP interrupt, so
 * effectively 2nd timeout is ignored within hardware.
 *
 * First timeout is effectively watchdog pretimeout.
 */

/* GTI CWD Watchdog (GTI_CWD_WDOG) Register */
#define GTI_CWD_WDOG(reg_offset)	(0x8 * (reg_offset))
#define GTI_CWD_WDOG_MODE_INT_DEL3T_RST	0x3
#define GTI_CWD_WDOG_MODE_MASK		GENMASK_ULL(1, 0)
#define GTI_CWD_WDOG_LEN_SHIFT		4
#define GTI_CWD_WDOG_LEN_MASK		GENMASK_ULL(19, 4)
#define GTI_CWD_WDOG_CNT_SHIFT		20
#define GTI_CWD_WDOG_CNT_MASK		GENMASK_ULL(43, 20)

/* GTI CWD Watchdog Interrupt (GTI_CWD_INT) Register */
#define GTI_CWD_INT			0x200
#define GTI_CWD_INT_PENDING_STATUS(bit)	BIT_ULL(bit)

/* GTI CWD Watchdog Interrupt Enable Clear (GTI_CWD_INT_ENA_CLR) Register */
#define GTI_CWD_INT_ENA_CLR		0x210
#define GTI_CWD_INT_ENA_CLR_VAL(bit)	BIT_ULL(bit)

/* GTI CWD Watchdog Interrupt Enable Set (GTI_CWD_INT_ENA_SET) Register */
#define GTI_CWD_INT_ENA_SET		0x218
#define GTI_CWD_INT_ENA_SET_VAL(bit)	BIT_ULL(bit)

/* GTI CWD Watchdog Poke (GTI_CWD_POKE) Registers */
#define GTI_CWD_POKE(reg_offset)	(0x10000 + 0x8 * (reg_offset))
#define GTI_CWD_POKE_VAL		1

struct gti_match_data {
	u32 gti_num_timers;
};

static const struct gti_match_data match_data_octeontx2 = {
	.gti_num_timers = 54,
};

static const struct gti_match_data match_data_cn10k = {
	.gti_num_timers = 64,
};

struct gti_wdt_priv {
	struct watchdog_device wdev;
	void __iomem *base;
	u32 clock_freq;
	struct clk *sclk;
	/* wdt_timer_idx used for timer to be used for system watchdog */
	u32 wdt_timer_idx;
	const struct gti_match_data *data;
};

static irqreturn_t gti_wdt_interrupt(int irq, void *data)
{
	struct watchdog_device *wdev = data;
	struct gti_wdt_priv *priv = watchdog_get_drvdata(wdev);

	/* Clear Interrupt Pending Status */
	writeq(GTI_CWD_INT_PENDING_STATUS(priv->wdt_timer_idx),
	       priv->base + GTI_CWD_INT);

	watchdog_notify_pretimeout(wdev);

	return IRQ_HANDLED;
}

static int gti_wdt_ping(struct watchdog_device *wdev)
{
	struct gti_wdt_priv *priv = watchdog_get_drvdata(wdev);

	writeq(GTI_CWD_POKE_VAL,
	       priv->base + GTI_CWD_POKE(priv->wdt_timer_idx));

	return 0;
}

static int gti_wdt_start(struct watchdog_device *wdev)
{
	struct gti_wdt_priv *priv = watchdog_get_drvdata(wdev);
	u64 regval;

	if (!wdev->pretimeout)
		return -EINVAL;

	set_bit(WDOG_HW_RUNNING, &wdev->status);

	/* Clear any pending interrupt */
	writeq(GTI_CWD_INT_PENDING_STATUS(priv->wdt_timer_idx),
	       priv->base + GTI_CWD_INT);

	/* Enable Interrupt */
	writeq(GTI_CWD_INT_ENA_SET_VAL(priv->wdt_timer_idx),
	       priv->base + GTI_CWD_INT_ENA_SET);

	/* Set (Interrupt + SCP interrupt (DEL3T) + core domain reset) Mode */
	regval = readq(priv->base + GTI_CWD_WDOG(priv->wdt_timer_idx));
	regval |= GTI_CWD_WDOG_MODE_INT_DEL3T_RST;
	writeq(regval, priv->base + GTI_CWD_WDOG(priv->wdt_timer_idx));

	return 0;
}

static int gti_wdt_stop(struct watchdog_device *wdev)
{
	struct gti_wdt_priv *priv = watchdog_get_drvdata(wdev);
	u64 regval;

	/* Disable Interrupt */
	writeq(GTI_CWD_INT_ENA_CLR_VAL(priv->wdt_timer_idx),
	       priv->base + GTI_CWD_INT_ENA_CLR);

	/* Set GTI_CWD_WDOG.Mode = 0 to stop the timer */
	regval = readq(priv->base + GTI_CWD_WDOG(priv->wdt_timer_idx));
	regval &= ~GTI_CWD_WDOG_MODE_MASK;
	writeq(regval, priv->base + GTI_CWD_WDOG(priv->wdt_timer_idx));

	return 0;
}

static int gti_wdt_settimeout(struct watchdog_device *wdev,
					unsigned int timeout)
{
	struct gti_wdt_priv *priv = watchdog_get_drvdata(wdev);
	u64 timeout_wdog, regval;

	/* Update new timeout */
	wdev->timeout = timeout;

	/* Pretimeout is 1/3 of timeout */
	wdev->pretimeout = timeout / 3;

	/* Get clock cycles from pretimeout */
	timeout_wdog = (u64)priv->clock_freq * wdev->pretimeout;

	/* Watchdog counts in 1024 cycle steps */
	timeout_wdog = timeout_wdog >> 10;

	/* GTI_CWD_WDOG.CNT: reload counter is 16-bit */
	timeout_wdog = (timeout_wdog + 0xff) >> 8;
	if (timeout_wdog >= 0x10000)
		timeout_wdog = 0xffff;

	/*
	 * GTI_CWD_WDOG.LEN is 24bit, lower 8-bits should be zero and
	 * upper 16-bits are same as GTI_CWD_WDOG.CNT
	 */
	regval = readq(priv->base + GTI_CWD_WDOG(priv->wdt_timer_idx));
	regval &= GTI_CWD_WDOG_MODE_MASK;
	regval |= (timeout_wdog << (GTI_CWD_WDOG_CNT_SHIFT + 8)) |
		   (timeout_wdog << GTI_CWD_WDOG_LEN_SHIFT);
	writeq(regval, priv->base + GTI_CWD_WDOG(priv->wdt_timer_idx));

	return 0;
}

static int gti_wdt_set_pretimeout(struct watchdog_device *wdev,
					unsigned int timeout)
{
	struct gti_wdt_priv *priv = watchdog_get_drvdata(wdev);
	struct watchdog_device *wdog_dev = &priv->wdev;

	if (!timeout) {
		/* Disable Interrupt */
		writeq(GTI_CWD_INT_ENA_CLR_VAL(priv->wdt_timer_idx),
		       priv->base + GTI_CWD_INT_ENA_CLR);
		return 0;
	}

	/* pretimeout should 1/3 of max_timeout */
	if (timeout * 3 <= wdog_dev->max_timeout)
		return gti_wdt_settimeout(wdev, timeout * 3);

	return -EINVAL;
}

static void gti_clk_disable_unprepare(void *data)
{
	clk_disable_unprepare(data);
}

static int gti_wdt_get_cntfrq(struct platform_device *pdev,
			      struct gti_wdt_priv *priv)
{
	int err;

	priv->sclk = devm_clk_get_enabled(&pdev->dev, NULL);
	if (IS_ERR(priv->sclk))
		return PTR_ERR(priv->sclk);

	err = devm_add_action_or_reset(&pdev->dev,
				       gti_clk_disable_unprepare, priv->sclk);
	if (err)
		return err;

	priv->clock_freq = clk_get_rate(priv->sclk);
	if (!priv->clock_freq)
		return -EINVAL;

	return 0;
}

static const struct watchdog_info gti_wdt_ident = {
	.identity = "Marvell GTI watchdog",
	.options = WDIOF_SETTIMEOUT | WDIOF_PRETIMEOUT | WDIOF_KEEPALIVEPING |
		   WDIOF_MAGICCLOSE | WDIOF_CARDRESET,
};

static const struct watchdog_ops gti_wdt_ops = {
	.owner = THIS_MODULE,
	.start = gti_wdt_start,
	.stop = gti_wdt_stop,
	.ping = gti_wdt_ping,
	.set_timeout = gti_wdt_settimeout,
	.set_pretimeout = gti_wdt_set_pretimeout,
};

static int gti_wdt_probe(struct platform_device *pdev)
{
	struct gti_wdt_priv *priv;
	struct device *dev = &pdev->dev;
	struct watchdog_device *wdog_dev;
	u64 max_pretimeout;
	u32 wdt_idx;
	int irq;
	int err;

	priv = devm_kzalloc(&pdev->dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	priv->base = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(priv->base))
		return dev_err_probe(&pdev->dev, PTR_ERR(priv->base),
			      "reg property not valid/found\n");

	err = gti_wdt_get_cntfrq(pdev, priv);
	if (err)
		return dev_err_probe(&pdev->dev, err,
				     "GTI clock frequency not valid/found");

	priv->data = of_device_get_match_data(dev);

	/* default use last timer for watchdog */
	priv->wdt_timer_idx = priv->data->gti_num_timers - 1;

	err = of_property_read_u32(dev->of_node, "marvell,wdt-timer-index",
				   &wdt_idx);
	if (!err) {
		if (wdt_idx >= priv->data->gti_num_timers)
			return dev_err_probe(&pdev->dev, -EINVAL,
				"GTI wdog timer index not valid");

		priv->wdt_timer_idx = wdt_idx;
	}

	wdog_dev = &priv->wdev;
	wdog_dev->info = &gti_wdt_ident,
	wdog_dev->ops = &gti_wdt_ops,
	wdog_dev->parent = dev;
	/*
	 * Watchdog counter is 24 bit where lower 8 bits are zeros
	 * This counter decrements every 1024 clock cycles.
	 */
	max_pretimeout = (GTI_CWD_WDOG_CNT_MASK >> GTI_CWD_WDOG_CNT_SHIFT);
	max_pretimeout &= ~0xFFUL;
	max_pretimeout = (max_pretimeout * 1024) / priv->clock_freq;
	wdog_dev->pretimeout = max_pretimeout;

	/* Maximum timeout is 3 times the pretimeout */
	wdog_dev->max_timeout = max_pretimeout * 3;
	wdog_dev->max_hw_heartbeat_ms = max_pretimeout * 1000;
	/* Minimum first timeout (pretimeout) is 1, so min_timeout as 3 */
	wdog_dev->min_timeout = 3;
	wdog_dev->timeout = wdog_dev->pretimeout;

	watchdog_set_drvdata(wdog_dev, priv);
	platform_set_drvdata(pdev, priv);
	gti_wdt_settimeout(wdog_dev, wdog_dev->timeout);
	watchdog_stop_on_reboot(wdog_dev);
	watchdog_stop_on_unregister(wdog_dev);

	err = devm_watchdog_register_device(dev, wdog_dev);
	if (err)
		return err;

	irq = platform_get_irq(pdev, 0);
	if (irq < 0)
		return irq;

	err = devm_request_irq(dev, irq, gti_wdt_interrupt, 0,
			       pdev->name, &priv->wdev);
	if (err)
		return dev_err_probe(dev, err, "Failed to register interrupt handler\n");

	dev_info(dev, "Watchdog enabled (timeout=%d sec)\n", wdog_dev->timeout);
	return 0;
}

static const struct of_device_id gti_wdt_of_match[] = {
	{ .compatible = "marvell,cn9670-wdt", .data = &match_data_octeontx2},
	{ .compatible = "marvell,cn10624-wdt", .data = &match_data_cn10k},
	{ },
};
MODULE_DEVICE_TABLE(of, gti_wdt_of_match);

static struct platform_driver gti_wdt_driver = {
	.driver = {
		.name = "gti-wdt",
		.of_match_table = gti_wdt_of_match,
	},
	.probe = gti_wdt_probe,
};
module_platform_driver(gti_wdt_driver);

MODULE_AUTHOR("Bharat Bhushan <bbhushan2@marvell.com>");
MODULE_DESCRIPTION("Marvell GTI watchdog driver");
MODULE_LICENSE("GPL");
