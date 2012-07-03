#ifndef __PLAT_IO_H
#define __PLAT_IO_H

#define IO_SPACE_LIMIT	0xffffffff

#define __io(a)		__typesafe_io(a)
#define __mem_pci(a)	(a)

#ifdef __ASSEMBLER__
#define IOMEM(x)	(x)
#else
#define IOMEM(x)	((void __force __iomem *)(x))
#endif

#endif
