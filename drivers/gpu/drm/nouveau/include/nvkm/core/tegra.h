/* SPDX-License-Identifier: MIT */
#ifndef __NVKM_DEVICE_TEGRA_H__
#define __NVKM_DEVICE_TEGRA_H__
#include <core/device.h>
#include <core/mm.h>

struct nvkm_device_tegra {
	const struct nvkm_device_tegra_func *func;
	struct nvkm_device device;
	struct platform_device *pdev;
	int irq;

	struct reset_control *rst;
	struct clk *clk;
	struct clk *clk_ref;
	struct clk *clk_pwr;

	struct regulator *vdd;

	struct {
		/*
		 * Protects accesses to mm from subsystems
		 */
		struct mutex mutex;

		struct nvkm_mm mm;
		struct iommu_domain *domain;
		unsigned long pgshift;
	} iommu;

	int gpu_speedo;
	int gpu_speedo_id;
};

struct nvkm_device_tegra_func {
	/*
	 * If an IOMMU is used, indicates which address bit will trigger a
	 * IOMMU translation when set (when this bit is not set, IOMMU is
	 * bypassed). A value of 0 means an IOMMU is never used.
	 */
	u8 iommu_bit;
	/*
	 * Whether the chip requires a reference clock
	 */
	bool require_ref_clk;
	/*
	 * Whether the chip requires the VDD regulator
	 */
	bool require_vdd;
};

int nvkm_device_tegra_new(const struct nvkm_device_tegra_func *,
			  struct platform_device *,
			  const char *cfg, const char *dbg,
			  bool detect, bool mmio, u64 subdev_mask,
			  struct nvkm_device **);
#endif
