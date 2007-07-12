/*
 * File:         arch/blackfin/mach-common/cacheinit.S
 * Based on:
 * Author:       LG Soft India
 *
 * Created:      ?
 * Description:  cache initialization
 *
 * Modified:
 *               Copyright 2004-2006 Analog Devices Inc.
 *
 * Bugs:         Enter bugs at http://blackfin.uclinux.org/
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, see the file COPYING, or write
 * to the Free Software Foundation, Inc.,
 * 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 */

/* This function sets up the data and instruction cache. The
 * tables like icplb table, dcplb table and Page Descriptor table
 * are defined in cplbtab.h. You can configure those tables for
 * your suitable requirements
 */

#include <linux/linkage.h>
#include <asm/blackfin.h>

.text

#ifdef ANOMALY_05000125
#if defined(CONFIG_BLKFIN_CACHE)
ENTRY(_bfin_write_IMEM_CONTROL)

	/* Enable Instruction Cache */
	P0.l = (IMEM_CONTROL & 0xFFFF);
	P0.h = (IMEM_CONTROL >> 16);

	/* Anomaly 05000125 */
	CLI R1;
	SSYNC;		/* SSYNC required before writing to IMEM_CONTROL. */
	.align 8;
	[P0] = R0;
	SSYNC;
	STI R1;
	RTS;

ENDPROC(_bfin_write_IMEM_CONTROL)
#endif

#if defined(CONFIG_BLKFIN_DCACHE)
ENTRY(_bfin_write_DMEM_CONTROL)
	CLI R1;
	SSYNC;		/* SSYNC required before writing to DMEM_CONTROL. */
	.align 8;
	[P0] = R0;
	SSYNC;
	STI R1;
	RTS;

ENDPROC(_bfin_write_DMEM_CONTROL)
#endif

#endif
