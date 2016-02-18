#ifndef __NVKM_ICCSENSE_PRIV_H__
#define __NVKM_ICCSENSE_PRIV_H__
#define nvkm_iccsense(p) container_of((p), struct nvkm_iccsense, subdev)
#include <subdev/iccsense.h>

void nvkm_iccsense_ctor(struct nvkm_device *, int, struct nvkm_iccsense *);
int nvkm_iccsense_new_(struct nvkm_device *, int, struct nvkm_iccsense **);
#endif
