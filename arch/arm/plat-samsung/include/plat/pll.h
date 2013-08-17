/* linux/arch/arm/plat-samsung/include/plat/pll.h
 *
 * Copyright (c) 2009-2011 Samsung Electronics Co., Ltd.
 *		http://www.samsung.com/
 *
 * Copyright 2008 Openmoko, Inc.
 * Copyright 2008 Simtec Electronics
 *	Ben Dooks <ben@simtec.co.uk>
 *	http://armlinux.simtec.co.uk/
 *
 * Samsung PLL codes
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
*/

#include <asm/div64.h>

#define S3C24XX_PLL_MDIV_MASK		(0xFF)
#define S3C24XX_PLL_PDIV_MASK		(0x1F)
#define S3C24XX_PLL_SDIV_MASK		(0x3)
#define S3C24XX_PLL_MDIV_SHIFT		(12)
#define S3C24XX_PLL_PDIV_SHIFT		(4)
#define S3C24XX_PLL_SDIV_SHIFT		(0)

static inline unsigned int s3c24xx_get_pll(unsigned int pllval,
					   unsigned int baseclk)
{
	unsigned int mdiv, pdiv, sdiv;
	uint64_t fvco;

	mdiv = (pllval >> S3C24XX_PLL_MDIV_SHIFT) & S3C24XX_PLL_MDIV_MASK;
	pdiv = (pllval >> S3C24XX_PLL_PDIV_SHIFT) & S3C24XX_PLL_PDIV_MASK;
	sdiv = (pllval >> S3C24XX_PLL_SDIV_SHIFT) & S3C24XX_PLL_SDIV_MASK;

	fvco = (uint64_t)baseclk * (mdiv + 8);
	do_div(fvco, (pdiv + 2) << sdiv);

	return (unsigned int)fvco;
}

#define S3C2416_PLL_MDIV_MASK		(0x3FF)
#define S3C2416_PLL_PDIV_MASK		(0x3F)
#define S3C2416_PLL_SDIV_MASK		(0x7)
#define S3C2416_PLL_MDIV_SHIFT		(14)
#define S3C2416_PLL_PDIV_SHIFT		(5)
#define S3C2416_PLL_SDIV_SHIFT		(0)

static inline unsigned int s3c2416_get_pll(unsigned int pllval,
					   unsigned int baseclk)
{
	unsigned int mdiv, pdiv, sdiv;
	uint64_t fvco;

	mdiv = (pllval >> S3C2416_PLL_MDIV_SHIFT) & S3C2416_PLL_MDIV_MASK;
	pdiv = (pllval >> S3C2416_PLL_PDIV_SHIFT) & S3C2416_PLL_PDIV_MASK;
	sdiv = (pllval >> S3C2416_PLL_SDIV_SHIFT) & S3C2416_PLL_SDIV_MASK;

	fvco = (uint64_t)baseclk * mdiv;
	do_div(fvco, (pdiv << sdiv));

	return (unsigned int)fvco;
}

#define S3C6400_PLL_MDIV_MASK		(0x3FF)
#define S3C6400_PLL_PDIV_MASK		(0x3F)
#define S3C6400_PLL_SDIV_MASK		(0x7)
#define S3C6400_PLL_MDIV_SHIFT		(16)
#define S3C6400_PLL_PDIV_SHIFT		(8)
#define S3C6400_PLL_SDIV_SHIFT		(0)

static inline unsigned long s3c6400_get_pll(unsigned long baseclk,
					    u32 pllcon)
{
	u32 mdiv, pdiv, sdiv;
	u64 fvco = baseclk;

	mdiv = (pllcon >> S3C6400_PLL_MDIV_SHIFT) & S3C6400_PLL_MDIV_MASK;
	pdiv = (pllcon >> S3C6400_PLL_PDIV_SHIFT) & S3C6400_PLL_PDIV_MASK;
	sdiv = (pllcon >> S3C6400_PLL_SDIV_SHIFT) & S3C6400_PLL_SDIV_MASK;

	fvco *= mdiv;
	do_div(fvco, (pdiv << sdiv));

	return (unsigned long)fvco;
}

#define PLL6553X_MDIV_MASK	(0x7F)
#define PLL6553X_PDIV_MASK	(0x1F)
#define PLL6553X_SDIV_MASK	(0x3)
#define PLL6553X_KDIV_MASK	(0xFFFF)
#define PLL6553X_MDIV_SHIFT	(16)
#define PLL6553X_PDIV_SHIFT	(8)
#define PLL6553X_SDIV_SHIFT	(0)

