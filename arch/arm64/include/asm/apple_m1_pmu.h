// SPDX-License-Identifier: GPL-2.0

#ifndef __ASM_APPLE_M1_PMU_h
#define __ASM_APPLE_M1_PMU_h

#include <linux/bits.h>
#include <asm/sysreg.h>

/* Counters */
#define SYS_IMP_APL_PMC0_EL1	sys_reg(3, 2, 15, 0, 0)
#define SYS_IMP_APL_PMC1_EL1	sys_reg(3, 2, 15, 1, 0)
#define SYS_IMP_APL_PMC2_EL1	sys_reg(3, 2, 15, 2, 0)
#define SYS_IMP_APL_PMC3_EL1	sys_reg(3, 2, 15, 3, 0)
#define SYS_IMP_APL_PMC4_EL1	sys_reg(3, 2, 15, 4, 0)
#define SYS_IMP_APL_PMC5_EL1	sys_reg(3, 2, 15, 5, 0)
#define SYS_IMP_APL_PMC6_EL1	sys_reg(3, 2, 15, 6, 0)
#define SYS_IMP_APL_PMC7_EL1	sys_reg(3, 2, 15, 7, 0)
#define SYS_IMP_APL_PMC8_EL1	sys_reg(3, 2, 15, 9, 0)
#define SYS_IMP_APL_PMC9_EL1	sys_reg(3, 2, 15, 10, 0)

/* Core PMC control register */
#define SYS_IMP_APL_PMCR0_EL1	sys_reg(3, 1, 15, 0, 0)
#define PMCR0_CNT_ENABLE_0_7	GENMASK(7, 0)
#define PMCR0_IMODE		GENMASK(10, 8)
#define PMCR0_IMODE_OFF		0
#define PMCR0_IMODE_PMI		1
#define PMCR0_IMODE_AIC		2
#define PMCR0_IMODE_HALT	3
#define PMCR0_IMODE_FIQ		4
#define PMCR0_IACT		BIT(11)
#define PMCR0_PMI_ENABLE_0_7	GENMASK(19, 12)
#define PMCR0_STOP_CNT_ON_PMI	BIT(20)
#define PMCR0_CNT_GLOB_L2C_EVT	BIT(21)
#define PMCR0_DEFER_PMI_TO_ERET	BIT(22)
#define PMCR0_ALLOW_CNT_EN_EL0	BIT(30)
#define PMCR0_CNT_ENABLE_8_9	GENMASK(33, 32)
#define PMCR0_PMI_ENABLE_8_9	GENMASK(45, 44)

#define SYS_IMP_APL_PMCR1_EL1	sys_reg(3, 1, 15, 1, 0)
#define SYS_IMP_APL_PMCR1_EL12	sys_reg(3, 1, 15, 7, 2)
#define PMCR1_COUNT_A64_EL0_0_7	GENMASK(15, 8)
#define PMCR1_COUNT_A64_EL1_0_7	GENMASK(23, 16)
#define PMCR1_COUNT_A64_EL0_8_9	GENMASK(41, 40)
#define PMCR1_COUNT_A64_EL1_8_9	GENMASK(49, 48)

#define SYS_IMP_APL_PMCR2_EL1	sys_reg(3, 1, 15, 2, 0)
#define SYS_IMP_APL_PMCR3_EL1	sys_reg(3, 1, 15, 3, 0)
#define SYS_IMP_APL_PMCR4_EL1	sys_reg(3, 1, 15, 4, 0)

#define SYS_IMP_APL_PMESR0_EL1	sys_reg(3, 1, 15, 5, 0)
#define PMESR0_EVT_CNT_2	GENMASK(7, 0)
#define PMESR0_EVT_CNT_3	GENMASK(15, 8)
#define PMESR0_EVT_CNT_4	GENMASK(23, 16)
#define PMESR0_EVT_CNT_5	GENMASK(31, 24)

#define SYS_IMP_APL_PMESR1_EL1	sys_reg(3, 1, 15, 6, 0)
#define PMESR1_EVT_CNT_6	GENMASK(7, 0)
#define PMESR1_EVT_CNT_7	GENMASK(15, 8)
#define PMESR1_EVT_CNT_8	GENMASK(23, 16)
#define PMESR1_EVT_CNT_9	GENMASK(31, 24)

#define SYS_IMP_APL_PMSR_EL1	sys_reg(3, 1, 15, 13, 0)
#define PMSR_OVERFLOW		GENMASK(9, 0)

#endif /* __ASM_APPLE_M1_PMU_h */
