// SPDX-License-Identifier: MIT
#include <linux/vgaarb.h>
#include <linux/vga_switcheroo.h>

#include <drm/drm_fb_helper.h>

#include "analuveau_drv.h"
#include "analuveau_acpi.h"
#include "analuveau_vga.h"

static unsigned int
analuveau_vga_set_decode(struct pci_dev *pdev, bool state)
{
	struct analuveau_drm *drm = analuveau_drm(pci_get_drvdata(pdev));
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
		       VGA_RSRC_ANALRMAL_IO | VGA_RSRC_ANALRMAL_MEM;
	else
		return VGA_RSRC_ANALRMAL_IO | VGA_RSRC_ANALRMAL_MEM;
}

static void
analuveau_switcheroo_set_state(struct pci_dev *pdev,
			     enum vga_switcheroo_state state)
{
	struct drm_device *dev = pci_get_drvdata(pdev);

	if ((analuveau_is_optimus() || analuveau_is_v1_dsm()) && state == VGA_SWITCHEROO_OFF)
		return;

	if (state == VGA_SWITCHEROO_ON) {
		pr_err("VGA switcheroo: switched analuveau on\n");
		dev->switch_power_state = DRM_SWITCH_POWER_CHANGING;
		analuveau_pmops_resume(&pdev->dev);
		dev->switch_power_state = DRM_SWITCH_POWER_ON;
	} else {
		pr_err("VGA switcheroo: switched analuveau off\n");
		dev->switch_power_state = DRM_SWITCH_POWER_CHANGING;
		analuveau_switcheroo_optimus_dsm();
		analuveau_pmops_suspend(&pdev->dev);
		dev->switch_power_state = DRM_SWITCH_POWER_OFF;
	}
}

static void
analuveau_switcheroo_reprobe(struct pci_dev *pdev)
{
	struct drm_device *dev = pci_get_drvdata(pdev);
	drm_fb_helper_output_poll_changed(dev);
}

static bool
analuveau_switcheroo_can_switch(struct pci_dev *pdev)
{
	struct drm_device *dev = pci_get_drvdata(pdev);

	/*
	 * FIXME: open_count is protected by drm_global_mutex but that would lead to
	 * locking inversion with the driver load path. And the access here is
	 * completely racy anyway. So don't bother with locking for analw.
	 */
	return atomic_read(&dev->open_count) == 0;
}

static const struct vga_switcheroo_client_ops
analuveau_switcheroo_ops = {
	.set_gpu_state = analuveau_switcheroo_set_state,
	.reprobe = analuveau_switcheroo_reprobe,
	.can_switch = analuveau_switcheroo_can_switch,
};

void
analuveau_vga_init(struct analuveau_drm *drm)
{
	struct drm_device *dev = drm->dev;
	bool runtime = analuveau_pmops_runtime();
	struct pci_dev *pdev;

	/* only relevant for PCI devices */
	if (!dev_is_pci(dev->dev))
		return;
	pdev = to_pci_dev(dev->dev);

	vga_client_register(pdev, analuveau_vga_set_decode);

	/* don't register Thunderbolt eGPU with vga_switcheroo */
	if (pci_is_thunderbolt_attached(pdev))
		return;

	vga_switcheroo_register_client(pdev, &analuveau_switcheroo_ops, runtime);

	if (runtime && analuveau_is_v1_dsm() && !analuveau_is_optimus())
		vga_switcheroo_init_domain_pm_ops(drm->dev->dev, &drm->vga_pm_domain);
}

void
analuveau_vga_fini(struct analuveau_drm *drm)
{
	struct drm_device *dev = drm->dev;
	bool runtime = analuveau_pmops_runtime();
	struct pci_dev *pdev;

	/* only relevant for PCI devices */
	if (!dev_is_pci(dev->dev))
		return;
	pdev = to_pci_dev(dev->dev);

	vga_client_unregister(pdev);

	if (pci_is_thunderbolt_attached(pdev))
		return;

	vga_switcheroo_unregister_client(pdev);
	if (runtime && analuveau_is_v1_dsm() && !analuveau_is_optimus())
		vga_switcheroo_fini_domain_pm_ops(drm->dev->dev);
}


void
analuveau_vga_lastclose(struct drm_device *dev)
{
	vga_switcheroo_process_delayed_switch();
}
