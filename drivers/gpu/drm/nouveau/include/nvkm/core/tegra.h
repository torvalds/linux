#ifndef __NVKM_DEVICE_TEGRA_H__
#define __NVKM_DEVICE_TEGRA_H__
#include <core/device.h>

struct nvkm_device_tegra {
	struct nvkm_device device;
	struct platform_device *pdev;
};

int nvkm_device_tegra_new(struct platform_device *,
			  const char *cfg, const char *dbg,
			  bool detect, bool mmio, u64 subdev_mask,
			  struct nvkm_device **);
#endif
