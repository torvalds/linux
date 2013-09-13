/*
 * This file describes the structure passed from the BootX application
 * (for MacOS) when it is used to boot Linux.
 *
 * Written by Benjamin Herrenschmidt.
 */

#ifndef __ASM_BOOTX_H__
#define __ASM_BOOTX_H__

#include <uapi/asm/bootx.h>

/* (*) The format of the colormap is 256 * 3 * 2 bytes. Each color index
 * is represented by 3 short words containing a 16 bits (unsigned) color
 * component. Later versions may contain the gamma table for direct-color
 * devices here.
 */
#define BOOTX_COLORTABLE_SIZE    (256UL*3UL*2UL)

/* BootX passes the device-tree using a format that comes from earlier
 * ppc32 kernels. This used to match what is in prom.h, but not anymore
 * so we now define it here
 */
struct bootx_dt_prop {
	u32	name;
	int	length;
	u32	value;
	u32	next;
};

struct bootx_dt_node {
	u32	unused0;
	u32	unused1;
	u32	phandle;	/* not really available */
	u32	unused2;
	u32	unused3;
	u32	unused4;
	u32	unused5;
	u32	full_name;
	u32	properties;
	u32	parent;
	u32	child;
	u32	sibling;
	u32	next;
	u32	allnext;
};

extern void bootx_init(unsigned long r4, unsigned long phys);

#endif
