// SPDX-License-Identifier: GPL-2.0
/*
 * JZ47xx SoCs TCU IRQ driver
 * Copyright (C) 2019 Paul Cercueil <paul@crapouillou.net>
 */

#include <linux/bitops.h>
#include <linux/clk.h>
#include <linux/clockchips.h>
#include <linux/clocksource.h>
#include <linux/interrupt.h>
#include <linux/mfd/ingenic-tcu.h>
#include <linux/mfd/syscon.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/of_irq.h>
#include <linux/of_platform.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>
#include <linux/sched_clock.h>

#include <dt-bindings/clock/ingenic,tcu.h>

struct ingenic_soc_info {
	unsigned int num_channels;
};

struct ingenic_tcu {
	struct regmap *map;
	struct clk *timer_clk, *cs_clk;
	unsigned int timer_channel, cs_channel;
	struct clock_event_device cevt;
	struct clocksource cs;
	char name[4];
	unsigned long pwm_channels_mask;
};

static struct ingenic_tcu *ingenic_tcu;

static u64 notrace ingenic_tcu_timer_read(void)
{
	struct ingenic_tcu *tcu = ingenic_tcu;
	unsigned int count;

	regmap_read(tcu->map, TCU_REG_TCNTc(tcu->cs_channel), &count);

	return count;
}

static u64 notrace ingenic_tcu_timer_cs_read(struct clocksource *cs)
{
	return ingenic_tcu_timer_read();
}

static inline struct ingenic_tcu *to_ingenic_tcu(struct clock_event_device *evt)
{
	return container_of(evt, struct ingenic_tcu, cevt);
}

static int ingenic_tcu_cevt_set_state_shutdown(struct clock_event_device *evt)
{
	struct ingenic_tcu *tcu = to_ingenic_tcu(evt);

	regmap_write(tcu->map, TCU_REG_TECR, BIT(tcu->timer_channel));

	return 0;
}

static int ingenic_tcu_cevt_set_next(unsigned long next,
				     struct clock_event_device *evt)
{
	struct ingenic_tcu *tcu = to_ingenic_tcu(evt);

	if (next > 0xffff)
		return -EINVAL;

	regmap_write(tcu->map, TCU_REG_TDFRc(tcu->timer_channel), next);
	regmap_write(tcu->map, TCU_REG_TCNTc(tcu->timer_channel), 0);
	regmap_write(tcu->map, TCU_REG_TESR, BIT(tcu->timer_channel));

	return 0;
}

static irqreturn_t ingenic_tcu_cevt_cb(int irq, void *dev_id)
{
	struct clock_event_device *evt = dev_id;
	struct ingenic_tcu *tcu = to_ingenic_tcu(evt);

	regmap_write(tcu->map, TCU_REG_TECR, BIT(tcu->timer_channel));

	if (evt->event_handler)
		evt->event_handler(evt);

	return IRQ_HANDLED;
}

static struct clk * __init ingenic_tcu_get_clock(struct device_node *np, int id)
{
	struct of_phandle_args args;

	args.np = np;
	args.args_count = 1;
	args.args[0] = id;

	return of_clk_get_from_provider(&args);
}

static int __init ingenic_tcu_timer_init(struct device_node *np,
					 struct ingenic_tcu *tcu)
{
	unsigned int timer_virq, channel = tcu->timer_channel;
	struct irq_domain *domain;
	unsigned long rate;
	int err;

	tcu->timer_clk = ingenic_tcu_get_clock(np, channel);
	if (IS_ERR(tcu->timer_clk))
		return PTR_ERR(tcu->timer_clk);

	err = clk_prepare_enable(tcu->timer_clk);
	if (err)
		goto err_clk_put;

	rate = clk_get_rate(tcu->timer_clk);
	if (!rate) {
		err = -EINVAL;
		goto err_clk_disable;
	}

	domain = irq_find_host(np);
	if (!domain) {
		err = -ENODEV;
		goto err_clk_disable;
	}

	timer_virq = irq_create_mapping(domain, channel);
	if (!timer_virq) {
		err = -EINVAL;
		goto err_clk_disable;
	}

