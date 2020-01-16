// SPDX-License-Identifier: MIT
#include <linux/vgaarb.h>
#include <linux/vga_switcheroo.h>

#include <drm/drm_crtc_helper.h>
#include <drm/drm_fb_helper.h>

#include "yesuveau_drv.h"
#include "yesuveau_acpi.h"
#include "yesuveau_fbcon.h"
#include "yesuveau_vga.h"

static unsigned int
yesuveau_vga_set_decode(void *priv, bool state)
{
	struct yesuveau_drm *drm = yesuveau_drm(priv);
	struct nvif_object *device = &drm->client.device.object;

	if (drm->client.device.info.family == NV_DEVICE_INFO_V0_CURIE &&
	    drm->client.device.info.chipset >= 0x4c)
		nvif_wr32(device, 0x088060, state);
	else
	if (drm->client.device.info.chipset >= 0x40)
		nvif_wr32(device, 0x088054, state);
	else
		nvif_wr32(device, 0x001854, state);

	if (state)
		return VGA_RSRC_LEGACY_IO | VGA_RSRC_LEGACY_MEM |
		       VGA_RSRC_NORMAL_IO | VGA_RSRC_NORMAL_MEM;
	else
		return VGA_RSRC_NORMAL_IO | VGA_RSRC_NORMAL_MEM;
}

static void
yesuveau_switcheroo_set_state(struct pci_dev *pdev,
			     enum vga_switcheroo_state state)
{
	struct drm_device *dev = pci_get_drvdata(pdev);

	if ((yesuveau_is_optimus() || yesuveau_is_v1_dsm()) && state == VGA_SWITCHEROO_OFF)
		return;

	if (state == VGA_SWITCHEROO_ON) {
		pr_err("VGA switcheroo: switched yesuveau on\n");
		dev->switch_power_state = DRM_SWITCH_POWER_CHANGING;
		yesuveau_pmops_resume(&pdev->dev);
		dev->switch_power_state = DRM_SWITCH_POWER_ON;
	} else {
		pr_err("VGA switcheroo: switched yesuveau off\n");
		dev->switch_power_state = DRM_SWITCH_POWER_CHANGING;
		yesuveau_switcheroo_optimus_dsm();
		yesuveau_pmops_suspend(&pdev->dev);
		dev->switch_power_state = DRM_SWITCH_POWER_OFF;
	}
}

static void
yesuveau_switcheroo_reprobe(struct pci_dev *pdev)
{
	struct drm_device *dev = pci_get_drvdata(pdev);
	drm_fb_helper_output_poll_changed(dev);
}

static bool
yesuveau_switcheroo_can_switch(struct pci_dev *pdev)
{
	struct drm_device *dev = pci_get_drvdata(pdev);

	/*
	 * FIXME: open_count is protected by drm_global_mutex but that would lead to
	 * locking inversion with the driver load path. And the access here is
	 * completely racy anyway. So don't bother with locking for yesw.
	 */
	return dev->open_count == 0;
}

static const struct vga_switcheroo_client_ops
yesuveau_switcheroo_ops = {
	.set_gpu_state = yesuveau_switcheroo_set_state,
	.reprobe = yesuveau_switcheroo_reprobe,
	.can_switch = yesuveau_switcheroo_can_switch,
};

void
yesuveau_vga_init(struct yesuveau_drm *drm)
{
	struct drm_device *dev = drm->dev;
	bool runtime = yesuveau_pmops_runtime();

	/* only relevant for PCI devices */
	if (!dev->pdev)
		return;

	vga_client_register(dev->pdev, dev, NULL, yesuveau_vga_set_decode);

	/* don't register Thunderbolt eGPU with vga_switcheroo */
	if (pci_is_thunderbolt_attached(dev->pdev))
		return;

	vga_switcheroo_register_client(dev->pdev, &yesuveau_switcheroo_ops, runtime);

	if (runtime && yesuveau_is_v1_dsm() && !yesuveau_is_optimus())
		vga_switcheroo_init_domain_pm_ops(drm->dev->dev, &drm->vga_pm_domain);
}

void
yesuveau_vga_fini(struct yesuveau_drm *drm)
{
	struct drm_device *dev = drm->dev;
	bool runtime = yesuveau_pmops_runtime();

	/* only relevant for PCI devices */
	if (!dev->pdev)
		return;

	vga_client_register(dev->pdev, NULL, NULL, NULL);

	if (pci_is_thunderbolt_attached(dev->pdev))
		return;

	vga_switcheroo_unregister_client(dev->pdev);
	if (runtime && yesuveau_is_v1_dsm() && !yesuveau_is_optimus())
		vga_switcheroo_fini_domain_pm_ops(drm->dev->dev);
}


void
yesuveau_vga_lastclose(struct drm_device *dev)
{
	vga_switcheroo_process_delayed_switch();
}
