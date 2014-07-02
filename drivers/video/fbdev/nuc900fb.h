/*
 *
 * Copyright (c) 2009 Nuvoton technology corporation
 * All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 *   Author:
 *        Wang Qiang(rurality.linux@gmail.com)  2009/12/16
 */

#ifndef __NUC900FB_H
#define __NUC900FB_H

#include <mach/map.h>
#include <linux/platform_data/video-nuc900fb.h>

enum nuc900_lcddrv_type {
	LCDDRV_NUC910,
	LCDDRV_NUC930,
	LCDDRV_NUC932,
	LCDDRV_NUC950,
	LCDDRV_NUC960,
};


#define PALETTE_BUFFER_SIZE	256
#define PALETTE_BUFF_CLEAR 	(0x80000000) /* entry is clear/invalid */

struct nuc900fb_info {
	struct device		*dev;
	struct clk		*clk;

	struct resource		*mem;
	void __iomem		*io;
	void __iomem		*irq_base;
	int 			drv_type;
	struct nuc900fb_hw	regs;
	unsigned long		clk_rate;

#ifdef CONFIG_CPU_FREQ
	struct notifier_block	freq_transition;
#endif

	/* keep these registers in case we need to re-write palette */
	u32			palette_buffer[PALETTE_BUFFER_SIZE];
	u32			pseudo_pal[16];
};

int nuc900fb_init(void);

#endif /* __NUC900FB_H */
