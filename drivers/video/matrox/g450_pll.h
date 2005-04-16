#ifndef __G450_PLL_H__
#define __G450_PLL_H__

#include "matroxfb_base.h"

int matroxfb_g450_setclk(WPMINFO unsigned int fout, unsigned int pll);
unsigned int g450_mnp2f(CPMINFO unsigned int mnp);
void matroxfb_g450_setpll_cond(WPMINFO unsigned int mnp, unsigned int pll);

#endif	/* __G450_PLL_H__ */
