/*
 * Copyright (c) 2017, Linaro Ltd.  All rights reserved.
 *
 * Author: Daniel Lezcano <daniel.lezcano@linaro.org>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */
#include <linux/clk.h>
#include <linux/interrupt.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/of_irq.h>
#include <linux/slab.h>

#include "timer-of.h"

static __init void timer_irq_exit(struct of_timer_irq *of_irq)
{
	struct timer_of *to = container_of(of_irq, struct timer_of, of_irq);

	struct clock_event_device *clkevt = &to->clkevt;

	of_irq->percpu ? free_percpu_irq(of_irq->irq, clkevt) :
		free_irq(of_irq->irq, clkevt);
}

static __init int timer_irq_init(struct device_node *np,
				 struct of_timer_irq *of_irq)
{
	int ret;
	struct timer_of *to = container_of(of_irq, struct timer_of, of_irq);
	struct clock_event_device *clkevt = &to->clkevt;

	if (of_irq->name) {
		of_irq->irq = ret = of_irq_get_byname(np, of_irq->name);
		if (ret < 0) {
			pr_err("Failed to get interrupt %s for %s\n",
			       of_irq->name, np->full_name);
			return ret;
		}
	} else	{
		of_irq->irq = irq_of_parse_and_map(np, of_irq->index);
	}
	if (!of_irq->irq) {
		pr_err("Failed to map interrupt for %pOF\n", np);
		return -EINVAL;
	}

	ret = of_irq->percpu ?
		request_percpu_irq(of_irq->irq, of_irq->handler,
				   np->full_name, clkevt) :
		request_irq(of_irq->irq, of_irq->handler,
			    of_irq->flags ? of_irq->flags : IRQF_TIMER,
			    np->full_name, clkevt);
	if (ret) {
		pr_err("Failed to request irq %d for %pOF\n", of_irq->irq, np);
		return ret;
	}

	clkevt->irq = of_irq->irq;

	return 0;
}

static __init void timer_clk_exit(struct of_timer_clk *of_clk)
{
	of_clk->rate = 0;
	clk_disable_unprepare(of_clk->clk);
	clk_put(of_clk->clk);
}

static __init int timer_clk_init(struct device_node *np,
				 struct of_timer_clk *of_clk)
{
	int ret;

	of_clk->clk = of_clk->name ? of_clk_get_by_name(np, of_clk->name) :
		of_clk_get(np, of_clk->index);
	if (IS_ERR(of_clk->clk)) {
		pr_err("Failed to get clock for %pOF\n", np);
		return PTR_ERR(of_clk->clk);
	}

	ret = clk_prepare_enable(of_clk->clk);
	if (ret) {
		pr_err("Failed for enable clock for %pOF\n", np);
		goto out_clk_put;
	}

	of_clk->rate = clk_get_rate(of_clk->clk);
	if (!of_clk->rate) {
		ret = -EINVAL;
		pr_err("Failed to get clock rate for %pOF\n", np);
		goto out_clk_disable;
	}

	of_clk->period = DIV_ROUND_UP(of_clk->rate, HZ);
out:
	return ret;

out_clk_disable:
	clk_disable_unprepare(of_clk->clk);
out_clk_put:
	clk_put(of_clk->clk);

	goto out;
}

static __init void timer_base_exit(struct of_timer_base *of_base)
{
	iounmap(of_base->base);
}

static __init int timer_base_init(struct device_node *np,
				  struct of_timer_base *of_base)
{
	const char *name = of_base->name ? of_base->name : np->full_name;

	of_base->base = of_io_request_and_map(np, of_base->index, name);
	if (IS_ERR(of_base->base)) {
		pr_err("Failed to iomap (%s)\n", name);
		return PTR_ERR(of_base->base);
	}

	return 0;
}

int __init timer_of_init(struct device_node *np, struct timer_of *to)
{
	int ret = -EINVAL;
	int flags = 0;

	if (to->flags & TIMER_OF_BASE) {
		ret = timer_base_init(np, &to->of_base);
		if (ret)
			goto out_fail;
		flags |= TIMER_OF_BASE;
	}

	if (to->flags & TIMER_OF_CLOCK) {
		ret = timer_clk_init(np, &to->of_clk);
		if (ret)
			goto out_fail;
		flags |= TIMER_OF_CLOCK;
	}

	if (to->flags & TIMER_OF_IRQ) {
		ret = timer_irq_init(np, &to->of_irq);
		if (ret)
			goto out_fail;
		flags |= TIMER_OF_IRQ;
	}

	if (!to->clkevt.name)
		to->clkevt.name = np->name;
	return ret;

out_fail:
	if (flags & TIMER_OF_IRQ)
		timer_irq_exit(&to->of_irq);

	if (flags & TIMER_OF_CLOCK)
		timer_clk_exit(&to->of_clk);

	if (flags & TIMER_OF_BASE)
		timer_base_exit(&to->of_base);
	return ret;
}

/**
 * timer_of_cleanup - release timer_of ressources
 * @to: timer_of structure
 *
 * Release the ressources that has been used in timer_of_init().
 * This function should be called in init error cases
 */
void __init timer_of_cleanup(struct timer_of *to)
{
	if (to->flags & TIMER_OF_IRQ)
		timer_irq_exit(&to->of_irq);

	if (to->flags & TIMER_OF_CLOCK)
		timer_clk_exit(&to->of_clk);

	if (to->flags & TIMER_OF_BASE)
		timer_base_exit(&to->of_base);
}
