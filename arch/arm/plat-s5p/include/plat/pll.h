/* arch/arm/plat-s5p/include/plat/pll.h
 *
 * Copyright (c) 2009 Samsung Electronics Co., Ltd.
 *		http://www.samsung.com/
 *
 * S5P PLL code
 *
 * Based on arch/arm/plat-s3c64xx/include/plat/pll.h
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
*/

#define PLL45XX_MDIV_MASK	(0x3FF)
#define PLL45XX_PDIV_MASK	(0x3F)
#define PLL45XX_SDIV_MASK	(0x7)
#define PLL45XX_MDIV_SHIFT	(16)
#define PLL45XX_PDIV_SHIFT	(8)
#define PLL45XX_SDIV_SHIFT	(0)

#include <asm/div64.h>

enum pll45xx_type_t {
	pll_4500,
	pll_4502,
	pll_4508
};

static inline unsigned long s5p_get_pll45xx(unsigned long baseclk, u32 pll_con,
					    enum pll45xx_type_t pll_type)
{
	u32 mdiv, pdiv, sdiv;
	u64 fvco = baseclk;

	mdiv = (pll_con >> PLL45XX_MDIV_SHIFT) & PLL45XX_MDIV_MASK;
	pdiv = (pll_con >> PLL45XX_PDIV_SHIFT) & PLL45XX_PDIV_MASK;
	sdiv = (pll_con >> PLL45XX_SDIV_SHIFT) & PLL45XX_SDIV_MASK;

	if (pll_type == pll_4508)
		sdiv = sdiv - 1;

	fvco *= mdiv;
	do_div(fvco, (pdiv << sdiv));

	return (unsigned long)fvco;
}

#define PLL90XX_MDIV_MASK	(0xFF)
#define PLL90XX_PDIV_MASK	(0x3F)
#define PLL90XX_SDIV_MASK	(0x7)
#define PLL90XX_KDIV_MASK	(0xffff)
#define PLL90XX_MDIV_SHIFT	(16)
#define PLL90XX_PDIV_SHIFT	(8)
#define PLL90XX_SDIV_SHIFT	(0)
#define PLL90XX_KDIV_SHIFT	(0)

static inline unsigned long s5p_get_pll90xx(unsigned long baseclk,
					    u32 pll_con, u32 pll_conk)
{
	unsigned long result;
	u32 mdiv, pdiv, sdiv, kdiv;
	u64 tmp;

	mdiv = (pll_con >> PLL90XX_MDIV_SHIFT) & PLL90XX_MDIV_MASK;
	pdiv = (pll_con >> PLL90XX_PDIV_SHIFT) & PLL90XX_PDIV_MASK;
	sdiv = (pll_con >> PLL90XX_SDIV_SHIFT) & PLL90XX_SDIV_MASK;
	kdiv = pll_conk & PLL90XX_KDIV_MASK;

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