static inline unsigned long s3c_get_pll6553x(unsigned long baseclk,
					     u32 pll_con0, u32 pll_con1)
{
	unsigned long result;
	u32 mdiv, pdiv, sdiv, kdiv;
	u64 tmp;

	mdiv = (pll_con0 >> PLL6553X_MDIV_SHIFT) & PLL6553X_MDIV_MASK;
	pdiv = (pll_con0 >> PLL6553X_PDIV_SHIFT) & PLL6553X_PDIV_MASK;
	sdiv = (pll_con0 >> PLL6553X_SDIV_SHIFT) & PLL6553X_SDIV_MASK;
	kdiv = pll_con1 & PLL6553X_KDIV_MASK;

	/*
	 * We need to multiple baseclk by mdiv (the integer part) and kdiv
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

#define PLL35XX_MDIV_MASK	(0x3FF)
#define PLL35XX_PDIV_MASK	(0x3F)
#define PLL35XX_SDIV_MASK	(0x7)
#define PLL35XX_MDIV_SHIFT	(16)
#define PLL35XX_PDIV_SHIFT	(8)
#define PLL35XX_SDIV_SHIFT	(0)

static inline unsigned long s5p_get_pll35xx(unsigned long baseclk, u32 pll_con)
{
	u32 mdiv, pdiv, sdiv;
	u64 fvco = baseclk;

	mdiv = (pll_con >> PLL35XX_MDIV_SHIFT) & PLL35XX_MDIV_MASK;
	pdiv = (pll_con >> PLL35XX_PDIV_SHIFT) & PLL35XX_PDIV_MASK;
	sdiv = (pll_con >> PLL35XX_SDIV_SHIFT) & PLL35XX_SDIV_MASK;

	fvco *= mdiv;
	do_div(fvco, (pdiv << sdiv));

	return (unsigned long)fvco;
}

#define PLL36XX_KDIV_MASK	(0xFFFF)
#define PLL36XX_MDIV_MASK	(0x1FF)
#define PLL36XX_PDIV_MASK	(0x3F)
#define PLL36XX_SDIV_MASK	(0x7)
#define PLL36XX_MDIV_SHIFT	(16)
#define PLL36XX_PDIV_SHIFT	(8)
#define PLL36XX_SDIV_SHIFT	(0)

static inline unsigned long s5p_get_pll36xx(unsigned long baseclk,
					    u32 pll_con0, u32 pll_con1)
{
	unsigned long result;
	u32 mdiv, pdiv, sdiv, kdiv;
	u64 tmp;

	mdiv = (pll_con0 >> PLL36XX_MDIV_SHIFT) & PLL36XX_MDIV_MASK;
	pdiv = (pll_con0 >> PLL36XX_PDIV_SHIFT) & PLL36XX_PDIV_MASK;
	sdiv = (pll_con0 >> PLL36XX_SDIV_SHIFT) & PLL36XX_SDIV_MASK;
	kdiv = pll_con1 & PLL36XX_KDIV_MASK;

	tmp = baseclk;

	tmp *= (mdiv << 16) + kdiv;
	do_div(tmp, (pdiv << sdiv));
	result = tmp >> 16;

	return result;
}

#define PLL45XX_MDIV_MASK	(0x3FF)
#define PLL45XX_PDIV_MASK	(0x3F)
#define PLL45XX_SDIV_MASK	(0x7)
#define PLL45XX_MDIV_SHIFT	(16)
#define PLL45XX_PDIV_SHIFT	(8)
#define PLL45XX_SDIV_SHIFT	(0)

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

/* CON0 bit-fields */
#define PLL46XX_MDIV_MASK	(0x1FF)
#define PLL46XX_PDIV_MASK	(0x3F)
#define PLL46XX_SDIV_MASK	(0x7)
#define PLL46XX_LOCKED_SHIFT	(29)
#define PLL46XX_MDIV_SHIFT	(16)
#define PLL46XX_PDIV_SHIFT	(8)
#define PLL46XX_SDIV_SHIFT	(0)

/* CON1 bit-fields */
#define PLL46XX_MRR_MASK	(0x1F)
#define PLL46XX_MFR_MASK	(0x3F)
#define PLL46XX_KDIV_MASK	(0xFFFF)
#define PLL4650C_KDIV_MASK	(0xFFF)
#define PLL46XX_MRR_SHIFT	(24)
#define PLL46XX_MFR_SHIFT	(16)
#define PLL46XX_KDIV_SHIFT	(0)

enum pll46xx_type_t {
	pll_4600,
	pll_4650,
	pll_4650c,
};

static inline unsigned long s5p_get_pll46xx(unsigned long baseclk,
					    u32 pll_con0, u32 pll_con1,
					    enum pll46xx_type_t pll_type)
{
	unsigned long result;
	u32 mdiv, pdiv, sdiv, kdiv;
	u64 tmp;

	mdiv = (pll_con0 >> PLL46XX_MDIV_SHIFT) & PLL46XX_MDIV_MASK;
	pdiv = (pll_con0 >> PLL46XX_PDIV_SHIFT) & PLL46XX_PDIV_MASK;
	sdiv = (pll_con0 >> PLL46XX_SDIV_SHIFT) & PLL46XX_SDIV_MASK;
	kdiv = pll_con1 & PLL46XX_KDIV_MASK;

	if (pll_type == pll_4650c)
		kdiv = pll_con1 & PLL4650C_KDIV_MASK;
	else
		kdiv = pll_con1 & PLL46XX_KDIV_MASK;

	tmp = baseclk;

	if (pll_type == pll_4600) {
		tmp *= (mdiv << 16) + kdiv;
		do_div(tmp, (pdiv << sdiv));
		result = tmp >> 16;
	} else {
		tmp *= (mdiv << 10) + kdiv;
		do_div(tmp, (pdiv << sdiv));
		result = tmp >> 10;
	}

	return result;
}

#define PLL90XX_MDIV_MASK	(0xFF)
#define PLL90XX_PDIV_MASK	(0x3F)
#define PLL90XX_SDIV_MASK	(0x7)
#define PLL90XX_KDIV_MASK	(0xffff)
#define PLL90XX_LOCKED_SHIFT	(29)
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

	/*
	 * We need to multiple baseclk by mdiv (the integer part) and kdiv
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

#define PLL65XX_MDIV_MASK	(0x3FF)
#define PLL65XX_PDIV_MASK	(0x3F)
#define PLL65XX_SDIV_MASK	(0x7)
#define PLL65XX_MDIV_SHIFT	(16)
#define PLL65XX_PDIV_SHIFT	(8)
#define PLL65XX_SDIV_SHIFT	(0)

static inline unsigned long s5p_get_pll65xx(unsigned long baseclk, u32 pll_con)
{
	u32 mdiv, pdiv, sdiv;
	u64 fvco = baseclk;

	mdiv = (pll_con >> PLL65XX_MDIV_SHIFT) & PLL65XX_MDIV_MASK;
	pdiv = (pll_con >> PLL65XX_PDIV_SHIFT) & PLL65XX_PDIV_MASK;
	sdiv = (pll_con >> PLL65XX_SDIV_SHIFT) & PLL65XX_SDIV_MASK;

	fvco *= mdiv;
	do_div(fvco, (pdiv << sdiv));

	return (unsigned long)fvco;
}

/*EXYNOS5410 VPLL/EPLL  PLL TYPE*/
#define PLL2650_KDIV_MASK      (0xFFFF)
#define PLL2650_MDIV_MASK      (0x3FF)
#define PLL2650_PDIV_MASK      (0x3F)
#define PLL2650_SDIV_MASK      (0x7)
#define PLL2650_MDIV_SHIFT     (16)
#define PLL2650_PDIV_SHIFT     (8)
#define PLL2650_SDIV_SHIFT     (0)

static inline unsigned long s5p_get_pll2650(unsigned long baseclk,
						u32 pll_con0, u32 pll_con1)
{
	unsigned long result;
	u32 mdiv, pdiv, sdiv, kdiv;
	u64 tmp;

	mdiv = (pll_con0 >> PLL2650_MDIV_SHIFT) & PLL2650_MDIV_MASK;
	pdiv = (pll_con0 >> PLL2650_PDIV_SHIFT) & PLL2650_PDIV_MASK;
	sdiv = (pll_con0 >> PLL2650_SDIV_SHIFT) & PLL2650_SDIV_MASK;
	kdiv = pll_con1 & PLL2650_KDIV_MASK;

	tmp = baseclk;

	tmp *= (mdiv << 16) + kdiv;
	do_div(tmp, (pdiv << sdiv));
	result = tmp >> 16;

	return result;
}

struct pll_div_data {
	u32 rate;
	u32 pdiv;
	u32 mdiv;
	u32 sdiv;
	u32 mfr;
	u32 mrr;
	u32 vsel;
};

/* For vpll  */
struct vpll_div_data {
	u32 rate;
	u32 pdiv;
	u32 mdiv;
	u32 sdiv;
	u32 k;
	u32 mfr;
	u32 mrr;
	u32 vsel;
};

/* EXYNOS5410 BPLL PLL TYPE */
#define PLL2550_MDIV_MASK	(0xFFF)
#define PLL2550_PDIV_MASK	(0x3F)
#define PLL2550_SDIV_MASK	(0x7)
#define PLL2550_LOCKED		(29)
#define PLL2550_MDIV_SHIFT	(16)
#define PLL2550_PDIV_SHIFT	(8)
#define PLL2550_SDIV_SHIFT	(0)
