#ifndef __NV50_KMS_OVLY_H__
#define __NV50_KMS_OVLY_H__
#include "wndw.h"

int ovly507e_new(struct nouveau_drm *, int, s32, struct nv50_wndw **);

int nv50_ovly_new(struct nouveau_drm *, int head, struct nv50_wndw **);
#endif
