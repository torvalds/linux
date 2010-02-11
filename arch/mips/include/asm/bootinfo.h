/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file COPYING in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 1995, 1996, 2003 by Ralf Baechle
 * Copyright (C) 1995, 1996 Andreas Busse
 * Copyright (C) 1995, 1996 Stoned Elipot
 * Copyright (C) 1995, 1996 Paul M. Antoine.
 * Copyright (C) 2009       Zhang Le
 */
#ifndef _ASM_BOOTINFO_H
#define _ASM_BOOTINFO_H

#include <linux/types.h>
#include <asm/setup.h>

/*
 * The MACH_ IDs are sort of equivalent to PCI product IDs.  As such the
 * numbers do not necessarily reflect technical relations or similarities
 * between systems.
 */

/*
 * Valid machtype values for group unknown
 */
#define  MACH_UNKNOWN		0	/* whatever...			*/

/*
 * Valid machtype for group DEC
 */
#define  MACH_DSUNKNOWN		0
#define  MACH_DS23100		1	/* DECstation 2100 or 3100	*/
#define  MACH_DS5100		2	/* DECsystem 5100		*/
#define  MACH_DS5000_200	3	/* DECstation 5000/200		*/
#define  MACH_DS5000_1XX	4	/* DECstation 5000/120, 125, 133, 150 */
#define  MACH_DS5000_XX		5	/* DECstation 5000/20, 25, 33, 50 */
#define  MACH_DS5000_2X0	6	/* DECstation 5000/240, 260	*/
#define  MACH_DS5400		7	/* DECsystem 5400		*/
#define  MACH_DS5500		8	/* DECsystem 5500		*/
#define  MACH_DS5800		9	/* DECsystem 5800		*/
#define  MACH_DS5900		10	/* DECsystem 5900		*/

/*
 * Valid machtype for group PMC-MSP
 */
#define MACH_MSP4200_EVAL       0	/* PMC-Sierra MSP4200 Evaluation */
#define MACH_MSP4200_GW         1	/* PMC-Sierra MSP4200 Gateway demo */
#define MACH_MSP4200_FPGA       2	/* PMC-Sierra MSP4200 Emulation */
#define MACH_MSP7120_EVAL       3	/* PMC-Sierra MSP7120 Evaluation */
#define MACH_MSP7120_GW         4	/* PMC-Sierra MSP7120 Residential GW */
#define MACH_MSP7120_FPGA       5	/* PMC-Sierra MSP7120 Emulation */
#define MACH_MSP_OTHER        255	/* PMC-Sierra unknown board type */

/*
 * Valid machtype for group Mikrotik
 */
#define	MACH_MIKROTIK_RB532	0	/* Mikrotik RouterBoard 532 	*/
#define MACH_MIKROTIK_RB532A	1	/* Mikrotik RouterBoard 532A 	*/

/*
 * Valid machtype for Loongson family
 */
#define MACH_LOONGSON_UNKNOWN  0
#define MACH_LEMOTE_FL2E       1
#define MACH_LEMOTE_FL2F       2
#define MACH_LEMOTE_ML2F7      3
#define MACH_LEMOTE_YL2F89     4
#define MACH_DEXXON_GDIUM2F10  5
#define MACH_LEMOTE_NAS        6
#define MACH_LEMOTE_LL2F       7
#define MACH_LOONGSON_END      8

extern char *system_type;
const char *get_system_type(void);

extern unsigned long mips_machtype;

#define BOOT_MEM_MAP_MAX	32
#define BOOT_MEM_RAM		1
#define BOOT_MEM_ROM_DATA	2
#define BOOT_MEM_RESERVED	3

/*
 * A memory map that's built upon what was determined
 * or specified on the command line.
 */
struct boot_mem_map {
	int nr_map;
	struct boot_mem_map_entry {
		phys_t addr;	/* start of memory segment */
		phys_t size;	/* size of memory segment */
		long type;		/* type of memory segment */
	} map[BOOT_MEM_MAP_MAX];
};

extern struct boot_mem_map boot_mem_map;

extern void add_memory_region(phys_t start, phys_t size, long type);

extern void prom_init(void);
extern void prom_free_prom_memory(void);

extern void free_init_pages(const char *what,
			    unsigned long begin, unsigned long end);

/*
 * Initial kernel command line, usually setup by prom_init()
 */
extern char arcs_cmdline[COMMAND_LINE_SIZE];

/*
 * Registers a0, a1, a3 and a4 as passed to the kernel entry by firmware
 */
extern unsigned long fw_arg0, fw_arg1, fw_arg2, fw_arg3;

/*
 * Platform memory detection hook called by setup_arch
 */
extern void plat_mem_setup(void);

#endif /* _ASM_BOOTINFO_H */
