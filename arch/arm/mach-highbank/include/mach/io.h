#ifndef __MACH_IO_H
#define __MACH_IO_H

#define __io(a)		({ (void)(a); __typesafe_io(0); })
#define __mem_pci(a)	(a)

#endif
