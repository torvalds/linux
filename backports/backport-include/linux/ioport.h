#ifndef __BACKPORT_LINUX_IOPORT_H
#define __BACKPORT_LINUX_IOPORT_H
#include_next <linux/ioport.h>

#ifndef IORESOURCE_REG
#define IORESOURCE_REG		0x00000300
#endif

#endif /* __BACKPORT_LINUX_IOPORT_H */
