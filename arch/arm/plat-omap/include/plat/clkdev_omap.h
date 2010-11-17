/*
 * clkdev <-> OMAP integration
 *
 * Russell King <linux@arm.linux.org.uk>
 *
 */

#ifndef __ARCH_ARM_PLAT_OMAP_INCLUDE_PLAT_CLKDEV_OMAP_H
#define __ARCH_ARM_PLAT_OMAP_INCLUDE_PLAT_CLKDEV_OMAP_H

#include <linux/clkdev.h>

struct omap_clk {
	u16				cpu;
	struct clk_lookup		lk;
};

#define CLK(dev, con, ck, cp) 		\
	{				\
		 .cpu = cp,		\
		.lk = {			\
			.dev_id = dev,	\
			.con_id = con,	\
			.clk = ck,	\
		},			\
	}

/* Platform flags for the clkdev-OMAP integration code */
#define CK_310		(1 << 0)
#define CK_7XX		(1 << 1)	/* 7xx, 850 */
#define CK_1510		(1 << 2)
#define CK_16XX		(1 << 3)	/* 16xx, 17xx, 5912 */
#define CK_242X		(1 << 4)
#define CK_243X		(1 << 5)
#define CK_3XXX		(1 << 6)	/* OMAP3 + AM3 common clocks*/
#define CK_343X		(1 << 7)	/* OMAP34xx common clocks */
#define CK_3430ES1	(1 << 8)	/* 34xxES1 only */
#define CK_3430ES2	(1 << 9)	/* 34xxES2, ES3, non-Sitara 35xx only */
#define CK_3505		(1 << 10)
#define CK_3517		(1 << 11)
#define CK_36XX		(1 << 12)	/* OMAP36xx/37xx-specific clocks */
#define CK_443X		(1 << 13)

#define CK_AM35XX	(CK_3505 | CK_3517)	/* all Sitara AM35xx */



#endif

