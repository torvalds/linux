/*
 * Copyright (c) 2010-2011, Code Aurora Forum. All rights reserved.
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

#ifndef _ASM_MMU_H
#define _ASM_MMU_H

#include <asm/vdso.h>

/*
 * Architecture-specific state for a mm_struct.
 * For the Hexagon Virtual Machine, it can be a copy
 * of the pointer to the page table base.
 */
struct mm_context {
	unsigned long long generation;
	unsigned long ptbase;
	struct hexagon_vdso *vdso;
};

typedef struct mm_context mm_context_t;

#endif
