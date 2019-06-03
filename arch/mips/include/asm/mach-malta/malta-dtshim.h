/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Copyright (C) 2015 Imagination Technologies
 * Author: Paul Burton <paul.burton@mips.com>
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
