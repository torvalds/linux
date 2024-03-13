// SPDX-License-Identifier: GPL-2.0-only
/*
 * Export of symbols defined in assembly files and/or libgcc.
 *
 * Copyright (c) 2010-2011, The Linux Foundation. All rights reserved.
 */

#include <linux/dma-mapping.h>
#include <asm/hexagon_vm.h>
#include <asm/io.h>
#include <linux/uaccess.h>

/* Additional functions */
EXPORT_SYMBOL(__clear_user_hexagon);
EXPORT_SYMBOL(raw_copy_from_user);
EXPORT_SYMBOL(raw_copy_to_user);
EXPORT_SYMBOL(iounmap);
EXPORT_SYMBOL(__vmgetie);
EXPORT_SYMBOL(__vmsetie);
EXPORT_SYMBOL(__vmyield);
EXPORT_SYMBOL(empty_zero_page);
EXPORT_SYMBOL(ioremap);
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
DECLARE_EXPORT(__hexagon_divsi3);
DECLARE_EXPORT(__hexagon_modsi3);
DECLARE_EXPORT(__hexagon_udivsi3);
DECLARE_EXPORT(__hexagon_umodsi3);
DECLARE_EXPORT(csum_tcpudp_magic);
