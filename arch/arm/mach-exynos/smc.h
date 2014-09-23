/*
 *  Copyright (c) 2012 Samsung Electronics.
 *
 * EXYNOS - SMC Call
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef __ASM_ARCH_EXYNOS_SMC_H
#define __ASM_ARCH_EXYNOS_SMC_H

#define SMC_CMD_INIT		(-1)
#define SMC_CMD_INFO		(-2)
/* For Power Management */
#define SMC_CMD_SLEEP		(-3)
#define SMC_CMD_CPU1BOOT	(-4)
#define SMC_CMD_CPU0AFTR	(-5)
/* For CP15 Access */
#define SMC_CMD_C15RESUME	(-11)
/* For L2 Cache Access */
#define SMC_CMD_L2X0CTRL	(-21)
#define SMC_CMD_L2X0SETUP1	(-22)
#define SMC_CMD_L2X0SETUP2	(-23)
#define SMC_CMD_L2X0INVALL	(-24)
#define SMC_CMD_L2X0DEBUG	(-25)

#ifndef __ASSEMBLY__

extern void exynos_smc(u32 cmd, u32 arg1, u32 arg2, u32 arg3);

#endif /* __ASSEMBLY__ */

#endif
