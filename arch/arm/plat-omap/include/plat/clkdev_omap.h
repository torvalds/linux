/*
 * clkdev <-> OMAP integration
 *
 * Russell King <linux@arm.linux.org.uk>
 *
 */

#ifndef __ARCH_ARM_PLAT_OMAP_INCLUDE_PLAT_CLKDEV_OMAP_H
#define __ARCH_ARM_PLAT_OMAP_INCLUDE_PLAT_CLKDEV_OMAP_H

#include <asm/clkdev.h>

struct omap_clk {
	u32		cpu;
	struct clk_lookup lk;
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


#define CK_243X		(1 << 0)
#define CK_242X		(1 << 1)
#define CK_343X		(1 << 2)
#define CK_3430ES1	(1 << 3)
#define CK_3430ES2	(1 << 4)


#endif

