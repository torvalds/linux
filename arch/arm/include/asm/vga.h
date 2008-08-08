#ifndef ASMARM_VGA_H
#define ASMARM_VGA_H

#include <mach/hardware.h>
#include <asm/io.h>

#define VGA_MAP_MEM(x,s)	(PCIMEM_BASE + (x))

#define vga_readb(x)	(*((volatile unsigned char *)x))
#define vga_writeb(x,y)	(*((volatile unsigned char *)y) = (x))

#endif
