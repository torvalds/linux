/*
 * linux/arch/arm/mach-at91/clock.h
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/clkdev.h>

#define CLK_TYPE_PRIMARY	0x1
#define CLK_TYPE_PLL		0x2
#define CLK_TYPE_PROGRAMMABLE	0x4
#define CLK_TYPE_PERIPHERAL	0x8
#define CLK_TYPE_SYSTEM		0x10


struct clk {
	struct list_head node;
	const char	*name;		/* unique clock name */
	struct clk_lookup cl;
	unsigned long	rate_hz;
	struct clk	*parent;
	u32		pmc_mask;
	void		(*mode)(struct clk *, int);
	unsigned	id:3;		/* PCK0..4, or 32k/main/a/b */
	unsigned	type;		/* clock type */
	u16		users;
};


extern int __init clk_register(struct clk *clk);
extern struct clk mck;
extern struct clk utmi_clk;

#define CLKDEV_CON_ID(_id, _clk)			\
	{						\
		.con_id = _id,				\
		.clk = _clk,				\
	}

#define CLKDEV_CON_DEV_ID(_con_id, _dev_id, _clk)	\
	{						\
		.con_id = _con_id,			\
		.dev_id = _dev_id,			\
		.clk = _clk,				\
	}
