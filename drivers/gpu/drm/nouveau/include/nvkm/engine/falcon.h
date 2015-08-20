#ifndef __NVKM_FALCON_H__
#define __NVKM_FALCON_H__
#define nvkm_falcon(p) container_of((p), struct nvkm_falcon, engine)
#include <core/engine.h>
struct nvkm_fifo_chan;

struct nvkm_falcon_data {
	bool external;
};

struct nvkm_falcon {
	struct nvkm_engine engine;
	const struct nvkm_falcon_func *func;

	u32 addr;
	u8  version;
	u8  secret;

	struct nvkm_memory *core;
	bool external;

	struct {
		u32 limit;
		u32 *data;
		u32  size;
	} code;

	struct {
		u32 limit;
		u32 *data;
		u32  size;
	} data;
};

struct nvkm_falcon_func {
	void (*intr)(struct nvkm_falcon *, struct nvkm_fifo_chan *);
	struct nvkm_sclass sclass[];
};

#define nv_falcon(priv) ((struct nvkm_falcon *)priv)

#define nvkm_falcon_create(a,p,e,c,b,d,i,f,r)                                 \
	nvkm_falcon_create_((a), (p), (e), (c), (b), (d), (i), (f),              \
			       sizeof(**r),(void **)r)
#define nvkm_falcon_destroy(p)                                              \
	nvkm_engine_destroy(&(p)->engine)
#define nvkm_falcon_init(p) ({                                              \
	struct nvkm_falcon *_falcon = (p);                                   \
	_nvkm_falcon_init(nv_object(_falcon));                               \
})
#define nvkm_falcon_fini(p,s) ({                                            \
	struct nvkm_falcon *_falcon = (p);                                   \
	_nvkm_falcon_fini(nv_object(_falcon), (s));                          \
})

int nvkm_falcon_create_(const struct nvkm_falcon_func *,
			struct nvkm_object *, struct nvkm_object *,
			struct nvkm_oclass *, u32, bool, const char *,
			const char *, int, void **);
#define _nvkm_falcon_dtor _nvkm_engine_dtor
int  _nvkm_falcon_init(struct nvkm_object *);
int  _nvkm_falcon_fini(struct nvkm_object *, bool);
#endif
