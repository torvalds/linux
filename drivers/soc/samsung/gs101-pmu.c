// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright 2025 Linaro Ltd.
 *
 * GS101 PMU (Power Management Unit) support
 */

#include <linux/arm-smccc.h>
#include <linux/array_size.h>
#include <linux/soc/samsung/exynos-pmu.h>
#include <linux/soc/samsung/exynos-regs-pmu.h>

#include "exynos-pmu.h"

#define PMUALIVE_MASK			GENMASK(13, 0)
#define TENSOR_SET_BITS			(BIT(15) | BIT(14))
#define TENSOR_CLR_BITS			BIT(15)
#define TENSOR_SMC_PMU_SEC_REG		0x82000504
#define TENSOR_PMUREG_READ		0
#define TENSOR_PMUREG_WRITE		1
#define TENSOR_PMUREG_RMW		2

const struct exynos_pmu_data gs101_pmu_data = {
	.pmu_secure = true,
	.pmu_cpuhp = true,
};

/*
 * Tensor SoCs are configured so that PMU_ALIVE registers can only be written
 * from EL3, but are still read accessible. As Linux needs to write some of
 * these registers, the following functions are provided and exposed via
 * regmap.
 *
 * Note: This SMC interface is known to be implemented on gs101 and derivative
 * SoCs.
 */

/* Write to a protected PMU register. */
int tensor_sec_reg_write(void *context, unsigned int reg, unsigned int val)
{
	struct arm_smccc_res res;
	unsigned long pmu_base = (unsigned long)context;

	arm_smccc_smc(TENSOR_SMC_PMU_SEC_REG, pmu_base + reg,
		      TENSOR_PMUREG_WRITE, val, 0, 0, 0, 0, &res);

	/* returns -EINVAL if access isn't allowed or 0 */
	if (res.a0)
		pr_warn("%s(): SMC failed: %d\n", __func__, (int)res.a0);

	return (int)res.a0;
}

/* Read/Modify/Write a protected PMU register. */
static int tensor_sec_reg_rmw(void *context, unsigned int reg,
			      unsigned int mask, unsigned int val)
{
	struct arm_smccc_res res;
	unsigned long pmu_base = (unsigned long)context;

	arm_smccc_smc(TENSOR_SMC_PMU_SEC_REG, pmu_base + reg,
		      TENSOR_PMUREG_RMW, mask, val, 0, 0, 0, &res);

	/* returns -EINVAL if access isn't allowed or 0 */
	if (res.a0)
		pr_warn("%s(): SMC failed: %d\n", __func__, (int)res.a0);

	return (int)res.a0;
}

/*
 * Read a protected PMU register. All PMU registers can be read by Linux.
 * Note: The SMC read register is not used, as only registers that can be
 * written are readable via SMC.
 */
int tensor_sec_reg_read(void *context, unsigned int reg, unsigned int *val)
{
	*val = pmu_raw_readl(reg);
	return 0;
}

/*
 * For SoCs that have set/clear bit hardware this function can be used when
 * the PMU register will be accessed by multiple masters.
 *
 * For example, to set bits 13:8 in PMU reg offset 0x3e80
 * tensor_set_bits_atomic(ctx, 0x3e80, 0x3f00, 0x3f00);
 *
 * Set bit 8, and clear bits 13:9 PMU reg offset 0x3e80
 * tensor_set_bits_atomic(0x3e80, 0x100, 0x3f00);
 */
static int tensor_set_bits_atomic(void *context, unsigned int offset, u32 val,
				  u32 mask)
{
	int ret;
	unsigned int i;

	for (i = 0; i < 32; i++) {
		if (!(mask & BIT(i)))
			continue;

		offset &= ~TENSOR_SET_BITS;

		if (val & BIT(i))
			offset |= TENSOR_SET_BITS;
		else
			offset |= TENSOR_CLR_BITS;

		ret = tensor_sec_reg_write(context, offset, i);
		if (ret)
			return ret;
	}
	return 0;
}

static bool tensor_is_atomic(unsigned int reg)
{
	/*
	 * Use atomic operations for PMU_ALIVE registers (offset 0~0x3FFF)
	 * as the target registers can be accessed by multiple masters. SFRs
	 * that don't support atomic are added to the switch statement below.
	 */
	if (reg > PMUALIVE_MASK)
		return false;

	switch (reg) {
	case GS101_SYSIP_DAT0:
	case GS101_SYSTEM_CONFIGURATION:
		return false;
	default:
		return true;
	}
}

int tensor_sec_update_bits(void *context, unsigned int reg, unsigned int mask,
			   unsigned int val)
{
	if (!tensor_is_atomic(reg))
		return tensor_sec_reg_rmw(context, reg, mask, val);

	return tensor_set_bits_atomic(context, reg, val, mask);
}
