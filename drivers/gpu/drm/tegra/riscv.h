/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2022, NVIDIA Corporation.
 */

#ifndef DRM_TEGRA_RISCV_H
#define DRM_TEGRA_RISCV_H

struct tegra_drm_riscv_descriptor {
	u32 manifest_offset;
	u32 code_offset;
	u32 code_size;
	u32 data_offset;
	u32 data_size;
};

struct tegra_drm_riscv {
	/* User initializes */
	struct device *dev;
	void __iomem *regs;

	struct tegra_drm_riscv_descriptor bl_desc;
	struct tegra_drm_riscv_descriptor os_desc;
};

int tegra_drm_riscv_read_descriptors(struct tegra_drm_riscv *riscv);
int tegra_drm_riscv_boot_bootrom(struct tegra_drm_riscv *riscv, phys_addr_t image_address,
				 u32 gscid, const struct tegra_drm_riscv_descriptor *desc);

#endif
