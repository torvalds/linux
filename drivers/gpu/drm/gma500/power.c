/**************************************************************************
 * Copyright (c) 2009-2011, Intel Corporation.
 * All Rights Reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 *
 * Authors:
 *    Benjamin Defnet <benjamin.r.defnet@intel.com>
 *    Rajesh Poornachandran <rajesh.poornachandran@intel.com>
 * Massively reworked
 *    Alan Cox <alan@linux.intel.com>
 */

#include "power.h"
#include "psb_drv.h"
#include "psb_reg.h"
#include "psb_intel_reg.h"
#include <linux/mutex.h>
#include <linux/pm_runtime.h>

static struct mutex power_mutex;	/* Serialize power ops */
static DEFINE_SPINLOCK(power_ctrl_lock);	/* Serialize power claim */

/**
 *	gma_power_init		-	initialise power manager
 *	@dev: our device
 *
 *	Set up for power management tracking of our hardware.
 */
void gma_power_init(struct drm_device *dev)
{
	struct drm_psb_private *dev_priv = dev->dev_private;

	/* FIXME: Move APM/OSPM base into relevant device code */
	dev_priv->apm_base = dev_priv->apm_reg & 0xffff;
	dev_priv->ospm_base &= 0xffff;

	dev_priv->display_power = true;	/* We start active */
	dev_priv->display_count = 0;	/* Currently no users */
	dev_priv->suspended = false;	/* And not suspended */
	mutex_init(&power_mutex);

	if (dev_priv->ops->init_pm)
		dev_priv->ops->init_pm(dev);
}

/**
 *	gma_power_uninit	-	end power manager
 *	@dev: device to end for
 *
 *	Undo the effects of gma_power_init
 */
void gma_power_uninit(struct drm_device *dev)
{
	pm_runtime_disable(dev->dev);
	pm_runtime_set_suspended(dev->dev);
}

/**
 *	gma_suspend_display	-	suspend the display logic
 *	@dev: our DRM device
 *
 *	Suspend the display logic of the graphics interface
 */
static void gma_suspend_display(struct drm_device *dev)
{
	struct drm_psb_private *dev_priv = dev->dev_private;

	if (dev_priv->suspended)
		return;
	dev_priv->ops->save_regs(dev);
	dev_priv->ops->power_down(dev);
	dev_priv->display_power = false;
}

/**
 *	gma_resume_display	-	resume display side logic
 *	@pdev: PCI device
 *
 *	Resume the display hardware restoring state and enabling
 *	as necessary.
 */
static void gma_resume_display(struct pci_dev *pdev)
{
	struct drm_device *dev = pci_get_drvdata(pdev);
	struct drm_psb_private *dev_priv = dev->dev_private;

	/* turn on the display power island */
	dev_priv->ops->power_up(dev);
	dev_priv->suspended = false;
	dev_priv->display_power = true;

	PSB_WVDC32(dev_priv->pge_ctl | _PSB_PGETBL_ENABLED, PSB_PGETBL_CTL);
	pci_write_config_word(pdev, PSB_GMCH_CTRL,
			dev_priv->gmch_ctrl | _PSB_GMCH_ENABLED);

	psb_gtt_restore(dev); /* Rebuild our GTT mappings */
	dev_priv->ops->restore_regs(dev);
}

/**
 *	gma_suspend_pci		-	suspend PCI side
 *	@pdev: PCI device
 *
 *	Perform the suspend processing on our PCI device state
 */
static void gma_suspend_pci(struct pci_dev *pdev)
{
	struct drm_device *dev = pci_get_drvdata(pdev);
	struct drm_psb_private *dev_priv = dev->dev_private;
	int bsm, vbt;

	if (dev_priv->suspended)
		return;

	pci_save_state(pdev);
	pci_read_config_dword(pdev, 0x5C, &bsm);
	dev_priv->regs.saveBSM = bsm;
	pci_read_config_dword(pdev, 0xFC, &vbt);
	dev_priv->regs.saveVBT = vbt;
	pci_read_config_dword(pdev, PSB_PCIx_MSI_ADDR_LOC, &dev_priv->msi_addr);
	pci_read_config_dword(pdev, PSB_PCIx_MSI_DATA_LOC, &dev_priv->msi_data);

	pci_disable_device(pdev);
	pci_set_power_state(pdev, PCI_D3hot);

	dev_priv->suspended = true;
}

/**
 *	gma_resume_pci		-	resume helper
 *	@pdev: our PCI device
 *
 *	Perform the resume processing on our PCI device state - rewrite
 *	register state and re-enable the PCI device
 */
