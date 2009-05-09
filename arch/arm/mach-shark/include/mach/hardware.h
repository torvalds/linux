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

#define UNCACHEABLE_ADDR        0xdf010000

#define pcibios_assign_all_busses()     1

#define PCIBIOS_MIN_IO          0x6000
#define PCIBIOS_MIN_MEM         0x50000000
#define PCIMEM_BASE		0xe8000000

#endif

