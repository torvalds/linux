#include "drmP.h"
#include "drm.h"
#include "nouveau_drv.h"
#include "nouveau_drm.h"

int
nv50_fb_init(struct drm_device *dev)
{
	struct drm_nouveau_private *dev_priv = dev->dev_private;

	/* Not a clue what this is exactly.  Without pointing it at a
	 * scratch page, VRAM->GART blits with M2MF (as in DDX DFS)
	 * cause IOMMU "read from address 0" errors (rh#561267)
	 */
	nv_wr32(dev, 0x100c08, dev_priv->gart_info.sg_dummy_bus >> 8);

	/* This is needed to get meaningful information from 100c90
	 * on traps. No idea what these values mean exactly. */
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
