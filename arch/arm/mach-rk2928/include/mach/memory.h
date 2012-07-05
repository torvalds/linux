#ifndef __MACH_MEMORY_H
#define __MACH_MEMORY_H

#include <mach/io.h>

/*
 * SRAM memory whereabouts
 */
#define SRAM_CODE_OFFSET	(RK2928_IMEM_BASE + 0x0000)
#define SRAM_CODE_END		(RK2928_IMEM_BASE + 0x0FFF)
#define SRAM_DATA_OFFSET	(RK2928_IMEM_BASE + 0x1000)
#define SRAM_DATA_END		(RK2928_IMEM_BASE + 0x1FFF)

#endif
