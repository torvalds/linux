/*
 * arch/arm/mach-ep93xx/include/mach/memory.h
 */

#ifndef __ASM_ARCH_MEMORY_H
#define __ASM_ARCH_MEMORY_H

#if defined(CONFIG_EP93XX_SDCE3_SYNC_PHYS_OFFSET)
#define PHYS_OFFSET		UL(0x00000000)
#elif defined(CONFIG_EP93XX_SDCE0_PHYS_OFFSET)
#define PHYS_OFFSET		UL(0xc0000000)
#else
#error "Kconfig bug: No EP93xx PHYS_OFFSET set"
#endif

#endif
