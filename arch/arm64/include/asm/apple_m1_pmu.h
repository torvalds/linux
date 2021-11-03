// SPDX-License-Identifier: GPL-2.0

#ifndef __ASM_APPLE_M1_PMU_h
#define __ASM_APPLE_M1_PMU_h

#include <linux/bits.h>
#include <asm/sysreg.h>

/* Core PMC control register */
#define SYS_IMP_APL_PMCR0_EL1	sys_reg(3, 1, 15, 0, 0)
#define PMCR0_IMODE		GENMASK(10, 8)
#define PMCR0_IMODE_OFF		0
#define PMCR0_IMODE_PMI		1
#define PMCR0_IMODE_AIC		2
#define PMCR0_IMODE_HALT	3
#define PMCR0_IMODE_FIQ		4
#define PMCR0_IACT		BIT(11)

#endif /* __ASM_APPLE_M1_PMU_h */
