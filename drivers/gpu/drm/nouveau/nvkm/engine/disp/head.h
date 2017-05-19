#ifndef __NVKM_DISP_HEAD_H__
#define __NVKM_DISP_HEAD_H__
#include "priv.h"

struct nvkm_head {
	const struct nvkm_head_func *func;
	struct nvkm_disp *disp;
	int id;

	struct list_head head;

	struct nvkm_head_state {
		u16 htotal;
		u16 hsynce;
		u16 hblanke;
		u16 hblanks;
		u16 vtotal;
		u16 vsynce;
		u16 vblanke;
		u16 vblanks;
		u32 hz;

		/* Prior to GF119, these are set by the OR. */
		struct {
			u8 depth;
		} or;
	} arm, asy;
};

int nvkm_head_new_(const struct nvkm_head_func *, struct nvkm_disp *, int id);
void nvkm_head_del(struct nvkm_head **);
int nvkm_head_mthd_scanoutpos(struct nvkm_object *,
			      struct nvkm_head *, void *, u32);
struct nvkm_head *nvkm_head_find(struct nvkm_disp *, int id);

struct nvkm_head_func {
	void (*state)(struct nvkm_head *, struct nvkm_head_state *);
	void (*rgpos)(struct nvkm_head *, u16 *hline, u16 *vline);
	void (*rgclk)(struct nvkm_head *, int div);
	void (*vblank_get)(struct nvkm_head *);
	void (*vblank_put)(struct nvkm_head *);
};

void nv50_head_rgpos(struct nvkm_head *, u16 *, u16 *);

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
