// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) STMicroelectronics 2019 - All Rights Reserved
 * Authors: Benjamin Gaignard <benjamin.gaignard@st.com> for STMicroelectronics.
 *	    Pascal Paillet <p.paillet@st.com> for STMicroelectronics.
 */

#include <linux/clk.h>
#include <linux/clockchips.h>
#include <linux/interrupt.h>
#include <linux/mfd/stm32-lptimer.h>
#include <linux/module.h>
#include <linux/of_address.h>
#include <linux/of_irq.h>
#include <linux/platform_device.h>
#include <linux/pm_wakeirq.h>

#define CFGR_PSC_OFFSET		9
#define STM32_LP_RATING		1000
#define STM32_TARGET_CLKRATE	(32000 * HZ)
#define STM32_LP_MAX_PSC	7

struct stm32_lp_private {
	struct regmap *reg;
	struct clock_event_device clkevt;
	unsigned long period;
	struct device *dev;
};

static struct stm32_lp_private*
to_priv(struct clock_event_device *clkevt)
{
	return container_of(clkevt, struct stm32_lp_private, clkevt);
}

static int stm32_clkevent_lp_shutdown(struct clock_event_device *clkevt)
{
	struct stm32_lp_private *priv = to_priv(clkevt);

	regmap_write(priv->reg, STM32_LPTIM_CR, 0);
	regmap_write(priv->reg, STM32_LPTIM_IER, 0);
	/* clear pending flags */
	regmap_write(priv->reg, STM32_LPTIM_ICR, STM32_LPTIM_ARRMCF);

	return 0;
}

static int stm32_clkevent_lp_set_timer(unsigned long evt,
				       struct clock_event_device *clkevt,
				       int is_periodic)
{
	struct stm32_lp_private *priv = to_priv(clkevt);

	/* disable LPTIMER to be able to write into IER register*/
	regmap_write(priv->reg, STM32_LPTIM_CR, 0);
	/* enable ARR interrupt */
	regmap_write(priv->reg, STM32_LPTIM_IER, STM32_LPTIM_ARRMIE);
	/* enable LPTIMER to be able to write into ARR register */
	regmap_write(priv->reg, STM32_LPTIM_CR, STM32_LPTIM_ENABLE);
	/* set next event counter */
	regmap_write(priv->reg, STM32_LPTIM_ARR, evt);

	/* start counter */
	if (is_periodic)
		regmap_write(priv->reg, STM32_LPTIM_CR,
			     STM32_LPTIM_CNTSTRT | STM32_LPTIM_ENABLE);
	else
		regmap_write(priv->reg, STM32_LPTIM_CR,
			     STM32_LPTIM_SNGSTRT | STM32_LPTIM_ENABLE);

	return 0;
}

static int stm32_clkevent_lp_set_next_event(unsigned long evt,
					    struct clock_event_device *clkevt)
{
	return stm32_clkevent_lp_set_timer(evt, clkevt,
					   clockevent_state_periodic(clkevt));
}

static int stm32_clkevent_lp_set_periodic(struct clock_event_device *clkevt)
{
	struct stm32_lp_private *priv = to_priv(clkevt);

	return stm32_clkevent_lp_set_timer(priv->period, clkevt, true);
}

static int stm32_clkevent_lp_set_oneshot(struct clock_event_device *clkevt)
{
	struct stm32_lp_private *priv = to_priv(clkevt);

	return stm32_clkevent_lp_set_timer(priv->period, clkevt, false);
}

static irqreturn_t stm32_clkevent_lp_irq_handler(int irq, void *dev_id)
{
	struct clock_event_device *clkevt = (struct clock_event_device *)dev_id;
	struct stm32_lp_private *priv = to_priv(clkevt);

	regmap_write(priv->reg, STM32_LPTIM_ICR, STM32_LPTIM_ARRMCF);

	if (clkevt->event_handler)
		clkevt->event_handler(clkevt);

	return IRQ_HANDLED;
}

