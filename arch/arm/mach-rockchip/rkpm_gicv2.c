// SPDX-License-Identifier: (GPL-2.0+ OR MIT)
/*
 * Copyright (c) 2023 Rockchip Electronics Co., Ltd.
 */

#include <linux/io.h>
#include <linux/irqchip/arm-gic.h>

#include "rkpm_helpers.h"
#include "rkpm_gicv2.h"

void rkpm_gicv2_dist_save(void __iomem *dist_base,
			  struct plat_gicv2_dist_ctx_t *ctx)
{
	int i;
	int gic_irqs;

	gic_irqs = readl_relaxed(dist_base + GIC_DIST_CTR) & 0x1f;
	gic_irqs = (gic_irqs + 1) << 5;
	if (gic_irqs > 1020)
		gic_irqs = 1020;

	for (i = 32; i < gic_irqs; i += 4)
		ctx->saved_spi_target[i >> 2] =
			readl_relaxed(dist_base + GIC_DIST_TARGET + i);

	for (i = 32; i < gic_irqs; i += 4)
		ctx->saved_spi_prio[i >> 2] =
			readl_relaxed(dist_base + GIC_DIST_PRI + i);

	for (i = 32; i < gic_irqs; i += 16)
		ctx->saved_spi_conf[i >> 4] =
			readl_relaxed(dist_base + GIC_DIST_CONFIG +
				     (i >> 4 << 2));

	for (i = 32; i < gic_irqs; i += 32)
		ctx->saved_spi_grp[i >> 5] =
			readl_relaxed(dist_base + GIC_DIST_IGROUP +
				     (i >> 5 << 2));

	for (i = 32; i < gic_irqs; i += 32)
		ctx->saved_spi_active[i >> 5] =
			readl_relaxed(dist_base + GIC_DIST_ACTIVE_SET +
				     (i >> 5 << 2));

	for (i = 32; i < gic_irqs; i += 32)
		ctx->saved_spi_enable[i >> 5] =
			readl_relaxed(dist_base + GIC_DIST_ENABLE_SET +
				     (i >> 5 << 2));

	ctx->saved_gicd_ctrl = readl_relaxed(dist_base + GIC_DIST_CTRL);
}

void rkpm_gicv2_dist_restore(void __iomem *dist_base,
			     struct plat_gicv2_dist_ctx_t *ctx)
{
	int i = 0;
	int gic_irqs;

	gic_irqs = readl_relaxed(dist_base + GIC_DIST_CTR) & 0x1f;
	gic_irqs = (gic_irqs + 1) << 5;
	if (gic_irqs > 1020)
		gic_irqs = 1020;

	writel_relaxed(0, dist_base + GIC_DIST_CTRL);
	dsb(sy);

	for (i = 32; i < gic_irqs; i += 4)
		writel_relaxed(ctx->saved_spi_target[i >> 2],
			       dist_base + GIC_DIST_TARGET + i);

	for (i = 32; i < gic_irqs; i += 4)
		writel_relaxed(ctx->saved_spi_prio[i >> 2],
			       dist_base + GIC_DIST_PRI + i);

	for (i = 32; i < gic_irqs; i += 16)
		writel_relaxed(ctx->saved_spi_conf[i >> 4],
			       dist_base + GIC_DIST_CONFIG + (i >> 4 << 2));

	for (i = 32; i < gic_irqs; i += 32)
		writel_relaxed(ctx->saved_spi_grp[i >> 5],
			       dist_base + GIC_DIST_IGROUP + (i >> 5 << 2));

	for (i = 32; i < gic_irqs; i += 32) {
		writel_relaxed(~0U, dist_base + GIC_DIST_ACTIVE_CLEAR + (i >> 5 << 2));
		dsb(sy);
		writel_relaxed(ctx->saved_spi_active[i >> 5],
			       dist_base + GIC_DIST_ACTIVE_SET + (i >> 5 << 2));
	}

	for (i = 32; i < gic_irqs; i += 32) {
		writel_relaxed(~0U, dist_base + GIC_DIST_ENABLE_CLEAR + (i >> 5 << 2));
		dsb(sy);
		writel_relaxed(ctx->saved_spi_enable[i >> 5],
			       dist_base + GIC_DIST_ENABLE_SET + (i >> 5 << 2));
	}

	dsb(sy);

	writel_relaxed(ctx->saved_gicd_ctrl, dist_base + GIC_DIST_CTRL);
	dsb(sy);
}

void rkpm_gicv2_cpu_save(void __iomem *dist_base,
			 void __iomem *cpu_base,
			 struct plat_gicv2_cpu_ctx_t *ctx)
{
	int i;

	ctx->saved_ppi_enable =
		readl_relaxed(dist_base + GIC_DIST_ENABLE_SET);

	ctx->saved_ppi_active =
		readl_relaxed(dist_base + GIC_DIST_ACTIVE_SET);

	for (i = 0; i < DIV_ROUND_UP(32, 16); i++)
		ctx->saved_ppi_conf[i] =
			readl_relaxed(dist_base + GIC_DIST_CONFIG + i * 4);

	for (i = 0; i < DIV_ROUND_UP(32, 4); i++)
		ctx->saved_ppi_prio[i] =
			readl_relaxed(dist_base + GIC_DIST_PRI + i * 4);

	ctx->saved_ppi_grp =
			readl_relaxed(dist_base + GIC_DIST_IGROUP);

	ctx->saved_gicc_pmr =
			readl_relaxed(cpu_base + GIC_CPU_PRIMASK);
	ctx->saved_gicc_ctrl =
			readl_relaxed(cpu_base + GIC_CPU_CTRL);
}

void rkpm_gicv2_cpu_restore(void __iomem *dist_base,
			    void __iomem *cpu_base,
			    struct plat_gicv2_cpu_ctx_t *ctx)
{
	int i;

	writel_relaxed(0, cpu_base + GIC_CPU_CTRL);
	dsb(sy);

	writel_relaxed(~0U, dist_base + GIC_DIST_ENABLE_CLEAR);
	dsb(sy);
	writel_relaxed(ctx->saved_ppi_enable, dist_base + GIC_DIST_ENABLE_SET);

	writel_relaxed(~0U, dist_base + GIC_DIST_ACTIVE_CLEAR);
	dsb(sy);
	writel_relaxed(ctx->saved_ppi_active, dist_base + GIC_DIST_ACTIVE_SET);

	for (i = 0; i < DIV_ROUND_UP(32, 16); i++)
		writel_relaxed(ctx->saved_ppi_conf[i], dist_base + GIC_DIST_CONFIG + i * 4);

	for (i = 0; i < DIV_ROUND_UP(32, 4); i++)
		writel_relaxed(ctx->saved_ppi_prio[i], dist_base + GIC_DIST_PRI + i * 4);

	writel_relaxed(ctx->saved_ppi_grp, dist_base + GIC_DIST_IGROUP);
	writel_relaxed(ctx->saved_gicc_pmr, cpu_base + GIC_CPU_PRIMASK);
	dsb(sy);

	writel_relaxed(ctx->saved_gicc_ctrl, cpu_base + GIC_CPU_CTRL);
	dsb(sy);
}
