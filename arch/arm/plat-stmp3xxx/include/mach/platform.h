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

#ifndef __ASSEMBLER__
#include <linux/io.h>
#endif
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

#define HW_STMP3XXX_SET		0x04
#define HW_STMP3XXX_CLR		0x08
#define HW_STMP3XXX_TOG		0x0c

#ifndef __ASSEMBLER__
static inline void stmp3xxx_clearl(u32 v, void __iomem *r)
{
	__raw_writel(v, r + HW_STMP3XXX_CLR);
}

static inline void stmp3xxx_setl(u32 v, void __iomem *r)
{
	__raw_writel(v, r + HW_STMP3XXX_SET);
}
#endif

#define BF(value, field) (((value) << BP_##field) & BM_##field)

#endif /* __ASM_ARCH_PLATFORM_H */
