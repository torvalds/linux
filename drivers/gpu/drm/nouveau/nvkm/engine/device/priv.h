#ifndef __NVKM_DEVICE_PRIV_H__
#define __NVKM_DEVICE_PRIV_H__
#include <core/device.h>

int  nvkm_device_ctor(const struct nvkm_device_func *,
		      const struct nvkm_device_quirk *,
		      void *, enum nv_bus_type type, u64 handle,
		      const char *name, const char *cfg, const char *dbg,
		      bool detect, bool mmio, u64 subdev_mask,
		      struct nvkm_device *);
int  nvkm_device_init(struct nvkm_device *);
int  nvkm_device_fini(struct nvkm_device *, bool suspend);

extern struct nvkm_oclass nvkm_control_oclass[];

int nv04_identify(struct nvkm_device *);
int nv10_identify(struct nvkm_device *);
int nv20_identify(struct nvkm_device *);
int nv30_identify(struct nvkm_device *);
int nv40_identify(struct nvkm_device *);
int nv50_identify(struct nvkm_device *);
int gf100_identify(struct nvkm_device *);
int gk104_identify(struct nvkm_device *);
int gm100_identify(struct nvkm_device *);
#endif
