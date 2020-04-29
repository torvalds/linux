#ifndef __NV50_KMS_CORE_H__
#define __NV50_KMS_CORE_H__
#include "disp.h"
#include "atom.h"

struct nv50_core {
	const struct nv50_core_func *func;
	struct nv50_dmac chan;
	bool assign_windows;
};

int nv50_core_new(struct nouveau_drm *, struct nv50_core **);
void nv50_core_del(struct nv50_core **);

struct nv50_core_func {
	void (*init)(struct nv50_core *);
	void (*ntfy_init)(struct nouveau_bo *, u32 offset);
	int (*ntfy_wait_done)(struct nouveau_bo *, u32 offset,
			      struct nvif_device *);
	void (*update)(struct nv50_core *, u32 *interlock, bool ntfy);

	struct {
		void (*owner)(struct nv50_core *);
	} wndw;

	const struct nv50_head_func *head;
	const struct nv50_outp_func {
		void (*ctrl)(struct nv50_core *, int or, u32 ctrl,
			     struct nv50_head_atom *);
	} *dac, *pior, *sor;
};

int core507d_new(struct nouveau_drm *, s32, struct nv50_core **);
int core507d_new_(const struct nv50_core_func *, struct nouveau_drm *, s32,
		  struct nv50_core **);
void core507d_init(struct nv50_core *);
void core507d_ntfy_init(struct nouveau_bo *, u32);
int core507d_ntfy_wait_done(struct nouveau_bo *, u32, struct nvif_device *);
void core507d_update(struct nv50_core *, u32 *, bool);

extern const struct nv50_outp_func dac507d;
extern const struct nv50_outp_func sor507d;
extern const struct nv50_outp_func pior507d;

int core827d_new(struct nouveau_drm *, s32, struct nv50_core **);

int core907d_new(struct nouveau_drm *, s32, struct nv50_core **);
extern const struct nv50_outp_func dac907d;
extern const struct nv50_outp_func sor907d;

int core917d_new(struct nouveau_drm *, s32, struct nv50_core **);

int corec37d_new(struct nouveau_drm *, s32, struct nv50_core **);
int corec37d_ntfy_wait_done(struct nouveau_bo *, u32, struct nvif_device *);
void corec37d_update(struct nv50_core *, u32 *, bool);
void corec37d_wndw_owner(struct nv50_core *);
extern const struct nv50_outp_func sorc37d;

int corec57d_new(struct nouveau_drm *, s32, struct nv50_core **);
#endif
