#ifndef __NV50_KMS_CURS_H__
#define __NV50_KMS_CURS_H__
#include "wndw.h"

int curs507a_new(struct nouveau_drm *, int, s32, struct nv50_wndw **);

int nv50_curs_new(struct nouveau_drm *, int head, struct nv50_wndw **);
#endif
