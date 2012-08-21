/* arch/arm/mach-rk29/include/mach/clock.h
 *
 * Copyright (C) 2011 ROCKCHIP, Inc.
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */

#ifndef __ASM_ARCH_RK30_CLOCK_H
#define __ASM_ARCH_RK30_CLOCK_H

/**
 * struct clk_notifier_data - rate data to pass to the notifier callback
 * @clk: struct clk * being changed
 * @old_rate: previous rate of this clock
 * @new_rate: new rate of this clock
 *
 * For a pre-notifier, old_rate is the clock's rate before this rate
 * change, and new_rate is what the rate will be in the future.  For a
 * post-notifier, old_rate and new_rate are both set to the clock's
 * current rate (this was done to optimize the implementation).
 */
struct clk_notifier_data {
	struct clk		*clk;
	unsigned long		old_rate;
	unsigned long		new_rate;
};

/*
 * Clk notifier callback types
 *
 * Since the notifier is called with interrupts disabled, any actions
 * taken by callbacks must be extremely fast and lightweight.
 *
 * CLK_PRE_RATE_CHANGE - called after all callbacks have approved the
 *     rate change, immediately before the clock rate is changed, to
 *     indicate that the rate change will proceed.  Drivers must
 *     immediately terminate any operations that will be affected by
 *     the rate change.  Callbacks must always return NOTIFY_DONE.
 *
 * CLK_ABORT_RATE_CHANGE: called if the rate change failed for some
 *     reason after CLK_PRE_RATE_CHANGE.  In this case, all registered
 *     notifiers on the clock will be called with
 *     CLK_ABORT_RATE_CHANGE. Callbacks must always return
 *     NOTIFY_DONE.
 *
 * CLK_POST_RATE_CHANGE - called after the clock rate change has
 *     successfully completed.  Callbacks must always return
 *     NOTIFY_DONE.
 *
 */
#define CLK_PRE_RATE_CHANGE		1
#define CLK_POST_RATE_CHANGE		2
#define CLK_ABORT_RATE_CHANGE		3

#define CLK_PRE_ENABLE			4
#define CLK_POST_ENABLE			5
#define CLK_ABORT_ENABLE		6

#define CLK_PRE_DISABLE			7
#define CLK_POST_DISABLE		8
#define CLK_ABORT_DISABLE		9

struct notifier_block;

extern int clk_notifier_register(struct clk *clk, struct notifier_block *nb);
extern int clk_notifier_unregister(struct clk *clk, struct notifier_block *nb);

#endif





