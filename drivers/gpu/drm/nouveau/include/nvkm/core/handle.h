#ifndef __NVKM_HANDLE_H__
#define __NVKM_HANDLE_H__
#include <core/os.h>
struct nvkm_object;

struct nvkm_handle {
	struct nvkm_namedb *namedb;
	struct list_head node;

	struct list_head head;
	struct list_head tree;
	u32 name;
	u32 priv;

	u8  route;
	u64 token;

	struct nvkm_handle *parent;
	struct nvkm_object *object;
};

int  nvkm_handle_create(struct nvkm_object *, u32 parent, u32 handle,
			struct nvkm_object *, struct nvkm_handle **);
void nvkm_handle_destroy(struct nvkm_handle *);
int  nvkm_handle_init(struct nvkm_handle *);
int  nvkm_handle_fini(struct nvkm_handle *, bool suspend);

struct nvkm_object *nvkm_handle_ref(struct nvkm_object *, u32 name);

struct nvkm_handle *nvkm_handle_get_class(struct nvkm_object *, u16);
struct nvkm_handle *nvkm_handle_get_vinst(struct nvkm_object *, u64);
struct nvkm_handle *nvkm_handle_get_cinst(struct nvkm_object *, u32);
void nvkm_handle_put(struct nvkm_handle *);
#endif
