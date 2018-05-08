#ifndef __NV50_KMS_BASE_H__
#define __NV50_KMS_BASE_H__
#include "wndw.h"

int base507c_new(struct nouveau_drm *, int, s32, struct nv50_wndw **);

int nv50_base_new(struct nouveau_drm *, int head, struct nv50_wndw **);
#endif
