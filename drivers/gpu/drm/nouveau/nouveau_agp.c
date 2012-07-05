#include <linux/module.h>

#include "drmP.h"
#include "drm.h"

#include "nouveau_drv.h"
#include "nouveau_agp.h"

#if __OS_HAS_AGP
MODULE_PARM_DESC(agpmode, "AGP mode (0 to disable AGP)");
static int nouveau_agpmode = -1;
module_param_named(agpmode, nouveau_agpmode, int, 0400);

static unsigned long
get_agp_mode(struct drm_device *dev, unsigned long mode)
{
	struct drm_nouveau_private *dev_priv = dev->dev_private;

	/*
	 * FW seems to be broken on nv18, it makes the card lock up
	 * randomly.
	 */
	if (dev_priv->chipset == 0x18)
		mode &= ~PCI_AGP_COMMAND_FW;

	/*
	 * AGP mode set in the command line.
	 */
	if (nouveau_agpmode > 0) {
		bool agpv3 = mode & 0x8;
		int rate = agpv3 ? nouveau_agpmode / 4 : nouveau_agpmode;

		mode = (mode & ~0x7) | (rate & 0x7);
	}

	return mode;
}

static bool
nouveau_agp_enabled(struct drm_device *dev)
{
	struct drm_nouveau_private *dev_priv = dev->dev_private;

	if (!drm_pci_device_is_agp(dev) || !dev->agp)
		return false;

	switch (dev_priv->gart_info.type) {
	case NOUVEAU_GART_NONE:
		if (!nouveau_agpmode)
			return false;
		break;
	case NOUVEAU_GART_AGP:
		break;
	default:
		return false;
	}

	return true;
}
#endif

void
nouveau_agp_reset(struct drm_device *dev)
{
#if __OS_HAS_AGP
	u32 save[2];
	int ret;

	if (!nouveau_agp_enabled(dev))
		return;

	/* First of all, disable fast writes, otherwise if it's
	 * already enabled in the AGP bridge and we disable the card's
	 * AGP controller we might be locking ourselves out of it. */
	if ((nv_rd32(dev, NV04_PBUS_PCI_NV_19) |
	     dev->agp->mode) & PCI_AGP_COMMAND_FW) {
		struct drm_agp_info info;
		struct drm_agp_mode mode;

		ret = drm_agp_info(dev, &info);
		if (ret)
			return;

		mode.mode  = get_agp_mode(dev, info.mode);
		mode.mode &= ~PCI_AGP_COMMAND_FW;

		ret = drm_agp_enable(dev, mode);
		if (ret)
			return;
	}


	/* clear busmaster bit, and disable AGP */
	save[0] = nv_mask(dev, NV04_PBUS_PCI_NV_1, 0x00000004, 0x00000000);
	nv_wr32(dev, NV04_PBUS_PCI_NV_19, 0);

	/* reset PGRAPH, PFIFO and PTIMER */
	save[1] = nv_mask(dev, 0x000200, 0x00011100, 0x00000000);
	nv_mask(dev, 0x000200, 0x00011100, save[1]);

	/* and restore bustmaster bit (gives effect of resetting AGP) */
	nv_wr32(dev, NV04_PBUS_PCI_NV_1, save[0]);
#endif
}

void
nouveau_agp_init(struct drm_device *dev)
{
#if __OS_HAS_AGP
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	struct drm_agp_info info;
	struct drm_agp_mode mode;
	int ret;

	if (!nouveau_agp_enabled(dev))
		return;

	ret = drm_agp_acquire(dev);
	if (ret) {
		NV_ERROR(dev, "Unable to acquire AGP: %d\n", ret);
		return;
	}

	ret = drm_agp_info(dev, &info);
	if (ret) {
		NV_ERROR(dev, "Unable to get AGP info: %d\n", ret);
		return;
	}

	/* see agp.h for the AGPSTAT_* modes available */
	mode.mode = get_agp_mode(dev, info.mode);

	ret = drm_agp_enable(dev, mode);
	if (ret) {
		NV_ERROR(dev, "Unable to enable AGP: %d\n", ret);
		return;
	}

	dev_priv->gart_info.type = NOUVEAU_GART_AGP;
	dev_priv->gart_info.aper_base = info.aperture_base;
	dev_priv->gart_info.aper_size = info.aperture_size;
#endif
}

void
nouveau_agp_fini(struct drm_device *dev)
{
#if __OS_HAS_AGP
	if (dev->agp && dev->agp->acquired)
		drm_agp_release(dev);
#endif
}
