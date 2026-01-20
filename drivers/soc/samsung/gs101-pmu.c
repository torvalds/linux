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
#include <linux/regmap.h>

#include "exynos-pmu.h"

#define PMUALIVE_MASK			GENMASK(13, 0)
#define TENSOR_SET_BITS			(BIT(15) | BIT(14))
#define TENSOR_CLR_BITS			BIT(15)
#define TENSOR_SMC_PMU_SEC_REG		0x82000504
#define TENSOR_PMUREG_READ		0
#define TENSOR_PMUREG_WRITE		1
#define TENSOR_PMUREG_RMW		2

static const struct regmap_range gs101_pmu_registers[] = {
	regmap_reg_range(GS101_OM_STAT, GS101_SYSTEM_INFO),
	regmap_reg_range(GS101_IDLE_IP(0), GS101_IDLE_IP_MASK(3)),
	regmap_reg_range(GS101_DATARAM_STATE_SLC_CH(0),
			 GS101_PPMPURAM_INFORM_SCL_CH(3)),
	regmap_reg_range(GS101_INFORM0, GS101_SYSIP_DAT(0)),
	/* skip SYSIP_DAT1 SYSIP_DAT2 */
	regmap_reg_range(GS101_SYSIP_DAT(3), GS101_PWR_HOLD_SW_TRIP),
	regmap_reg_range(GS101_GSA_INFORM(0), GS101_GSA_INFORM(1)),
	regmap_reg_range(GS101_INFORM4, GS101_IROM_INFORM),
	regmap_reg_range(GS101_IROM_CPU_INFORM(0), GS101_IROM_CPU_INFORM(7)),
	regmap_reg_range(GS101_PMU_SPARE(0), GS101_PMU_SPARE(3)),
	/* skip most IROM_xxx registers */
	regmap_reg_range(GS101_DREX_CALIBRATION(0), GS101_DREX_CALIBRATION(7)),

#define CLUSTER_CPU_RANGE(cl, cpu)					\
	regmap_reg_range(GS101_CLUSTER_CPU_CONFIGURATION(cl, cpu),	\
			 GS101_CLUSTER_CPU_OPTION(cl, cpu)),		\
	regmap_reg_range(GS101_CLUSTER_CPU_OUT(cl, cpu),		\
			 GS101_CLUSTER_CPU_IN(cl, cpu)),		\
	regmap_reg_range(GS101_CLUSTER_CPU_INT_IN(cl, cpu),		\
			 GS101_CLUSTER_CPU_INT_DIR(cl, cpu))

	/* cluster 0..2 and cpu 0..4 or 0..1 */
	CLUSTER_CPU_RANGE(GS101_CLUSTER0_OFFSET, 0),
	CLUSTER_CPU_RANGE(GS101_CLUSTER0_OFFSET, 1),
	CLUSTER_CPU_RANGE(GS101_CLUSTER0_OFFSET, 2),
	CLUSTER_CPU_RANGE(GS101_CLUSTER0_OFFSET, 3),
	CLUSTER_CPU_RANGE(GS101_CLUSTER1_OFFSET, 0),
	CLUSTER_CPU_RANGE(GS101_CLUSTER1_OFFSET, 1),
	CLUSTER_CPU_RANGE(GS101_CLUSTER2_OFFSET, 0),
	CLUSTER_CPU_RANGE(GS101_CLUSTER2_OFFSET, 1),
#undef CLUSTER_CPU_RANGE

#define CLUSTER_NONCPU_RANGE(cl)					\
	regmap_reg_range(GS101_CLUSTER_NONCPU_CONFIGURATION(cl),	\
			 GS101_CLUSTER_NONCPU_OPTION(cl)),		\
	regmap_reg_range(GS101_CLUSTER_NONCPU_OUT(cl),			\
			 GS101_CLUSTER_NONCPU_IN(cl)),			\
	regmap_reg_range(GS101_CLUSTER_NONCPU_INT_IN(cl),		\
			 GS101_CLUSTER_NONCPU_INT_DIR(cl)),		\
	regmap_reg_range(GS101_CLUSTER_NONCPU_DUALRAIL_CTRL_OUT(cl),	\
			 GS101_CLUSTER_NONCPU_DUALRAIL_POS_OUT(cl)),	\
	regmap_reg_range(GS101_CLUSTER_NONCPU_DUALRAIL_CTRL_IN(cl),	\
			 GS101_CLUSTER_NONCPU_DUALRAIL_CTRL_IN(cl))

	CLUSTER_NONCPU_RANGE(0),
	regmap_reg_range(GS101_CLUSTER0_NONCPU_DSU_PCH,
			 GS101_CLUSTER0_NONCPU_DSU_PCH),
	CLUSTER_NONCPU_RANGE(1),
	CLUSTER_NONCPU_RANGE(2),
#undef CLUSTER_NONCPU_RANGE

#define SUBBLK_RANGE(blk)						\
	regmap_reg_range(GS101_SUBBLK_CONFIGURATION(blk),		\
			 GS101_SUBBLK_CTRL(blk)),			\
	regmap_reg_range(GS101_SUBBLK_OUT(blk), GS101_SUBBLK_IN(blk)),	\
	regmap_reg_range(GS101_SUBBLK_INT_IN(blk),			\
			 GS101_SUBBLK_INT_DIR(blk)),			\
	regmap_reg_range(GS101_SUBBLK_MEMORY_OUT(blk),			\
			 GS101_SUBBLK_MEMORY_IN(blk))

	SUBBLK_RANGE(GS101_SUBBBLK_OFFSET_ALIVE),
	SUBBLK_RANGE(GS101_SUBBBLK_OFFSET_AOC),
	SUBBLK_RANGE(GS101_SUBBBLK_OFFSET_APM),
	SUBBLK_RANGE(GS101_SUBBBLK_OFFSET_CMU),
	SUBBLK_RANGE(GS101_SUBBBLK_OFFSET_BUS0),
	SUBBLK_RANGE(GS101_SUBBBLK_OFFSET_BUS1),
	SUBBLK_RANGE(GS101_SUBBBLK_OFFSET_BUS2),
	SUBBLK_RANGE(GS101_SUBBBLK_OFFSET_CORE),
	SUBBLK_RANGE(GS101_SUBBBLK_OFFSET_EH),
	SUBBLK_RANGE(GS101_SUBBBLK_OFFSET_CPUCL0),
	SUBBLK_RANGE(GS101_SUBBBLK_OFFSET_CPUCL1),
	SUBBLK_RANGE(GS101_SUBBBLK_OFFSET_CPUCL2),
	SUBBLK_RANGE(GS101_SUBBBLK_OFFSET_G3D),
	SUBBLK_RANGE(GS101_SUBBBLK_OFFSET_EMBEDDED_CPUCL0),
	SUBBLK_RANGE(GS101_SUBBBLK_OFFSET_EMBEDDED_G3D),
	SUBBLK_RANGE(GS101_SUBBBLK_OFFSET_HSI0),
	SUBBLK_RANGE(GS101_SUBBBLK_OFFSET_HSI1),
	SUBBLK_RANGE(GS101_SUBBBLK_OFFSET_HSI2),
	SUBBLK_RANGE(GS101_SUBBBLK_OFFSET_DPU),
	SUBBLK_RANGE(GS101_SUBBBLK_OFFSET_DISP),
	SUBBLK_RANGE(GS101_SUBBBLK_OFFSET_G2D),
	SUBBLK_RANGE(GS101_SUBBBLK_OFFSET_MFC),
	SUBBLK_RANGE(GS101_SUBBBLK_OFFSET_CSIS),
	SUBBLK_RANGE(GS101_SUBBBLK_OFFSET_PDP),
	SUBBLK_RANGE(GS101_SUBBBLK_OFFSET_DNS),
	SUBBLK_RANGE(GS101_SUBBBLK_OFFSET_G3AA),
	SUBBLK_RANGE(GS101_SUBBBLK_OFFSET_IPP),
	SUBBLK_RANGE(GS101_SUBBBLK_OFFSET_ITP),
	SUBBLK_RANGE(GS101_SUBBBLK_OFFSET_MCSC),
	SUBBLK_RANGE(GS101_SUBBBLK_OFFSET_GDC),
	SUBBLK_RANGE(GS101_SUBBBLK_OFFSET_TNR),
	SUBBLK_RANGE(GS101_SUBBBLK_OFFSET_BO),
	SUBBLK_RANGE(GS101_SUBBBLK_OFFSET_TPU),
	SUBBLK_RANGE(GS101_SUBBBLK_OFFSET_MIF0),
	SUBBLK_RANGE(GS101_SUBBBLK_OFFSET_MIF1),
	SUBBLK_RANGE(GS101_SUBBBLK_OFFSET_MIF2),
	SUBBLK_RANGE(GS101_SUBBBLK_OFFSET_MIF3),
	SUBBLK_RANGE(GS101_SUBBBLK_OFFSET_MISC),
	SUBBLK_RANGE(GS101_SUBBBLK_OFFSET_PERIC0),
	SUBBLK_RANGE(GS101_SUBBBLK_OFFSET_PERIC1),
	SUBBLK_RANGE(GS101_SUBBBLK_OFFSET_S2D),
#undef SUBBLK_RANGE

#define SUBBLK_CPU_RANGE(blk)						\
	regmap_reg_range(GS101_SUBBLK_CPU_CONFIGURATION(blk),		\
			 GS101_SUBBLK_CPU_OPTION(blk)),			\
	regmap_reg_range(GS101_SUBBLK_CPU_OUT(blk),			\
			 GS101_SUBBLK_CPU_IN(blk)),			\
	regmap_reg_range(GS101_SUBBLK_CPU_INT_IN(blk),			\
			 GS101_SUBBLK_CPU_INT_DIR(blk))

	SUBBLK_CPU_RANGE(GS101_SUBBBLK_CPU_OFFSET_APM),
	SUBBLK_CPU_RANGE(GS101_SUBBBLK_CPU_OFFSET_DBGCORE),
	SUBBLK_CPU_RANGE(GS101_SUBBBLK_CPU_OFFSET_SSS),
#undef SUBBLK_CPU_RANGE

	regmap_reg_range(GS101_MIF_CONFIGURATION, GS101_MIF_CTRL),
	regmap_reg_range(GS101_MIF_OUT, GS101_MIF_IN),
	regmap_reg_range(GS101_MIF_INT_IN, GS101_MIF_INT_DIR),
	regmap_reg_range(GS101_TOP_CONFIGURATION, GS101_TOP_OPTION),
	regmap_reg_range(GS101_TOP_OUT, GS101_TOP_IN),
	regmap_reg_range(GS101_TOP_INT_IN, GS101_WAKEUP2_STAT),
	regmap_reg_range(GS101_WAKEUP2_INT_IN, GS101_WAKEUP2_INT_DIR),
	regmap_reg_range(GS101_SYSTEM_CONFIGURATION, GS101_USER_DEFINED_OUT),
	regmap_reg_range(GS101_SYSTEM_OUT, GS101_SYSTEM_IN),
	regmap_reg_range(GS101_SYSTEM_INT_IN, GS101_EINT_WAKEUP_MASK3),
	regmap_reg_range(GS101_USER_DEFINED_INT_IN, GS101_SCAN2DRAM_INT_DIR),
	/* skip HCU_START */
	regmap_reg_range(GS101_CUSTOM_OUT, GS101_CUSTOM_IN),
	regmap_reg_range(GS101_CUSTOM_INT_IN, GS101_CUSTOM_INT_DIR),
	regmap_reg_range(GS101_ACK_LAST_CPU, GS101_HCU_R(3)),
	regmap_reg_range(GS101_HCU_SP, GS101_HCU_PC),
	/* skip PMU_RAM_CTRL */
	regmap_reg_range(GS101_APM_HCU_CTRL, GS101_APM_HCU_CTRL),
	regmap_reg_range(GS101_APM_NMI_ENABLE, GS101_RST_STAT_PMU),
	regmap_reg_range(GS101_HPM_INT_IN, GS101_BOOT_STAT),
	regmap_reg_range(GS101_PMLINK_OUT, GS101_PMLINK_AOC_CTRL),
	regmap_reg_range(GS101_TCXO_BUF_CTRL, GS101_ADD_CTRL),
	regmap_reg_range(GS101_HCU_TIMEOUT_RESET, GS101_HCU_TIMEOUT_SCAN2DRAM),
	regmap_reg_range(GS101_TIMER(0), GS101_TIMER(3)),
	regmap_reg_range(GS101_PPC_MIF(0), GS101_PPC_EH),
	/* PPC_OFFSET, skip PPC_CPUCL1_0 PPC_CPUCL1_1 */
	regmap_reg_range(GS101_EXT_REGULATOR_MIF_DURATION, GS101_TCXO_DURATION),
	regmap_reg_range(GS101_BURNIN_CTRL, GS101_TMU_SUB_TRIP),
	regmap_reg_range(GS101_MEMORY_CEN, GS101_MEMORY_SMX_FEEDBACK),
	regmap_reg_range(GS101_SLC_PCH_CHANNEL, GS101_SLC_PCH_CB),
	regmap_reg_range(GS101_FORCE_NOMC, GS101_FORCE_NOMC),
	regmap_reg_range(GS101_FORCE_BOOST, GS101_PMLINK_SLC_BUSY),
	regmap_reg_range(GS101_BOOTSYNC_OUT, GS101_CTRL_SECJTAG_ALIVE),
	regmap_reg_range(GS101_CTRL_DIV_PLL_ALV_DIVLOW, GS101_CTRL_CLKDIV__CLKRTC),
	regmap_reg_range(GS101_CTRL_SOC32K, GS101_CTRL_SBU_SW_EN),
	regmap_reg_range(GS101_PAD_CTRL_CLKOUT0, GS101_PAD_CTRL_WRESETO_n),
	regmap_reg_range(GS101_PHY_CTRL_USB20, GS101_PHY_CTRL_UFS),
};

