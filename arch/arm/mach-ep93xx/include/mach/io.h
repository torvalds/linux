/*
 * arch/arm/mach-ep93xx/include/mach/io.h
 */

#define IO_SPACE_LIMIT		0xffffffff

#define __io(p)		__typesafe_io(p)
#define __mem_pci(p)	(p)