	snprintf(tcu->name, sizeof(tcu->name), "TCU");

	err = request_irq(timer_virq, ingenic_tcu_cevt_cb, IRQF_TIMER,
			  tcu->name, &tcu->cevt);
	if (err)
		goto err_irq_dispose_mapping;

	tcu->cevt.cpumask = cpumask_of(smp_processor_id());
	tcu->cevt.features = CLOCK_EVT_FEAT_ONESHOT;
	tcu->cevt.name = tcu->name;
	tcu->cevt.rating = 200;
	tcu->cevt.set_state_shutdown = ingenic_tcu_cevt_set_state_shutdown;
	tcu->cevt.set_next_event = ingenic_tcu_cevt_set_next;

	clockevents_config_and_register(&tcu->cevt, rate, 10, 0xffff);

	return 0;

err_irq_dispose_mapping:
	irq_dispose_mapping(timer_virq);
err_clk_disable:
	clk_disable_unprepare(tcu->timer_clk);
err_clk_put:
	clk_put(tcu->timer_clk);
	return err;
}

static int __init ingenic_tcu_clocksource_init(struct device_node *np,
					       struct ingenic_tcu *tcu)
{
	unsigned int channel = tcu->cs_channel;
	struct clocksource *cs = &tcu->cs;
	unsigned long rate;
	int err;

	tcu->cs_clk = ingenic_tcu_get_clock(np, channel);
	if (IS_ERR(tcu->cs_clk))
		return PTR_ERR(tcu->cs_clk);

	err = clk_prepare_enable(tcu->cs_clk);
	if (err)
		goto err_clk_put;

	rate = clk_get_rate(tcu->cs_clk);
	if (!rate) {
		err = -EINVAL;
		goto err_clk_disable;
	}

	/* Reset channel */
	regmap_update_bits(tcu->map, TCU_REG_TCSRc(channel),
			   0xffff & ~TCU_TCSR_RESERVED_BITS, 0);

	/* Reset counter */
	regmap_write(tcu->map, TCU_REG_TDFRc(channel), 0xffff);
	regmap_write(tcu->map, TCU_REG_TCNTc(channel), 0);

	/* Enable channel */
	regmap_write(tcu->map, TCU_REG_TESR, BIT(channel));

	cs->name = "ingenic-timer";
	cs->rating = 200;
	cs->flags = CLOCK_SOURCE_IS_CONTINUOUS;
	cs->mask = CLOCKSOURCE_MASK(16);
	cs->read = ingenic_tcu_timer_cs_read;

	err = clocksource_register_hz(cs, rate);
	if (err)
		goto err_clk_disable;

	return 0;

err_clk_disable:
	clk_disable_unprepare(tcu->cs_clk);
err_clk_put:
	clk_put(tcu->cs_clk);
	return err;
}

static const struct ingenic_soc_info jz4740_soc_info = {
	.num_channels = 8,
};

static const struct ingenic_soc_info jz4725b_soc_info = {
	.num_channels = 6,
};

static const struct of_device_id ingenic_tcu_of_match[] = {
	{ .compatible = "ingenic,jz4740-tcu", .data = &jz4740_soc_info, },
	{ .compatible = "ingenic,jz4725b-tcu", .data = &jz4725b_soc_info, },
	{ .compatible = "ingenic,jz4770-tcu", .data = &jz4740_soc_info, },
	{ .compatible = "ingenic,x1000-tcu", .data = &jz4740_soc_info, },
	{ /* sentinel */ }
};

