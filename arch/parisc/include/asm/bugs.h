/* SPDX-License-Identifier: GPL-2.0 */
/*
 *  include/asm-parisc/s.h
 *
 *  Copyright (C) 1999	Mike Shaver
 */

/*
 * This is included by init/main.c to check for architecture-dependent s.
 *
 * Needs:
 *	void check_s(void);
 */

#include <asm/processor.h>

static inline void check_s(void)
{
//	identify_cpu(&boot_cpu_data);
}
