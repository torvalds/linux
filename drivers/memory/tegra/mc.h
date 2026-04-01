/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2014-2026 NVIDIA CORPORATION.  All rights reserved.
 */

#ifndef MEMORY_TEGRA_MC_H
#define MEMORY_TEGRA_MC_H

#include <linux/bits.h>
#include <linux/io.h>
#include <linux/types.h>

#include <soc/tegra/mc.h>

#define MC_INTSTATUS					0x00
/* Bit field of MC_INTSTATUS register */
#define MC_INT_DECERR_EMEM				BIT(6)
#define MC_INT_INVALID_GART_PAGE			BIT(7)
#define MC_INT_SECURITY_VIOLATION			BIT(8)
#define MC_INT_ARBITRATION_EMEM				BIT(9)
#define MC_INT_INVALID_SMMU_PAGE			BIT(10)
#define MC_INT_INVALID_APB_ASID_UPDATE			BIT(11)
#define MC_INT_DECERR_VPR				BIT(12)
#define MC_INT_SECERR_SEC				BIT(13)
#define MC_INT_DECERR_MTS				BIT(16)
#define MC_INT_DECERR_GENERALIZED_CARVEOUT		BIT(17)
#define MC_INT_DECERR_ROUTE_SANITY			BIT(20)
#define MC_INT_DECERR_ROUTE_SANITY_GIC_MSI		BIT(21)

#define MC_INTMASK					0x04
#define MC_GART_ERROR_REQ				0x30
#define MC_EMEM_ADR_CFG					0x54
#define MC_EMEM_ADR_CFG_EMEM_NUMDEV			BIT(0)

#define MC_DECERR_EMEM_OTHERS_STATUS			0x58
#define MC_SECURITY_VIOLATION_STATUS			0x74
#define MC_EMEM_ARB_CFG					0x90
#define MC_EMEM_ARB_CFG_CYCLES_PER_UPDATE(x)		((x) & 0x1ff)
#define MC_EMEM_ARB_CFG_CYCLES_PER_UPDATE_MASK		0x1ff

#define MC_EMEM_ARB_OUTSTANDING_REQ			0x94
#define MC_EMEM_ARB_OUTSTANDING_REQ_HOLDOFF_OVERRIDE	BIT(30)
#define MC_EMEM_ARB_OUTSTANDING_REQ_LIMIT_ENABLE	BIT(31)
#define MC_EMEM_ARB_OUTSTANDING_REQ_MAX_MASK		0x1ff

#define MC_EMEM_ARB_TIMING_RCD				0x98
#define MC_EMEM_ARB_TIMING_RP				0x9c
#define MC_EMEM_ARB_TIMING_RC				0xa0
#define MC_EMEM_ARB_TIMING_RAS				0xa4
#define MC_EMEM_ARB_TIMING_FAW				0xa8
#define MC_EMEM_ARB_TIMING_RRD				0xac
#define MC_EMEM_ARB_TIMING_RAP2PRE			0xb0
#define MC_EMEM_ARB_TIMING_WAP2PRE			0xb4
#define MC_EMEM_ARB_TIMING_R2R				0xb8
#define MC_EMEM_ARB_TIMING_W2W				0xbc
#define MC_EMEM_ARB_TIMING_R2W				0xc0
#define MC_EMEM_ARB_TIMING_W2R				0xc4
#define MC_EMEM_ARB_MISC2				0xc8
#define MC_EMEM_ARB_DA_TURNS				0xd0
#define MC_EMEM_ARB_DA_COVERS				0xd4
#define MC_EMEM_ARB_MISC0				0xd8
#define MC_EMEM_ARB_MISC1				0xdc
#define MC_EMEM_ARB_RING1_THROTTLE			0xe0
#define MC_EMEM_ARB_OVERRIDE				0xe8
#define MC_EMEM_ARB_OVERRIDE_EACK_MASK			0x3

#define MC_TIMING_CONTROL_DBG				0xf8
#define MC_TIMING_CONTROL				0xfc
#define MC_TIMING_UPDATE				BIT(0)

#define MC_GLOBAL_INTSTATUS				0xf24

