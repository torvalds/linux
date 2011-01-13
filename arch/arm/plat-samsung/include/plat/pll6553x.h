/* arch/arm/plat-samsung/include/plat/pll6553x.h
 *	partially from arch/arm/mach-s3c64xx/include/mach/pll.h
 *
 * Copyright 2008 Openmoko, Inc.
 * Copyright 2008 Simtec Electronics
 *	Ben Dooks <ben@simtec.co.uk>
 *	http://armlinux.simtec.co.uk/
 *
 * Samsung PLL6553x PLL code
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
*/

/* S3C6400 and compatible (S3C2416, etc.) EPLL code */

#define PLL6553X_MDIV_MASK	((1 << (23-16)) - 1)
#define PLL6553X_PDIV_MASK	((1 << (13-8)) - 1)
#define PLL6553X_SDIV_MASK	((1 << (2-0)) - 1)
#define PLL6553X_MDIV_SHIFT	(16)
#define PLL6553X_PDIV_SHIFT	(8)
#define PLL6553X_SDIV_SHIFT	(0)
#define PLL6553X_KDIV_MASK	(0xffff)

static inline unsigned long s3c_get_pll6553x(unsigned long baseclk,
					     u32 pll0, u32 pll1)
{
	unsigned long result;
	u32 mdiv, pdiv, sdiv, kdiv;
	u64 tmp;

	mdiv = (pll0 >> PLL6553X_MDIV_SHIFT) & PLL6553X_MDIV_MASK;
	pdiv = (pll0 >> PLL6553X_PDIV_SHIFT) & PLL6553X_PDIV_MASK;
	sdiv = (pll0 >> PLL6553X_SDIV_SHIFT) & PLL6553X_SDIV_MASK;
	kdiv = pll1 & PLL6553X_KDIV_MASK;

	/* We need to multiple baseclk by mdiv (the integer part) and kdiv
	 * which is in 2^16ths, so shift mdiv up (does not overflow) and
	 * add kdiv before multiplying. The use of tmp is to avoid any
	 * overflows before shifting bac down into result when multipling
	 * by the mdiv and kdiv pair.
	 */

	tmp = baseclk;
	tmp *= (mdiv << 16) + kdiv;
	do_div(tmp, (pdiv << sdiv));
	result = tmp >> 16;

	return result;
}
