// SPDX-License-Identifier: GPL-2.0
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/delay.h>

#include <asm/l2cache.h>
#include <asm/metag_isa.h>

/* If non-0, then initialise the L2 cache */
static int l2cache_init = 1;
/* If non-0, then initialise the L2 cache prefetch */
static int l2cache_init_pf = 1;

int l2c_pfenable;

static volatile u32 l2c_testdata[16] __initdata __aligned(64);

static int __init parse_l2cache(char *p)
{
	char *cp = p;

	if (get_option(&cp, &l2cache_init) != 1) {
		pr_err("Bad l2cache parameter (%s)\n", p);
		return 1;
	}
	return 0;
}
early_param("l2cache", parse_l2cache);

static int __init parse_l2cache_pf(char *p)
{
	char *cp = p;

	if (get_option(&cp, &l2cache_init_pf) != 1) {
		pr_err("Bad l2cache_pf parameter (%s)\n", p);
		return 1;
	}
	return 0;
}
early_param("l2cache_pf", parse_l2cache_pf);

static int __init meta_l2c_setup(void)
{
	/*
	 * If the L2 cache isn't even present, don't do anything, but say so in
	 * the log.
	 */
	if (!meta_l2c_is_present()) {
		pr_info("L2 Cache: Not present\n");
		return 0;
	}

	/*
	 * Check whether the line size is recognised.
	 */
	if (!meta_l2c_linesize()) {
		pr_warn_once("L2 Cache: unknown line size id (config=0x%08x)\n",
			     meta_l2c_config());
	}

	/*
	 * Initialise state.
	 */
	l2c_pfenable = _meta_l2c_pf_is_enabled();

	/*
	 * Enable the L2 cache and print to log whether it was already enabled
	 * by the bootloader.
	 */
	if (l2cache_init) {
		pr_info("L2 Cache: Enabling... ");
		if (meta_l2c_enable())
			pr_cont("already enabled\n");
		else
			pr_cont("done\n");
	} else {
		pr_info("L2 Cache: Not enabling\n");
	}

	/*
	 * Enable L2 cache prefetch.
	 */
	if (l2cache_init_pf) {
		pr_info("L2 Cache: Enabling prefetch... ");
		if (meta_l2c_pf_enable(1))
			pr_cont("already enabled\n");
		else
			pr_cont("done\n");
	} else {
		pr_info("L2 Cache: Not enabling prefetch\n");
	}

	return 0;
}
core_initcall(meta_l2c_setup);

int meta_l2c_disable(void)
{
	unsigned long flags;
	int en;

	if (!meta_l2c_is_present())
		return 1;

	/*
	 * Prevent other threads writing during the writeback, otherwise the
	 * writes will get "lost" when the L2 is disabled.
	 */
	__global_lock2(flags);
	en = meta_l2c_is_enabled();
	if (likely(en)) {
		_meta_l2c_pf_enable(0);
		wr_fence();
		_meta_l2c_purge();
		_meta_l2c_enable(0);
	}
	__global_unlock2(flags);

	return !en;
}

int meta_l2c_enable(void)
{
	unsigned long flags;
	int en;

	if (!meta_l2c_is_present())
		return 0;

	/*
	 * Init (clearing the L2) can happen while the L2 is disabled, so other
	 * threads are safe to continue executing, however we must not init the
	 * cache if it's already enabled (dirty lines would be discarded), so
	 * this operation should still be atomic with other threads.
	 */
	__global_lock1(flags);
	en = meta_l2c_is_enabled();
	if (likely(!en)) {
		_meta_l2c_init();
		_meta_l2c_enable(1);
		_meta_l2c_pf_enable(l2c_pfenable);
	}
	__global_unlock1(flags);

	return en;
}

int meta_l2c_pf_enable(int pfenable)
{
	unsigned long flags;
	int en = l2c_pfenable;

	if (!meta_l2c_is_present())
		return 0;

	/*
	 * We read modify write the enable register, so this operation must be
	 * atomic with other threads.
	 */
	__global_lock1(flags);
	en = l2c_pfenable;
	l2c_pfenable = pfenable;
	if (meta_l2c_is_enabled())
		_meta_l2c_pf_enable(pfenable);
	__global_unlock1(flags);

	return en;
}

int meta_l2c_flush(void)
{
	unsigned long flags;
	int en;

	/*
	 * Prevent other threads writing during the writeback. This also
	 * involves read modify writes.
	 */
	__global_lock2(flags);
	en = meta_l2c_is_enabled();
	if (likely(en)) {
		_meta_l2c_pf_enable(0);
		wr_fence();
		_meta_l2c_purge();
		_meta_l2c_enable(0);
		_meta_l2c_init();
		_meta_l2c_enable(1);
		_meta_l2c_pf_enable(l2c_pfenable);
	}
	__global_unlock2(flags);

	return !en;
}
