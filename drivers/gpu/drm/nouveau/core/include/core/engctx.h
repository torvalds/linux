#ifndef __NOUVEAU_ENGCTX_H__
#define __NOUVEAU_ENGCTX_H__

#include <core/object.h>
#include <core/gpuobj.h>

#include <subdev/vm.h>

#define NV_ENGCTX_(eng,var) (NV_ENGCTX_CLASS | ((var) << 8) | (eng))
#define NV_ENGCTX(name,var)  NV_ENGCTX_(NVDEV_ENGINE_##name, (var))

struct nouveau_engctx {
	struct nouveau_gpuobj base;
	struct nouveau_vma vma;
	struct list_head head;
	unsigned long save;
	u64 addr;
};

static inline struct nouveau_engctx *
nv_engctx(void *obj)
{
#if CONFIG_NOUVEAU_DEBUG >= NV_DBG_PARANOIA
	if (unlikely(!nv_iclass(obj, NV_ENGCTX_CLASS)))
		nv_assert("BAD CAST -> NvEngCtx, %08x", nv_hclass(obj));
#endif
	return obj;
}

#define nouveau_engctx_create(p,e,c,g,s,a,f,d)                                 \
	nouveau_engctx_create_((p), (e), (c), (g), (s), (a), (f),              \
			       sizeof(**d), (void **)d)

int  nouveau_engctx_create_(struct nouveau_object *, struct nouveau_object *,
			    struct nouveau_oclass *, struct nouveau_object *,
			    u32 size, u32 align, u32 flags,
			    int length, void **data);
void nouveau_engctx_destroy(struct nouveau_engctx *);
int  nouveau_engctx_init(struct nouveau_engctx *);
int  nouveau_engctx_fini(struct nouveau_engctx *, bool suspend);

void _nouveau_engctx_dtor(struct nouveau_object *);
int  _nouveau_engctx_init(struct nouveau_object *);
int  _nouveau_engctx_fini(struct nouveau_object *, bool suspend);
#define _nouveau_engctx_rd32 _nouveau_gpuobj_rd32
#define _nouveau_engctx_wr32 _nouveau_gpuobj_wr32

struct nouveau_object *nouveau_engctx_get(struct nouveau_engine *, u64 addr);
void nouveau_engctx_put(struct nouveau_object *);

#endif
