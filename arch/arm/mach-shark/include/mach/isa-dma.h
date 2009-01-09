/*
 * arch/arm/mach-shark/include/mach/isa-dma.h
 *
 * by Alexander Schulz
 */
#ifndef __ASM_ARCH_DMA_H
#define __ASM_ARCH_DMA_H

/* Use only the lowest 4MB, nothing else works.
 * The rest is not DMAable. See dev /  .properties
 * in OpenFirmware.
 */
#define MAX_DMA_CHANNELS	8
#define DMA_ISA_CASCADE         4

#endif /* _ASM_ARCH_DMA_H */

