#include "nouveau_revcompat.h"
#include "nouveau_drv.h"
#include "nv50_display.h"

struct nouveau_drm *
nouveau_newpriv(struct drm_device *dev)
{
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	return dev_priv->newpriv;
}

struct nouveau_bo *
nv50sema(struct drm_device *dev, int crtc)
{
	return nv50_display(dev)->crtc[crtc].sem.bo;
}

struct nouveau_bo *
nvd0sema(struct drm_device *dev, int crtc)
{
	return nvd0_display_crtc_sema(dev, crtc);
}
