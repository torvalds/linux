#ifndef __NOUVEAU_REVCOMPAT_H__
#define __NOUVEAU_REVCOMPAT_H__

#include "drmP.h"

struct nouveau_drm *
nouveau_newpriv(struct drm_device *);

struct nouveau_bo *nv50sema(struct drm_device *dev, int crtc);
struct nouveau_bo *nvd0sema(struct drm_device *dev, int crtc);

#endif