/* Bit field of MC_ERR_STATUS_0 register */
#define MC_ERR_STATUS_RW				BIT(16)
#define MC_ERR_STATUS_SECURITY				BIT(17)
#define MC_ERR_STATUS_NONSECURE				BIT(25)
#define MC_ERR_STATUS_WRITABLE				BIT(26)
#define MC_ERR_STATUS_READABLE				BIT(27)

#define MC_ERR_STATUS_GSC_ADR_HI_MASK			0xffff
#define MC_ERR_STATUS_GSC_ADR_HI_SHIFT			16
#define MC_ERR_STATUS_RT_ADR_HI_SHIFT			15

#define MC_ERR_STATUS_TYPE_SHIFT			28
#define MC_ERR_STATUS_TYPE_INVALID_SMMU_PAGE		(0x6 << 28)
#define MC_ERR_STATUS_RT_TYPE_MASK			(0xf << 28)
#define MC_ERR_STATUS_RT_TYPE_SHIFT			28

#define MC_ERR_STATUS_ADR_HI_SHIFT			20

#define MC_BROADCAST_CHANNEL				~0

/* Tegra264 specific registers */

/* Registers for MSS HUB */
#define MSS_HUB_GLOBAL_INTSTATUS_0			0x6000
#define MSS_HUBC_INTR					BIT(0)
#define MSS_HUB_GLOBAL_MASK				0x7F00
#define MSS_HUB_GLOBAL_SHIFT				8

#define MSS_HUB_HUBC_INTSTATUS_0			0x6008
#define MSS_HUB_INTRSTATUS_0				0x600c
#define MSS_HUB_HUBC_INTMASK_0				0x6010
#define MSS_HUB_HUBC_SCRUB_DONE_INTMASK			BIT(0)

#define MSS_HUB_HUBC_INTPRIORITY_0			0x6014
#define MSS_HUB_INTRMASK_0				0x6018
#define MSS_HUB_COALESCER_ERR_INTMASK			BIT(0)
#define MSS_HUB_SMMU_BYPASS_ALLOW_ERR_INTMASK		BIT(1)
#define MSS_HUB_ILLEGAL_TBUGRP_ID_INTMASK		BIT(2)
#define MSS_HUB_MSI_ERR_INTMASK				BIT(3)
#define MSS_HUB_POISON_RSP_INTMASK			BIT(4)
#define MSS_HUB_RESTRICTED_ACCESS_ERR_INTMASK		BIT(5)
#define MSS_HUB_RESERVED_PA_ERR_INTMASK			BIT(6)

#define MSS_HUB_INTRPRIORITY_0				0x601c
#define MSS_HUB_SMMU_BYPASS_ALLOW_ERR_STATUS_0		0x6020
#define MSS_HUB_MSI_ERR_STATUS_0			0x6024
#define MSS_HUB_POISON_RSP_STATUS_0			0x6028
#define MSS_HUB_COALESCE_ERR_STATUS_0			0x60e0
#define MSS_HUB_COALESCE_ERR_ADR_HI_0			0x60e4
#define MSS_HUB_COALESCE_ERR_ADR_0			0x60e8
#define MSS_HUB_RESTRICTED_ACCESS_ERR_STATUS_0		0x638c
#define MSS_HUB_RESERVED_PA_ERR_STATUS_0		0x6390
#define MSS_HUB_ILLEGAL_TBUGRP_ID_ERR_STATUS_0		0x63b0

/* Registers for channels */
#define MC_CH_INTSTATUS_0				0x82d4
#define MC_CH_INTMASK_0					0x82d8
#define WCAM_ERR_INTMASK				BIT(19)

#define MC_ERR_GENERALIZED_CARVEOUT_STATUS_1_0		0xbc74

/* Registers for MCF */
#define MCF_COMMON_INTSTATUS0_0_0			0xce04
#define MCF_INTSTATUS_0					0xce2c
#define MCF_INTMASK_0					0xce30
#define MCF_INTPRIORITY_0				0xce34

/* Registers for SBS */
#define MSS_SBS_INTSTATUS_0				0xec08
#define MSS_SBS_INTMASK_0				0xec0c
#define MSS_SBS_FILL_FIFO_ISO_OVERFLOW_INTMASK		BIT(0)
#define MSS_SBS_FILL_FIFO_SISO_OVERFLOW_INTMASK		BIT(1)
#define MSS_SBS_FILL_FIFO_NISO_OVERFLOW_INTMASK		BIT(2)

