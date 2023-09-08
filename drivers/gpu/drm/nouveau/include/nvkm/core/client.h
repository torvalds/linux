/* SPDX-License-Identifier: MIT */
#ifndef __NVKM_CLIENT_H__
#define __NVKM_CLIENT_H__
#define nvkm_client(p) container_of((p), struct nvkm_client, object)
#include <core/object.h>

struct nvkm_client {
	struct nvkm_object object;
	char name[32];
	u64 device;
	u32 debug;

	struct rb_root objroot;

	void *data;
	int (*event)(u64 token, void *argv, u32 argc);

	struct list_head umem;
	spinlock_t lock;
};

int  nvkm_client_new(const char *name, u64 device, const char *cfg, const char *dbg,
		     int (*)(u64, void *, u32), struct nvkm_client **);
struct nvkm_client *nvkm_client_search(struct nvkm_client *, u64 handle);

/* logging for client-facing objects */
#define nvif_printk(o,l,p,f,a...) do {                                         \
	const struct nvkm_object *_object = (o);                               \
	const struct nvkm_client *_client = _object->client;                   \
	if (_client->debug >= NV_DBG_##l)                                      \
		printk(KERN_##p "nouveau: %s:%08x:%08x: "f, _client->name,     \
		       _object->handle, _object->oclass, ##a);                 \
} while(0)
#define nvif_fatal(o,f,a...) nvif_printk((o), FATAL, CRIT, f, ##a)
#define nvif_error(o,f,a...) nvif_printk((o), ERROR,  ERR, f, ##a)
#define nvif_debug(o,f,a...) nvif_printk((o), DEBUG, INFO, f, ##a)
#define nvif_trace(o,f,a...) nvif_printk((o), TRACE, INFO, f, ##a)
#define nvif_info(o,f,a...)  nvif_printk((o),  INFO, INFO, f, ##a)
#define nvif_ioctl(o,f,a...) nvif_trace((o), "ioctl: "f, ##a)
#endif
