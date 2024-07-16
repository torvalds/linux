#ifndef __NV50_KMS_WIMM_H__
#define __NV50_KMS_WIMM_H__
#include "wndw.h"

int nv50_wimm_init(struct nouveau_drm *drm, struct nv50_wndw *);

int wimmc37b_init(struct nouveau_drm *, s32, struct nv50_wndw *);
#endif
