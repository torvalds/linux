/*
 * Copyright (C) 2016 Imagination Technologies
 * Author: Paul Burton <paul.burton@imgtec.com>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2 of the License, or (at your
 * option) any later version.
 */

#ifndef __MIPS_SEAD3_DTSHIM_H__
#define __MIPS_SEAD3_DTSHIM_H__

#include <linux/init.h>

#ifdef CONFIG_MIPS_SEAD3

extern void __init *sead3_dt_shim(void *fdt);

#else /* !CONFIG_MIPS_SEAD3 */

static inline void *sead3_dt_shim(void *fdt)
{
	return fdt;
}

#endif /* !CONFIG_MIPS_SEAD3 */

#endif /* __MIPS_SEAD3_DTSHIM_H__ */
