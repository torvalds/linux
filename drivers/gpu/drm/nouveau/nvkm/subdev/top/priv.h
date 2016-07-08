#ifndef __NVKM_TOP_PRIV_H__
#define __NVKM_TOP_PRIV_H__
#define nvkm_top(p) container_of((p), struct nvkm_top, subdev)
#include <subdev/top.h>

struct nvkm_top_func {
	int (*oneinit)(struct nvkm_top *);
};

int nvkm_top_new_(const struct nvkm_top_func *, struct nvkm_device *,
		  int, struct nvkm_top **);

struct nvkm_top_device {
	enum nvkm_devidx index;
	u32 addr;
	int fault;
	int engine;
	int runlist;
	int reset;
	int intr;
	struct list_head head;
};

struct nvkm_top_device *nvkm_top_device_new(struct nvkm_top *);
#endif
