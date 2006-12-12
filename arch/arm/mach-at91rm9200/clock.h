/*
 * linux/arch/arm/mach-at91rm9200/clock.h
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#define CLK_TYPE_PRIMARY	0x1
#define CLK_TYPE_PLL		0x2
#define CLK_TYPE_PROGRAMMABLE	0x4
#define CLK_TYPE_PERIPHERAL	0x8
#define CLK_TYPE_SYSTEM		0x10


struct clk {
	struct list_head node;
	const char	*name;		/* unique clock name */
	const char	*function;	/* function of the clock */
	struct device	*dev;		/* device associated with function */
	unsigned long	rate_hz;
	struct clk	*parent;
	u32		pmc_mask;
	void		(*mode)(struct clk *, int);
	unsigned	id:2;		/* PCK0..3, or 32k/main/a/b */
	unsigned	type;		/* clock type */
	u16		users;
};


extern int __init clk_register(struct clk *clk);
