#ifndef __ASM_MACH_REGS_RTC_H
#define __ASM_MACH_REGS_RTC_H

#include <mach/addr-map.h>

#define RTC_VIRT_BASE	(APB_VIRT_BASE + 0x10000)
#define RTC_REG(x)	(*((volatile u32 __iomem *)(RTC_VIRT_BASE + (x))))

/*
 * Real Time Clock
 */

#define RCNR		RTC_REG(0x00)	/* RTC Count Register */
#define RTAR		RTC_REG(0x04)	/* RTC Alarm Register */
#define RTSR		RTC_REG(0x08)	/* RTC Status Register */
#define RTTR		RTC_REG(0x0C)	/* RTC Timer Trim Register */

#define RTSR_HZE	(1 << 3)	/* HZ interrupt enable */
#define RTSR_ALE	(1 << 2)	/* RTC alarm interrupt enable */
#define RTSR_HZ		(1 << 1)	/* HZ rising-edge detected */
#define RTSR_AL		(1 << 0)	/* RTC alarm detected */

#endif /* __ASM_MACH_REGS_RTC_H */
