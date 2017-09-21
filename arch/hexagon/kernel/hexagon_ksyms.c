/*
 * Export of symbols defined in assembly files and/or libgcc.
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

#include <linux/dma-mapping.h>
#include <asm/hexagon_vm.h>
#include <asm/io.h>
#include <linux/uaccess.h>

/* Additional functions */
EXPORT_SYMBOL(__clear_user_hexagon);
EXPORT_SYMBOL(raw_copy_from_user);
EXPORT_SYMBOL(raw_copy_to_user);
EXPORT_SYMBOL(__iounmap);
EXPORT_SYMBOL(__strnlen_user);
EXPORT_SYMBOL(__vmgetie);
EXPORT_SYMBOL(__vmsetie);
EXPORT_SYMBOL(__vmyield);
EXPORT_SYMBOL(empty_zero_page);
EXPORT_SYMBOL(ioremap_nocache);
EXPORT_SYMBOL(memcpy);
EXPORT_SYMBOL(memset);

/* Additional variables */
EXPORT_SYMBOL(__phys_offset);
EXPORT_SYMBOL(_dflt_cache_att);

#define DECLARE_EXPORT(name)     \
	extern void name(void); EXPORT_SYMBOL(name)

/* Symbols found in libgcc that assorted kernel modules need */
DECLARE_EXPORT(__hexagon_memcpy_likely_aligned_min32bytes_mult8bytes);

/* Additional functions */
DECLARE_EXPORT(__divsi3);
DECLARE_EXPORT(__modsi3);
DECLARE_EXPORT(__udivsi3);
DECLARE_EXPORT(__umodsi3);
DECLARE_EXPORT(csum_tcpudp_magic);
