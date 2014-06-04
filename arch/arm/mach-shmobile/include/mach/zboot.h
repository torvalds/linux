#ifndef ZBOOT_H
#define ZBOOT_H

#include <mach/zboot_macros.h>

/**************************************************
 *
 *		board specific settings
 *
 **************************************************/

#ifdef CONFIG_MACH_MACKEREL
#define MEMORY_START	0x40000000
#include "mach/head-mackerel.txt"
#elif defined(CONFIG_MACH_KZM9G) || defined(CONFIG_MACH_KZM9G_REFERENCE)
#define MEMORY_START	0x43000000
#include "mach/head-kzm9g.txt"
#else
#error "unsupported board."
#endif

#endif /* ZBOOT_H */
