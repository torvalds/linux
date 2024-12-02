/* SPDX-License-Identifier: MIT */
#ifndef __NVIF_CLIENT_H__
#define __NVIF_CLIENT_H__

#include <nvif/object.h>

struct nvif_client {
	struct nvif_object object;
	const struct nvif_driver *driver;
	u64 version;
	u8 route;
};

int  nvif_client_ctor(struct nvif_client *parent, const char *name, u64 device,
		      struct nvif_client *);
void nvif_client_dtor(struct nvif_client *);
int  nvif_client_ioctl(struct nvif_client *, void *, u32);
int  nvif_client_suspend(struct nvif_client *);
int  nvif_client_resume(struct nvif_client *);

/*XXX*/
#include <core/client.h>
#define nvxx_client(a) ({                                                      \
	struct nvif_client *_client = (a);                                     \
	(struct nvkm_client *)_client->object.priv;                            \
})
#endif
