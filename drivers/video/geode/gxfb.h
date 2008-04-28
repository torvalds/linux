/*
 * Copyright (C) 2008 Andres Salomon <dilinger@debian.org>
 *
 * Geode GX2 register tables
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */
#ifndef _GXFB_H_
#define _GXFB_H_

#include <linux/io.h>

static inline uint32_t read_dc(struct geodefb_par *par, int reg)
{
	return readl(par->dc_regs + reg);
}

static inline void write_dc(struct geodefb_par *par, int reg, uint32_t val)
{
	writel(val, par->dc_regs + reg);

}

static inline uint32_t read_vp(struct geodefb_par *par, int reg)
{
	return readl(par->vid_regs + reg);
}

static inline void write_vp(struct geodefb_par *par, int reg, uint32_t val)
{
	writel(val, par->vid_regs + reg);
}

static inline uint32_t read_fp(struct geodefb_par *par, int reg)
{
	return readl(par->vid_regs + reg);
}

static inline void write_fp(struct geodefb_par *par, int reg, uint32_t val)
{
	writel(val, par->vid_regs + reg);
}

#endif
