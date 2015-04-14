#ifndef __NVKM_DEVICE_PRIV_H__
#define __NVKM_DEVICE_PRIV_H__
#include <core/device.h>

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
