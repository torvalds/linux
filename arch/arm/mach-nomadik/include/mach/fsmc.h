
/* Definitions for the Nomadik FSMC "Flexible Static Memory controller" */

#ifndef __ASM_ARCH_FSMC_H
#define __ASM_ARCH_FSMC_H

#include <mach/hardware.h>
/*
 * Register list
 */

/* bus control reg. and bus timing reg. for CS0..CS3 */
#define FSMC_BCR(x)     (NOMADIK_FSMC_VA + (x << 3))
#define FSMC_BTR(x)     (NOMADIK_FSMC_VA + (x << 3) + 0x04)

/* PC-card and NAND:
 * PCR = control register
 * PMEM = memory timing
 * PATT = attribute timing
 * PIO = I/O timing
 * PECCR = ECC result
 */
#define FSMC_PCR(x)     (NOMADIK_FSMC_VA + ((2 + x) << 5) + 0x00)
#define FSMC_PMEM(x)    (NOMADIK_FSMC_VA + ((2 + x) << 5) + 0x08)
#define FSMC_PATT(x)    (NOMADIK_FSMC_VA + ((2 + x) << 5) + 0x0c)
#define FSMC_PIO(x)     (NOMADIK_FSMC_VA + ((2 + x) << 5) + 0x10)
#define FSMC_PECCR(x)   (NOMADIK_FSMC_VA + ((2 + x) << 5) + 0x14)

#endif /* __ASM_ARCH_FSMC_H */
