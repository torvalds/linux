#ifndef __NOUVEAU_OBJECT_H__
#define __NOUVEAU_OBJECT_H__

#include <core/os.h>
#include <core/printk.h>

#define NV_PARENT_CLASS 0x80000000
#define NV_NAMEDB_CLASS 0x40000000
#define NV_CLIENT_CLASS 0x20000000
#define NV_SUBDEV_CLASS 0x10000000
#define NV_ENGINE_CLASS 0x08000000
#define NV_MEMOBJ_CLASS 0x04000000
#define NV_GPUOBJ_CLASS 0x02000000
#define NV_ENGCTX_CLASS 0x01000000
#define NV_OBJECT_CLASS 0x0000ffff

struct nouveau_object {
	struct nouveau_oclass *oclass;
	struct nouveau_object *parent;
	struct nouveau_object *engine;
	atomic_t refcount;
	atomic_t usecount;
#if CONFIG_NOUVEAU_DEBUG >= NV_DBG_PARANOIA
#define NOUVEAU_OBJECT_MAGIC 0x75ef0bad
	struct list_head list;
	u32 _magic;
#endif
};

static inline struct nouveau_object *
nv_object(void *obj)
{
#if CONFIG_NOUVEAU_DEBUG >= NV_DBG_PARANOIA
	if (likely(obj)) {
		struct nouveau_object *object = obj;
		if (unlikely(object->_magic != NOUVEAU_OBJECT_MAGIC))
			nv_assert("BAD CAST -> NvObject, invalid magic");
	}
#endif
	return obj;
}

#define nouveau_object_create(p,e,c,s,d)                                       \
	nouveau_object_create_((p), (e), (c), (s), sizeof(**d), (void **)d)
int  nouveau_object_create_(struct nouveau_object *, struct nouveau_object *,
			    struct nouveau_oclass *, u32, int size, void **);
void nouveau_object_destroy(struct nouveau_object *);
int  nouveau_object_init(struct nouveau_object *);
int  nouveau_object_fini(struct nouveau_object *, bool suspend);

extern struct nouveau_ofuncs nouveau_object_ofuncs;

/* Don't allocate dynamically, because lockdep needs lock_class_keys to be in
 * ".data". */
struct nouveau_oclass {
	u32 handle;
	struct nouveau_ofuncs * const ofuncs;
	struct nouveau_omthds * const omthds;
	struct lock_class_key lock_class_key;
};

#define nv_oclass(o)    nv_object(o)->oclass
#define nv_hclass(o)    nv_oclass(o)->handle
#define nv_iclass(o,i) (nv_hclass(o) & (i))
#define nv_mclass(o)    nv_iclass(o, NV_OBJECT_CLASS)

static inline struct nouveau_object *
nv_pclass(struct nouveau_object *parent, u32 oclass)
{
	while (parent && !nv_iclass(parent, oclass))
		parent = parent->parent;
	return parent;
}

struct nouveau_omthds {
	u32 start;
	u32 limit;
	int (*call)(struct nouveau_object *, u32, void *, u32);
};

struct nouveau_ofuncs {
	int  (*ctor)(struct nouveau_object *, struct nouveau_object *,
		     struct nouveau_oclass *, void *data, u32 size,
		     struct nouveau_object **);
	void (*dtor)(struct nouveau_object *);
	int  (*init)(struct nouveau_object *);
	int  (*fini)(struct nouveau_object *, bool suspend);
	u8   (*rd08)(struct nouveau_object *, u64 offset);
	u16  (*rd16)(struct nouveau_object *, u64 offset);
	u32  (*rd32)(struct nouveau_object *, u64 offset);
	void (*wr08)(struct nouveau_object *, u64 offset, u8 data);
	void (*wr16)(struct nouveau_object *, u64 offset, u16 data);
	void (*wr32)(struct nouveau_object *, u64 offset, u32 data);
};

static inline struct nouveau_ofuncs *
nv_ofuncs(void *obj)
{
	return nv_oclass(obj)->ofuncs;
}

int  nouveau_object_ctor(struct nouveau_object *, struct nouveau_object *,
			 struct nouveau_oclass *, void *, u32,
			 struct nouveau_object **);
void nouveau_object_ref(struct nouveau_object *, struct nouveau_object **);
int nouveau_object_inc(struct nouveau_object *);
int nouveau_object_dec(struct nouveau_object *, bool suspend);

int nouveau_object_new(struct nouveau_object *, u32 parent, u32 handle,
		       u16 oclass, void *data, u32 size,
		       struct nouveau_object **);
int nouveau_object_del(struct nouveau_object *, u32 parent, u32 handle);
void nouveau_object_debug(void);

static inline int
nv_exec(void *obj, u32 mthd, void *data, u32 size)
{
	struct nouveau_omthds *method = nv_oclass(obj)->omthds;

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

static inline u8
nv_ro08(void *obj, u64 addr)
{
	u8 data = nv_ofuncs(obj)->rd08(obj, addr);
	nv_spam(obj, "nv_ro08 0x%08x 0x%02x\n", addr, data);
	return data;
}

static inline u16
nv_ro16(void *obj, u64 addr)
{
	u16 data = nv_ofuncs(obj)->rd16(obj, addr);
	nv_spam(obj, "nv_ro16 0x%08x 0x%04x\n", addr, data);
	return data;
}

static inline u32
nv_ro32(void *obj, u64 addr)
{
	u32 data = nv_ofuncs(obj)->rd32(obj, addr);
	nv_spam(obj, "nv_ro32 0x%08x 0x%08x\n", addr, data);
	return data;
}

static inline void
nv_wo08(void *obj, u64 addr, u8 data)
{
	nv_spam(obj, "nv_wo08 0x%08x 0x%02x\n", addr, data);
	nv_ofuncs(obj)->wr08(obj, addr, data);
}

static inline void
nv_wo16(void *obj, u64 addr, u16 data)
{
	nv_spam(obj, "nv_wo16 0x%08x 0x%04x\n", addr, data);
	nv_ofuncs(obj)->wr16(obj, addr, data);
}

static inline void
nv_wo32(void *obj, u64 addr, u32 data)
{
	nv_spam(obj, "nv_wo32 0x%08x 0x%08x\n", addr, data);
	nv_ofuncs(obj)->wr32(obj, addr, data);
}

static inline u32
nv_mo32(void *obj, u64 addr, u32 mask, u32 data)
{
	u32 temp = nv_ro32(obj, addr);
	nv_wo32(obj, addr, (temp & ~mask) | data);
	return temp;
}

static inline int
nv_memcmp(void *obj, u32 addr, const char *str, u32 len)
{
	unsigned char c1, c2;

	while (len--) {
		c1 = nv_ro08(obj, addr++);
		c2 = *(str++);
		if (c1 != c2)
			return c1 - c2;
	}
	return 0;
}

#endif
