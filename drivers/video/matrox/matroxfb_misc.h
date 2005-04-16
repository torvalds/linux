#ifndef __MATROXFB_MISC_H__
#define __MATROXFB_MISC_H__

#include "matroxfb_base.h"

/* also for modules */
int matroxfb_PLL_calcclock(const struct matrox_pll_features* pll, unsigned int freq, unsigned int fmax,
	unsigned int* in, unsigned int* feed, unsigned int* post);
static inline int PLL_calcclock(CPMINFO unsigned int freq, unsigned int fmax,
		unsigned int* in, unsigned int* feed, unsigned int* post) {
	return matroxfb_PLL_calcclock(&ACCESS_FBINFO(features.pll), freq, fmax, in, feed, post);
}

int matroxfb_vgaHWinit(WPMINFO struct my_timming* m);
void matroxfb_vgaHWrestore(WPMINFO2);
void matroxfb_read_pins(WPMINFO2);

#endif	/* __MATROXFB_MISC_H__ */
