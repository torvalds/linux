/*
 * Copyright (C) 2014 NVIDIA CORPORATION.  All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef MEMORY_TEGRA_MC_H
#define MEMORY_TEGRA_MC_H

#include <linux/io.h>
#include <linux/types.h>

#include <soc/tegra/mc.h>

#define MC_INT_DECERR_MTS (1 << 16)
#define MC_INT_SECERR_SEC (1 << 13)
#define MC_INT_DECERR_VPR (1 << 12)
#define MC_INT_INVALID_APB_ASID_UPDATE (1 << 11)
#define MC_INT_INVALID_SMMU_PAGE (1 << 10)
#define MC_INT_ARBITRATION_EMEM (1 << 9)
#define MC_INT_SECURITY_VIOLATION (1 << 8)
#define MC_INT_INVALID_GART_PAGE (1 << 7)
#define MC_INT_DECERR_EMEM (1 << 6)

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
