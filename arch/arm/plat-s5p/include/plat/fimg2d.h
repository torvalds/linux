/* linux/arch/arm/plat-s5p/include/plat/fimg2d.h
 *
 * Copyright (c) 2010 Samsung Electronics Co., Ltd.
 *	http://www.samsung.com/
 *
 * Platform Data Structure for Samsung Graphics 2D Hardware
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
*/

#ifndef __ASM_ARCH_FIMG2D_H
#define __ASM_ARCH_FIMG2D_H __FILE__

struct fimg2d_platdata {
	int hw_ver;
	const char *parent_clkname;
	const char *clkname;
	const char *gate_clkname;
	unsigned long clkrate;
};

extern void __init s5p_fimg2d_set_platdata(struct fimg2d_platdata *pd);

#endif /* __ASM_ARCH_FIMG2D_H */
