/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2014 NVIDIA CORPORATION.  All rights reserved.
 */

#ifndef MEMORY_TEGRA_MC_H
#define MEMORY_TEGRA_MC_H

#include <linux/bits.h>
#include <linux/io.h>
#include <linux/types.h>

#include <soc/tegra/mc.h>

#define MC_INT_DECERR_MTS				BIT(16)
#define MC_INT_SECERR_SEC				BIT(13)
#define MC_INT_DECERR_VPR				BIT(12)
#define MC_INT_INVALID_APB_ASID_UPDATE			BIT(11)
#define MC_INT_INVALID_SMMU_PAGE			BIT(10)
#define MC_INT_ARBITRATION_EMEM				BIT(9)
#define MC_INT_SECURITY_VIOLATION			BIT(8)
#define MC_INT_INVALID_GART_PAGE			BIT(7)
#define MC_INT_DECERR_EMEM				BIT(6)

#define MC_EMEM_ARB_OUTSTANDING_REQ			0x94
#define MC_EMEM_ARB_OUTSTANDING_REQ_MAX_MASK		0x1ff
#define MC_EMEM_ARB_OUTSTANDING_REQ_HOLDOFF_OVERRIDE	BIT(30)
#define MC_EMEM_ARB_OUTSTANDING_REQ_LIMIT_ENABLE	BIT(31)

#define MC_EMEM_ARB_OVERRIDE				0xe8
#define MC_EMEM_ARB_OVERRIDE_EACK_MASK			0x3

#define MC_TIMING_CONTROL				0xfc
#define MC_TIMING_UPDATE				BIT(0)

static inline u32 mc_readl(struct tegra_mc *mc, unsigned long offset)
{
	return readl_relaxed(mc->regs + offset);
}

static inline void mc_writel(struct tegra_mc *mc, u32 value,
			     unsigned long offset)
{
	writel_relaxed(value, mc->regs + offset);
}

extern const struct tegra_mc_reset_ops tegra_mc_reset_ops_common;

#ifdef CONFIG_ARCH_TEGRA_2x_SOC
extern const struct tegra_mc_soc tegra20_mc_soc;
#endif

#ifdef CONFIG_ARCH_TEGRA_3x_SOC
extern const struct tegra_mc_soc tegra30_mc_soc;
#endif

#ifdef CONFIG_ARCH_TEGRA_114_SOC
extern const struct tegra_mc_soc tegra114_mc_soc;
#endif

#ifdef CONFIG_ARCH_TEGRA_124_SOC
extern const struct tegra_mc_soc tegra124_mc_soc;
#endif

#ifdef CONFIG_ARCH_TEGRA_132_SOC
extern const struct tegra_mc_soc tegra132_mc_soc;
#endif

#ifdef CONFIG_ARCH_TEGRA_210_SOC
extern const struct tegra_mc_soc tegra210_mc_soc;
#endif

#endif /* MEMORY_TEGRA_MC_H */
