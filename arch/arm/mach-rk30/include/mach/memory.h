#ifndef __MACH_MEMORY_H
#define __MACH_MEMORY_H

/*
 * Physical DRAM offset.
 */
#define PLAT_PHYS_OFFSET	UL(0x60000000)

/*
 * SRAM memory whereabouts
 */
#define SRAM_CODE_OFFSET	0xFEF00100
#define SRAM_CODE_END		0xFEF02FFF
#define SRAM_DATA_OFFSET	0xFEF03000
#define SRAM_DATA_END		0xFEF03FFF

#endif
