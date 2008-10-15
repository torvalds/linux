/*
 * arch/arm/mach-ep93xx/include/mach/memory.h
 */

#ifndef __ASM_ARCH_MEMORY_H
#define __ASM_ARCH_MEMORY_H

#define PHYS_OFFSET		UL(0x00000000)

#define __bus_to_virt(x)	__phys_to_virt(x)
#define __virt_to_bus(x)	__virt_to_phys(x)


#endif