static int __init ingenic_tcu_init(struct device_node *np)
{
	const struct of_device_id *id = of_match_node(ingenic_tcu_of_match, np);
	const struct ingenic_soc_info *soc_info = id->data;
	struct ingenic_tcu *tcu;
	struct regmap *map;
	long rate;
	int ret;

	of_node_clear_flag(np, OF_POPULATED);

	map = device_node_to_regmap(np);
	if (IS_ERR(map))
		return PTR_ERR(map);

	tcu = kzalloc(sizeof(*tcu), GFP_KERNEL);
	if (!tcu)
		return -ENOMEM;

	/* Enable all TCU channels for PWM use by default except channels 0/1 */
	tcu->pwm_channels_mask = GENMASK(soc_info->num_channels - 1, 2);
	of_property_read_u32(np, "ingenic,pwm-channels-mask",
			     (u32 *)&tcu->pwm_channels_mask);

	/* Verify that we have at least two free channels */
	if (hweight8(tcu->pwm_channels_mask) > soc_info->num_channels - 2) {
		pr_crit("%s: Invalid PWM channel mask: 0x%02lx\n", __func__,
			tcu->pwm_channels_mask);
		ret = -EINVAL;
		goto err_free_ingenic_tcu;
	}

	tcu->map = map;
	ingenic_tcu = tcu;

	tcu->timer_channel = find_first_zero_bit(&tcu->pwm_channels_mask,
						 soc_info->num_channels);
	tcu->cs_channel = find_next_zero_bit(&tcu->pwm_channels_mask,
					     soc_info->num_channels,
					     tcu->timer_channel + 1);

	ret = ingenic_tcu_clocksource_init(np, tcu);
	if (ret) {
		pr_crit("%s: Unable to init clocksource: %d\n", __func__, ret);
		goto err_free_ingenic_tcu;
	}

	ret = ingenic_tcu_timer_init(np, tcu);
	if (ret)
		goto err_tcu_clocksource_cleanup;

	/* Register the sched_clock at the end as there's no way to undo it */
	rate = clk_get_rate(tcu->cs_clk);
	sched_clock_register(ingenic_tcu_timer_read, 16, rate);

	return 0;

err_tcu_clocksource_cleanup:
	clocksource_unregister(&tcu->cs);
	clk_disable_unprepare(tcu->cs_clk);
	clk_put(tcu->cs_clk);
err_free_ingenic_tcu:
	kfree(tcu);
	return ret;
}

TIMER_OF_DECLARE(jz4740_tcu_intc,  "ingenic,jz4740-tcu",  ingenic_tcu_init);
TIMER_OF_DECLARE(jz4725b_tcu_intc, "ingenic,jz4725b-tcu", ingenic_tcu_init);
TIMER_OF_DECLARE(jz4770_tcu_intc,  "ingenic,jz4770-tcu",  ingenic_tcu_init);
TIMER_OF_DECLARE(x1000_tcu_intc,  "ingenic,x1000-tcu",  ingenic_tcu_init);

static int __init ingenic_tcu_probe(struct platform_device *pdev)
{
	platform_set_drvdata(pdev, ingenic_tcu);

	return 0;
}

static int __maybe_unused ingenic_tcu_suspend(struct device *dev)
{
	struct ingenic_tcu *tcu = dev_get_drvdata(dev);

	clk_disable(tcu->cs_clk);
	clk_disable(tcu->timer_clk);
	return 0;
}

static int __maybe_unused ingenic_tcu_resume(struct device *dev)
{
	struct ingenic_tcu *tcu = dev_get_drvdata(dev);
	int ret;

	ret = clk_enable(tcu->timer_clk);
	if (ret)
		return ret;

	ret = clk_enable(tcu->cs_clk);
	if (ret) {
		clk_disable(tcu->timer_clk);
		return ret;
	}

	return 0;
}

static const struct dev_pm_ops __maybe_unused ingenic_tcu_pm_ops = {
	/* _noirq: We want the TCU clocks to be gated last / ungated first */
	.suspend_noirq = ingenic_tcu_suspend,
	.resume_noirq  = ingenic_tcu_resume,
};

static struct platform_driver ingenic_tcu_driver = {
	.driver = {
		.name	= "ingenic-tcu-timer",
#ifdef CONFIG_PM_SLEEP
		.pm	= &ingenic_tcu_pm_ops,
#endif
		.of_match_table = ingenic_tcu_of_match,
	},
};
builtin_platform_driver_probe(ingenic_tcu_driver, ingenic_tcu_probe);
