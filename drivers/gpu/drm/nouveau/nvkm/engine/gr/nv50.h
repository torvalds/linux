#ifndef __NV50_GR_H__
#define __NV50_GR_H__
#include <engine/gr.h>
struct nvkm_device;
struct nvkm_gpuobj;

int  nv50_grctx_init(struct nvkm_device *, u32 *size);
void nv50_grctx_fill(struct nvkm_device *, struct nvkm_gpuobj *);
#endif
