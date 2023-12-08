// SPDX-License-Identifier: GPL-2.0-or-later
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
 */

#include <linux/module.h>
#include <linux/pci.h>

#include <drm/drm_aperture.h>
#include <drm/drm_atomic_helper.h>
#include <drm/drm_drv.h>
#include <drm/drm_gem_framebuffer_helper.h>
#include <drm/drm_gem_vram_helper.h>
#include <drm/drm_managed.h>
#include <drm/drm_module.h>
#include <drm/drm_vblank.h>

#include "hibmc_drm_drv.h"
#include "hibmc_drm_regs.h"

DEFINE_DRM_GEM_FOPS(hibmc_fops);

static irqreturn_t hibmc_interrupt(int irq, void *arg)
{
	struct drm_device *dev = (struct drm_device *)arg;
	struct hibmc_drm_private *priv = to_hibmc_drm_private(dev);
	u32 status;

	status = readl(priv->mmio + HIBMC_RAW_INTERRUPT);

	if (status & HIBMC_RAW_INTERRUPT_VBLANK(1)) {
		writel(HIBMC_RAW_INTERRUPT_VBLANK(1),
		       priv->mmio + HIBMC_RAW_INTERRUPT);
		drm_handle_vblank(dev, 0);
	}

	return IRQ_HANDLED;
}

static int hibmc_dumb_create(struct drm_file *file, struct drm_device *dev,
			     struct drm_mode_create_dumb *args)
{
	return drm_gem_vram_fill_create_dumb(file, dev, 0, 128, args);
}

static const struct drm_driver hibmc_driver = {
	.driver_features	= DRIVER_GEM | DRIVER_MODESET | DRIVER_ATOMIC,
	.fops			= &hibmc_fops,
	.name			= "hibmc",
	.date			= "20160828",
	.desc			= "hibmc drm driver",
	.major			= 1,
	.minor			= 0,
	.debugfs_init		= drm_vram_mm_debugfs_init,
	.dumb_create            = hibmc_dumb_create,
	.dumb_map_offset        = drm_gem_ttm_dumb_map_offset,
	.gem_prime_mmap		= drm_gem_prime_mmap,
};

static int __maybe_unused hibmc_pm_suspend(struct device *dev)
{
	struct drm_device *drm_dev = dev_get_drvdata(dev);

	return drm_mode_config_helper_suspend(drm_dev);
}

static int  __maybe_unused hibmc_pm_resume(struct device *dev)
{
	struct drm_device *drm_dev = dev_get_drvdata(dev);

	return drm_mode_config_helper_resume(drm_dev);
}

static const struct dev_pm_ops hibmc_pm_ops = {
	SET_SYSTEM_SLEEP_PM_OPS(hibmc_pm_suspend,
				hibmc_pm_resume)
};

static const struct drm_mode_config_funcs hibmc_mode_funcs = {
	.mode_valid = drm_vram_helper_mode_valid,
	.atomic_check = drm_atomic_helper_check,
	.atomic_commit = drm_atomic_helper_commit,
	.fb_create = drm_gem_fb_create,
};

static int hibmc_kms_init(struct hibmc_drm_private *priv)
{
	struct drm_device *dev = &priv->dev;
	int ret;

	ret = drmm_mode_config_init(dev);
	if (ret)
		return ret;

	dev->mode_config.min_width = 0;
	dev->mode_config.min_height = 0;
	dev->mode_config.max_width = 1920;
	dev->mode_config.max_height = 1200;

	dev->mode_config.fb_base = priv->fb_base;
	dev->mode_config.preferred_depth = 32;
	dev->mode_config.prefer_shadow = 1;

	dev->mode_config.funcs = (void *)&hibmc_mode_funcs;

	ret = hibmc_de_init(priv);
	if (ret) {
		drm_err(dev, "failed to init de: %d\n", ret);
		return ret;
	}

	ret = hibmc_vdac_init(priv);
	if (ret) {
		drm_err(dev, "failed to init vdac: %d\n", ret);
		return ret;
	}

	return 0;
}

/*
 * It can operate in one of three modes: 0, 1 or Sleep.
 */
