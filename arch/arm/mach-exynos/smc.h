/* SPDX-License-Identifier: GPL-2.0 */
/*
 *  Copyright (c) 2012 Samsung Electronics.
 *
 * EXYNOS - SMC Call
 */

#ifndef __ASM_ARCH_EXYNOS_SMC_H
#define __ASM_ARCH_EXYNOS_SMC_H

#define SMC_CMD_INIT		(-1)
#define SMC_CMD_INFO		(-2)
/* For Power Management */
#define SMC_CMD_SLEEP		(-3)
#define SMC_CMD_CPU1BOOT	(-4)
#define SMC_CMD_CPU0AFTR	(-5)
#define SMC_CMD_SAVE		(-6)
#define SMC_CMD_SHUTDOWN	(-7)
/* For CP15 Access */
#define SMC_CMD_C15RESUME	(-11)
/* For L2 Cache Access */
#define SMC_CMD_L2X0CTRL	(-21)
#define SMC_CMD_L2X0SETUP1	(-22)
#define SMC_CMD_L2X0SETUP2	(-23)
#define SMC_CMD_L2X0INVALL	(-24)
#define SMC_CMD_L2X0DEBUG	(-25)

/* For Accessing CP15/SFR (General) */
#define SMC_CMD_REG		(-101)

/* defines for SMC_CMD_REG */
#define SMC_REG_CLASS_SFR_W	(0x1 << 30)
#define SMC_REG_ID_SFR_W(addr)	(SMC_REG_CLASS_SFR_W | ((addr) >> 2))

#ifndef __ASSEMBLY__

extern void exynos_smc(u32 cmd, u32 arg1, u32 arg2, u32 arg3);

#endif /* __ASSEMBLY__ */

/* op type for SMC_CMD_SAVE and SMC_CMD_SHUTDOWN */
#define OP_TYPE_CORE		0x0
#define OP_TYPE_CLUSTER		0x1

/* Power State required for SMC_CMD_SAVE and SMC_CMD_SHUTDOWN */
#define SMC_POWERSTATE_IDLE	0x1

#endif
