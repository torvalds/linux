#ifndef __MATROXFB_G450_H__
#define __MATROXFB_G450_H__

#include "matroxfb_base.h"

#ifdef CONFIG_FB_MATROX_G
void matroxfb_g450_connect(WPMINFO2);
void matroxfb_g450_shutdown(WPMINFO2);
#else
static inline void matroxfb_g450_connect(WPMINFO2) { };
static inline void matroxfb_g450_shutdown(WPMINFO2) { };
#endif

#endif /* __MATROXFB_G450_H__ */
