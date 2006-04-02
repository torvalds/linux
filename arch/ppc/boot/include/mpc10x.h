/*
 * Common defines for the Motorola SPS MPC106/8240/107 Host bridge/Mem
 * ctrl/EPIC/etc.
 *
 * Author: Tom Rini <trini@mvista.com>
 *
 * This is a heavily stripped down version of:
 * include/asm-ppc/mpc10x.h
 *
 * Author: Mark A. Greer
 *         mgreer@mvista.com
 *
 * 2001-2002 (c) MontaVista, Software, Inc.  This file is licensed under
 * the terms of the GNU General Public License version 2.  This program
 * is licensed "as is" without any warranty of any kind, whether express
 * or implied.
 */
#ifndef __BOOT_MPC10X_H__
#define __BOOT_MPC10X_H__

/*
 * The values here don't completely map everything but should work in most
 * cases.
 *
 * MAP A (PReP Map)
 *   Processor: 0x80000000 - 0x807fffff -> PCI I/O: 0x00000000 - 0x007fffff
 *   Processor: 0xc0000000 - 0xdfffffff -> PCI MEM: 0x00000000 - 0x1fffffff
 *   PCI MEM:   0x80000000 -> Processor System Memory: 0x00000000
 *   EUMB mapped to: ioremap_base - 0x00100000 (ioremap_base - 1 MB)
 *
 * MAP B (CHRP Map)
 *   Processor: 0xfe000000 - 0xfebfffff -> PCI I/O: 0x00000000 - 0x00bfffff
 *   Processor: 0x80000000 - 0xbfffffff -> PCI MEM: 0x80000000 - 0xbfffffff
 *   PCI MEM:   0x00000000 -> Processor System Memory: 0x00000000
 *   EUMB mapped to: ioremap_base - 0x00100000 (ioremap_base - 1 MB)
 */

/* Define the type of map to use */
#define	MPC10X_MEM_MAP_A		1
#define	MPC10X_MEM_MAP_B		2

/* Map A (PReP Map) Defines */
#define	MPC10X_MAPA_CNFG_ADDR		0x80000cf8
#define	MPC10X_MAPA_CNFG_DATA		0x80000cfc

/* Map B (CHRP Map) Defines */
#define	MPC10X_MAPB_CNFG_ADDR		0xfec00000
#define	MPC10X_MAPB_CNFG_DATA		0xfee00000

/* Define offsets for the memory controller registers in the config space */
#define MPC10X_MCTLR_MEM_START_1	0x80	/* Banks 0-3 */
#define MPC10X_MCTLR_MEM_START_2	0x84	/* Banks 4-7 */
#define MPC10X_MCTLR_EXT_MEM_START_1	0x88	/* Banks 0-3 */
#define MPC10X_MCTLR_EXT_MEM_START_2	0x8c	/* Banks 4-7 */

#define MPC10X_MCTLR_MEM_END_1		0x90	/* Banks 0-3 */
#define MPC10X_MCTLR_MEM_END_2		0x94	/* Banks 4-7 */
#define MPC10X_MCTLR_EXT_MEM_END_1	0x98	/* Banks 0-3 */
#define MPC10X_MCTLR_EXT_MEM_END_2	0x9c	/* Banks 4-7 */

#define MPC10X_MCTLR_MEM_BANK_ENABLES	0xa0

#endif	/* __BOOT_MPC10X_H__ */
