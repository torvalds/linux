#ifndef MMCIF_H
#define MMCIF_H

/**************************************************
 *
 *		board specific settings
 *
 **************************************************/

#ifdef CONFIG_MACH_AP4EVB
#include "mach/mmcif-ap4eb.h"
#elif CONFIG_MACH_MACKEREL
#include "mach/mmcif-mackerel.h"
#else
#error "unsupported board."
#endif

#endif /* MMCIF_H */
