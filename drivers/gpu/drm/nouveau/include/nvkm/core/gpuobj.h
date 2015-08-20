#ifndef __NVKM_GPUOBJ_H__
#define __NVKM_GPUOBJ_H__
#include <core/object.h>
#include <core/memory.h>
#include <core/mm.h>
struct nvkm_vma;
struct nvkm_vm;

#define NVOBJ_FLAG_ZERO_ALLOC 0x00000001
#define NVOBJ_FLAG_HEAP       0x00000004

struct nvkm_gpuobj {
	struct nvkm_object object;
	const struct nvkm_gpuobj_func *func;
	struct nvkm_gpuobj *parent;
	struct nvkm_memory *memory;
	struct nvkm_mm_node *node;

	u64 addr;
	u32 size;
	struct nvkm_mm heap;

	void __iomem *map;
};

struct nvkm_gpuobj_func {
	void *(*acquire)(struct nvkm_gpuobj *);
	void (*release)(struct nvkm_gpuobj *);
	u32 (*rd32)(struct nvkm_gpuobj *, u32 offset);
	void (*wr32)(struct nvkm_gpuobj *, u32 offset, u32 data);
};

int  nvkm_gpuobj_new(struct nvkm_device *, u32 size, int align, bool zero,
		     struct nvkm_gpuobj *parent, struct nvkm_gpuobj **);
void nvkm_gpuobj_del(struct nvkm_gpuobj **);

static inline struct nvkm_gpuobj *
nv_gpuobj(void *obj)
{
#if CONFIG_NOUVEAU_DEBUG >= NV_DBG_PARANOIA
	BUG_ON(!nv_iclass(obj, NV_GPUOBJ_CLASS));
#endif
	return obj;
}

#define nvkm_gpuobj_create(p,e,c,v,g,s,a,f,d)                               \
	nvkm_gpuobj_create_((p), (e), (c), (v), (g), (s), (a), (f),         \
			       sizeof(**d), (void **)d)
#define nvkm_gpuobj_init(p) _nvkm_object_init(&(p)->object)
#define nvkm_gpuobj_fini(p,s) _nvkm_object_fini(&(p)->object, (s))
int  nvkm_gpuobj_create_(struct nvkm_object *, struct nvkm_object *,
			    struct nvkm_oclass *, u32 pclass,
			    struct nvkm_object *, u32 size, u32 align,
			    u32 flags, int length, void **);
void nvkm_gpuobj_destroy(struct nvkm_gpuobj *);

int nvkm_gpuobj_wrap(struct nvkm_memory *, struct nvkm_gpuobj **);
int nvkm_gpuobj_map(struct nvkm_gpuobj *, struct nvkm_vm *, u32 access,
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
