#ifndef DDK750_HELP_H__
#define DDK750_HELP_H__
#include "ddk750_chip.h"
#ifndef USE_INTERNAL_REGISTER_ACCESS

#include <linux/ioport.h>
#include <asm/io.h>
#include <asm/uaccess.h>
#include "sm750_help.h"


#if 0
/* if 718 big endian turned on,be aware that don't use this driver for general use,only for ppc big-endian */
#warning "big endian on target cpu and enable nature big endian support of 718 capability !"
#define PEEK32(addr)  			__raw_readl(mmio750 + addr)
#define POKE32(addr, data) 		__raw_writel(data, mmio750 + addr)
#else /* software control endianness */
#define PEEK32(addr) readl(addr + mmio750)
#define POKE32(addr, data) writel(data, addr + mmio750)
#endif

extern void __iomem *mmio750;
extern char revId750;
extern unsigned short devId750;
#else
/* implement if you want use it*/
#endif

#endif
