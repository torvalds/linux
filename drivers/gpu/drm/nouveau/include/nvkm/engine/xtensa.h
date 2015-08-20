#ifndef __NVKM_XTENSA_H__
#define __NVKM_XTENSA_H__
#define nvkm_xtensa(p) container_of((p), struct nvkm_xtensa, engine)
#include <core/engine.h>

struct nvkm_xtensa {
	struct nvkm_engine engine;
	const struct nvkm_xtensa_func *func;

	u32 addr;
	struct nvkm_memory *gpu_fw;
	u32 fifo_val;
	u32 unkd28;
};

struct nvkm_xtensa_func {
	void (*init)(struct nvkm_xtensa *);
	struct nvkm_sclass sclass[];
};

#define nvkm_xtensa_create(p,e,c,b,d,i,f,r)				\
	nvkm_xtensa_create_((p), (e), (c), (b), (d), (i), (f),	\
			       sizeof(**r),(void **)r)

int nvkm_xtensa_create_(struct nvkm_object *,
			   struct nvkm_object *,
			   struct nvkm_oclass *, u32, bool,
			   const char *, const char *,
			   int, void **);
#define _nvkm_xtensa_dtor _nvkm_engine_dtor
int _nvkm_xtensa_init(struct nvkm_object *);
int _nvkm_xtensa_fini(struct nvkm_object *, bool);
#endif
