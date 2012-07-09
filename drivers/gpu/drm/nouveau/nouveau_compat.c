#include "nouveau_drm.h"
#include "nouveau_compat.h"

void *nouveau_newpriv(struct drm_device *);

u8
_nv_rd08(struct drm_device *dev, u32 reg)
{
	struct nouveau_drm *drm = nouveau_newpriv(dev);
	return nv_ro08(drm->device, reg);
}

void
_nv_wr08(struct drm_device *dev, u32 reg, u8 val)
{
	struct nouveau_drm *drm = nouveau_newpriv(dev);
	nv_wo08(drm->device, reg, val);
}

u32
_nv_rd32(struct drm_device *dev, u32 reg)
{
	struct nouveau_drm *drm = nouveau_newpriv(dev);
	return nv_ro32(drm->device, reg);
}

void
_nv_wr32(struct drm_device *dev, u32 reg, u32 val)
{
	struct nouveau_drm *drm = nouveau_newpriv(dev);
	nv_wo32(drm->device, reg, val);
}

u32
_nv_mask(struct drm_device *dev, u32 reg, u32 mask, u32 val)
{
	u32 tmp = _nv_rd32(dev, reg);
	_nv_wr32(dev, reg, (tmp & ~mask) | val);
	return tmp;
}
