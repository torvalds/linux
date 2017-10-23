/* Hisilicon Hibmc SoC drm driver
 *
 * Based on the bochs drm driver.
 *
 * Copyright (c) 2016 Huawei Limited.
 *
 * Author:
 *	Rongrong Zou <zourongrong@huawei.com>
 *	Rongrong Zou <zourongrong@gmail.com>
 *	Jianhua Li <lijianhua@huawei.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 */

#include <linux/console.h>
#include <linux/module.h>

#include <drm/drm_atomic_helper.h>
#include <drm/drm_crtc_helper.h>

#include "hibmc_drm_drv.h"
#include "hibmc_drm_regs.h"

static const struct file_operations hibmc_fops = {
	.owner		= THIS_MODULE,
	.open		= drm_open,
	.release	= drm_release,
	.unlocked_ioctl	= drm_ioctl,
	.compat_ioctl	= drm_compat_ioctl,
	.mmap		= hibmc_mmap,
	.poll		= drm_poll,
	.read		= drm_read,
	.llseek		= no_llseek,
};

irqreturn_t hibmc_drm_interrupt(int irq, void *arg)
{
	struct drm_device *dev = (struct drm_device *)arg;
	struct hibmc_drm_private *priv =
		(struct hibmc_drm_private *)dev->dev_private;
	u32 status;

	status = readl(priv->mmio + HIBMC_RAW_INTERRUPT);

	if (status & HIBMC_RAW_INTERRUPT_VBLANK(1)) {
		writel(HIBMC_RAW_INTERRUPT_VBLANK(1),
		       priv->mmio + HIBMC_RAW_INTERRUPT);
		drm_handle_vblank(dev, 0);
	}

	return IRQ_HANDLED;
}

static struct drm_driver hibmc_driver = {
	.driver_features	= DRIVER_GEM | DRIVER_MODESET |
				  DRIVER_ATOMIC | DRIVER_HAVE_IRQ,
	.fops			= &hibmc_fops,
	.name			= "hibmc",
	.date			= "20160828",
	.desc			= "hibmc drm driver",
	.major			= 1,
	.minor			= 0,
	.gem_free_object_unlocked = hibmc_gem_free_object,
	.dumb_create            = hibmc_dumb_create,
	.dumb_map_offset        = hibmc_dumb_mmap_offset,
	.irq_handler		= hibmc_drm_interrupt,
};

static int __maybe_unused hibmc_pm_suspend(struct device *dev)
{
	struct pci_dev *pdev = to_pci_dev(dev);
	struct drm_device *drm_dev = pci_get_drvdata(pdev);
	struct hibmc_drm_private *priv = drm_dev->dev_private;

	drm_kms_helper_poll_disable(drm_dev);
	priv->suspend_state = drm_atomic_helper_suspend(drm_dev);
	if (IS_ERR(priv->suspend_state)) {
		DRM_ERROR("drm_atomic_helper_suspend failed: %ld\n",
			  PTR_ERR(priv->suspend_state));
		drm_kms_helper_poll_enable(drm_dev);
		return PTR_ERR(priv->suspend_state);
	}

	return 0;
}

static int  __maybe_unused hibmc_pm_resume(struct device *dev)
{
	struct pci_dev *pdev = to_pci_dev(dev);
	struct drm_device *drm_dev = pci_get_drvdata(pdev);
	struct hibmc_drm_private *priv = drm_dev->dev_private;

	drm_atomic_helper_resume(drm_dev, priv->suspend_state);
	drm_kms_helper_poll_enable(drm_dev);

	return 0;
}

static const struct dev_pm_ops hibmc_pm_ops = {
	SET_SYSTEM_SLEEP_PM_OPS(hibmc_pm_suspend,
				hibmc_pm_resume)
};

static int hibmc_kms_init(struct hibmc_drm_private *priv)
{
	int ret;

	drm_mode_config_init(priv->dev);
	priv->mode_config_initialized = true;

	priv->dev->mode_config.min_width = 0;
	priv->dev->mode_config.min_height = 0;
	priv->dev->mode_config.max_width = 1920;
	priv->dev->mode_config.max_height = 1440;

	priv->dev->mode_config.fb_base = priv->fb_base;
	priv->dev->mode_config.preferred_depth = 24;
	priv->dev->mode_config.prefer_shadow = 0;

	priv->dev->mode_config.funcs = (void *)&hibmc_mode_funcs;

	ret = hibmc_de_init(priv);
	if (ret) {
		DRM_ERROR("failed to init de: %d\n", ret);
		return ret;
	}

	ret = hibmc_vdac_init(priv);
	if (ret) {
		DRM_ERROR("failed to init vdac: %d\n", ret);
		return ret;
	}

	return 0;
}