/* Bit field of MC_ERR_ROUTE_SANITY_STATUS_0 register */
#define MC_ERR_ROUTE_SANITY_RW				BIT(12)
#define MC_ERR_ROUTE_SANITY_SEC				BIT(13)

#define ERR_GENERALIZED_APERTURE_ID_SHIFT		0
#define ERR_GENERALIZED_APERTURE_ID_MASK		0x1F
#define ERR_GENERALIZED_CARVEOUT_APERTURE_ID_SHIFT	5
#define ERR_GENERALIZED_CARVEOUT_APERTURE_ID_MASK	0x1F

static inline u32 tegra_mc_scale_percents(u64 val, unsigned int percents)
{
	val = val * percents;
	do_div(val, 100);

	return min_t(u64, val, U32_MAX);
}

static inline struct tegra_mc *
icc_provider_to_tegra_mc(struct icc_provider *provider)
{
	return container_of(provider, struct tegra_mc, provider);
}

static inline u32 mc_ch_readl(const struct tegra_mc *mc, int ch,
			      unsigned long offset)
{
	if (!mc->bcast_ch_regs)
		return 0;

	if (ch == MC_BROADCAST_CHANNEL)
		return readl_relaxed(mc->bcast_ch_regs + offset);

	return readl_relaxed(mc->ch_regs[ch] + offset);
}

static inline void mc_ch_writel(const struct tegra_mc *mc, int ch,
				u32 value, unsigned long offset)
{
	if (!mc->bcast_ch_regs)
		return;

	if (ch == MC_BROADCAST_CHANNEL)
		writel_relaxed(value, mc->bcast_ch_regs + offset);
	else
		writel_relaxed(value, mc->ch_regs[ch] + offset);
}

static inline u32 mc_readl(const struct tegra_mc *mc, unsigned long offset)
{
	return readl_relaxed(mc->regs + offset);
}

static inline void mc_writel(const struct tegra_mc *mc, u32 value,
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

#ifdef CONFIG_ARCH_TEGRA_186_SOC
extern const struct tegra_mc_soc tegra186_mc_soc;
#endif

#ifdef CONFIG_ARCH_TEGRA_194_SOC
extern const struct tegra_mc_soc tegra194_mc_soc;
#endif

#ifdef CONFIG_ARCH_TEGRA_234_SOC
extern const struct tegra_mc_soc tegra234_mc_soc;
#endif

#ifdef CONFIG_ARCH_TEGRA_264_SOC
extern const struct tegra_mc_soc tegra264_mc_soc;
#endif

#if defined(CONFIG_ARCH_TEGRA_3x_SOC) || \
    defined(CONFIG_ARCH_TEGRA_114_SOC) || \
    defined(CONFIG_ARCH_TEGRA_124_SOC) || \
    defined(CONFIG_ARCH_TEGRA_132_SOC) || \
    defined(CONFIG_ARCH_TEGRA_210_SOC)
int tegra30_mc_probe(struct tegra_mc *mc);
extern const struct tegra_mc_ops tegra30_mc_ops;
#endif

#if defined(CONFIG_ARCH_TEGRA_186_SOC) || \
    defined(CONFIG_ARCH_TEGRA_194_SOC) || \
    defined(CONFIG_ARCH_TEGRA_234_SOC) || \
    defined(CONFIG_ARCH_TEGRA_264_SOC)
extern const struct tegra_mc_ops tegra186_mc_ops;
#endif

irqreturn_t tegra30_mc_handle_irq(int irq, void *data);
extern const irq_handler_t tegra30_mc_irq_handlers[1];
extern const char * const tegra_mc_status_names[32];
extern const char * const tegra20_mc_error_names[8];

/*
 * These IDs are for internal use of Tegra ICC drivers. The ID numbers are
 * chosen such that they don't conflict with the device-tree ICC node IDs.
 */
#define TEGRA_ICC_MC		1000
#define TEGRA_ICC_EMC		1001
#define TEGRA_ICC_EMEM		1002

#endif /* MEMORY_TEGRA_MC_H */
