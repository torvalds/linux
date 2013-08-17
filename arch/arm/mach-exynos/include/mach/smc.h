/*
 * Copyright (c) 2012 Samsung Electronics Co., Ltd.
 *		http://www.samsung.com
 *
 * EXYNOS - SMC Call
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
*/

#ifndef __ASM_ARCH_SMC_H
#define __ASM_ARCH_SMC_H __FILE__

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

/* MACRO for SMC_CMD_REG */
#define SMC_REG_CLASS_CP15	(0x0 << 30)
#define SMC_REG_CLASS_SFR_W	(0x1 << 30)
#define SMC_REG_CLASS_SFR_R	(0x3 << 30)
#define SMC_REG_CLASS_MASK	(0x3 << 30)
#define SMC_REG_ID_CP15(CRn, Op1, CRm, Op2) \
	   (SMC_REG_CLASS_CP15 | (CRn << 10) | (Op1 << 7) | (CRm << 3) | (Op2))
#define SMC_REG_ID_SFR_W(ADDR)	(SMC_REG_CLASS_SFR_W | ((ADDR) >> 2))
#define SMC_REG_ID_SFR_R(ADDR)	(SMC_REG_CLASS_SFR_R | ((ADDR) >> 2))

/* op type for SMC_CMD_SAVE and SMC_CMD_SHUTDOWN */
#define OP_TYPE_CORE            0x0
#define OP_TYPE_CLUSTER         0x1

/* Power State required for SMC_CMD_SAVE and SMC_CMD_SHUTDOWN */
#define SMC_POWERSTATE_SLEEP    0x0
#define SMC_POWERSTATE_IDLE     0x1
#define SMC_POWERSTATE_SWITCH   0x2

#ifndef __ASSEMBLY__
u32 exynos_smc(u32 cmd, u32 arg1, u32 arg2, u32 arg3);
u32 exynos_smc_readsfr(u32 addr, u32 *val);
#endif

#endif /* __ASM_ARCH_SMC_H */
