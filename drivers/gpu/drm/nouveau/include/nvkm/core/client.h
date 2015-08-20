#ifndef __NVKM_CLIENT_H__
#define __NVKM_CLIENT_H__
#include <core/namedb.h>

struct nvkm_client {
	struct nvkm_namedb namedb;
	struct nvkm_handle *root;
	struct nvkm_device *device;
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
	BUG_ON(!nv_iclass(obj, NV_CLIENT_CLASS));
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

int  nvkm_client_new(const char *name, u64 device, const char *cfg,
		     const char *dbg, struct nvkm_client **);
void nvkm_client_del(struct nvkm_client **);
int  nvkm_client_init(struct nvkm_client *);
int  nvkm_client_fini(struct nvkm_client *, bool suspend);
const char *nvkm_client_name(void *obj);

int nvkm_client_notify_new(struct nvkm_object *, struct nvkm_event *,
			   void *data, u32 size);
int nvkm_client_notify_del(struct nvkm_client *, int index);
int nvkm_client_notify_get(struct nvkm_client *, int index);
int nvkm_client_notify_put(struct nvkm_client *, int index);

/* logging for client-facing objects */
#define nvif_printk(o,l,p,f,a...) do {                                         \
	struct nvkm_object *_object = (o);                                     \
	struct nvkm_client *_client = nvkm_client(_object);                    \
	if (_client->debug >= NV_DBG_##l)                                      \
		printk(KERN_##p "nouveau: %s: "f, _client->name, ##a);         \
} while(0)
#define nvif_error(o,f,a...) nvif_printk((o), ERROR,  ERR, f, ##a)
#define nvif_debug(o,f,a...) nvif_printk((o), DEBUG, INFO, f, ##a)
#define nvif_trace(o,f,a...) nvif_printk((o), TRACE, INFO, f, ##a)
#define nvif_ioctl(o,f,a...) nvif_trace((o), "ioctl: "f, ##a)
#endif
