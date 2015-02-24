#ifndef __NVKM_NAMEDB_H__
#define __NVKM_NAMEDB_H__
#include <core/parent.h>
struct nvkm_handle;

struct nvkm_namedb {
	struct nvkm_parent parent;
	rwlock_t lock;
	struct list_head list;
};

static inline struct nvkm_namedb *
nv_namedb(void *obj)
{
#if CONFIG_NOUVEAU_DEBUG >= NV_DBG_PARANOIA
	if (unlikely(!nv_iclass(obj, NV_NAMEDB_CLASS)))
		nv_assert("BAD CAST -> NvNameDB, %08x", nv_hclass(obj));
#endif
	return obj;
}

#define nvkm_namedb_create(p,e,c,v,s,m,d)                                   \
	nvkm_namedb_create_((p), (e), (c), (v), (s), (m),                   \
			       sizeof(**d), (void **)d)
#define nvkm_namedb_init(p)                                                 \
	nvkm_parent_init(&(p)->parent)
#define nvkm_namedb_fini(p,s)                                               \
	nvkm_parent_fini(&(p)->parent, (s))
#define nvkm_namedb_destroy(p)                                              \
	nvkm_parent_destroy(&(p)->parent)

int  nvkm_namedb_create_(struct nvkm_object *, struct nvkm_object *,
			    struct nvkm_oclass *, u32 pclass,
			    struct nvkm_oclass *, u64 engcls,
			    int size, void **);

int  _nvkm_namedb_ctor(struct nvkm_object *, struct nvkm_object *,
			  struct nvkm_oclass *, void *, u32,
			  struct nvkm_object **);
#define _nvkm_namedb_dtor _nvkm_parent_dtor
#define _nvkm_namedb_init _nvkm_parent_init
#define _nvkm_namedb_fini _nvkm_parent_fini

int  nvkm_namedb_insert(struct nvkm_namedb *, u32 name, struct nvkm_object *,
			struct nvkm_handle *);
void nvkm_namedb_remove(struct nvkm_handle *);

struct nvkm_handle *nvkm_namedb_get(struct nvkm_namedb *, u32);
struct nvkm_handle *nvkm_namedb_get_class(struct nvkm_namedb *, u16);
struct nvkm_handle *nvkm_namedb_get_vinst(struct nvkm_namedb *, u64);
struct nvkm_handle *nvkm_namedb_get_cinst(struct nvkm_namedb *, u32);
void nvkm_namedb_put(struct nvkm_handle *);
#endif
