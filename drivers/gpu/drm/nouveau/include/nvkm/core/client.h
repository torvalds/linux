#ifndef __NVKM_CLIENT_H__
#define __NVKM_CLIENT_H__
#include <core/object.h>

struct nvkm_client {
	struct nvkm_object object;
	char name[32];
	u64 device;
	u32 debug;

	struct nvkm_client_notify *notify[16];
	struct rb_root objroot;

	struct nvkm_handle *root;

	bool super;
	void *data;
	int (*ntfy)(const void *, u32, const void *, u32);

	struct nvkm_vm *vm;
};

bool nvkm_client_insert(struct nvkm_client *, struct nvkm_handle *);
void nvkm_client_remove(struct nvkm_client *, struct nvkm_handle *);
struct nvkm_handle *nvkm_client_search(struct nvkm_client *, u64 handle);

int  nvkm_client_new(const char *name, u64 device, const char *cfg,
		     const char *dbg, struct nvkm_client **);
void nvkm_client_del(struct nvkm_client **);
int  nvkm_client_init(struct nvkm_client *);
int  nvkm_client_fini(struct nvkm_client *, bool suspend);
const char *nvkm_client_name(void *obj);

static inline struct nvkm_client *
nvkm_client(struct nvkm_object *object)
{
	while (object && object->parent)
		object = object->parent;
	return container_of(object, struct nvkm_client, object);
}

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
