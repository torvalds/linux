#ifndef __NV50_KMS_OVLY_H__
#define __NV50_KMS_OVLY_H__
#include "wndw.h"

int ovly507e_new(struct nouveau_drm *, int, s32, struct nv50_wndw **);
int ovly507e_new_(const struct nv50_wndw_func *, const u32 *format,
		  struct nouveau_drm *, int head, s32 oclass,
		  u32 interlock_data, struct nv50_wndw **);

extern const u32 ovly827e_format[];

int ovly827e_new(struct nouveau_drm *, int, s32, struct nv50_wndw **);
int ovly907e_new(struct nouveau_drm *, int, s32, struct nv50_wndw **);

int nv50_ovly_new(struct nouveau_drm *, int head, struct nv50_wndw **);
#endif
