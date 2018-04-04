#ifndef __NVKM_OCLASS_H__
#define __NVKM_OCLASS_H__
#include <core/os.h>
#include <core/debug.h>
struct nvkm_oclass;
struct nvkm_object;

struct nvkm_sclass {
	int minver;
	int maxver;
	s32 oclass;
	const struct nvkm_object_func *func;
	int (*ctor)(const struct nvkm_oclass *, void *data, u32 size,
		    struct nvkm_object **);
};

struct nvkm_oclass {
	int (*ctor)(const struct nvkm_oclass *, void *data, u32 size,
		    struct nvkm_object **);
	struct nvkm_sclass base;
	const void *priv;
	const void *engn;
	u32 handle;
	u8  route;
	u64 token;
	u64 object;
	struct nvkm_client *client;
	struct nvkm_object *parent;
	struct nvkm_engine *engine;
};
#endif
