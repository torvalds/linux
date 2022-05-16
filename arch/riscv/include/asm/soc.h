/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Copyright (C) 2020 Western Digital Corporation or its affiliates.
 * Copyright (C) 2020 Google, Inc
 */

#ifndef _ASM_RISCV_SOC_H
#define _ASM_RISCV_SOC_H

#include <linux/of.h>
#include <linux/linkage.h>
#include <linux/types.h>

#define SOC_EARLY_INIT_DECLARE(name, compat, fn)			\
	static const struct of_device_id __soc_early_init__##name	\
		__used __section("__soc_early_init_table")		\
		 = { .compatible = compat, .data = fn  }

void soc_early_init(void);

extern unsigned long __soc_early_init_table_start;
extern unsigned long __soc_early_init_table_end;

/*
 * Allows Linux to provide a device tree, which is necessary for SOCs that
 * don't provide a useful one on their own.
 */
struct soc_builtin_dtb {
	unsigned long vendor_id;
	unsigned long arch_id;
	unsigned long imp_id;
	void *(*dtb_func)(void);
};

/*
 * The argument name must specify a valid DTS file name without the dts
 * extension.
 */
#define SOC_BUILTIN_DTB_DECLARE(name, vendor, arch, impl)		\
	extern void *__dtb_##name##_begin;				\
									\
	static __init __used						\
	void *__soc_builtin_dtb_f__##name(void)				\
	{								\
		return (void *)&__dtb_##name##_begin;			\
	}								\
									\
	static const struct soc_builtin_dtb __soc_builtin_dtb__##name	\
		__used __section("__soc_builtin_dtb_table") =		\
	{								\
		.vendor_id = vendor,					\
		.arch_id   = arch,					\
		.imp_id    = impl,					\
		.dtb_func  = __soc_builtin_dtb_f__##name,		\
	}

extern unsigned long __soc_builtin_dtb_table_start;
extern unsigned long __soc_builtin_dtb_table_end;

void *soc_lookup_builtin_dtb(void);

#endif
