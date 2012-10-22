#ifndef _OMAP_DEBUG_DEVICES_H
#define _OMAP_DEBUG_DEVICES_H

#include <linux/types.h>

/* for TI reference platforms sharing the same debug card */
extern int debug_card_init(u32 addr, unsigned gpio);

#endif