static void hibmc_kms_fini(struct hibmc_drm_private *priv)
{
	if (priv->mode_config_initialized) {
		drm_mode_config_cleanup(priv->dev);
		priv->mode_config_initialized = false;
	}
}

/*
 * It can operate in one of three modes: 0, 1 or Sleep.
 */
void hibmc_set_power_mode(struct hibmc_drm_private *priv,
			  unsigned int power_mode)
{
	unsigned int control_value = 0;
	void __iomem   *mmio = priv->mmio;
	unsigned int input = 1;

	if (power_mode > HIBMC_PW_MODE_CTL_MODE_SLEEP)
		return;

	if (power_mode == HIBMC_PW_MODE_CTL_MODE_SLEEP)
		input = 0;

	control_value = readl(mmio + HIBMC_POWER_MODE_CTRL);
	control_value &= ~(HIBMC_PW_MODE_CTL_MODE_MASK |
			   HIBMC_PW_MODE_CTL_OSC_INPUT_MASK);
	control_value |= HIBMC_FIELD(HIBMC_PW_MODE_CTL_MODE, power_mode);
	control_value |= HIBMC_FIELD(HIBMC_PW_MODE_CTL_OSC_INPUT, input);
	writel(control_value, mmio + HIBMC_POWER_MODE_CTRL);
}

void hibmc_set_current_gate(struct hibmc_drm_private *priv, unsigned int gate)
{
	unsigned int gate_reg;
	unsigned int mode;
	void __iomem   *mmio = priv->mmio;

	/* Get current power mode. */
	mode = (readl(mmio + HIBMC_POWER_MODE_CTRL) &
		HIBMC_PW_MODE_CTL_MODE_MASK) >> HIBMC_PW_MODE_CTL_MODE_SHIFT;

	switch (mode) {
	case HIBMC_PW_MODE_CTL_MODE_MODE0:
		gate_reg = HIBMC_MODE0_GATE;
		break;

	case HIBMC_PW_MODE_CTL_MODE_MODE1:
		gate_reg = HIBMC_MODE1_GATE;
		break;

	default:
		gate_reg = HIBMC_MODE0_GATE;
		break;
	}
	writel(gate, mmio + gate_reg);
}

static void hibmc_hw_config(struct hibmc_drm_private *priv)
{
	unsigned int reg;

	/* On hardware reset, power mode 0 is default. */
	hibmc_set_power_mode(priv, HIBMC_PW_MODE_CTL_MODE_MODE0);

	/* Enable display power gate & LOCALMEM power gate*/
	reg = readl(priv->mmio + HIBMC_CURRENT_GATE);
	reg &= ~HIBMC_CURR_GATE_DISPLAY_MASK;
	reg &= ~HIBMC_CURR_GATE_LOCALMEM_MASK;
	reg |= HIBMC_CURR_GATE_DISPLAY(1);
	reg |= HIBMC_CURR_GATE_LOCALMEM(1);

	hibmc_set_current_gate(priv, reg);

	/*
	 * Reset the memory controller. If the memory controller
	 * is not reset in chip,the system might hang when sw accesses
	 * the memory.The memory should be resetted after
	 * changing the MXCLK.
	 */
	reg = readl(priv->mmio + HIBMC_MISC_CTRL);
	reg &= ~HIBMC_MSCCTL_LOCALMEM_RESET_MASK;
	reg |= HIBMC_MSCCTL_LOCALMEM_RESET(0);
	writel(reg, priv->mmio + HIBMC_MISC_CTRL);

	reg &= ~HIBMC_MSCCTL_LOCALMEM_RESET_MASK;
	reg |= HIBMC_MSCCTL_LOCALMEM_RESET(1);

	writel(reg, priv->mmio + HIBMC_MISC_CTRL);
}

static int hibmc_hw_map(struct hibmc_drm_private *priv)
{
	struct drm_device *dev = priv->dev;
	struct pci_dev *pdev = dev->pdev;
	resource_size_t addr, size, ioaddr, iosize;

	ioaddr = pci_resource_start(pdev, 1);
	iosize = pci_resource_len(pdev, 1);
	priv->mmio = devm_ioremap_nocache(dev->dev, ioaddr, iosize);
	if (!priv->mmio) {
		DRM_ERROR("Cannot map mmio region\n");
		return -ENOMEM;
	}

	addr = pci_resource_start(pdev, 0);
	size = pci_resource_len(pdev, 0);
	priv->fb_map = devm_ioremap(dev->dev, addr, size);
	if (!priv->fb_map) {
		DRM_ERROR("Cannot map framebuffer\n");
		return -ENOMEM;
	}
	priv->fb_base = addr;
	priv->fb_size = size;

	return 0;
}

