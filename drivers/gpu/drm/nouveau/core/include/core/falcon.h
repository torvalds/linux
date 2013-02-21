#ifndef __NOUVEAU_FALCON_H__
#define __NOUVEAU_FALCON_H__

#include <core/engine.h>
#include <core/engctx.h>
#include <core/gpuobj.h>

struct nouveau_falcon_chan {
	struct nouveau_engctx base;
};

#define nouveau_falcon_context_create(p,e,c,g,s,a,f,d)                         \
	nouveau_engctx_create((p), (e), (c), (g), (s), (a), (f), (d))
#define nouveau_falcon_context_destroy(d)                                      \
	nouveau_engctx_destroy(&(d)->base)
#define nouveau_falcon_context_init(d)                                         \
	nouveau_engctx_init(&(d)->base)
#define nouveau_falcon_context_fini(d,s)                                       \
	nouveau_engctx_fini(&(d)->base, (s))

#define _nouveau_falcon_context_ctor _nouveau_engctx_ctor
#define _nouveau_falcon_context_dtor _nouveau_engctx_dtor
#define _nouveau_falcon_context_init _nouveau_engctx_init
#define _nouveau_falcon_context_fini _nouveau_engctx_fini
#define _nouveau_falcon_context_rd32 _nouveau_engctx_rd32
#define _nouveau_falcon_context_wr32 _nouveau_engctx_wr32

struct nouveau_falcon_data {
	bool external;
};

struct nouveau_falcon {
	struct nouveau_engine base;

	u32 addr;
	u8  version;
	u8  secret;

	struct nouveau_gpuobj *core;
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

#define nouveau_falcon_create(p,e,c,b,d,i,f,r)                                 \
	nouveau_falcon_create_((p), (e), (c), (b), (d), (i), (f),              \
			       sizeof(**r),(void **)r)
#define nouveau_falcon_destroy(p)                                              \
	nouveau_engine_destroy(&(p)->base)
#define nouveau_falcon_init(p) ({                                              \
	struct nouveau_falcon *falcon = (p);                                   \
	_nouveau_falcon_init(nv_object(falcon));                               \
})
#define nouveau_falcon_fini(p,s) ({                                            \
	struct nouveau_falcon *falcon = (p);                                   \
	_nouveau_falcon_fini(nv_object(falcon), (s));                          \
})

int nouveau_falcon_create_(struct nouveau_object *, struct nouveau_object *,
			   struct nouveau_oclass *, u32, bool, const char *,
			   const char *, int, void **);

#define _nouveau_falcon_dtor _nouveau_engine_dtor
int  _nouveau_falcon_init(struct nouveau_object *);
int  _nouveau_falcon_fini(struct nouveau_object *, bool);
u32  _nouveau_falcon_rd32(struct nouveau_object *, u64);
void _nouveau_falcon_wr32(struct nouveau_object *, u64, u32);

#endif
