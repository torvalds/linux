/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2007  Maciej W. Rozycki
 */
#ifndef _ASM_BUGS_H
#define _ASM_BUGS_H

#include <linux/bug.h>
#include <linux/smp.h>

#include <asm/cpu.h>
#include <asm/cpu-info.h>

extern int daddiu_bug;

extern void check_bugs64_early(void);

extern void check_bugs32(void);
extern void check_bugs64(void);

static inline void check_bugs_early(void)
{
	if (IS_ENABLED(CONFIG_CPU_R4X00_BUGS64))
		check_bugs64_early();
}

static inline int r4k_daddiu_bug(void)
{
	if (!IS_ENABLED(CONFIG_CPU_R4X00_BUGS64))
		return 0;

	WARN_ON(daddiu_bug < 0);
	return daddiu_bug != 0;
}

#endif /* _ASM_BUGS_H */
