#ifndef __NVKM_SEC2_PRIV_H__
#define __NVKM_SEC2_PRIV_H__
#include <engine/sec2.h>

#define nvkm_sec2(p) container_of((p), struct nvkm_sec2, engine)

int nvkm_sec2_new_(struct nvkm_device *, int, struct nvkm_sec2 **);

#endif
