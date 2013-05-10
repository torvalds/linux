#ifndef ASMARM_VGA_H
#define ASMARM_VGA_H

#include <linux/io.h>
#include <mach/hardware.h>

#define VGA_MAP_MEM(x,s)	(PCIMEM_BASE + (x))

#define vga_readb(x)	(*((volatile unsigned char *)x))
#define vga_writeb(x,y)	(*((volatile unsigned char *)y) = (x))

#endif
