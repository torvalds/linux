#ifndef __NVIF_CLIENT_H__
#define __NVIF_CLIENT_H__

#include <nvif/object.h>

struct nvif_client {
	struct nvif_object base;
	struct nvif_object *object; /*XXX: hack for nvif_object() */
	const struct nvif_driver *driver;
	bool super;
};

static inline struct nvif_client *
nvif_client(struct nvif_object *object)
{
	while (object && object->parent != object)
		object = object->parent;
	return (void *)object;
}

int  nvif_client_init(void (*dtor)(struct nvif_client *), const char *,
		      const char *, u64, const char *, const char *,
		      struct nvif_client *);
void nvif_client_fini(struct nvif_client *);
int  nvif_client_new(const char *, const char *, u64, const char *,
		     const char *, struct nvif_client **);
void nvif_client_ref(struct nvif_client *, struct nvif_client **);
int  nvif_client_ioctl(struct nvif_client *, void *, u32);
int  nvif_client_suspend(struct nvif_client *);
int  nvif_client_resume(struct nvif_client *);

/*XXX*/
#include <core/client.h>
#define nvxx_client(a) ({ \
	struct nvif_client *_client = nvif_client(nvif_object(a)); \
	nvkm_client(_client->base.priv); \
})

#endif
