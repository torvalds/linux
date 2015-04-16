#ifndef __NVKM_XTENSA_H__
#define __NVKM_XTENSA_H__
#include <core/engine.h>
struct nvkm_gpuobj;

struct nvkm_xtensa {
	struct nvkm_engine base;

	u32 addr;
	struct nvkm_gpuobj *gpu_fw;
	u32 fifo_val;
	u32 unkd28;
};

#define nvkm_xtensa_create(p,e,c,b,d,i,f,r)				\
	nvkm_xtensa_create_((p), (e), (c), (b), (d), (i), (f),	\
			       sizeof(**r),(void **)r)

int _nvkm_xtensa_engctx_ctor(struct nvkm_object *,
				struct nvkm_object *,
				struct nvkm_oclass *, void *, u32,
				struct nvkm_object **);

void _nvkm_xtensa_intr(struct nvkm_subdev *);
int nvkm_xtensa_create_(struct nvkm_object *,
			   struct nvkm_object *,
			   struct nvkm_oclass *, u32, bool,
			   const char *, const char *,
			   int, void **);
#define _nvkm_xtensa_dtor _nvkm_engine_dtor
int _nvkm_xtensa_init(struct nvkm_object *);
int _nvkm_xtensa_fini(struct nvkm_object *, bool);
u32  _nvkm_xtensa_rd32(struct nvkm_object *, u64);
void _nvkm_xtensa_wr32(struct nvkm_object *, u64, u32);
#endif
