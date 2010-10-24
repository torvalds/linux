#include "drmP.h"
#include "drm.h"
#include "nouveau_drv.h"
#include "nouveau_drm.h"

void
nv10_fb_init_tile_region(struct drm_device *dev, int i, uint32_t addr,
			 uint32_t size, uint32_t pitch, uint32_t flags)
{
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	struct nouveau_tile_reg *tile = &dev_priv->tile.reg[i];

	tile->addr = addr;
	tile->limit = max(1u, addr + size) - 1;
	tile->pitch = pitch;

	if (dev_priv->card_type == NV_20)
		tile->addr |= 1;
	else
		tile->addr |= 1 << 31;
}

void
nv10_fb_free_tile_region(struct drm_device *dev, int i)
{
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	struct nouveau_tile_reg *tile = &dev_priv->tile.reg[i];

	tile->addr = tile->limit = tile->pitch = 0;
}

void
nv10_fb_set_tile_region(struct drm_device *dev, int i)
{
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	struct nouveau_tile_reg *tile = &dev_priv->tile.reg[i];

	nv_wr32(dev, NV10_PFB_TLIMIT(i), tile->limit);
	nv_wr32(dev, NV10_PFB_TSIZE(i), tile->pitch);
	nv_wr32(dev, NV10_PFB_TILE(i), tile->addr);
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
		pfb->set_tile_region(dev, i);

	return 0;
}

void
nv10_fb_takedown(struct drm_device *dev)
{
}
