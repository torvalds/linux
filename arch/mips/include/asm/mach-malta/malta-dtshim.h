/*
 * Copyright (C) 2015 Imagination Technologies
 * Author: Paul Burton <paul.burton@imgtec.com>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2 of the License, or (at your
 * option) any later version.
 */

#ifndef __MIPS_MALTA_DTSHIM_H__
#define __MIPS_MALTA_DTSHIM_H__

#include <linux/init.h>

#ifdef CONFIG_MIPS_MALTA

extern void __init *malta_dt_shim(void *fdt);

#else /* !CONFIG_MIPS_MALTA */

static inline void *malta_dt_shim(void *fdt)
{
	return fdt;
}

#endif /* !CONFIG_MIPS_MALTA */

#endif /* __MIPS_MALTA_DTSHIM_H__ */
