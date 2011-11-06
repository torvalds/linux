/*
 * Export of symbols defined in assembly files and/or libgcc.
 *
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

#include <asm/hexagon_vm.h>
#include <asm/uaccess.h>

EXPORT_SYMBOL(__copy_from_user_hexagon);
EXPORT_SYMBOL(__copy_to_user_hexagon);
EXPORT_SYMBOL(__vmgetie);
EXPORT_SYMBOL(__vmsetie);
EXPORT_SYMBOL(memcpy);
EXPORT_SYMBOL(memset);

#define DECLARE_EXPORT(name)     \
	extern void name(void); EXPORT_SYMBOL(name)

/* Symbols found in libgcc that assorted kernel modules need */
DECLARE_EXPORT(__hexagon_memcpy_likely_aligned_min32bytes_mult8bytes);

DECLARE_EXPORT(__hexagon_divsi3);
DECLARE_EXPORT(__hexagon_modsi3);
DECLARE_EXPORT(__hexagon_udivsi3);
DECLARE_EXPORT(__hexagon_umodsi3);
