#ifndef __NV50_KMS_CORE_H__
#define __NV50_KMS_CORE_H__
#include "disp.h"
#include "atom.h"

struct nv50_core {
	const struct nv50_core_func *func;
	struct nv50_dmac chan;
};

int nv50_core_new(struct nouveau_drm *, struct nv50_core **);
void nv50_core_del(struct nv50_core **);

struct nv50_core_func {
	const struct nv50_head_func *head;
	const struct nv50_outp_func {
		void (*ctrl)(struct nv50_core *, int or, u32 ctrl,
			     struct nv50_head_atom *);
	} *dac, *pior, *sor;
};

int core507d_new(struct nouveau_drm *, s32, struct nv50_core **);
extern const struct nv50_outp_func dac507d;
extern const struct nv50_outp_func sor507d;
extern const struct nv50_outp_func pior507d;
#endif
