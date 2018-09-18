/*
 * Copyright (C) 2013-2017 Oracle Corporation
 * This file is based on ast_drv.c
 * Copyright 2012 Red Hat Inc.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sub license, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT. IN NO EVENT SHALL
 * THE COPYRIGHT HOLDERS, AUTHORS AND/OR ITS SUPPLIERS BE LIABLE FOR ANY CLAIM,
 * DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR
 * OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE
 * USE OR OTHER DEALINGS IN THE SOFTWARE.
 *
 * The above copyright notice and this permission notice (including the
 * next paragraph) shall be included in all copies or substantial portions
 * of the Software.
 *
 * Authors: Dave Airlie <airlied@redhat.com>
 *          Michael Thayer <michael.thayer@oracle.com,
 *          Hans de Goede <hdegoede@redhat.com>
 */
#include <linux/module.h>
#include <linux/console.h>
#include <linux/vt_kern.h>

#include <drm/drmP.h>
#include <drm/drm_crtc_helper.h>

#include "vbox_drv.h"

static int vbox_modeset = -1;

MODULE_PARM_DESC(modeset, "Disable/Enable modesetting");
module_param_named(modeset, vbox_modeset, int, 0400);

static struct drm_driver driver;

static const struct pci_device_id pciidlist[] = {
	{ 0x80ee, 0xbeef, PCI_ANY_ID, PCI_ANY_ID, 0, 0, 0 },
	{ 0, 0, 0},
};
MODULE_DEVICE_TABLE(pci, pciidlist);

static int vbox_pci_probe(struct pci_dev *pdev, const struct pci_device_id *ent)
{
	struct vbox_private *vbox = NULL;
	struct drm_device *dev = NULL;
	int ret = 0;

	if (!vbox_check_supported(VBE_DISPI_ID_HGSMI))
		return -ENODEV;

	dev = drm_dev_alloc(&driver, &pdev->dev);
	if (IS_ERR(dev))
		return PTR_ERR(dev);

	ret = pci_enable_device(pdev);
	if (ret)
		goto err_dev_put;

	dev->pdev = pdev;
	pci_set_drvdata(pdev, dev);

	vbox = devm_kzalloc(&pdev->dev, sizeof(*vbox), GFP_KERNEL);
	if (!vbox) {
		ret = -ENOMEM;
		goto err_pci_disable;
	}

	dev->dev_private = vbox;
	vbox->dev = dev;

	mutex_init(&vbox->hw_mutex);

	ret = vbox_hw_init(vbox);
	if (ret)
		goto err_pci_disable;

	ret = vbox_mm_init(vbox);
	if (ret)
		goto err_hw_fini;

	ret = vbox_mode_init(dev);
	if (ret)
		goto err_mm_fini;

	ret = vbox_irq_init(vbox);
	if (ret)
		goto err_mode_fini;

	ret = vbox_fbdev_init(dev);
	if (ret)
		goto err_irq_fini;

	ret = drm_dev_register(dev, 0);
	if (ret)
		goto err_fbdev_fini;

	return 0;

err_fbdev_fini:
	vbox_fbdev_fini(dev);
err_irq_fini:
	vbox_irq_fini(vbox);
err_mode_fini:
	vbox_mode_fini(dev);
err_mm_fini:
	vbox_mm_fini(vbox);
err_hw_fini:
	vbox_hw_fini(vbox);
err_pci_disable:
	pci_disable_device(pdev);
err_dev_put:
	drm_dev_put(dev);
	return ret;
}

static void vbox_pci_remove(struct pci_dev *pdev)
{
	struct drm_device *dev = pci_get_drvdata(pdev);
	struct vbox_private *vbox = dev->dev_private;

	drm_dev_unregister(dev);
	vbox_fbdev_fini(dev);
	vbox_irq_fini(vbox);
	vbox_mode_fini(dev);
	vbox_mm_fini(vbox);
	vbox_hw_fini(vbox);
	drm_dev_put(dev);
}

static int vbox_drm_freeze(struct drm_device *dev)
{
	struct vbox_private *vbox = dev->dev_private;

	drm_kms_helper_poll_disable(dev);

	pci_save_state(dev->pdev);

	drm_fb_helper_set_suspend_unlocked(&vbox->fbdev->helper, true);

	return 0;
}

static int vbox_drm_thaw(struct drm_device *dev)
{
	struct vbox_private *vbox = dev->dev_private;

	drm_mode_config_reset(dev);
	drm_helper_resume_force_mode(dev);
	drm_fb_helper_set_suspend_unlocked(&vbox->fbdev->helper, false);

	return 0;
}

static int vbox_drm_resume(struct drm_device *dev)
{
	int ret;

	if (pci_enable_device(dev->pdev))
		return -EIO;

	ret = vbox_drm_thaw(dev);
	if (ret)
		return ret;

	drm_kms_helper_poll_enable(dev);

	return 0;
}

static int vbox_pm_suspend(struct device *dev)
{
	struct pci_dev *pdev = to_pci_dev(dev);
	struct drm_device *ddev = pci_get_drvdata(pdev);
	int error;

	error = vbox_drm_freeze(ddev);
	if (error)
		return error;

	pci_disable_device(pdev);
	pci_set_power_state(pdev, PCI_D3hot);

	return 0;
}

static int vbox_pm_resume(struct device *dev)
{
	struct drm_device *ddev = pci_get_drvdata(to_pci_dev(dev));

	return vbox_drm_resume(ddev);
}

