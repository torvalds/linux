#ifndef __NVKM_DISP_HEAD_H__
#define __NVKM_DISP_HEAD_H__
#include "priv.h"

struct nvkm_head {
	const struct nvkm_head_func *func;
	struct nvkm_disp *disp;
	int id;

	struct list_head head;
};

int nvkm_head_new_(const struct nvkm_head_func *, struct nvkm_disp *, int id);
void nvkm_head_del(struct nvkm_head **);
struct nvkm_head *nvkm_head_find(struct nvkm_disp *, int id);

struct nvkm_head_func {
};

#define HEAD_MSG(h,l,f,a...) do {                                              \
	struct nvkm_head *_h = (h);                                            \
	nvkm_##l(&_h->disp->engine.subdev, "head-%d: "f"\n", _h->id, ##a);     \
} while(0)
#define HEAD_WARN(h,f,a...) HEAD_MSG((h), warn, f, ##a)
#define HEAD_DBG(h,f,a...) HEAD_MSG((h), debug, f, ##a)

int nv04_head_new(struct nvkm_disp *, int id);
int nv50_head_new(struct nvkm_disp *, int id);
int gf119_head_new(struct nvkm_disp *, int id);
#endif
