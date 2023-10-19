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

#endif
