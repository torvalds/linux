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
#define MACH_MSP4200_EVAL	0	/* PMC-Sierra MSP4200 Evaluation */
#define MACH_MSP4200_GW		1	/* PMC-Sierra MSP4200 Gateway demo */
#define MACH_MSP4200_FPGA	2	/* PMC-Sierra MSP4200 Emulation */
#define MACH_MSP7120_EVAL	3	/* PMC-Sierra MSP7120 Evaluation */
#define MACH_MSP7120_GW		4	/* PMC-Sierra MSP7120 Residential GW */
#define MACH_MSP7120_FPGA	5	/* PMC-Sierra MSP7120 Emulation */
#define MACH_MSP_OTHER	      255	/* PMC-Sierra unknown board type */

/*
 * Valid machtype for group Mikrotik
 */
#define MACH_MIKROTIK_RB532	0	/* Mikrotik RouterBoard 532	*/
#define MACH_MIKROTIK_RB532A	1	/* Mikrotik RouterBoard 532A	*/

/*
 * Valid machtype for Loongson family
 */
enum loongson_machine_type {
	MACH_LOONGSON_UNKNOWN,
	MACH_LEMOTE_FL2E,
	MACH_LEMOTE_FL2F,
	MACH_LEMOTE_ML2F7,
	MACH_LEMOTE_YL2F89,
	MACH_DEXXON_GDIUM2F10,
	MACH_LEMOTE_NAS,
	MACH_LEMOTE_LL2F,
	MACH_LOONGSON_GENERIC,
	MACH_LOONGSON_END
};

/*
 * Valid machtype for group INGENIC
 */
#define  MACH_INGENIC_JZ4730	0	/* JZ4730 SOC		*/
#define  MACH_INGENIC_JZ4740	1	/* JZ4740 SOC		*/
#define  MACH_INGENIC_JZ4770	2	/* JZ4770 SOC		*/
#define  MACH_INGENIC_JZ4780	3	/* JZ4780 SOC		*/
#define  MACH_INGENIC_X1000		4	/* X1000 SOC		*/

extern char *system_type;
const char *get_system_type(void);

extern unsigned long mips_machtype;

#define BOOT_MEM_RAM		1
#define BOOT_MEM_ROM_DATA	2
#define BOOT_MEM_RESERVED	3
#define BOOT_MEM_INIT_RAM	4
#define BOOT_MEM_NOMAP		5

extern void add_memory_region(phys_addr_t start, phys_addr_t size, long type);
extern void detect_memory_region(phys_addr_t start, phys_addr_t sz_min,  phys_addr_t sz_max);

extern void prom_init(void);
extern void prom_free_prom_memory(void);
extern void prom_cleanup(void);

extern void free_init_pages(const char *what,
			    unsigned long begin, unsigned long end);

extern void (*free_init_pages_eva)(void *begin, void *end);

/*
 * Initial kernel command line, usually setup by prom_init()
 */
extern char arcs_cmdline[COMMAND_LINE_SIZE];

/*
 * Registers a0, a1, a3 and a4 as passed to the kernel entry by firmware
 */
extern unsigned long fw_arg0, fw_arg1, fw_arg2, fw_arg3;

#ifdef CONFIG_USE_OF
extern unsigned long fw_passed_dtb;
#endif

/*
 * Platform memory detection hook called by setup_arch
 */
extern void plat_mem_setup(void);

#ifdef CONFIG_SWIOTLB
/*
 * Optional platform hook to call swiotlb_setup().
 */
extern void plat_swiotlb_setup(void);

#else

static inline void plat_swiotlb_setup(void) {}

#endif /* CONFIG_SWIOTLB */

#ifdef CONFIG_USE_OF
/**
 * plat_get_fdt() - Return a pointer to the platform's device tree blob
 *
 * This function provides a platform independent API to get a pointer to the
 * flattened device tree blob. The interface between bootloader and kernel
 * is not consistent across platforms so it is necessary to provide this
 * API such that common startup code can locate the FDT.
 *
 * This is used by the KASLR code to get command line arguments and random
 * seed from the device tree. Any platform wishing to use KASLR should
 * provide this API and select SYS_SUPPORTS_RELOCATABLE.
 *
 * Return: Pointer to the flattened device tree blob.
 */
extern void *plat_get_fdt(void);

#ifdef CONFIG_RELOCATABLE

/**
 * plat_fdt_relocated() - Update platform's information about relocated dtb
 *
 * This function provides a platform-independent API to set platform's
 * information about relocated DTB if it needs to be moved due to kernel
 * relocation occurring at boot.
 */
void plat_fdt_relocated(void *new_location);

#endif /* CONFIG_RELOCATABLE */
#endif /* CONFIG_USE_OF */

#endif /* _ASM_BOOTINFO_H */
