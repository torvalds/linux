/*
 * Copyright 2008 Freescale Semiconductor, Inc. All Rights Reserved.
 * Copyright 2008 Embedded Alley Solutions, Inc All Rights Reserved.
 */

/*
 * The code contained herein is licensed under the GNU General Public
 * License. You may obtain a copy of the GNU General Public License
 * Version 2 or later at the following locations:
 *
 * http://www.opensource.org/licenses/gpl-license.html
 * http://www.gnu.org/copyleft/gpl.html
 */
#ifndef __ASM_PLAT_PLATFORM_H
#define __ASM_PLAT_PLATFORM_H

#include <asm/sizes.h>

/* Virtual address where registers are mapped */
#define STMP3XXX_REGS_PHBASE	0x80000000
#ifdef __ASSEMBLER__
#define STMP3XXX_REGS_BASE	0xF0000000
#else
#define STMP3XXX_REGS_BASE	(void __iomem *)0xF0000000
#endif
#define STMP3XXX_REGS_SIZE	SZ_1M

/* Virtual address where OCRAM is mapped */
#define STMP3XXX_OCRAM_PHBASE	0x00000000
#ifdef __ASSEMBLER__
#define STMP3XXX_OCRAM_BASE	0xf1000000
#else
#define STMP3XXX_OCRAM_BASE	(void __iomem *)0xf1000000
#endif
#define STMP3XXX_OCRAM_SIZE	(32 * SZ_1K)

#ifdef CONFIG_ARCH_STMP37XX
#define IRQ_PRIORITY_REG_RD	HW_ICOLL_PRIORITYn_RD
#define IRQ_PRIORITY_REG_WR	HW_ICOLL_PRIORITYn_WR
#endif

#ifdef CONFIG_ARCH_STMP378X
#define IRQ_PRIORITY_REG_RD	HW_ICOLL_INTERRUPTn_RD
#define IRQ_PRIORITY_REG_WR	HW_ICOLL_INTERRUPTn_WR
#endif

#endif /* __ASM_ARCH_PLATFORM_H */
