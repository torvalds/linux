#include "drmP.h"
#include "drm.h"
#include "nouveau_drv.h"
#include "nouveau_drm.h"

int
nv04_mc_init(struct drm_device *dev)
{
	/* Power up everything, resetting each individual unit will
	 * be done later if needed.
	 */

	nv_wr32(dev, NV03_PMC_ENABLE, 0xFFFFFFFF);

	/* Disable PROM access. */
	nv_wr32(dev, NV_PBUS_PCI_NV_20, NV_PBUS_PCI_NV_20_ROM_SHADOW_ENABLED);

	return 0;
}

void
nv04_mc_takedown(struct drm_device *dev)
{
}
