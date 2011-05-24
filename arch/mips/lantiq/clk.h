/*
 *  This program is free software; you can redistribute it and/or modify it
 *  under the terms of the GNU General Public License version 2 as published
 *  by the Free Software Foundation.
 *
 * Copyright (C) 2010 John Crispin <blogic@openwrt.org>
 */

#ifndef _LTQ_CLK_H__
#define _LTQ_CLK_H__

extern void clk_init(void);

extern unsigned long ltq_get_cpu_hz(void);
extern unsigned long ltq_get_fpi_hz(void);
extern unsigned long ltq_get_io_region_clock(void);

#endif
