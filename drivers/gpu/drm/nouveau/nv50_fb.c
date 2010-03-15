#include "drmP.h"
#include "drm.h"
#include "nouveau_drv.h"
#include "nouveau_drm.h"

int
nv50_fb_init(struct drm_device *dev)
{
	/* This is needed to get meaningful information from 100c90
	 * on traps. No idea what these values mean exactly. */
	struct drm_nouveau_private *dev_priv = dev->dev_private;

	switch (dev_priv->chipset) {
	case 0x50:
		nv_wr32(dev, 0x100c90, 0x0707ff);
		break;
	case 0xa5:
	case 0xa8:
		nv_wr32(dev, 0x100c90, 0x0d0fff);
		break;
	default:
		nv_wr32(dev, 0x100c90, 0x1d07ff);
		break;
	}

	return 0;
}

void
nv50_fb_takedown(struct drm_device *dev)
{
}
