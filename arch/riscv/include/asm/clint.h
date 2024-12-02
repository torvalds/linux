/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2020 Google, Inc
 */

#ifndef _ASM_RISCV_CLINT_H
#define _ASM_RISCV_CLINT_H

#include <linux/types.h>
#include <asm/mmio.h>

#ifdef CONFIG_RISCV_M_MODE
/*
 * This lives in the CLINT driver, but is accessed directly by timex.h to avoid
 * any overhead when accessing the MMIO timer.
 *
 * The ISA defines mtime as a 64-bit memory-mapped register that increments at
 * a constant frequency, but it doesn't define some other constraints we depend
 * on (most notably ordering constraints, but also some simpler stuff like the
 * memory layout).  Thus, this is called "clint_time_val" instead of something
 * like "riscv_mtime", to signify that these non-ISA assumptions must hold.
 */
extern u64 __iomem *clint_time_val;
#endif

#endif
