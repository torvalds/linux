#ifndef __NOUVEAU_NAMEDB_H__
#define __NOUVEAU_NAMEDB_H__

#include <core/parent.h>

struct nouveau_handle;

struct nouveau_namedb {
	struct nouveau_parent base;
	rwlock_t lock;
	struct list_head list;
};

static inline struct nouveau_namedb *
nv_namedb(void *obj)
{
#if CONFIG_NOUVEAU_DEBUG >= NV_DBG_PARANOIA
	if (unlikely(!nv_iclass(obj, NV_NAMEDB_CLASS)))
		nv_assert("BAD CAST -> NvNameDB, %08x", nv_hclass(obj));
#endif
	return obj;
}

#define nouveau_namedb_create(p,e,c,v,s,m,d)                                   \
	nouveau_namedb_create_((p), (e), (c), (v), (s), (m),                   \
			       sizeof(**d), (void **)d)
#define nouveau_namedb_init(p)                                                 \
	nouveau_parent_init(&(p)->base)
#define nouveau_namedb_fini(p,s)                                               \
	nouveau_parent_fini(&(p)->base, (s))
#define nouveau_namedb_destroy(p)                                              \
	nouveau_parent_destroy(&(p)->base)

int  nouveau_namedb_create_(struct nouveau_object *, struct nouveau_object *,
			    struct nouveau_oclass *, u32 pclass,
			    struct nouveau_oclass *, u64 engcls,
			    int size, void **);

int  _nouveau_namedb_ctor(struct nouveau_object *, struct nouveau_object *,
			  struct nouveau_oclass *, void *, u32,
			  struct nouveau_object **);
#define _nouveau_namedb_dtor _nouveau_parent_dtor
#define _nouveau_namedb_init _nouveau_parent_init
#define _nouveau_namedb_fini _nouveau_parent_fini

int  nouveau_namedb_insert(struct nouveau_namedb *, u32 name,
			   struct nouveau_object *, struct nouveau_handle *);
void nouveau_namedb_remove(struct nouveau_handle *);

struct nouveau_handle *nouveau_namedb_get(struct nouveau_namedb *, u32);
struct nouveau_handle *nouveau_namedb_get_class(struct nouveau_namedb *, u16);
struct nouveau_handle *nouveau_namedb_get_vinst(struct nouveau_namedb *, u64);
struct nouveau_handle *nouveau_namedb_get_cinst(struct nouveau_namedb *, u32);
void nouveau_namedb_put(struct nouveau_handle *);

#endif
