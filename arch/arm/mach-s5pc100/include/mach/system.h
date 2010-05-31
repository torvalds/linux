/* linux/arch/arm/mach-s5pc100/include/mach/system.h
 *
 * Copyright 2009 Samsung Electronics Co.
 *      Byungho Min <bhmin@samsung.com>
 *
 * S5PC100 - system implementation
 *
 * Based on mach-s3c6400/include/mach/system.h
 */

#ifndef __ASM_ARCH_SYSTEM_H
#define __ASM_ARCH_SYSTEM_H __FILE__

#include <linux/io.h>
#include <mach/map.h>
#include <mach/regs-clock.h>

static void arch_idle(void)
{
	/* nothing here yet */
}

static void arch_reset(char mode, const char *cmd)
{
	__raw_writel(S5PC100_SWRESET_RESETVAL, S5PC100_SWRESET);
	return;
}
#endif /* __ASM_ARCH_IRQ_H */
