/* SPDX-License-Identifier: MIT */
#ifndef __NVIF_CLIENT_H__
#define __NVIF_CLIENT_H__

#include <nvif/object.h>

struct nvif_client {
	struct nvif_object object;
	const struct nvif_driver *driver;
};

int  nvif_client_ctor(struct nvif_client *parent, const char *name, struct nvif_client *);
void nvif_client_dtor(struct nvif_client *);
int  nvif_client_suspend(struct nvif_client *);
int  nvif_client_resume(struct nvif_client *);

/*XXX*/
#endif
