#ifndef DDK750_HELP_H__
#define DDK750_HELP_H__
#include "ddk750_chip.h"
#ifndef USE_INTERNAL_REGISTER_ACCESS

#include <linux/ioport.h>
#include <asm/io.h>
#include <asm/uaccess.h>
#include "sm750_help.h"

/* software control endianness */
#define PEEK32(addr) readl(addr + mmio750)
#define POKE32(addr, data) writel(data, addr + mmio750)

extern void __iomem *mmio750;
extern char revId750;
extern unsigned short devId750;
#else
/* implement if you want use it*/
#endif

#endif
