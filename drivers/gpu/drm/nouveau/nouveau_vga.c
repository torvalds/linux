#include <linux/vgaarb.h>
#include <linux/vga_switcheroo.h>

#include <drm/drmP.h>
#include <drm/drm_crtc_helper.h>

#include "nouveau_drv.h"
#include "nouveau_acpi.h"
#include "nouveau_fbcon.h"
#include "nouveau_vga.h"

static unsigned int
nouveau_vga_set_decode(void *priv, bool state)
{
	struct nouveau_drm *drm = nouveau_drm(priv);
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
nouveau_switcheroo_set_state(struct pci_dev *pdev,
			     enum vga_switcheroo_state state)
{
	struct drm_device *dev = pci_get_drvdata(pdev);

	if ((nouveau_is_optimus() || nouveau_is_v1_dsm()) && state == VGA_SWITCHEROO_OFF)
		return;

	if (state == VGA_SWITCHEROO_ON) {
		pr_err("VGA switcheroo: switched nouveau on\n");
		dev->switch_power_state = DRM_SWITCH_POWER_CHANGING;
		nouveau_pmops_resume(&pdev->dev);
		drm_kms_helper_poll_enable(dev);
		dev->switch_power_state = DRM_SWITCH_POWER_ON;
	} else {
		pr_err("VGA switcheroo: switched nouveau off\n");
		dev->switch_power_state = DRM_SWITCH_POWER_CHANGING;
		drm_kms_helper_poll_disable(dev);
		nouveau_switcheroo_optimus_dsm();
		nouveau_pmops_suspend(&pdev->dev);
		dev->switch_power_state = DRM_SWITCH_POWER_OFF;
	}
}

static void
nouveau_switcheroo_reprobe(struct pci_dev *pdev)
{
	struct drm_device *dev = pci_get_drvdata(pdev);
	nouveau_fbcon_output_poll_changed(dev);
}

static bool
nouveau_switcheroo_can_switch(struct pci_dev *pdev)
{
	struct drm_device *dev = pci_get_drvdata(pdev);

	/*
	 * FIXME: open_count is protected by drm_global_mutex but that would lead to
	 * locking inversion with the driver load path. And the access here is
	 * completely racy anyway. So don't bother with locking for now.
	 */
	return dev->open_count == 0;
}

static const struct vga_switcheroo_client_ops
nouveau_switcheroo_ops = {
	.set_gpu_state = nouveau_switcheroo_set_state,
	.reprobe = nouveau_switcheroo_reprobe,
	.can_switch = nouveau_switcheroo_can_switch,
};

void
nouveau_vga_init(struct nouveau_drm *drm)
{
	struct drm_device *dev = drm->dev;
	bool runtime = false;

	/* only relevant for PCI devices */
	if (!dev->pdev)
		return;

	vga_client_register(dev->pdev, dev, NULL, nouveau_vga_set_decode);

	/* don't register Thunderbolt eGPU with vga_switcheroo */
	if (pci_is_thunderbolt_attached(dev->pdev))
		return;

	if (nouveau_runtime_pm == 1)
		runtime = true;
	if ((nouveau_runtime_pm == -1) && (nouveau_is_optimus() || nouveau_is_v1_dsm()))
		runtime = true;
	vga_switcheroo_register_client(dev->pdev, &nouveau_switcheroo_ops, runtime);

	if (runtime && nouveau_is_v1_dsm() && !nouveau_is_optimus())
		vga_switcheroo_init_domain_pm_ops(drm->dev->dev, &drm->vga_pm_domain);
}

void
nouveau_vga_fini(struct nouveau_drm *drm)
{
	struct drm_device *dev = drm->dev;
	bool runtime = false;

	vga_client_register(dev->pdev, NULL, NULL, NULL);

	if (pci_is_thunderbolt_attached(dev->pdev))
		return;

	if (nouveau_runtime_pm == 1)
		runtime = true;
	if ((nouveau_runtime_pm == -1) && (nouveau_is_optimus() || nouveau_is_v1_dsm()))
		runtime = true;

	vga_switcheroo_unregister_client(dev->pdev);
	if (runtime && nouveau_is_v1_dsm() && !nouveau_is_optimus())
		vga_switcheroo_fini_domain_pm_ops(drm->dev->dev);
}


void
nouveau_vga_lastclose(struct drm_device *dev)
{
	vga_switcheroo_process_delayed_switch();
}