static bool gma_resume_pci(struct pci_dev *pdev)
{
	struct drm_device *dev = pci_get_drvdata(pdev);
	struct drm_psb_private *dev_priv = dev->dev_private;
	int ret;

	if (!dev_priv->suspended)
		return true;

	pci_set_power_state(pdev, PCI_D0);
	pci_restore_state(pdev);
	pci_write_config_dword(pdev, 0x5c, dev_priv->regs.saveBSM);
	pci_write_config_dword(pdev, 0xFC, dev_priv->regs.saveVBT);
	/* restoring MSI address and data in PCIx space */
	pci_write_config_dword(pdev, PSB_PCIx_MSI_ADDR_LOC, dev_priv->msi_addr);
	pci_write_config_dword(pdev, PSB_PCIx_MSI_DATA_LOC, dev_priv->msi_data);
	ret = pci_enable_device(pdev);

	if (ret != 0)
		dev_err(&pdev->dev, "pci_enable failed: %d\n", ret);
	else
		dev_priv->suspended = false;
	return !dev_priv->suspended;
}

/**
 *	gma_power_suspend		-	bus callback for suspend
 *	@_dev: our device
 *
 *	Called back by the PCI layer during a suspend of the system. We
 *	perform the necessary shut down steps and save enough state that
 *	we can undo this when resume is called.
 */
int gma_power_suspend(struct device *_dev)
{
	struct pci_dev *pdev = to_pci_dev(_dev);
	struct drm_device *dev = pci_get_drvdata(pdev);
	struct drm_psb_private *dev_priv = dev->dev_private;

	mutex_lock(&power_mutex);
	if (!dev_priv->suspended) {
		if (dev_priv->display_count) {
			mutex_unlock(&power_mutex);
			dev_err(dev->dev, "GPU hardware busy, cannot suspend\n");
			return -EBUSY;
		}
		psb_irq_uninstall(dev);
		gma_suspend_display(dev);
		gma_suspend_pci(pdev);
	}
	mutex_unlock(&power_mutex);
	return 0;
}

/**
 *	gma_power_resume		-	resume power
 *	@_dev: our device
 *
 *	Resume the PCI side of the graphics and then the displays
 */
int gma_power_resume(struct device *_dev)
{
	struct pci_dev *pdev = to_pci_dev(_dev);
	struct drm_device *dev = pci_get_drvdata(pdev);

	mutex_lock(&power_mutex);
	gma_resume_pci(pdev);
	gma_resume_display(pdev);
	psb_irq_preinstall(dev);
	psb_irq_postinstall(dev);
	mutex_unlock(&power_mutex);
	return 0;
}

/**
 *	gma_power_is_on		-	returne true if power is on
 *	@dev: our DRM device
 *
 *	Returns true if the display island power is on at this moment
 */
bool gma_power_is_on(struct drm_device *dev)
{
	struct drm_psb_private *dev_priv = dev->dev_private;
	return dev_priv->display_power;
}

/**
 *	gma_power_begin		-	begin requiring power
 *	@dev: our DRM device
 *	@force_on: true to force power on
 *
 *	Begin an action that requires the display power island is enabled.
 *	We refcount the islands.
 */
bool gma_power_begin(struct drm_device *dev, bool force_on)
{
	struct drm_psb_private *dev_priv = dev->dev_private;
	struct pci_dev *pdev = to_pci_dev(dev->dev);
	int ret;
	unsigned long flags;

	spin_lock_irqsave(&power_ctrl_lock, flags);
	/* Power already on ? */
	if (dev_priv->display_power) {
		dev_priv->display_count++;
		pm_runtime_get(dev->dev);
		spin_unlock_irqrestore(&power_ctrl_lock, flags);
		return true;
	}
	if (force_on == false)
		goto out_false;

	/* Ok power up needed */
	ret = gma_resume_pci(pdev);
	if (ret == 0) {
		psb_irq_preinstall(dev);
		psb_irq_postinstall(dev);
		pm_runtime_get(dev->dev);
		dev_priv->display_count++;
		spin_unlock_irqrestore(&power_ctrl_lock, flags);
		return true;
	}
out_false:
	spin_unlock_irqrestore(&power_ctrl_lock, flags);
	return false;
}

/**
 *	gma_power_end		-	end use of power
 *	@dev: Our DRM device
 *
 *	Indicate that one of our gma_power_begin() requested periods when
 *	the diplay island power is needed has completed.
 */
void gma_power_end(struct drm_device *dev)
{
	struct drm_psb_private *dev_priv = dev->dev_private;
	unsigned long flags;
	spin_lock_irqsave(&power_ctrl_lock, flags);
	dev_priv->display_count--;
	WARN_ON(dev_priv->display_count < 0);
	spin_unlock_irqrestore(&power_ctrl_lock, flags);
	pm_runtime_put(dev->dev);
}

int psb_runtime_suspend(struct device *dev)
{
	return gma_power_suspend(dev);
}

int psb_runtime_resume(struct device *dev)
{
	return gma_power_resume(dev);
}

int psb_runtime_idle(struct device *dev)
{
	struct drm_device *drmdev = pci_get_drvdata(to_pci_dev(dev));
	struct drm_psb_private *dev_priv = drmdev->dev_private;
	if (dev_priv->display_count)
		return 0;
	else
		return 1;
}

int gma_power_thaw(struct device *_dev)
{
	return gma_power_resume(_dev);
}

int gma_power_freeze(struct device *_dev)
{
	return gma_power_suspend(_dev);
}

int gma_power_restore(struct device *_dev)
{
	return gma_power_resume(_dev);
}
