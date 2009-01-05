/*
 * arch/arm/mach-shark/include/mach/hardware.h
 *
 * by Alexander Schulz
 *
 * derived from:
 * arch/arm/mach-ebsa110/include/mach/hardware.h
 * Copyright (C) 1996-1999 Russell King.
 */
#ifndef __ASM_ARCH_HARDWARE_H
#define __ASM_ARCH_HARDWARE_H

#ifndef __ASSEMBLY__

/*
 * Mapping areas
 */
#define IO_BASE			0xe0000000

#else

#define IO_BASE			0

#endif

#define IO_SIZE			0x08000000
#define IO_START		0x40000000
#define ROMCARD_SIZE		0x08000000
#define ROMCARD_START		0x10000000


/* defines for the Framebuffer */
#define FB_START		0x06000000
#define FB_SIZE			0x01000000

#define UNCACHEABLE_ADDR        0xdf010000

#define SEQUOIA_LED_GREEN       (1<<6)
#define SEQUOIA_LED_AMBER       (1<<5)
#define SEQUOIA_LED_BACK        (1<<7)

#define pcibios_assign_all_busses()     1

#define PCIBIOS_MIN_IO          0x6000
#define PCIBIOS_MIN_MEM         0x50000000
#define PCIMEM_BASE		0xe8000000

#endif

