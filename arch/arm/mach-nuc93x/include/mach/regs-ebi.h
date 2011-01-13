/*
 * arch/arm/mach-nuc93x/include/mach/regs-ebi.h
 *
 * Copyright (c) 2009 Nuvoton technology corporation.
 *
 * Wan ZongShun <mcuos.com@gmail.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation;version 2 of the License.
 *
 */

#ifndef __ASM_ARCH_REGS_EBI_H
#define __ASM_ARCH_REGS_EBI_H

/* EBI Control Registers */

#define EBI_BA		NUC93X_VA_EBI
#define REG_EBICON	(EBI_BA + 0x00)
#define REG_ROMCON	(EBI_BA + 0x04)
#define REG_SDCONF0	(EBI_BA + 0x08)
#define REG_SDCONF1	(EBI_BA + 0x0C)
#define REG_SDTIME0	(EBI_BA + 0x10)
#define REG_SDTIME1	(EBI_BA + 0x14)
#define REG_EXT0CON	(EBI_BA + 0x18)
#define REG_EXT1CON	(EBI_BA + 0x1C)
#define REG_EXT2CON	(EBI_BA + 0x20)
#define REG_EXT3CON	(EBI_BA + 0x24)
#define REG_EXT4CON	(EBI_BA + 0x28)
#define REG_CKSKEW	(EBI_BA + 0x2C)

#endif /*  __ASM_ARCH_REGS_EBI_H */
