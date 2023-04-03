/* SPDX-License-Identifier: (GPL-2.0+ OR MIT) */
/*
 * Copyright (c) 2023 Rockchip Electronics Co., Ltd.
 */

#ifndef RKPM_GICV2_H
#define RKPM_GICV2_H

struct plat_gicv2_dist_ctx_t {
	u32 saved_spi_target[DIV_ROUND_UP(1020, 4)];
	u32 saved_spi_prio[DIV_ROUND_UP(1020, 4)];
	u32 saved_spi_conf[DIV_ROUND_UP(1020, 16)];
	u32 saved_spi_grp[DIV_ROUND_UP(1020, 32)];
	u32 saved_spi_active[DIV_ROUND_UP(1020, 32)];
	u32 saved_spi_enable[DIV_ROUND_UP(1020, 32)];
	u32 saved_gicd_ctrl;
};

struct plat_gicv2_cpu_ctx_t {
	u32 saved_ppi_enable;
	u32 saved_ppi_active;
	u32 saved_ppi_conf[DIV_ROUND_UP(32, 16)];
	u32 saved_ppi_prio[DIV_ROUND_UP(32, 4)];
	u32 saved_ppi_grp;
	u32 saved_gicc_ctrl;
	u32 saved_gicc_pmr;
};

void rkpm_gicv2_dist_save(void __iomem *dist_base,
			  struct plat_gicv2_dist_ctx_t *ctx);
void rkpm_gicv2_dist_restore(void __iomem *dist_base,
			     struct plat_gicv2_dist_ctx_t *ctx);
void rkpm_gicv2_cpu_save(void __iomem *dist_base,
			 void __iomem *cpu_base,
			 struct plat_gicv2_cpu_ctx_t *ctx);
void rkpm_gicv2_cpu_restore(void __iomem *dist_base,
			    void __iomem *cpu_base,
			    struct plat_gicv2_cpu_ctx_t *ctx);
#endif
