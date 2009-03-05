/* linux/arch/arm/plat-s3c/include/plat/clock.h
 *
 * Copyright (c) 2004-2005 Simtec Electronics
 *	http://www.simtec.co.uk/products/SWLINUX/
 *	Written by Ben Dooks, <ben@simtec.co.uk>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
*/

#include <linux/spinlock.h>

struct clk {
	struct list_head      list;
	struct module        *owner;
	struct clk           *parent;
	const char           *name;
	int		      id;
	int		      usage;
	unsigned long         rate;
	unsigned long         ctrlbit;

	int		    (*enable)(struct clk *, int enable);
	int		    (*set_rate)(struct clk *c, unsigned long rate);
	unsigned long	    (*get_rate)(struct clk *c);
	unsigned long	    (*round_rate)(struct clk *c, unsigned long rate);
	int		    (*set_parent)(struct clk *c, struct clk *parent);
};

/* other clocks which may be registered by board support */

extern struct clk s3c24xx_dclk0;
extern struct clk s3c24xx_dclk1;
extern struct clk s3c24xx_clkout0;
extern struct clk s3c24xx_clkout1;
extern struct clk s3c24xx_uclk;

extern struct clk clk_usb_bus;

/* core clock support */

extern struct clk clk_f;
extern struct clk clk_h;
extern struct clk clk_p;
extern struct clk clk_mpll;
extern struct clk clk_upll;
extern struct clk clk_epll;
extern struct clk clk_xtal;
extern struct clk clk_ext;

/* S3C64XX specific clocks */
extern struct clk clk_h2;
extern struct clk clk_27m;
extern struct clk clk_48m;

/* exports for arch/arm/mach-s3c2410
 *
 * Please DO NOT use these outside of arch/arm/mach-s3c2410
*/

extern spinlock_t clocks_lock;

extern int s3c2410_clkcon_enable(struct clk *clk, int enable);

extern int s3c24xx_register_clock(struct clk *clk);
extern int s3c24xx_register_clocks(struct clk **clk, int nr_clks);

extern int s3c24xx_register_baseclocks(unsigned long xtal);

extern void s3c64xx_register_clocks(void);

extern void s3c24xx_setup_clocks(unsigned long fclk,
				 unsigned long hclk,
				 unsigned long pclk);

extern void s3c2410_setup_clocks(void);
extern void s3c2412_setup_clocks(void);
extern void s3c244x_setup_clocks(void);
extern void s3c2443_setup_clocks(void);

/* S3C64XX specific functions and clocks */

extern int s3c64xx_sclk_ctrl(struct clk *clk, int enable);

/* Init for pwm clock code */

extern void s3c_pwmclk_init(void);

