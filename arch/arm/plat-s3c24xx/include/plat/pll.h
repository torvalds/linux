/* linux/arch/arm/plat-s3c24xx/include/plat/pll.h
 *
 * Copyright 2008 Simtec Electronics
 *      Ben Dooks <ben@simtec.co.uk>
 *      http://armlinux.simtec.co.uk/
 *
 * S3C24xx - common pll registers and code
 */

#define S3C24XX_PLLCON_MDIVSHIFT	12
#define S3C24XX_PLLCON_PDIVSHIFT	4
#define S3C24XX_PLLCON_SDIVSHIFT	0
#define S3C24XX_PLLCON_MDIVMASK		((1<<(1+(19-12)))-1)
#define S3C24XX_PLLCON_PDIVMASK		((1<<5)-1)
#define S3C24XX_PLLCON_SDIVMASK		3

#include <asm/div64.h>

static inline unsigned int
s3c24xx_get_pll(unsigned int pllval, unsigned int baseclk)
{
	unsigned int mdiv, pdiv, sdiv;
	uint64_t fvco;

	mdiv = pllval >> S3C24XX_PLLCON_MDIVSHIFT;
	pdiv = pllval >> S3C24XX_PLLCON_PDIVSHIFT;
	sdiv = pllval >> S3C24XX_PLLCON_SDIVSHIFT;

	mdiv &= S3C24XX_PLLCON_MDIVMASK;
	pdiv &= S3C24XX_PLLCON_PDIVMASK;
	sdiv &= S3C24XX_PLLCON_SDIVMASK;

	fvco = (uint64_t)baseclk * (mdiv + 8);
	do_div(fvco, (pdiv + 2) << sdiv);

	return (unsigned int)fvco;
}

#define S3C2416_PLL_M_SHIFT	(14)
#define S3C2416_PLL_P_SHIFT	(5)
#define S3C2416_PLL_S_MASK	(7)
#define S3C2416_PLL_M_MASK	((1 << 10) - 1)
#define S3C2416_PLL_P_MASK	(63)

static inline unsigned int
s3c2416_get_pll(unsigned int pllval, unsigned int baseclk)
{
	unsigned int m, p, s;
	uint64_t fvco;

	m = pllval >> S3C2416_PLL_M_SHIFT;
	p = pllval >> S3C2416_PLL_P_SHIFT;

	s = pllval & S3C2416_PLL_S_MASK;
	m &= S3C2416_PLL_M_MASK;
	p &= S3C2416_PLL_P_MASK;

	fvco = (uint64_t)baseclk * m;
	do_div(fvco, (p << s));

	return (unsigned int)fvco;
}
