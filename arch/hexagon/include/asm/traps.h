/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Trap support for Hexagon
 *
 * Copyright (c) 2010-2011, The Linux Foundation. All rights reserved.
 */

#ifndef _ASM_HEXAGON_TRAPS_H
#define _ASM_HEXAGON_TRAPS_H

#include <asm/registers.h>

extern int die(const char *str, struct pt_regs *regs, long err);
extern int die_if_kernel(char *str, struct pt_regs *regs, long err);

#endif /* _ASM_HEXAGON_TRAPS_H */
