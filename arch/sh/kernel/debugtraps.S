/*
 * arch/sh/kernel/debugtraps.S
 *
 * Debug trap jump tables for SuperH
 *
 *  Copyright (C) 2006  Paul Mundt
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 */
#include <linux/sys.h>
#include <linux/linkage.h>

#if !defined(CONFIG_SH_KGDB)
#define kgdb_handle_exception	debug_trap_handler
#endif

#if !defined(CONFIG_SH_STANDARD_BIOS)
#define sh_bios_handler		debug_trap_handler
#endif

	.data

ENTRY(debug_trap_table)
	.long debug_trap_handler	/* 0x30 */
	.long debug_trap_handler	/* 0x31 */
	.long debug_trap_handler	/* 0x32 */
	.long debug_trap_handler	/* 0x33 */
	.long debug_trap_handler	/* 0x34 */
	.long debug_trap_handler	/* 0x35 */
	.long debug_trap_handler	/* 0x36 */
	.long debug_trap_handler	/* 0x37 */
	.long debug_trap_handler	/* 0x38 */
	.long debug_trap_handler	/* 0x39 */
	.long debug_trap_handler	/* 0x3a */
	.long debug_trap_handler	/* 0x3b */
	.long kgdb_handle_exception	/* 0x3c */
	.long debug_trap_handler	/* 0x3d */
	.long bug_trap_handler		/* 0x3e */
	.long sh_bios_handler		/* 0x3f */
