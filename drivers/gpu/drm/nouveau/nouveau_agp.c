#include <linux/module.h>

#include <core/device.h>

#include "nouveau_drm.h"
#include "nouveau_agp.h"
#include "nouveau_reg.h"

#if __OS_HAS_AGP
MODULE_PARM_DESC(agpmode, "AGP mode (0 to disable AGP)");
static int nouveau_agpmode = -1;
module_param_named(agpmode, nouveau_agpmode, int, 0400);

struct nouveau_agpmode_quirk {
	u16 hostbridge_vendor;
	u16 hostbridge_device;
	u16 chip_vendor;
	u16 chip_device;
	int mode;
};

static struct nouveau_agpmode_quirk nouveau_agpmode_quirk_list[] = {
	/* VIA Apollo PRO133x / GeForce FX 5600 Ultra, max agpmode 2, fdo #20341 */
	{ PCI_VENDOR_ID_VIA, 0x0691, PCI_VENDOR_ID_NVIDIA, 0x0311, 2 },

	{},
};

static unsigned long
get_agp_mode(struct nouveau_drm *drm, const struct drm_agp_info *info)
{
	struct nouveau_device *device = nv_device(drm->device);
	struct nouveau_agpmode_quirk *quirk = nouveau_agpmode_quirk_list;
	int agpmode = nouveau_agpmode;
	unsigned long mode = info->mode;

	/*
	 * FW seems to be broken on nv18, it makes the card lock up
	 * randomly.
	 */
	if (device->chipset == 0x18)
		mode &= ~PCI_AGP_COMMAND_FW;

	/*
	 * Go through the quirks list and adjust the agpmode accordingly.
	 */
	while (agpmode == -1 && quirk->hostbridge_vendor) {
		if (info->id_vendor == quirk->hostbridge_vendor &&
		    info->id_device == quirk->hostbridge_device &&
		    device->pdev->vendor == quirk->chip_vendor &&
		    device->pdev->device == quirk->chip_device) {
			agpmode = quirk->mode;
			nv_info(device, "Forcing agp mode to %dX. Use agpmode to override.\n",
				agpmode);
			break;
		}
		++quirk;
	}

	/*
	 * AGP mode set in the command line.
	 */
	if (agpmode > 0) {
		bool agpv3 = mode & 0x8;
		int rate = agpv3 ? agpmode / 4 : agpmode;

		mode = (mode & ~0x7) | (rate & 0x7);
	}

	return mode;
}

static bool
nouveau_agp_enabled(struct nouveau_drm *drm)
{
	struct drm_device *dev = drm->dev;

	if (!dev->pdev || !drm_pci_device_is_agp(dev) || !dev->agp)
		return false;

	if (drm->agp.stat == UNKNOWN) {
		if (!nouveau_agpmode)
			return false;
#ifdef __powerpc__
		/* Disable AGP by default on all PowerPC machines for
		 * now -- At least some UniNorth-2 AGP bridges are
		 * known to be broken: DMA from the host to the card
		 * works just fine, but writeback from the card to the
		 * host goes straight to memory untranslated bypassing
		 * the GATT somehow, making them quite painful to deal
		 * with...
		 */
		if (nouveau_agpmode == -1)
			return false;
#endif
		return true;
	}

	return (drm->agp.stat == ENABLED);
}
#endif

void
nouveau_agp_reset(struct nouveau_drm *drm)
{
#if __OS_HAS_AGP
	struct nouveau_device *device = nv_device(drm->device);
	struct drm_device *dev = drm->dev;
	u32 save[2];
	int ret;

	if (!nouveau_agp_enabled(drm))
		return;

	/* First of all, disable fast writes, otherwise if it's
	 * already enabled in the AGP bridge and we disable the card's
	 * AGP controller we might be locking ourselves out of it. */
	if ((nv_rd32(device, NV04_PBUS_PCI_NV_19) |
	     dev->agp->mode) & PCI_AGP_COMMAND_FW) {
		struct drm_agp_info info;
		struct drm_agp_mode mode;

		ret = drm_agp_info(dev, &info);
		if (ret)
			return;

		mode.mode  = get_agp_mode(drm, &info);
		mode.mode &= ~PCI_AGP_COMMAND_FW;

		ret = drm_agp_enable(dev, mode);
		if (ret)
			return;
	}


	/* clear busmaster bit, and disable AGP */
	save[0] = nv_mask(device, NV04_PBUS_PCI_NV_1, 0x00000004, 0x00000000);
	nv_wr32(device, NV04_PBUS_PCI_NV_19, 0);

	/* reset PGRAPH, PFIFO and PTIMER */
	save[1] = nv_mask(device, 0x000200, 0x00011100, 0x00000000);
	nv_mask(device, 0x000200, 0x00011100, save[1]);

	/* and restore bustmaster bit (gives effect of resetting AGP) */
	nv_wr32(device, NV04_PBUS_PCI_NV_1, save[0]);
#endif
}

void
nouveau_agp_init(struct nouveau_drm *drm)
{
#if __OS_HAS_AGP
	struct nouveau_device *device = nv_device(drm->device);
	struct drm_device *dev = drm->dev;
	struct drm_agp_info info;
	struct drm_agp_mode mode;
	int ret;

	if (!nouveau_agp_enabled(drm))
		return;
	drm->agp.stat = DISABLE;

	ret = drm_agp_acquire(dev);
	if (ret) {
		nv_error(device, "unable to acquire AGP: %d\n", ret);
		return;
	}

	ret = drm_agp_info(dev, &info);
	if (ret) {
		nv_error(device, "unable to get AGP info: %d\n", ret);
		return;
	}

	/* see agp.h for the AGPSTAT_* modes available */
	mode.mode = get_agp_mode(drm, &info);

	ret = drm_agp_enable(dev, mode);
	if (ret) {
		nv_error(device, "unable to enable AGP: %d\n", ret);
		return;
	}

	drm->agp.stat = ENABLED;
	drm->agp.base = info.aperture_base;
	drm->agp.size = info.aperture_size;
#endif
}

void
nouveau_agp_fini(struct nouveau_drm *drm)
{
#if __OS_HAS_AGP
	struct drm_device *dev = drm->dev;
	if (dev->agp && dev->agp->acquired)
		drm_agp_release(dev);
#endif
}
