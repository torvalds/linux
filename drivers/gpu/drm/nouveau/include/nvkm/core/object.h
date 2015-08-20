#ifndef __NVKM_OBJECT_H__
#define __NVKM_OBJECT_H__
#include <core/os.h>
#include <core/debug.h>

#define NV_PARENT_CLASS 0x80000000
#define NV_NAMEDB_CLASS 0x40000000
#define NV_CLIENT_CLASS 0x20000000
#define NV_SUBDEV_CLASS 0x10000000
#define NV_ENGINE_CLASS 0x08000000
#define NV_MEMOBJ_CLASS 0x04000000
#define NV_GPUOBJ_CLASS 0x02000000
#define NV_ENGCTX_CLASS 0x01000000

struct nvkm_object {
	struct nvkm_oclass *oclass;
	u32 pclass;
	struct nvkm_object *parent;
	struct nvkm_engine *engine;
	atomic_t refcount;
	atomic_t usecount;
#if CONFIG_NOUVEAU_DEBUG >= NV_DBG_PARANOIA
#define NVKM_OBJECT_MAGIC 0x75ef0bad
	struct list_head list;
	u32 _magic;
#endif
};

int nvkm_object_rd08(struct nvkm_object *, u64 addr, u8  *data);
int nvkm_object_rd16(struct nvkm_object *, u64 addr, u16 *data);
int nvkm_object_rd32(struct nvkm_object *, u64 addr, u32 *data);
int nvkm_object_wr08(struct nvkm_object *, u64 addr, u8   data);
int nvkm_object_wr16(struct nvkm_object *, u64 addr, u16  data);
int nvkm_object_wr32(struct nvkm_object *, u64 addr, u32  data);

static inline struct nvkm_object *
nv_object(void *obj)
{
#if CONFIG_NOUVEAU_DEBUG >= NV_DBG_PARANOIA
	if (likely(obj)) {
		struct nvkm_object *object = obj;
		BUG_ON(object->_magic != NVKM_OBJECT_MAGIC);
	}
#endif
	return obj;
}

#define nvkm_object_create(p,e,c,s,d)                                       \
	nvkm_object_create_((p), (e), (c), (s), sizeof(**d), (void **)d)
int  nvkm_object_create_(struct nvkm_object *, struct nvkm_object *,
			    struct nvkm_oclass *, u32, int size, void **);
void nvkm_object_destroy(struct nvkm_object *);
int  nvkm_object_init(struct nvkm_object *);
int  nvkm_object_fini(struct nvkm_object *, bool suspend);

int _nvkm_object_ctor(struct nvkm_object *, struct nvkm_object *,
			 struct nvkm_oclass *, void *, u32,
			 struct nvkm_object **);

extern struct nvkm_ofuncs nvkm_object_ofuncs;

/* Don't allocate dynamically, because lockdep needs lock_class_keys to be in
 * ".data". */
struct nvkm_oclass {
	s32 handle;
	struct nvkm_ofuncs * const ofuncs;
	struct nvkm_omthds * const omthds;
	struct lock_class_key lock_class_key;
};

#define nv_oclass(o)    nv_object(o)->oclass
#define nv_hclass(o)    nv_oclass(o)->handle
#define nv_iclass(o,i) (nv_object(o)->pclass & (i))
#define nv_mclass(o)    nv_oclass(o)->handle

static inline struct nvkm_object *
nv_pclass(struct nvkm_object *parent, u32 oclass)
{
	while (parent && !nv_iclass(parent, oclass))
		parent = parent->parent;
	return parent;
}

struct nvkm_omthds {
	u32 start;
	u32 limit;
	int (*call)(struct nvkm_object *, u32, void *, u32);
};

struct nvkm_event;
struct nvkm_ofuncs {
	int  (*ctor)(struct nvkm_object *, struct nvkm_object *,
		     struct nvkm_oclass *, void *data, u32 size,
		     struct nvkm_object **);
	void (*dtor)(struct nvkm_object *);
	int  (*init)(struct nvkm_object *);
	int  (*fini)(struct nvkm_object *, bool suspend);
	int  (*mthd)(struct nvkm_object *, u32, void *, u32);
	int  (*ntfy)(struct nvkm_object *, u32, struct nvkm_event **);
	int  (* map)(struct nvkm_object *, u64 *, u32 *);
	u8   (*rd08)(struct nvkm_object *, u64 offset);
	u16  (*rd16)(struct nvkm_object *, u64 offset);
	u32  (*rd32)(struct nvkm_object *, u64 offset);
	void (*wr08)(struct nvkm_object *, u64 offset, u8 data);
	void (*wr16)(struct nvkm_object *, u64 offset, u16 data);
	void (*wr32)(struct nvkm_object *, u64 offset, u32 data);
};

static inline struct nvkm_ofuncs *
nv_ofuncs(void *obj)
{
	return nv_oclass(obj)->ofuncs;
}

int  nvkm_object_ctor(struct nvkm_object *, struct nvkm_object *,
		      struct nvkm_oclass *, void *, u32,
		      struct nvkm_object **);
void nvkm_object_ref(struct nvkm_object *, struct nvkm_object **);
int  nvkm_object_inc(struct nvkm_object *);
int  nvkm_object_dec(struct nvkm_object *, bool suspend);

static inline int
nv_exec(void *obj, u32 mthd, void *data, u32 size)
{
	struct nvkm_omthds *method = nv_oclass(obj)->omthds;

	while (method && method->call) {
		if (mthd >= method->start && mthd <= method->limit)
			return method->call(obj, mthd, data, size);
		method++;
	}

	return -EINVAL;
}

static inline int
nv_call(void *obj, u32 mthd, u32 data)
{
	return nv_exec(obj, mthd, &data, sizeof(data));
}
#endif
