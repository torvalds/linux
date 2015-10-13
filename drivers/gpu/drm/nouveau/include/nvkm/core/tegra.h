#ifndef __NVKM_DEVICE_TEGRA_H__
#define __NVKM_DEVICE_TEGRA_H__
#include <core/device.h>
#include <core/mm.h>

struct nvkm_device_tegra {
	struct nvkm_device device;
	struct platform_device *pdev;
	int irq;

	struct reset_control *rst;
	struct clk *clk;
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
};

int nvkm_device_tegra_new(struct platform_device *,
			  const char *cfg, const char *dbg,
			  bool detect, bool mmio, u64 subdev_mask,
			  struct nvkm_device **);
#endif
