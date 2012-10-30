#ifndef __NOUVEAU_CLIENT_H__
#define __NOUVEAU_CLIENT_H__

#include <core/namedb.h>

struct nouveau_client {
	struct nouveau_namedb base;
	struct nouveau_handle *root;
	struct nouveau_object *device;
	char name[16];
	u32 debug;
	struct nouveau_vm *vm;
};

static inline struct nouveau_client *
nv_client(void *obj)
{
#if CONFIG_NOUVEAU_DEBUG >= NV_DBG_PARANOIA
	if (unlikely(!nv_iclass(obj, NV_CLIENT_CLASS)))
		nv_assert("BAD CAST -> NvClient, %08x", nv_hclass(obj));
#endif
	return obj;
}

static inline struct nouveau_client *
nouveau_client(void *obj)
{
	struct nouveau_object *client = nv_object(obj);
	while (client && !(nv_iclass(client, NV_CLIENT_CLASS)))
		client = client->parent;
	return (void *)client;
}

#define nouveau_client_create(n,c,oc,od,d)                                     \
	nouveau_client_create_((n), (c), (oc), (od), sizeof(**d), (void **)d)

int  nouveau_client_create_(const char *name, u64 device, const char *cfg,
			    const char *dbg, int, void **);
int  nouveau_client_init(struct nouveau_client *);
int  nouveau_client_fini(struct nouveau_client *, bool suspend);

#endif
