#include "drmP.h"
#include "drm.h"
#include "nouveau_drv.h"
#include "nouveau_drm.h"

int
nv40_mc_init(struct drm_device *dev)
{
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	uint32_t tmp;

	/* Power up everything, resetting each individual unit will
	 * be done later if needed.
	 */
	nv_wr32(dev, NV03_PMC_ENABLE, 0xFFFFFFFF);

	switch (dev_priv->chipset) {
	case 0x44:
	case 0x46: /* G72 */
	case 0x4e:
	case 0x4c: /* C51_G7X */
		tmp = nv_rd32(dev, NV04_PFB_FIFO_DATA);
		nv_wr32(dev, NV40_PMC_1700, tmp);
		nv_wr32(dev, NV40_PMC_1704, 0);
		nv_wr32(dev, NV40_PMC_1708, 0);
		nv_wr32(dev, NV40_PMC_170C, tmp);
		break;
	default:
		break;
	}

	return 0;
}

void
nv40_mc_takedown(struct drm_device *dev)
{
}
