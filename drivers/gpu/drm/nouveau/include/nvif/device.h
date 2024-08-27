/* SPDX-License-Identifier: MIT */
#ifndef __NVIF_DEVICE_H__
#define __NVIF_DEVICE_H__

#include <nvif/object.h>
#include <nvif/cl0080.h>
#include <nvif/user.h>

struct nvif_device {
	struct nvif_object object;
	struct nv_device_info_v0 info;

	struct nvif_fifo_runlist {
		u64 engines;
	} *runlist;
	int runlists;

	struct nvif_user user;
};

int  nvif_device_ctor(struct nvif_client *, const char *name, struct nvif_device *);
void nvif_device_dtor(struct nvif_device *);
int  nvif_device_map(struct nvif_device *);
u64  nvif_device_time(struct nvif_device *);
#endif
