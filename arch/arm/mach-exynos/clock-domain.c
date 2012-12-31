/* linux/arch/arm/mach-exynos/clock-domain.c
 *
 * Copyright (c) 2010-2011 Samsung Electronics Co., Ltd.
 *		http://www.samsung.com
 *
 * EXYNOS - Clock Domain support
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
*/

#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/err.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/cpufreq.h>
#include <linux/list.h>
#include <linux/rculist.h>
#include <linux/rcupdate.h>
#include <linux/device.h>

#include <plat/clock.h>

#include <mach/clock-domain.h>

static LIST_HEAD(clock_domain_list);
/* Lock to allow exclusive modification to the clock domain list */
static DEFINE_MUTEX(clock_domain_list_lock);

static struct clock_domain *find_clock_domain(unsigned int flag)
{
	struct clock_domain *tmp_domain, *domain = ERR_PTR(-ENODEV);

	list_for_each_entry_rcu(tmp_domain, &clock_domain_list, node) {
		if (tmp_domain->flag & flag) {
			domain = tmp_domain;
			break;
		}
	}

	return domain;
}

int clock_domain_enabled(unsigned int flag)
{
	struct clock_domain *domain;
	struct clock *clock;

	domain = find_clock_domain(flag);
	if (IS_ERR(domain)) {
		pr_err("Unable to find Clock Domain\n");
		return -EINVAL;
	}

	list_for_each_entry_rcu(clock, &domain->domain_list, node) {
		if (clock->clk->usage)
			return -EINVAL;
	}

	return 0;
}

int clock_add_domain(unsigned int flag, struct clk *clk)
{
	struct clock_domain *domain = NULL;
	struct clock *clock;

	/* allocate new clock node */
	clock = kzalloc(sizeof(struct clock), GFP_KERNEL);
	if (!clock) {
		pr_err("Unable to create new Clock node\n");
		return -ENOMEM;
	}

	/* Hold our list modification lock here */
	mutex_lock(&clock_domain_list_lock);

	/* Check for existing list for 'dev' */
	domain = find_clock_domain(flag);
	if (IS_ERR(domain)) {
		/*
		 * Allocate a new Clock Doamin.*/
		domain = kzalloc(sizeof(struct clock_domain), GFP_KERNEL);
		if (!domain) {
			mutex_unlock(&clock_domain_list_lock);
			kfree(clock);
			pr_err("Unable to create Clock Domain structure\n");
			return -ENOMEM;
		}

		domain->flag = flag;
		INIT_LIST_HEAD(&domain->domain_list);

		list_add_rcu(&domain->node, &clock_domain_list);
	}

	clock->clk = clk;

	list_add_rcu(&clock->node, &domain->domain_list);
	mutex_unlock(&clock_domain_list_lock);

	return 0;
}