void hibmc_set_power_mode(struct hibmc_drm_private *priv, u32 power_mode)
{
	u32 control_value = 0;
	void __iomem   *mmio = priv->mmio;
	u32 input = 1;

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
	u32 gate_reg;
	u32 mode;
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
	u32 reg;

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
	struct drm_device *dev = &priv->dev;
	struct pci_dev *pdev = to_pci_dev(dev->dev);
	resource_size_t addr, size, ioaddr, iosize;

	ioaddr = pci_resource_start(pdev, 1);
	iosize = pci_resource_len(pdev, 1);
	priv->mmio = devm_ioremap(dev->dev, ioaddr, iosize);
	if (!priv->mmio) {
		drm_err(dev, "Cannot map mmio region\n");
		return -ENOMEM;
	}

	addr = pci_resource_start(pdev, 0);
	size = pci_resource_len(pdev, 0);
	priv->fb_map = devm_ioremap(dev->dev, addr, size);
	if (!priv->fb_map) {
		drm_err(dev, "Cannot map framebuffer\n");
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
	struct pci_dev *pdev = to_pci_dev(dev->dev);

	drm_atomic_helper_shutdown(dev);

	free_irq(pdev->irq, dev);

	pci_disable_msi(to_pci_dev(dev->dev));

	return 0;
}

static int hibmc_load(struct drm_device *dev)
{
	struct pci_dev *pdev = to_pci_dev(dev->dev);
	struct hibmc_drm_private *priv = to_hibmc_drm_private(dev);
	int ret;

	ret = hibmc_hw_init(priv);
	if (ret)
		goto err;

	ret = drmm_vram_helper_init(dev, pci_resource_start(pdev, 0), priv->fb_size);
	if (ret) {
		drm_err(dev, "Error initializing VRAM MM; %d\n", ret);
		goto err;
	}

	ret = hibmc_kms_init(priv);
	if (ret)
		goto err;

	ret = drm_vblank_init(dev, dev->mode_config.num_crtc);
	if (ret) {
		drm_err(dev, "failed to initialize vblank: %d\n", ret);
		goto err;
	}

	ret = pci_enable_msi(pdev);
	if (ret) {
		drm_warn(dev, "enabling MSI failed: %d\n", ret);
	} else {
		/* PCI devices require shared interrupts. */
		ret = request_irq(pdev->irq, hibmc_interrupt, IRQF_SHARED,
				  dev->driver->name, dev);
		if (ret)
			drm_warn(dev, "install irq failed: %d\n", ret);
	}

	/* reset all the states of crtc/plane/encoder/connector */
	drm_mode_config_reset(dev);

	return 0;

err:
	hibmc_unload(dev);
	drm_err(dev, "failed to initialize drm driver: %d\n", ret);
	return ret;
}

static int hibmc_pci_probe(struct pci_dev *pdev,
			   const struct pci_device_id *ent)
{
	struct hibmc_drm_private *priv;
	struct drm_device *dev;
	int ret;

	ret = drm_aperture_remove_conflicting_pci_framebuffers(pdev, &hibmc_driver);
	if (ret)
		return ret;

	priv = devm_drm_dev_alloc(&pdev->dev, &hibmc_driver,
				  struct hibmc_drm_private, dev);
	if (IS_ERR(priv)) {
		DRM_ERROR("failed to allocate drm_device\n");
		return PTR_ERR(priv);
	}

	dev = &priv->dev;
	pci_set_drvdata(pdev, dev);

	ret = pcim_enable_device(pdev);
	if (ret) {
		drm_err(dev, "failed to enable pci device: %d\n", ret);
		goto err_return;
	}

	ret = hibmc_load(dev);
	if (ret) {
		drm_err(dev, "failed to load hibmc: %d\n", ret);
		goto err_return;
	}

	ret = drm_dev_register(dev, 0);
	if (ret) {
		drm_err(dev, "failed to register drv for userspace access: %d\n",
			  ret);
		goto err_unload;
	}

	drm_fbdev_generic_setup(dev, dev->mode_config.preferred_depth);

	return 0;

err_unload:
	hibmc_unload(dev);
err_return:
	return ret;
}

static void hibmc_pci_remove(struct pci_dev *pdev)
{
	struct drm_device *dev = pci_get_drvdata(pdev);

	drm_dev_unregister(dev);
	hibmc_unload(dev);
}

static const struct pci_device_id hibmc_pci_table[] = {
	{ PCI_VDEVICE(HUAWEI, 0x1711) },
	{0,}
};

static struct pci_driver hibmc_pci_driver = {
	.name =		"hibmc-drm",
	.id_table =	hibmc_pci_table,
	.probe =	hibmc_pci_probe,
	.remove =	hibmc_pci_remove,
	.driver.pm =    &hibmc_pm_ops,
};

drm_module_pci_driver(hibmc_pci_driver);

MODULE_DEVICE_TABLE(pci, hibmc_pci_table);
MODULE_AUTHOR("RongrongZou <zourongrong@huawei.com>");
MODULE_DESCRIPTION("DRM Driver for Hisilicon Hibmc");
MODULE_LICENSE("GPL v2");
