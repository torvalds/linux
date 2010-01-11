#include "drmP.h"
#include "drm.h"
#include "nouveau_drv.h"
#include "nouveau_drm.h"

void
nv10_fb_set_region_tiling(struct drm_device *dev, int i, uint32_t addr,
			  uint32_t size, uint32_t pitch)
{
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	uint32_t limit = max(1u, addr + size) - 1;

	if (pitch) {
		if (dev_priv->card_type >= NV_20)
			addr |= 1;
		else
			addr |= 1 << 31;
	}

	nv_wr32(dev, NV10_PFB_TLIMIT(i), limit);
	nv_wr32(dev, NV10_PFB_TSIZE(i), pitch);
	nv_wr32(dev, NV10_PFB_TILE(i), addr);
}

int
nv10_fb_init(struct drm_device *dev)
{
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	struct nouveau_fb_engine *pfb = &dev_priv->engine.fb;
	int i;

	pfb->num_tiles = NV10_PFB_TILE__SIZE;

	/* Turn all the tiling regions off. */
	for (i = 0; i < pfb->num_tiles; i++)
		pfb->set_region_tiling(dev, i, 0, 0, 0);

	return 0;
}

void
nv10_fb_takedown(struct drm_device *dev)
{
}
