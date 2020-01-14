#ifndef __NVKM_ACR_PRIV_H__
#define __NVKM_ACR_PRIV_H__
#include <subdev/acr.h>

struct nvkm_acr_fwif {
	int version;
	int (*load)(struct nvkm_acr *, int version,
		    const struct nvkm_acr_fwif *);
	const struct nvkm_acr_func *func;
};

int gm20b_acr_load(struct nvkm_acr *, int, const struct nvkm_acr_fwif *);
int gp102_acr_load(struct nvkm_acr *, int, const struct nvkm_acr_fwif *);

struct nvkm_acr_func {
};

int nvkm_acr_new_(const struct nvkm_acr_fwif *, struct nvkm_device *, int,
		  struct nvkm_acr **);

struct nvkm_acr_lsfw *nvkm_acr_lsfw_add(const struct nvkm_acr_lsf_func *,
					struct nvkm_acr *, struct nvkm_falcon *,
					enum nvkm_acr_lsf_id);
void nvkm_acr_lsfw_del(struct nvkm_acr_lsfw *);
void nvkm_acr_lsfw_del_all(struct nvkm_acr *);
#endif