static void stm32_clkevent_lp_set_prescaler(struct stm32_lp_private *priv,
					    unsigned long *rate)
{
	int i;

	for (i = 0; i <= STM32_LP_MAX_PSC; i++) {
		if (DIV_ROUND_CLOSEST(*rate, 1 << i) < STM32_TARGET_CLKRATE)
			break;
	}

	regmap_write(priv->reg, STM32_LPTIM_CFGR, i << CFGR_PSC_OFFSET);

	/* Adjust rate and period given the prescaler value */
	*rate = DIV_ROUND_CLOSEST(*rate, (1 << i));
	priv->period = DIV_ROUND_UP(*rate, HZ);
}

static void stm32_clkevent_lp_init(struct stm32_lp_private *priv,
				  struct device_node *np, unsigned long rate)
{
	priv->clkevt.name = np->full_name;
	priv->clkevt.cpumask = cpu_possible_mask;
	priv->clkevt.features = CLOCK_EVT_FEAT_PERIODIC |
				CLOCK_EVT_FEAT_ONESHOT;
	priv->clkevt.set_state_shutdown = stm32_clkevent_lp_shutdown;
	priv->clkevt.set_state_periodic = stm32_clkevent_lp_set_periodic;
	priv->clkevt.set_state_oneshot = stm32_clkevent_lp_set_oneshot;
	priv->clkevt.set_next_event = stm32_clkevent_lp_set_next_event;
	priv->clkevt.rating = STM32_LP_RATING;

	clockevents_config_and_register(&priv->clkevt, rate, 0x1,
					STM32_LPTIM_MAX_ARR);
}

static int stm32_clkevent_lp_probe(struct platform_device *pdev)
{
	struct stm32_lptimer *ddata = dev_get_drvdata(pdev->dev.parent);
	struct stm32_lp_private *priv;
	unsigned long rate;
	int ret, irq;

	priv = devm_kzalloc(&pdev->dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	priv->reg = ddata->regmap;
	ret = clk_prepare_enable(ddata->clk);
	if (ret)
		return -EINVAL;

	rate = clk_get_rate(ddata->clk);
	if (!rate) {
		ret = -EINVAL;
		goto out_clk_disable;
	}

	irq = platform_get_irq(to_platform_device(pdev->dev.parent), 0);
	if (irq <= 0) {
		ret = irq;
		goto out_clk_disable;
	}

	if (of_property_read_bool(pdev->dev.parent->of_node, "wakeup-source")) {
		ret = device_init_wakeup(&pdev->dev, true);
		if (ret)
			goto out_clk_disable;

		ret = dev_pm_set_wake_irq(&pdev->dev, irq);
		if (ret)
			goto out_clk_disable;
	}

	ret = devm_request_irq(&pdev->dev, irq, stm32_clkevent_lp_irq_handler,
			       IRQF_TIMER, pdev->name, &priv->clkevt);
	if (ret)
		goto out_clk_disable;

	stm32_clkevent_lp_set_prescaler(priv, &rate);

	stm32_clkevent_lp_init(priv, pdev->dev.parent->of_node, rate);

	priv->dev = &pdev->dev;

	return 0;

out_clk_disable:
	clk_disable_unprepare(ddata->clk);
	return ret;
}

static int stm32_clkevent_lp_remove(struct platform_device *pdev)
{
	return -EBUSY; /* cannot unregister clockevent */
}

static const struct of_device_id stm32_clkevent_lp_of_match[] = {
	{ .compatible = "st,stm32-lptimer-timer", },
	{},
};
MODULE_DEVICE_TABLE(of, stm32_clkevent_lp_of_match);

static struct platform_driver stm32_clkevent_lp_driver = {
	.probe	= stm32_clkevent_lp_probe,
	.remove = stm32_clkevent_lp_remove,
	.driver	= {
		.name = "stm32-lptimer-timer",
		.of_match_table = of_match_ptr(stm32_clkevent_lp_of_match),
	},
};
module_platform_driver(stm32_clkevent_lp_driver);

MODULE_ALIAS("platform:stm32-lptimer-timer");
MODULE_DESCRIPTION("STMicroelectronics STM32 clockevent low power driver");
MODULE_LICENSE("GPL v2");
