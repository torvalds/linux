/*
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License, version 2, as
 * published by the Free Software Foundation.
 *
 * Copyright (C) 2015 Naveen N. Rao, IBM Corporation
 */

#ifndef _ASM_PPC_TRACE_CLOCK_H
#define _ASM_PPC_TRACE_CLOCK_H

#include <linux/compiler.h>
#include <linux/types.h>

extern u64 notrace trace_clock_ppc_tb(void);

#define ARCH_TRACE_CLOCKS { trace_clock_ppc_tb, "ppc-tb", 0 },

#endif  /* _ASM_PPC_TRACE_CLOCK_H */
