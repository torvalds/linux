#ifndef __NV50_KMS_OVLY_H__
#define __NV50_KMS_OVLY_H__
#include "wndw.h"

int ovly507e_new(struct nouveau_drm *, int, s32, struct nv50_wndw **);
int ovly507e_new_(const struct nv50_wndw_func *, const u32 *format,
		  struct nouveau_drm *, int head, s32 oclass,
		  u32 interlock_data, struct nv50_wndw **);
int ovly507e_acquire(struct nv50_wndw *, struct nv50_wndw_atom *,
		     struct nv50_head_atom *);
void ovly507e_release(struct nv50_wndw *, struct nv50_wndw_atom *,
		      struct nv50_head_atom *);
int ovly507e_scale_set(struct nv50_wndw *, struct nv50_wndw_atom *);

extern const u32 ovly827e_format[];
void ovly827e_ntfy_reset(struct nouveau_bo *, u32);
int ovly827e_ntfy_wait_begun(struct nouveau_bo *, u32, struct nvif_device *);

extern const struct nv50_wndw_func ovly907e;

int ovly827e_new(struct nouveau_drm *, int, s32, struct nv50_wndw **);
int ovly907e_new(struct nouveau_drm *, int, s32, struct nv50_wndw **);
int ovly917e_new(struct nouveau_drm *, int, s32, struct nv50_wndw **);

int nv50_ovly_new(struct nouveau_drm *, int head, struct nv50_wndw **);
#endif
