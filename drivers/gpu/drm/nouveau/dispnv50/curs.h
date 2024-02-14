#ifndef __NV50_KMS_CURS_H__
#define __NV50_KMS_CURS_H__
#include "wndw.h"

int curs507a_new(struct nouveau_drm *, int, s32, struct nv50_wndw **);
int curs507a_new_(const struct nv50_wimm_func *, struct nouveau_drm *,
		  int head, s32 oclass, u32 interlock_data,
		  struct nv50_wndw **);

int curs907a_new(struct nouveau_drm *, int, s32, struct nv50_wndw **);
int cursc37a_new(struct nouveau_drm *, int, s32, struct nv50_wndw **);

int nv50_curs_new(struct nouveau_drm *, int head, struct nv50_wndw **);
#endif
