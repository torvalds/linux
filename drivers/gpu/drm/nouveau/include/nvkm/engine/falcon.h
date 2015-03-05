#ifndef __NVKM_FALCON_H__
#define __NVKM_FALCON_H__
#include <core/engctx.h>

struct nvkm_falcon_chan {
	struct nvkm_engctx base;
};

#define nvkm_falcon_context_create(p,e,c,g,s,a,f,d)                         \
	nvkm_engctx_create((p), (e), (c), (g), (s), (a), (f), (d))
#define nvkm_falcon_context_destroy(d)                                      \
	nvkm_engctx_destroy(&(d)->base)
#define nvkm_falcon_context_init(d)                                         \
	nvkm_engctx_init(&(d)->base)
#define nvkm_falcon_context_fini(d,s)                                       \
	nvkm_engctx_fini(&(d)->base, (s))

#define _nvkm_falcon_context_ctor _nvkm_engctx_ctor
#define _nvkm_falcon_context_dtor _nvkm_engctx_dtor
#define _nvkm_falcon_context_init _nvkm_engctx_init
#define _nvkm_falcon_context_fini _nvkm_engctx_fini
#define _nvkm_falcon_context_rd32 _nvkm_engctx_rd32
#define _nvkm_falcon_context_wr32 _nvkm_engctx_wr32

struct nvkm_falcon_data {
	bool external;
};

#include <core/engine.h>

struct nvkm_falcon {
	struct nvkm_engine base;

	u32 addr;
	u8  version;
	u8  secret;

	struct nvkm_gpuobj *core;
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

#define nv_falcon(priv) (&(priv)->base)

#define nvkm_falcon_create(p,e,c,b,d,i,f,r)                                 \
	nvkm_falcon_create_((p), (e), (c), (b), (d), (i), (f),              \
			       sizeof(**r),(void **)r)
#define nvkm_falcon_destroy(p)                                              \
	nvkm_engine_destroy(&(p)->base)
#define nvkm_falcon_init(p) ({                                              \
	struct nvkm_falcon *falcon = (p);                                   \
	_nvkm_falcon_init(nv_object(falcon));                               \
})
#define nvkm_falcon_fini(p,s) ({                                            \
	struct nvkm_falcon *falcon = (p);                                   \
	_nvkm_falcon_fini(nv_object(falcon), (s));                          \
})

int nvkm_falcon_create_(struct nvkm_object *, struct nvkm_object *,
			   struct nvkm_oclass *, u32, bool, const char *,
			   const char *, int, void **);

void nvkm_falcon_intr(struct nvkm_subdev *subdev);

#define _nvkm_falcon_dtor _nvkm_engine_dtor
int  _nvkm_falcon_init(struct nvkm_object *);
int  _nvkm_falcon_fini(struct nvkm_object *, bool);
u32  _nvkm_falcon_rd32(struct nvkm_object *, u64);
void _nvkm_falcon_wr32(struct nvkm_object *, u64, u32);
#endif
