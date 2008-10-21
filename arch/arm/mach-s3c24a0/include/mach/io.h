/* arch/arm/mach-s3c24a0/include/mach/io.h
 *
 * Copyright 2008 Simtec Electronics
 *	Ben Dooks <ben-linux@fluff.org>
 *
 * IO access and mapping routines for the S3C24A0
 */

#ifndef __ASM_ARM_ARCH_IO_H
#define __ASM_ARM_ARCH_IO_H

/* No current ISA/PCI bus support. */
#define __io(a)		((void __iomem *)(a))
#define __mem_pci(a)	(a)

#endif
