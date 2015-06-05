#ifndef __NVKM_GPUOBJ_H__
#define __NVKM_GPUOBJ_H__
#include <core/object.h>
#include <core/mm.h>
struct nvkm_vma;
struct nvkm_vm;

#define NVOBJ_FLAG_ZERO_ALLOC 0x00000001
#define NVOBJ_FLAG_ZERO_FREE  0x00000002
#define NVOBJ_FLAG_HEAP       0x00000004

struct nvkm_gpuobj {
	struct nvkm_object object;
	struct nvkm_object *parent;
	struct nvkm_mm_node *node;
	struct nvkm_mm heap;

	u32 flags;
	u64 addr;
	u32 size;
};

static inline struct nvkm_gpuobj *
nv_gpuobj(void *obj)
{
#if CONFIG_NOUVEAU_DEBUG >= NV_DBG_PARANOIA
	if (unlikely(!nv_iclass(obj, NV_GPUOBJ_CLASS)))
		nv_assert("BAD CAST -> NvGpuObj, %08x", nv_hclass(obj));
#endif
	return obj;
}

#define nvkm_gpuobj_create(p,e,c,v,g,s,a,f,d)                               \
	nvkm_gpuobj_create_((p), (e), (c), (v), (g), (s), (a), (f),         \
			       sizeof(**d), (void **)d)
#define nvkm_gpuobj_init(p) nvkm_object_init(&(p)->object)
#define nvkm_gpuobj_fini(p,s) nvkm_object_fini(&(p)->object, (s))
int  nvkm_gpuobj_create_(struct nvkm_object *, struct nvkm_object *,
			    struct nvkm_oclass *, u32 pclass,
			    struct nvkm_object *, u32 size, u32 align,
			    u32 flags, int length, void **);
void nvkm_gpuobj_destroy(struct nvkm_gpuobj *);

int  nvkm_gpuobj_new(struct nvkm_object *, struct nvkm_object *, u32 size,
		     u32 align, u32 flags, struct nvkm_gpuobj **);
int  nvkm_gpuobj_dup(struct nvkm_object *, struct nvkm_gpuobj *,
		     struct nvkm_gpuobj **);
int  nvkm_gpuobj_map(struct nvkm_gpuobj *, u32 acc, struct nvkm_vma *);
int  nvkm_gpuobj_map_vm(struct nvkm_gpuobj *, struct nvkm_vm *, u32 access,
			struct nvkm_vma *);
void nvkm_gpuobj_unmap(struct nvkm_vma *);

static inline void
nvkm_gpuobj_ref(struct nvkm_gpuobj *obj, struct nvkm_gpuobj **ref)
{
	nvkm_object_ref(&obj->object, (struct nvkm_object **)ref);
}

void _nvkm_gpuobj_dtor(struct nvkm_object *);
int  _nvkm_gpuobj_init(struct nvkm_object *);
int  _nvkm_gpuobj_fini(struct nvkm_object *, bool);
u32  _nvkm_gpuobj_rd32(struct nvkm_object *, u64);
void _nvkm_gpuobj_wr32(struct nvkm_object *, u64, u32);
#endif