static int vbox_pm_freeze(struct device *dev)
{
	struct pci_dev *pdev = to_pci_dev(dev);
	struct drm_device *ddev = pci_get_drvdata(pdev);

	if (!ddev || !ddev->dev_private)
		return -ENODEV;

	return vbox_drm_freeze(ddev);
}

static int vbox_pm_thaw(struct device *dev)
{
	struct drm_device *ddev = pci_get_drvdata(to_pci_dev(dev));

	return vbox_drm_thaw(ddev);
}

static int vbox_pm_poweroff(struct device *dev)
{
	struct drm_device *ddev = pci_get_drvdata(to_pci_dev(dev));

	return vbox_drm_freeze(ddev);
}

static const struct dev_pm_ops vbox_pm_ops = {
	.suspend = vbox_pm_suspend,
	.resume = vbox_pm_resume,
	.freeze = vbox_pm_freeze,
	.thaw = vbox_pm_thaw,
	.poweroff = vbox_pm_poweroff,
	.restore = vbox_pm_resume,
};

static struct pci_driver vbox_pci_driver = {
	.name = DRIVER_NAME,
	.id_table = pciidlist,
	.probe = vbox_pci_probe,
	.remove = vbox_pci_remove,
	.driver.pm = &vbox_pm_ops,
};

static const struct file_operations vbox_fops = {
	.owner = THIS_MODULE,
	.open = drm_open,
	.release = drm_release,
	.unlocked_ioctl = drm_ioctl,
	.mmap = vbox_mmap,
	.poll = drm_poll,
#ifdef CONFIG_COMPAT
	.compat_ioctl = drm_compat_ioctl,
#endif
	.read = drm_read,
};

static int vbox_master_set(struct drm_device *dev,
			   struct drm_file *file_priv, bool from_open)
{
	struct vbox_private *vbox = dev->dev_private;

	/*
	 * We do not yet know whether the new owner can handle hotplug, so we
	 * do not advertise dynamic modes on the first query and send a
	 * tentative hotplug notification after that to see if they query again.
	 */
	vbox->initial_mode_queried = false;

	mutex_lock(&vbox->hw_mutex);
	/*
	 * Disable VBVA when someone releases master in case the next person
	 * tries tries to do VESA.
	 */
	/** @todo work out if anyone is likely to and whether it will work. */
	/*
	 * Update: we also disable it because if the new master does not do
	 * dirty rectangle reporting (e.g. old versions of Plymouth) then at
	 * least the first screen will still be updated. We enable it as soon
	 * as we receive a dirty rectangle report.
	 */
	vbox_disable_accel(vbox);
	mutex_unlock(&vbox->hw_mutex);

	return 0;
}

static void vbox_master_drop(struct drm_device *dev, struct drm_file *file_priv)
{
	struct vbox_private *vbox = dev->dev_private;

	/* See vbox_master_set() */
	vbox->initial_mode_queried = false;

	mutex_lock(&vbox->hw_mutex);
	vbox_disable_accel(vbox);
	mutex_unlock(&vbox->hw_mutex);
}

static struct drm_driver driver = {
	.driver_features =
	    DRIVER_MODESET | DRIVER_GEM | DRIVER_HAVE_IRQ | DRIVER_IRQ_SHARED |
	    DRIVER_PRIME,
	.dev_priv_size = 0,

	.lastclose = vbox_driver_lastclose,
	.master_set = vbox_master_set,
	.master_drop = vbox_master_drop,

	.fops = &vbox_fops,
	.irq_handler = vbox_irq_handler,
	.name = DRIVER_NAME,
	.desc = DRIVER_DESC,
	.date = DRIVER_DATE,
	.major = DRIVER_MAJOR,
	.minor = DRIVER_MINOR,
	.patchlevel = DRIVER_PATCHLEVEL,

	.gem_free_object_unlocked = vbox_gem_free_object,
	.dumb_create = vbox_dumb_create,
	.dumb_map_offset = vbox_dumb_mmap_offset,
	.dumb_destroy = drm_gem_dumb_destroy,
	.prime_handle_to_fd = drm_gem_prime_handle_to_fd,
	.prime_fd_to_handle = drm_gem_prime_fd_to_handle,
	.gem_prime_export = drm_gem_prime_export,
	.gem_prime_import = drm_gem_prime_import,
	.gem_prime_pin = vbox_gem_prime_pin,
	.gem_prime_unpin = vbox_gem_prime_unpin,
	.gem_prime_get_sg_table = vbox_gem_prime_get_sg_table,
	.gem_prime_import_sg_table = vbox_gem_prime_import_sg_table,
	.gem_prime_vmap = vbox_gem_prime_vmap,
	.gem_prime_vunmap = vbox_gem_prime_vunmap,
	.gem_prime_mmap = vbox_gem_prime_mmap,
};

static int __init vbox_init(void)
{
#ifdef CONFIG_VGA_CONSOLE
	if (vgacon_text_force() && vbox_modeset == -1)
		return -EINVAL;
#endif

	if (vbox_modeset == 0)
		return -EINVAL;

	return pci_register_driver(&vbox_pci_driver);
}

static void __exit vbox_exit(void)
{
	pci_unregister_driver(&vbox_pci_driver);
}

module_init(vbox_init);
module_exit(vbox_exit);

MODULE_AUTHOR("Oracle Corporation");
MODULE_DESCRIPTION(DRIVER_DESC);
MODULE_LICENSE("GPL and additional rights");
