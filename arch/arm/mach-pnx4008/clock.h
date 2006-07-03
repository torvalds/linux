/*
 * arch/arm/mach-pnx4008/clock.h
 *
 * Clock control driver for PNX4008 - internal header file
 *
 * Author: Vitaly Wool <source@mvista.com>
 *
 * 2006 (c) MontaVista Software, Inc. This file is licensed under
 * the terms of the GNU General Public License version 2. This program
 * is licensed "as is" without any warranty of any kind, whether express
 * or implied.
 */
#ifndef __ARCH_ARM_PNX4008_CLOCK_H__
#define __ARCH_ARM_PNX4008_CLOCK_H__

struct clk {
	struct list_head node;
	struct module *owner;
	const char *name;
	struct clk *parent;
	struct clk *propagate_next;
	u32 rate;
	u32 user_rate;
	s8 usecount;
	u32 flags;
	u32 scale_reg;
	u8 enable_shift;
	u32 enable_reg;
	u8 enable_shift1;
	u32 enable_reg1;
	u32 parent_switch_reg;
	 u32(*round_rate) (struct clk *, u32);
	int (*set_rate) (struct clk *, u32);
	int (*set_parent) (struct clk * clk, struct clk * parent);
};

/* Flags */
#define RATE_PROPAGATES      (1<<0)
#define NEEDS_INITIALIZATION (1<<1)
#define PARENT_SET_RATE      (1<<2)
#define FIXED_RATE           (1<<3)

#endif
