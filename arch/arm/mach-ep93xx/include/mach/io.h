/*
 * arch/arm/mach-ep93xx/include/mach/io.h
 */

#define IO_SPACE_LIMIT		0xffffffff

#define __io(p)			((void __iomem *)(p))
#define __mem_pci(p)		(p)
