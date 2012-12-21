#ifndef __MACH_MEMORY_H
#define __MACH_MEMORY_H

#include <plat/memory.h>
#include <mach/io.h>

/*
 * SRAM memory whereabouts
 */
#define SRAM_CODE_OFFSET	(RK30_IMEM_BASE + 0x0010)
#define SRAM_CODE_END		(RK30_IMEM_BASE + 0x2FFF)
#define SRAM_DATA_OFFSET	(RK30_IMEM_BASE + 0x3000)
#define SRAM_DATA_END		(RK30_IMEM_BASE + 0x3FFF - 64)

#endif