static const struct regmap_range gs101_pmu_ro_registers[] = {
	regmap_reg_range(GS101_OM_STAT, GS101_VERSION),
	regmap_reg_range(GS101_OTP_STATUS, GS101_OTP_STATUS),

	regmap_reg_range(GS101_DATARAM_STATE_SLC_CH(0),
			 GS101_PPMPURAM_STATE_SLC_CH(0)),
	regmap_reg_range(GS101_DATARAM_STATE_SLC_CH(1),
			 GS101_PPMPURAM_STATE_SLC_CH(1)),
	regmap_reg_range(GS101_DATARAM_STATE_SLC_CH(2),
			 GS101_PPMPURAM_STATE_SLC_CH(2)),
	regmap_reg_range(GS101_DATARAM_STATE_SLC_CH(3),
			 GS101_PPMPURAM_STATE_SLC_CH(3)),

#define CLUSTER_CPU_RANGE(cl, cpu)					\
	regmap_reg_range(GS101_CLUSTER_CPU_IN(cl, cpu),			\
			 GS101_CLUSTER_CPU_IN(cl, cpu)),		\
	regmap_reg_range(GS101_CLUSTER_CPU_INT_IN(cl, cpu),		\
			 GS101_CLUSTER_CPU_INT_IN(cl, cpu))

	CLUSTER_CPU_RANGE(GS101_CLUSTER0_OFFSET, 0),
	CLUSTER_CPU_RANGE(GS101_CLUSTER0_OFFSET, 1),
	CLUSTER_CPU_RANGE(GS101_CLUSTER0_OFFSET, 2),
	CLUSTER_CPU_RANGE(GS101_CLUSTER0_OFFSET, 3),
	CLUSTER_CPU_RANGE(GS101_CLUSTER1_OFFSET, 0),
	CLUSTER_CPU_RANGE(GS101_CLUSTER1_OFFSET, 1),
	CLUSTER_CPU_RANGE(GS101_CLUSTER2_OFFSET, 0),
	CLUSTER_CPU_RANGE(GS101_CLUSTER2_OFFSET, 1),
#undef CLUSTER_CPU_RANGE

#define CLUSTER_NONCPU_RANGE(cl)					\
	regmap_reg_range(GS101_CLUSTER_NONCPU_IN(cl),			\
			 GS101_CLUSTER_NONCPU_IN(cl)),			\
	regmap_reg_range(GS101_CLUSTER_NONCPU_INT_IN(cl),		\
			 GS101_CLUSTER_NONCPU_INT_IN(cl)),		\
	regmap_reg_range(GS101_CLUSTER_NONCPU_DUALRAIL_CTRL_IN(cl),	\
			 GS101_CLUSTER_NONCPU_DUALRAIL_CTRL_IN(cl))

	CLUSTER_NONCPU_RANGE(0),
	CLUSTER_NONCPU_RANGE(1),
	CLUSTER_NONCPU_RANGE(2),
	regmap_reg_range(GS101_CLUSTER_NONCPU_INT_EN(2),
			 GS101_CLUSTER_NONCPU_INT_DIR(2)),
#undef CLUSTER_NONCPU_RANGE

#define SUBBLK_RANGE(blk)						\
	regmap_reg_range(GS101_SUBBLK_IN(blk), GS101_SUBBLK_IN(blk)),	\
	regmap_reg_range(GS101_SUBBLK_INT_IN(blk),			\
			 GS101_SUBBLK_INT_IN(blk)),			\
	regmap_reg_range(GS101_SUBBLK_MEMORY_IN(blk),			\
			 GS101_SUBBLK_MEMORY_IN(blk))

	SUBBLK_RANGE(GS101_SUBBBLK_OFFSET_ALIVE),
	SUBBLK_RANGE(GS101_SUBBBLK_OFFSET_AOC),
	SUBBLK_RANGE(GS101_SUBBBLK_OFFSET_APM),
	SUBBLK_RANGE(GS101_SUBBBLK_OFFSET_CMU),
	SUBBLK_RANGE(GS101_SUBBBLK_OFFSET_BUS0),
	SUBBLK_RANGE(GS101_SUBBBLK_OFFSET_BUS1),
	SUBBLK_RANGE(GS101_SUBBBLK_OFFSET_BUS2),
	SUBBLK_RANGE(GS101_SUBBBLK_OFFSET_CORE),
	SUBBLK_RANGE(GS101_SUBBBLK_OFFSET_EH),
	SUBBLK_RANGE(GS101_SUBBBLK_OFFSET_CPUCL0),
	SUBBLK_RANGE(GS101_SUBBBLK_OFFSET_CPUCL1),
	SUBBLK_RANGE(GS101_SUBBBLK_OFFSET_CPUCL2),
	SUBBLK_RANGE(GS101_SUBBBLK_OFFSET_G3D),
	SUBBLK_RANGE(GS101_SUBBBLK_OFFSET_EMBEDDED_CPUCL0),
	SUBBLK_RANGE(GS101_SUBBBLK_OFFSET_EMBEDDED_G3D),
	SUBBLK_RANGE(GS101_SUBBBLK_OFFSET_HSI0),
	SUBBLK_RANGE(GS101_SUBBBLK_OFFSET_HSI1),
	SUBBLK_RANGE(GS101_SUBBBLK_OFFSET_HSI2),
	SUBBLK_RANGE(GS101_SUBBBLK_OFFSET_DPU),
	SUBBLK_RANGE(GS101_SUBBBLK_OFFSET_DISP),
	SUBBLK_RANGE(GS101_SUBBBLK_OFFSET_G2D),
	SUBBLK_RANGE(GS101_SUBBBLK_OFFSET_MFC),
	SUBBLK_RANGE(GS101_SUBBBLK_OFFSET_CSIS),
	SUBBLK_RANGE(GS101_SUBBBLK_OFFSET_PDP),
	SUBBLK_RANGE(GS101_SUBBBLK_OFFSET_DNS),
	SUBBLK_RANGE(GS101_SUBBBLK_OFFSET_G3AA),
	SUBBLK_RANGE(GS101_SUBBBLK_OFFSET_IPP),
	SUBBLK_RANGE(GS101_SUBBBLK_OFFSET_ITP),
	SUBBLK_RANGE(GS101_SUBBBLK_OFFSET_MCSC),
	SUBBLK_RANGE(GS101_SUBBBLK_OFFSET_GDC),
	SUBBLK_RANGE(GS101_SUBBBLK_OFFSET_TNR),
	SUBBLK_RANGE(GS101_SUBBBLK_OFFSET_BO),
	SUBBLK_RANGE(GS101_SUBBBLK_OFFSET_TPU),
	SUBBLK_RANGE(GS101_SUBBBLK_OFFSET_MIF0),
	SUBBLK_RANGE(GS101_SUBBBLK_OFFSET_MIF1),
	SUBBLK_RANGE(GS101_SUBBBLK_OFFSET_MIF2),
	SUBBLK_RANGE(GS101_SUBBBLK_OFFSET_MIF3),
	SUBBLK_RANGE(GS101_SUBBBLK_OFFSET_MISC),
	SUBBLK_RANGE(GS101_SUBBBLK_OFFSET_PERIC0),
	SUBBLK_RANGE(GS101_SUBBBLK_OFFSET_PERIC1),
	SUBBLK_RANGE(GS101_SUBBBLK_OFFSET_S2D),
#undef SUBBLK_RANGE

#define SUBBLK_CPU_RANGE(blk)						\
	regmap_reg_range(GS101_SUBBLK_CPU_IN(blk),			\
			 GS101_SUBBLK_CPU_IN(blk)),			\
	regmap_reg_range(GS101_SUBBLK_CPU_INT_IN(blk),			\
			 GS101_SUBBLK_CPU_INT_IN(blk))

	SUBBLK_CPU_RANGE(GS101_SUBBBLK_CPU_OFFSET_APM),
	SUBBLK_CPU_RANGE(GS101_SUBBBLK_CPU_OFFSET_DBGCORE),
	SUBBLK_CPU_RANGE(GS101_SUBBBLK_CPU_OFFSET_SSS),
#undef SUBBLK_CPU_RANGE

	regmap_reg_range(GS101_MIF_CONFIGURATION, GS101_MIF_CONFIGURATION),
	regmap_reg_range(GS101_MIF_IN, GS101_MIF_IN),
	regmap_reg_range(GS101_MIF_INT_IN, GS101_MIF_INT_IN),
	regmap_reg_range(GS101_TOP_IN, GS101_TOP_IN),
	regmap_reg_range(GS101_TOP_INT_IN, GS101_TOP_INT_IN),
	regmap_reg_range(GS101_WAKEUP2_INT_IN, GS101_WAKEUP2_INT_IN),
	regmap_reg_range(GS101_SYSTEM_IN, GS101_SYSTEM_IN),
	regmap_reg_range(GS101_SYSTEM_INT_IN, GS101_SYSTEM_INT_IN),
	regmap_reg_range(GS101_EINT_INT_IN, GS101_EINT_INT_IN),
	regmap_reg_range(GS101_EINT2_INT_IN, GS101_EINT2_INT_IN),
	regmap_reg_range(GS101_EINT3_INT_IN, GS101_EINT3_INT_IN),
	regmap_reg_range(GS101_USER_DEFINED_INT_IN, GS101_USER_DEFINED_INT_IN),
	regmap_reg_range(GS101_SCAN2DRAM_INT_IN, GS101_SCAN2DRAM_INT_IN),
	regmap_reg_range(GS101_CUSTOM_IN, GS101_CUSTOM_IN),
	regmap_reg_range(GS101_CUSTOM_INT_IN, GS101_CUSTOM_INT_IN),
	regmap_reg_range(GS101_HCU_R(0), GS101_HCU_R(3)),
	regmap_reg_range(GS101_HCU_SP, GS101_HCU_PC),
	regmap_reg_range(GS101_NMI_SRC_IN, GS101_NMI_SRC_IN),
	regmap_reg_range(GS101_HPM_INT_IN, GS101_HPM_INT_IN),
	regmap_reg_range(GS101_MEMORY_PGEN_FEEDBACK, GS101_MEMORY_PGEN_FEEDBACK),
	regmap_reg_range(GS101_MEMORY_SMX_FEEDBACK, GS101_MEMORY_SMX_FEEDBACK),
	regmap_reg_range(GS101_PMLINK_SLC_ACK, GS101_PMLINK_SLC_BUSY),
	regmap_reg_range(GS101_BOOTSYNC_IN, GS101_BOOTSYNC_IN),
	regmap_reg_range(GS101_SCAN_READY_IN, GS101_SCAN_READY_IN),
	regmap_reg_range(GS101_CTRL_PLL_ALV_LOCK, GS101_CTRL_PLL_ALV_LOCK),
};

static const struct regmap_access_table gs101_pmu_rd_table = {
	.yes_ranges = gs101_pmu_registers,
	.n_yes_ranges = ARRAY_SIZE(gs101_pmu_registers),
};

static const struct regmap_access_table gs101_pmu_wr_table = {
	.yes_ranges = gs101_pmu_registers,
	.n_yes_ranges = ARRAY_SIZE(gs101_pmu_registers),
	.no_ranges = gs101_pmu_ro_registers,
	.n_no_ranges = ARRAY_SIZE(gs101_pmu_ro_registers),
};

const struct exynos_pmu_data gs101_pmu_data = {
	.pmu_secure = true,
	.pmu_cpuhp = true,
	.rd_table = &gs101_pmu_rd_table,
	.wr_table = &gs101_pmu_wr_table,
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
	case GS101_SYSIP_DAT(0):
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
