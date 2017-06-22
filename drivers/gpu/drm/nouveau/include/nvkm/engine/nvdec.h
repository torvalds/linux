#ifndef __NVKM_NVDEC_H__
#define __NVKM_NVDEC_H__
#define nvkm_nvdec(p) container_of((p), struct nvkm_nvdec, engine)
#include <core/engine.h>

struct nvkm_nvdec {
	struct nvkm_engine engine;
	struct nvkm_falcon *falcon;
};

int gp102_nvdec_new(struct nvkm_device *, int, struct nvkm_nvdec **);
#endif
