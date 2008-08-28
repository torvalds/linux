/*
 * arch/arm/mach-orion5x/include/mach/memory.h
 *
 * Marvell Orion memory definitions
 */

#ifndef __ASM_ARCH_MEMORY_H
#define __ASM_ARCH_MEMORY_H

#define PHYS_OFFSET	UL(0x00000000)

#define __virt_to_bus(x)	__virt_to_phys(x)
#define __bus_to_virt(x)	__phys_to_virt(x)


#endif
