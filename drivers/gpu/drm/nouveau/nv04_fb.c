#include "drmP.h"
#include "drm.h"
#include "nouveau_drv.h"
#include "nouveau_drm.h"

int
nv04_fb_init(struct drm_device *dev)
{
	/* This is what the DDX did for NV_ARCH_04, but a mmio-trace shows
	 * nvidia reading PFB_CFG_0, then writing back its original value.
	 * (which was 0x701114 in this case)
	 */

	nv_wr32(dev, NV04_PFB_CFG0, 0x1114);
	return 0;
}

void
nv04_fb_takedown(struct drm_device *dev)
{
}
