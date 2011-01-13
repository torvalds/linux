/* linux/include/asm-arm/plat-s3c24xx/s3c2443.h
 *
 * Copyright (c) 2004-2005 Simtec Electronics
 *	Ben Dooks <ben@simtec.co.uk>
 *
 * Header file for s3c2443 cpu support
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
*/

#ifdef CONFIG_CPU_S3C2443

struct s3c2410_uartcfg;

extern  int s3c2443_init(void);

extern void s3c2443_map_io(void);

extern void s3c2443_init_uarts(struct s3c2410_uartcfg *cfg, int no);

extern void s3c2443_init_clocks(int xtal);

extern  int s3c2443_baseclk_add(void);

#else
#define s3c2443_init_clocks NULL
#define s3c2443_init_uarts NULL
#define s3c2443_map_io NULL
#define s3c2443_init NULL
#endif

/* common code used by s3c2443 and others.
 * note, not to be used outside of arch/arm/mach-s3c* */

struct clk;	/* some files don't need clk.h otherwise */

typedef unsigned int (*pll_fn)(unsigned int reg, unsigned int base);
typedef unsigned int (*fdiv_fn)(unsigned long clkcon0);

extern void s3c2443_common_setup_clocks(pll_fn get_mpll, fdiv_fn fdiv);
extern void s3c2443_common_init_clocks(int xtal, pll_fn get_mpll, fdiv_fn fdiv);

extern int s3c2443_clkcon_enable_h(struct clk *clk, int enable);
extern int s3c2443_clkcon_enable_p(struct clk *clk, int enable);
extern int s3c2443_clkcon_enable_s(struct clk *clk, int enable);

extern struct clksrc_clk clk_epllref;
extern struct clksrc_clk clk_esysclk;
extern struct clksrc_clk clk_msysclk;
