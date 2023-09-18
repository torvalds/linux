/* SPDX-License-Identifier: MIT */
#ifndef __NVKM_GSP_PRIV_H__
#define __NVKM_GSP_PRIV_H__
#include <subdev/gsp.h>
enum nvkm_acr_lsf_id;

struct nvkm_gsp_fwif {
	int version;
	int (*load)(struct nvkm_gsp *, int ver, const struct nvkm_gsp_fwif *);
	const struct nvkm_gsp_func *func;
};

int gv100_gsp_nofw(struct nvkm_gsp *, int, const struct nvkm_gsp_fwif *);

struct nvkm_gsp_func {
	const struct nvkm_falcon_func *flcn;

	void (*dtor)(struct nvkm_gsp *);
	int (*oneinit)(struct nvkm_gsp *);
	int (*init)(struct nvkm_gsp *);
	int (*fini)(struct nvkm_gsp *, bool suspend);
};

int nvkm_gsp_new_(const struct nvkm_gsp_fwif *, struct nvkm_device *, enum nvkm_subdev_type, int,
		  struct nvkm_gsp **);

extern const struct nvkm_gsp_func gv100_gsp;
#endif