static int hibmc_hw_init(struct hibmc_drm_private *priv)
{
	int ret;

	ret = hibmc_hw_map(priv);
	if (ret)
		return ret;

	hibmc_hw_config(priv);

	return 0;
}

static int hibmc_unload(struct drm_device *dev)
{
	struct hibmc_drm_private *priv = dev->dev_private;

	hibmc_fbdev_fini(priv);

	drm_atomic_helper_shutdown(dev);

	if (dev->irq_enabled)
		drm_irq_uninstall(dev);
	if (priv->msi_enabled)
		pci_disable_msi(dev->pdev);

	hibmc_kms_fini(priv);
	hibmc_mm_fini(priv);
	dev->dev_private = NULL;
	return 0;
}

static int hibmc_load(struct drm_device *dev)
{
	struct hibmc_drm_private *priv;
	int ret;

	priv = devm_kzalloc(dev->dev, sizeof(*priv), GFP_KERNEL);
	if (!priv) {
		DRM_ERROR("no memory to allocate for hibmc_drm_private\n");
		return -ENOMEM;
	}
	dev->dev_private = priv;
	priv->dev = dev;

	ret = hibmc_hw_init(priv);
	if (ret)
		goto err;

	ret = hibmc_mm_init(priv);
	if (ret)
		goto err;

	ret = hibmc_kms_init(priv);
	if (ret)
		goto err;

	ret = drm_vblank_init(dev, dev->mode_config.num_crtc);
	if (ret) {
		DRM_ERROR("failed to initialize vblank: %d\n", ret);
		goto err;
	}

	priv->msi_enabled = 0;
	ret = pci_enable_msi(dev->pdev);
	if (ret) {
		DRM_WARN("enabling MSI failed: %d\n", ret);
	} else {
		priv->msi_enabled = 1;
		ret = drm_irq_install(dev, dev->pdev->irq);
		if (ret)
			DRM_WARN("install irq failed: %d\n", ret);
	}

	/* reset all the states of crtc/plane/encoder/connector */
	drm_mode_config_reset(dev);

	ret = hibmc_fbdev_init(priv);
	if (ret) {
		DRM_ERROR("failed to initialize fbdev: %d\n", ret);
		goto err;
	}

	return 0;

err:
	hibmc_unload(dev);
	DRM_ERROR("failed to initialize drm driver: %d\n", ret);
	return ret;
}

static int hibmc_pci_probe(struct pci_dev *pdev,
			   const struct pci_device_id *ent)
{
	struct drm_device *dev;
	int ret;

	dev = drm_dev_alloc(&hibmc_driver, &pdev->dev);
	if (IS_ERR(dev)) {
		DRM_ERROR("failed to allocate drm_device\n");
		return PTR_ERR(dev);
	}

	dev->pdev = pdev;
	pci_set_drvdata(pdev, dev);

	ret = pci_enable_device(pdev);
	if (ret) {
		DRM_ERROR("failed to enable pci device: %d\n", ret);
		goto err_free;
	}

	ret = hibmc_load(dev);
	if (ret) {
		DRM_ERROR("failed to load hibmc: %d\n", ret);
		goto err_disable;
	}

	ret = drm_dev_register(dev, 0);
	if (ret) {
		DRM_ERROR("failed to register drv for userspace access: %d\n",
			  ret);
		goto err_unload;
	}
	return 0;

err_unload:
	hibmc_unload(dev);
err_disable:
	pci_disable_device(pdev);
err_free:
	drm_dev_unref(dev);

	return ret;
}

static void hibmc_pci_remove(struct pci_dev *pdev)
{
	struct drm_device *dev = pci_get_drvdata(pdev);

	drm_dev_unregister(dev);
	hibmc_unload(dev);
	drm_dev_unref(dev);
}

static struct pci_device_id hibmc_pci_table[] = {
	{0x19e5, 0x1711, PCI_ANY_ID, PCI_ANY_ID, 0, 0, 0},
	{0,}
};

static struct pci_driver hibmc_pci_driver = {
	.name =		"hibmc-drm",
	.id_table =	hibmc_pci_table,
	.probe =	hibmc_pci_probe,
	.remove =	hibmc_pci_remove,
	.driver.pm =    &hibmc_pm_ops,
};

static int __init hibmc_init(void)
{
	return pci_register_driver(&hibmc_pci_driver);
}

static void __exit hibmc_exit(void)
{
	return pci_unregister_driver(&hibmc_pci_driver);
}

module_init(hibmc_init);
module_exit(hibmc_exit);

MODULE_DEVICE_TABLE(pci, hibmc_pci_table);
MODULE_AUTHOR("RongrongZou <zourongrong@huawei.com>");
MODULE_DESCRIPTION("DRM Driver for Hisilicon Hibmc");
MODULE_LICENSE("GPL v2");
