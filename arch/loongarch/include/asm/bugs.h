/* SPDX-License-Identifier: GPL-2.0 */
/*
 * This is included by init/main.c to check for architecture-dependent bugs.
 *
 * Copyright (C) 2020-2022 Loongson Technology Corporation Limited
 */
#ifndef _ASM_BUGS_H
#define _ASM_BUGS_H

#include <asm/cpu.h>
#include <asm/cpu-info.h>

extern void check_bugs(void);

#endif /* _ASM_BUGS_H */
