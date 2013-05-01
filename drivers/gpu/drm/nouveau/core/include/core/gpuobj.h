#ifndef __NOUVEAU_GPUOBJ_H__
#define __NOUVEAU_GPUOBJ_H__

#include <core/object.h>
#include <core/device.h>
#include <core/parent.h>
#include <core/mm.h>

struct nouveau_vma;
struct nouveau_vm;

#define NVOBJ_FLAG_ZERO_ALLOC 0x00000001
#define NVOBJ_FLAG_ZERO_FREE  0x00000002
#define NVOBJ_FLAG_HEAP       0x00000004

struct nouveau_gpuobj {
	struct nouveau_object base;
	struct nouveau_object *parent;
	struct nouveau_mm_node *node;
	struct nouveau_mm heap;

	u32 flags;
	u64 addr;
	u32 size;
};

static inline struct nouveau_gpuobj *
nv_gpuobj(void *obj)
{
#if CONFIG_NOUVEAU_DEBUG >= NV_DBG_PARANOIA
	if (unlikely(!nv_iclass(obj, NV_GPUOBJ_CLASS)))
		nv_assert("BAD CAST -> NvGpuObj, %08x", nv_hclass(obj));
#endif
	return obj;
}

#define nouveau_gpuobj_create(p,e,c,v,g,s,a,f,d)                               \
	nouveau_gpuobj_create_((p), (e), (c), (v), (g), (s), (a), (f),         \
			       sizeof(**d), (void **)d)
#define nouveau_gpuobj_init(p) nouveau_object_init(&(p)->base)
#define nouveau_gpuobj_fini(p,s) nouveau_object_fini(&(p)->base, (s))
int  nouveau_gpuobj_create_(struct nouveau_object *, struct nouveau_object *,
			    struct nouveau_oclass *, u32 pclass,
			    struct nouveau_object *, u32 size, u32 align,
			    u32 flags, int length, void **);
void nouveau_gpuobj_destroy(struct nouveau_gpuobj *);

int nouveau_gpuobj_new(struct nouveau_object *, struct nouveau_object *,
		       u32 size, u32 align, u32 flags,
		       struct nouveau_gpuobj **);
int nouveau_gpuobj_dup(struct nouveau_object *, struct nouveau_gpuobj *,
		       struct nouveau_gpuobj **);

int nouveau_gpuobj_map(struct nouveau_gpuobj *, u32 acc, struct nouveau_vma *);
int nouveau_gpuobj_map_vm(struct nouveau_gpuobj *, struct nouveau_vm *,
			  u32 access, struct nouveau_vma *);
void nouveau_gpuobj_unmap(struct nouveau_vma *);

static inline void
nouveau_gpuobj_ref(struct nouveau_gpuobj *obj, struct nouveau_gpuobj **ref)
{
	nouveau_object_ref(&obj->base, (struct nouveau_object **)ref);
}

void _nouveau_gpuobj_dtor(struct nouveau_object *);
int  _nouveau_gpuobj_init(struct nouveau_object *);
int  _nouveau_gpuobj_fini(struct nouveau_object *, bool);
u32  _nouveau_gpuobj_rd32(struct nouveau_object *, u64);
void _nouveau_gpuobj_wr32(struct nouveau_object *, u64, u32);

#endif
