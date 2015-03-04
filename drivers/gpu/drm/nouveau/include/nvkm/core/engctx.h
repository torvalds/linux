#ifndef __NVKM_ENGCTX_H__
#define __NVKM_ENGCTX_H__
#include <core/gpuobj.h>

#include <subdev/mmu.h>

#define NV_ENGCTX_(eng,var) (NV_ENGCTX_CLASS | ((var) << 8) | (eng))
#define NV_ENGCTX(name,var)  NV_ENGCTX_(NVDEV_ENGINE_##name, (var))

struct nvkm_engctx {
	struct nvkm_gpuobj gpuobj;
	struct nvkm_vma vma;
	struct list_head head;
	unsigned long save;
	u64 addr;
};

static inline struct nvkm_engctx *
nv_engctx(void *obj)
{
#if CONFIG_NOUVEAU_DEBUG >= NV_DBG_PARANOIA
	if (unlikely(!nv_iclass(obj, NV_ENGCTX_CLASS)))
		nv_assert("BAD CAST -> NvEngCtx, %08x", nv_hclass(obj));
#endif
	return obj;
}

#define nvkm_engctx_create(p,e,c,g,s,a,f,d)                                 \
	nvkm_engctx_create_((p), (e), (c), (g), (s), (a), (f),              \
			       sizeof(**d), (void **)d)

int  nvkm_engctx_create_(struct nvkm_object *, struct nvkm_object *,
			    struct nvkm_oclass *, struct nvkm_object *,
			    u32 size, u32 align, u32 flags,
			    int length, void **data);
void nvkm_engctx_destroy(struct nvkm_engctx *);
int  nvkm_engctx_init(struct nvkm_engctx *);
int  nvkm_engctx_fini(struct nvkm_engctx *, bool suspend);

int  _nvkm_engctx_ctor(struct nvkm_object *, struct nvkm_object *,
			  struct nvkm_oclass *, void *, u32,
			  struct nvkm_object **);
void _nvkm_engctx_dtor(struct nvkm_object *);
int  _nvkm_engctx_init(struct nvkm_object *);
int  _nvkm_engctx_fini(struct nvkm_object *, bool suspend);
#define _nvkm_engctx_rd32 _nvkm_gpuobj_rd32
#define _nvkm_engctx_wr32 _nvkm_gpuobj_wr32

struct nvkm_object *nvkm_engctx_get(struct nvkm_engine *, u64 addr);
void nvkm_engctx_put(struct nvkm_object *);
#endif
