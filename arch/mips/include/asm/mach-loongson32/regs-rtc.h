/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Copyright (c) 2016 Yang Ling <gnaygnil@gmail.com>
 *
 * Loongson 1 RTC timer Register Definitions.
 */

#ifndef __ASM_MACH_LOONGSON32_REGS_RTC_H
#define __ASM_MACH_LOONGSON32_REGS_RTC_H

#define LS1X_RTC_REG(x) \
		((void __iomem *)KSEG1ADDR(LS1X_RTC_BASE + (x)))

#define LS1X_RTC_CTRL	LS1X_RTC_REG(0x40)

#define RTC_EXTCLK_OK	(BIT(5) | BIT(8))
#define RTC_EXTCLK_EN	BIT(8)

#endif /* __ASM_MACH_LOONGSON32_REGS_RTC_H */
