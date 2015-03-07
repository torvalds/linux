#ifndef __NVKM_CLIENT_H__
#define __NVKM_CLIENT_H__
#include <core/namedb.h>

struct nvkm_client {
	struct nvkm_namedb namedb;
	struct nvkm_handle *root;
	struct nvkm_object *device;
	char name[32];
	u32 debug;
	struct nvkm_vm *vm;
	bool super;
	void *data;

	int (*ntfy)(const void *, u32, const void *, u32);
	struct nvkm_client_notify *notify[16];
};

static inline struct nvkm_client *
nv_client(void *obj)
{
#if CONFIG_NOUVEAU_DEBUG >= NV_DBG_PARANOIA
	if (unlikely(!nv_iclass(obj, NV_CLIENT_CLASS)))
		nv_assert("BAD CAST -> NvClient, %08x", nv_hclass(obj));
#endif
	return obj;
}

static inline struct nvkm_client *
nvkm_client(void *obj)
{
	struct nvkm_object *client = nv_object(obj);
	while (client && !(nv_iclass(client, NV_CLIENT_CLASS)))
		client = client->parent;
	return (void *)client;
}

#define nvkm_client_create(n,c,oc,od,d)                                     \
	nvkm_client_create_((n), (c), (oc), (od), sizeof(**d), (void **)d)

int  nvkm_client_create_(const char *name, u64 device, const char *cfg,
			    const char *dbg, int, void **);
#define nvkm_client_destroy(p)                                              \
	nvkm_namedb_destroy(&(p)->base)

int  nvkm_client_init(struct nvkm_client *);
int  nvkm_client_fini(struct nvkm_client *, bool suspend);
const char *nvkm_client_name(void *obj);

int nvkm_client_notify_new(struct nvkm_object *, struct nvkm_event *,
			   void *data, u32 size);
int nvkm_client_notify_del(struct nvkm_client *, int index);
int nvkm_client_notify_get(struct nvkm_client *, int index);
int nvkm_client_notify_put(struct nvkm_client *, int index);
#endif
