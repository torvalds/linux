#include "drmP.h"
#include "drm.h"
#include "nouveau_drv.h"
#include "nouveau_drm.h"

void
nv40_fb_set_region_tiling(struct drm_device *dev, int i, uint32_t addr,
			  uint32_t size, uint32_t pitch)
{
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	uint32_t limit = max(1u, addr + size) - 1;

	if (pitch)
		addr |= 1;

	switch (dev_priv->chipset) {
	case 0x40:
		nv_wr32(dev, NV10_PFB_TLIMIT(i), limit);
		nv_wr32(dev, NV10_PFB_TSIZE(i), pitch);
		nv_wr32(dev, NV10_PFB_TILE(i), addr);
		break;

	default:
		nv_wr32(dev, NV40_PFB_TLIMIT(i), limit);
		nv_wr32(dev, NV40_PFB_TSIZE(i), pitch);
		nv_wr32(dev, NV40_PFB_TILE(i), addr);
		break;
	}
}

int
nv40_fb_init(struct drm_device *dev)
{
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	struct nouveau_fb_engine *pfb = &dev_priv->engine.fb;
	uint32_t tmp;
	int i;

	/* This is strictly a NV4x register (don't know about NV5x). */
	/* The blob sets these to all kinds of values, and they mess up our setup. */
	/* I got value 0x52802 instead. For some cards the blob even sets it back to 0x1. */
	/* Note: the blob doesn't read this value, so i'm pretty sure this is safe for all cards. */
	/* Any idea what this is? */
	nv_wr32(dev, NV40_PFB_UNK_800, 0x1);

	switch (dev_priv->chipset) {
	case 0x40:
	case 0x45:
		tmp = nv_rd32(dev, NV10_PFB_CLOSE_PAGE2);
		nv_wr32(dev, NV10_PFB_CLOSE_PAGE2, tmp & ~(1 << 15));
		pfb->num_tiles = NV10_PFB_TILE__SIZE;
		break;
	case 0x46: /* G72 */
	case 0x47: /* G70 */
	case 0x49: /* G71 */
	case 0x4b: /* G73 */
	case 0x4c: /* C51 (G7X version) */
		pfb->num_tiles = NV40_PFB_TILE__SIZE_1;
		break;
	default:
		pfb->num_tiles = NV40_PFB_TILE__SIZE_0;
		break;
	}

	/* Turn all the tiling regions off. */
	for (i = 0; i < pfb->num_tiles; i++)
		pfb->set_region_tiling(dev, i, 0, 0, 0);

	return 0;
}

void
nv40_fb_takedown(struct drm_device *dev)
{
}
