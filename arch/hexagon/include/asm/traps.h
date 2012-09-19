/*
 * Trap support for Hexagon
 *
 * Copyright (c) 2010-2011, The Linux Foundation. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 and
 * only version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
 * 02110-1301, USA.
 */

#ifndef _ASM_HEXAGON_TRAPS_H
#define _ASM_HEXAGON_TRAPS_H

#include <asm/registers.h>

extern int die(const char *str, struct pt_regs *regs, long err);
extern int die_if_kernel(char *str, struct pt_regs *regs, long err);

#endif /* _ASM_HEXAGON_TRAPS_H */
