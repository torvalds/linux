// SPDX-License-Identifier: GPL-2.0-only
/*
 *
 * Copyright (C) 2010 Thomas Langer <thomas.langer@lantiq.com>
 * Copyright (C) 2010 John Crispin <john@phrozen.org>
 */
#include <linux/io.h>
#include <linux/export.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/clk.h>
#include <linux/clkdev.h>
#include <linux/err.h>
#include <linux/list.h>

#include <asm/time.h>
#include <asm/irq.h>
#include <asm/div64.h>

#include <lantiq_soc.h>

#include "clk.h"
#include "prom.h"

/* lantiq socs have 3 static clocks */
static struct clk cpu_clk_generic[4];

void clkdev_add_static(unsigned long cpu, unsigned long fpi,
			unsigned long io, unsigned long ppe)
{
	cpu_clk_generic[0].rate = cpu;
	cpu_clk_generic[1].rate = fpi;
	cpu_clk_generic[2].rate = io;
	cpu_clk_generic[3].rate = ppe;
}

struct clk *clk_get_cpu(void)
{
	return &cpu_clk_generic[0];
}

struct clk *clk_get_fpi(void)
{
	return &cpu_clk_generic[1];
}
EXPORT_SYMBOL_GPL(clk_get_fpi);

struct clk *clk_get_io(void)
{
	return &cpu_clk_generic[2];
}

struct clk *clk_get_ppe(void)
{
	return &cpu_clk_generic[3];
}
EXPORT_SYMBOL_GPL(clk_get_ppe);

static inline int clk_good(struct clk *clk)
{
	return clk && !IS_ERR(clk);
}

unsigned long clk_get_rate(struct clk *clk)
{
	if (unlikely(!clk_good(clk)))
		return 0;

	if (clk->rate != 0)
		return clk->rate;

	if (clk->get_rate != NULL)
		return clk->get_rate();

	return 0;
}
EXPORT_SYMBOL(clk_get_rate);

int clk_set_rate(struct clk *clk, unsigned long rate)
{
	if (unlikely(!clk_good(clk)))
		return 0;
	if (clk->rates && *clk->rates) {
		unsigned long *r = clk->rates;

		while (*r && (*r != rate))
			r++;
		if (!*r) {
			pr_err("clk %s.%s: trying to set invalid rate %ld\n",
				clk->cl.dev_id, clk->cl.con_id, rate);
			return -1;
		}
	}
	clk->rate = rate;
	return 0;
}
EXPORT_SYMBOL(clk_set_rate);

long clk_round_rate(struct clk *clk, unsigned long rate)
{
	if (unlikely(!clk_good(clk)))
		return 0;
	if (clk->rates && *clk->rates) {
		unsigned long *r = clk->rates;

		while (*r && (*r != rate))
			r++;
		if (!*r) {
			return clk->rate;
		}
	}
	return rate;
}
EXPORT_SYMBOL(clk_round_rate);

int clk_enable(struct clk *clk)
{
	if (unlikely(!clk_good(clk)))
		return -1;

	if (clk->enable)
		return clk->enable(clk);

	return -1;
}
EXPORT_SYMBOL(clk_enable);

void clk_disable(struct clk *clk)
{
	if (unlikely(!clk_good(clk)))
		return;

	if (clk->disable)
		clk->disable(clk);
}
EXPORT_SYMBOL(clk_disable);

int clk_activate(struct clk *clk)
{
	if (unlikely(!clk_good(clk)))
		return -1;

	if (clk->activate)
		return clk->activate(clk);

	return -1;
}
EXPORT_SYMBOL(clk_activate);

void clk_deactivate(struct clk *clk)
{
	if (unlikely(!clk_good(clk)))
		return;

	if (clk->deactivate)
		clk->deactivate(clk);
}
EXPORT_SYMBOL(clk_deactivate);

struct clk *clk_get_parent(struct clk *clk)
{
	return NULL;
}
EXPORT_SYMBOL(clk_get_parent);

static inline u32 get_counter_resolution(void)
{
	u32 res;

	__asm__ __volatile__(
		".set	push\n"
		".set	mips32r2\n"
		"rdhwr	%0, $3\n"
		".set pop\n"
		: "=&r" (res)
		: /* no input */
		: "memory");

	return res;
}

void __init plat_time_init(void)
{
	struct clk *clk;

	ltq_soc_init();

	clk = clk_get_cpu();
	mips_hpt_frequency = clk_get_rate(clk) / get_counter_resolution();
	write_c0_compare(read_c0_count());
	pr_info("CPU Clock: %ldMHz\n", clk_get_rate(clk) / 1000000);
	clk_put(clk);
}
