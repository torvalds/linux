#include "drmP.h"
#include "drm.h"
#include "nouveau_drv.h"
#include "nouveau_drm.h"

int
nv40_fb_init(struct drm_device *dev)
{
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	uint32_t fb_bar_size, tmp;
	int num_tiles;
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
		num_tiles = NV10_PFB_TILE__SIZE;
		break;
	case 0x46: /* G72 */
	case 0x47: /* G70 */
	case 0x49: /* G71 */
	case 0x4b: /* G73 */
	case 0x4c: /* C51 (G7X version) */
		num_tiles = NV40_PFB_TILE__SIZE_1;
		break;
	default:
		num_tiles = NV40_PFB_TILE__SIZE_0;
		break;
	}

	fb_bar_size = drm_get_resource_len(dev, 0) - 1;
	switch (dev_priv->chipset) {
	case 0x40:
		for (i = 0; i < num_tiles; i++) {
			nv_wr32(dev, NV10_PFB_TILE(i), 0);
			nv_wr32(dev, NV10_PFB_TLIMIT(i), fb_bar_size);
		}
		break;
	default:
		for (i = 0; i < num_tiles; i++) {
			nv_wr32(dev, NV40_PFB_TILE(i), 0);
			nv_wr32(dev, NV40_PFB_TLIMIT(i), fb_bar_size);
		}
		break;
	}

	return 0;
}

void
nv40_fb_takedown(struct drm_device *dev)
{
}
